// Pure HTTP client. Templated over stream_engine and tls_provider
// concepts — no platform dependency. When given a real engine (from
// cppx.http.system), it does real HTTP. When given a fake engine,
// it's a pure function from request → response, testable without I/O.

export module cppx.http.client;
import cppx.bytes;
import std;
import cppx.http;

export namespace cppx::http {

namespace detail {

template <stream_engine S>
auto client_send_all(S& s, cppx::bytes::bytes_view data)
    -> std::expected<void, http_error>;

inline auto parse_headers_block(std::string_view block, headers& hdrs)
    -> std::expected<void, http_error>
{
    while (!block.empty()) {
        auto eol = block.find("\r\n");
        if (eol == std::string_view::npos) break;
        auto line = block.substr(0, eol);
        block = block.substr(eol + 2);
        if (line.empty()) continue;

        auto colon = line.find(':');
        if (colon == std::string_view::npos)
            return std::unexpected(http_error::response_parse_failed);
        auto key = line.substr(0, colon);
        auto val = line.substr(colon + 1);
        while (!val.empty() && val.front() == ' ')
            val = val.substr(1);
        hdrs.append(key, val);
    }
    return {};
}

struct response_head {
    response res;
    std::size_t content_length = 0;
    bool chunked = false;
};

inline auto try_parse_response_head(std::string& buf,
                                    std::size_t max_header,
                                    std::size_t max_body)
    -> std::expected<std::optional<response_head>, http_error>
{
    if (buf.size() > max_header)
        return std::unexpected(http_error::response_parse_failed);

    auto end = buf.find("\r\n\r\n");
    if (end == std::string::npos)
        return std::optional<response_head>{};
    end += 4;

    auto first_line_end = buf.find("\r\n");
    if (first_line_end == std::string::npos)
        return std::unexpected(http_error::response_parse_failed);
    auto status_line = std::string_view{buf}.substr(0, first_line_end);
    if (status_line.size() < 12 || status_line.substr(0, 5) != "HTTP/")
        return std::unexpected(http_error::response_parse_failed);

    auto sp1 = status_line.find(' ');
    if (sp1 == std::string_view::npos)
        return std::unexpected(http_error::response_parse_failed);
    auto code_start = sp1 + 1;
    auto sp2 = status_line.find(' ', code_start);
    auto code_str = (sp2 != std::string_view::npos)
        ? status_line.substr(code_start, sp2 - code_start)
        : status_line.substr(code_start);

    std::uint16_t code = 0;
    for (auto c : code_str) {
        if (c < '0' || c > '9')
            return std::unexpected(http_error::response_parse_failed);
        code = static_cast<std::uint16_t>(code * 10 + (c - '0'));
    }

    response_head head;
    head.res.stat = status{code};

    auto hdr_block = std::string_view{buf}.substr(
        first_line_end + 2, end - first_line_end - 4);
    auto hr = parse_headers_block(hdr_block, head.res.hdrs);
    if (!hr) return std::unexpected(hr.error());

    if (auto te = head.res.hdrs.get("transfer-encoding");
        te && te->find("chunked") != std::string_view::npos) {
        head.chunked = true;
    } else if (auto cl = head.res.hdrs.get("content-length")) {
        std::size_t len = 0;
        for (auto c : *cl) {
            if (c < '0' || c > '9')
                return std::unexpected(http_error::response_parse_failed);
            len = len * 10 + (c - '0');
        }
        if (len > max_body)
            return std::unexpected(http_error::body_too_large);
        head.content_length = len;
    }

    buf.erase(0, end);
    return std::optional<response_head>{std::move(head)};
}

inline auto write_all(std::ofstream& out, std::string_view chunk)
    -> std::expected<void, http_error>
{
    if (chunk.empty()) return {};
    out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    if (!out)
        return std::unexpected(http_error::send_failed);
    return {};
}

inline auto finalize_download(std::ofstream& out,
                              std::filesystem::path const& temp_path,
                              std::filesystem::path const& final_path)
    -> std::expected<void, http_error>
{
    out.close();
    if (!out)
        return std::unexpected(http_error::send_failed);

    std::error_code ec;
    std::filesystem::remove(final_path, ec);
    std::filesystem::rename(temp_path, final_path, ec);
    if (ec)
        return std::unexpected(http_error::send_failed);
    return {};
}

template <stream_engine S>
auto do_download_exchange(S& stream, request const& req,
                          std::filesystem::path const& path,
                          std::size_t max_body = default_download_body_limit)
    -> std::expected<response, http_error>
{
    auto wire = serialize(req);
    auto sr = client_send_all(stream, wire.view());
    if (!sr) return std::unexpected(sr.error());

    auto buf = std::string{};
    auto scratch = std::array<std::byte, 8192>{};
    std::optional<response_head> head;

    for (;;) {
        if (!head) {
            auto parsed = try_parse_response_head(
                buf, default_response_header_limit, max_body);
            if (!parsed) return std::unexpected(parsed.error());
            if (*parsed) {
                head = std::move(*parsed);
                if (!head->res.stat.ok() ||
                    (head->res.stat.code >= 300 && head->res.stat.code < 400)) {
                    return std::move(head->res);
                }
                break;
            }
        }

        auto n = stream.recv(scratch);
        if (!n) {
            if (n.error() == net_error::connection_closed)
                return std::unexpected(http_error::response_parse_failed);
            return std::unexpected(http_error::response_parse_failed);
        }
        buf.append(reinterpret_cast<char const*>(scratch.data()), *n);
    }

    auto temp_path = path;
    temp_path += ".part";
    std::error_code rm_ec;
    std::filesystem::remove(temp_path, rm_ec);
    auto out = std::ofstream{temp_path, std::ios::binary | std::ios::trunc};
    if (!out)
        return std::unexpected(http_error::send_failed);

    auto cleanup = [&] {
        out.close();
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
    };

    auto total_written = std::size_t{0};
    auto write_chunk = [&](std::string_view chunk)
        -> std::expected<void, http_error>
    {
        if (total_written + chunk.size() > max_body)
            return std::unexpected(http_error::body_too_large);
        auto wr = write_all(out, chunk);
        if (!wr) return wr;
        total_written += chunk.size();
        return {};
    };

    if (!head->chunked && head->content_length == 0) {
        auto fin = finalize_download(out, temp_path, path);
        if (!fin) {
            cleanup();
            return std::unexpected(fin.error());
        }
        head->res.body = {};
        return std::move(head->res);
    }

    if (head->chunked) {
        for (;;) {
            auto line_end = buf.find("\r\n");
            while (line_end == std::string::npos) {
                auto n = stream.recv(scratch);
                if (!n) {
                    cleanup();
                    return std::unexpected(http_error::response_parse_failed);
                }
                buf.append(reinterpret_cast<char const*>(scratch.data()), *n);
                line_end = buf.find("\r\n");
            }

            auto size_str = std::string_view{buf}.substr(0, line_end);
            auto semi = size_str.find(';');
            if (semi != std::string_view::npos)
                size_str = size_str.substr(0, semi);
            if (size_str.empty()) {
                cleanup();
                return std::unexpected(http_error::response_parse_failed);
            }

            std::size_t chunk_size = 0;
            for (auto c : size_str) {
                chunk_size <<= 4;
                if (c >= '0' && c <= '9') chunk_size += (c - '0');
                else if (c >= 'a' && c <= 'f') chunk_size += (c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') chunk_size += (c - 'A' + 10);
                else {
                    cleanup();
                    return std::unexpected(http_error::response_parse_failed);
                }
            }
            buf.erase(0, line_end + 2);

            if (chunk_size == 0) {
                auto trailer_end = buf.find("\r\n\r\n");
                while (trailer_end == std::string::npos && buf != "\r\n") {
                    auto n = stream.recv(scratch);
                    if (!n) {
                        cleanup();
                        return std::unexpected(http_error::response_parse_failed);
                    }
                    buf.append(reinterpret_cast<char const*>(scratch.data()), *n);
                    trailer_end = buf.find("\r\n\r\n");
                }
                if (buf == "\r\n") {
                    buf.clear();
                } else if (trailer_end != std::string::npos) {
                    buf.erase(0, trailer_end + 4);
                } else {
                    cleanup();
                    return std::unexpected(http_error::response_parse_failed);
                }

                auto fin = finalize_download(out, temp_path, path);
                if (!fin) {
                    cleanup();
                    return std::unexpected(fin.error());
                }
                head->res.body = {};
                return std::move(head->res);
            }

            while (buf.size() < chunk_size + 2) {
                auto n = stream.recv(scratch);
                if (!n) {
                    cleanup();
                    return std::unexpected(http_error::response_parse_failed);
                }
                buf.append(reinterpret_cast<char const*>(scratch.data()), *n);
            }

            if (buf.substr(chunk_size, 2) != "\r\n") {
                cleanup();
                return std::unexpected(http_error::response_parse_failed);
            }

            auto wr = write_chunk(std::string_view{buf}.substr(0, chunk_size));
            if (!wr) {
                cleanup();
                return std::unexpected(wr.error());
            }
            buf.erase(0, chunk_size + 2);
        }
    }

    if (head->content_length > 0) {
        while (total_written < head->content_length) {
            if (buf.empty()) {
                auto remain = head->content_length - total_written;
                auto next = std::min(remain, scratch.size());
                auto n = stream.recv(std::span<std::byte>{scratch}.first(next));
                if (!n) {
                    cleanup();
                    return std::unexpected(http_error::response_parse_failed);
                }
                buf.append(reinterpret_cast<char const*>(scratch.data()), *n);
                continue;
            }

            auto remain = head->content_length - total_written;
            auto chunk = std::min(remain, buf.size());
            auto wr = write_chunk(std::string_view{buf}.substr(0, chunk));
            if (!wr) {
                cleanup();
                return std::unexpected(wr.error());
            }
            buf.erase(0, chunk);
        }

        auto fin = finalize_download(out, temp_path, path);
        if (!fin) {
            cleanup();
            return std::unexpected(fin.error());
        }
        head->res.body = {};
        return std::move(head->res);
    }

    if (!buf.empty()) {
        auto wr = write_chunk(buf);
        if (!wr) {
            cleanup();
            return std::unexpected(wr.error());
        }
        buf.clear();
    }

    for (;;) {
        auto n = stream.recv(scratch);
        if (!n) {
            if (n.error() == net_error::connection_closed) {
                auto fin = finalize_download(out, temp_path, path);
                if (!fin) {
                    cleanup();
                    return std::unexpected(fin.error());
                }
                head->res.body = {};
                return std::move(head->res);
            }
            cleanup();
            return std::unexpected(http_error::response_parse_failed);
        }

        auto wr = write_chunk(std::string_view{
            reinterpret_cast<char const*>(scratch.data()), *n});
        if (!wr) {
            cleanup();
            return std::unexpected(wr.error());
        }
    }
}

// Send all bytes from a buffer, retrying on partial sends.
template <stream_engine S>
auto client_send_all(S& s, cppx::bytes::bytes_view data)
    -> std::expected<void, http_error>
{
    while (!data.empty()) {
        auto n = s.send(std::span{data.data(), data.size()});
        if (!n) return std::unexpected(http_error::send_failed);
        data = data.subview(*n);
    }
    return {};
}

// Perform the HTTP exchange on an already-connected stream:
// serialize request → send → recv loop → parse response.
template <stream_engine S>
auto do_exchange(S& stream, request const& req,
                 std::size_t max_body = 64 * 1024 * 1024)
    -> std::expected<response, http_error>
{
    // Serialize and send
    auto wire = serialize(req);
    auto sr = client_send_all(stream, wire.view());
    if (!sr) return std::unexpected(sr.error());

    // Receive and parse
    response_parser parser(default_response_header_limit, max_body);
    auto buf = std::array<std::byte, 8192>{};
    for (;;) {
        auto recv_span = std::span<std::byte>{buf}.first(
            parser.preferred_recv_size(buf.size()));
        auto n = stream.recv(recv_span);
        if (!n) {
            if (n.error() == net_error::connection_closed) {
                // Server closed — finalize what we have. This is
                // normal for HEAD responses and HTTP/1.0 without
                // Content-Length.
                return std::move(parser).finish();
            }
            return std::unexpected(http_error::response_parse_failed);
        }
        auto chunk = cppx::bytes::bytes_view{buf.data(), *n};
        auto state = parser.feed(chunk);
        if (!state)
            return std::unexpected(http_error::response_parse_failed);
        if (*state == parse_state::complete)
            return std::move(parser).finish();
        // HEAD responses: headers are parsed but body is never sent.
        // Once headers are done, return immediately.
        if (req.verb == method::HEAD && parser.headers_parsed())
            return std::move(parser).finish();
    }
}

} // namespace detail

// HTTP client templated over engine concepts. The engine determines
// whether this is a real HTTP client (system engines) or a test
// fixture (fake engines).
//
// Usage with real engines:
//   client<system::stream, system::tls> c;
//   auto resp = c.get("https://api.github.com/...");
//
// Usage in tests:
//   client<fake_stream, fake_tls> c;
//   auto resp = c.get("http://example.com/test");
//
template <stream_engine RawStream, tls_provider<RawStream> Tls>
class client {
    Tls tls_{};

public:
    client() = default;
    explicit client(Tls tls) : tls_{std::move(tls)} {}

    // Send a fully-constructed request and receive the response.
    // Automatically follows 3xx redirects up to max_redirects times.
    auto request(http::request const& req, int max_redirects = 5,
                 std::size_t max_body = 64 * 1024 * 1024)
        -> std::expected<response, http_error>
    {
        auto current = req;
        for (int hops = 0;; ++hops) {
            auto resp = single_request(current, max_body);
            if (!resp) return resp;

            auto code = resp->stat.code;
            if (code < 300 || code >= 400) return resp;
            if (hops >= max_redirects)
                return std::unexpected(http_error::redirect_limit);

            auto location = resp->hdrs.get("location");
            if (!location) return resp;

            auto next = url::parse(*location);
            if (!next) return std::unexpected(http_error::url_parse_failed);

            // 303 See Other: switch to GET regardless of original method
            if (code == 303) current.verb = method::GET;
            current.target = std::move(*next);
            current.hdrs.set("host", current.target.host);
        }
    }

    // Convenience: GET a URL string, returns the full response.
    auto get(std::string_view url_str, headers extra = {})
        -> std::expected<response, http_error>
    {
        auto u = url::parse(url_str);
        if (!u) return std::unexpected(http_error::url_parse_failed);

        http::request req;
        req.verb = method::GET;
        req.target = std::move(*u);
        req.hdrs = std::move(extra);
        return request(req);
    }

    // Convenience: HEAD request (like GET but no body in response).
    auto head(std::string_view url_str)
        -> std::expected<response, http_error>
    {
        auto u = url::parse(url_str);
        if (!u) return std::unexpected(http_error::url_parse_failed);

        http::request req;
        req.verb = method::HEAD;
        req.target = std::move(*u);
        return request(req);
    }

    // Convenience: POST with a body.
    auto post(std::string_view url_str, std::string_view content_type,
              cppx::bytes::byte_buffer body)
        -> std::expected<response, http_error>
    {
        auto u = url::parse(url_str);
        if (!u) return std::unexpected(http_error::url_parse_failed);

        http::request req;
        req.verb = method::POST;
        req.target = std::move(*u);
        req.hdrs.set("content-type", content_type);
        req.body = std::move(body);
        return request(req);
    }

    // Download a URL to a local file. Follows redirects and streams
    // the response body to disk once the final response headers have
    // been parsed. Downloads are uncapped by default; callers can
    // pass max_body to enforce their own limit.
    auto download_to(std::string_view url_str,
                     std::filesystem::path const& path,
                     std::size_t max_body = default_download_body_limit)
        -> std::expected<response, http_error>
    {
        return download_to(url_str, path, {}, max_body);
    }

    auto download_to(std::string_view url_str,
                     std::filesystem::path const& path,
                     headers extra,
                     std::size_t max_body = default_download_body_limit)
        -> std::expected<response, http_error>
    {
        auto u = url::parse(url_str);
        if (!u) return std::unexpected(http_error::url_parse_failed);

        http::request req;
        req.verb = method::GET;
        req.target = std::move(*u);
        req.hdrs = std::move(extra);

        auto current = req;
        for (int hops = 0;; ++hops) {
            auto resp = single_download_request(current, path, max_body);
            if (!resp) return resp;

            auto code = resp->stat.code;
            if (code < 300 || code >= 400) return resp;
            if (hops >= 5)
                return std::unexpected(http_error::redirect_limit);

            auto location = resp->hdrs.get("location");
            if (!location) return resp;

            auto next = url::parse(*location);
            if (!next) return std::unexpected(http_error::url_parse_failed);

            if (code == 303) current.verb = method::GET;
            current.target = std::move(*next);
            current.hdrs.set("host", current.target.host);
        }
    }

private:
    auto single_request(http::request const& req,
                        std::size_t max_body = 64 * 1024 * 1024)
        -> std::expected<response, http_error>
    {
        auto const& target = req.target;
        auto port = target.effective_port();

        auto raw = RawStream::connect(target.host, port);
        if (!raw)
            return std::unexpected(http_error::connection_failed);

        if (target.is_tls()) {
            auto tls_stream = tls_.wrap(std::move(*raw), target.host);
            if (!tls_stream)
                return std::unexpected(http_error::tls_failed);
            return detail::do_exchange(*tls_stream, req, max_body);
        }

        return detail::do_exchange(*raw, req, max_body);
    }

    auto single_download_request(http::request const& req,
                                 std::filesystem::path const& path,
                                 std::size_t max_body = default_download_body_limit)
        -> std::expected<response, http_error>
    {
        auto const& target = req.target;
        auto port = target.effective_port();

        auto raw = RawStream::connect(target.host, port);
        if (!raw)
            return std::unexpected(http_error::connection_failed);

        if (target.is_tls()) {
            auto tls_stream = tls_.wrap(std::move(*raw), target.host);
            if (!tls_stream)
                return std::unexpected(http_error::tls_failed);
            return detail::do_download_exchange(*tls_stream, req, path, max_body);
        }

        return detail::do_download_exchange(*raw, req, path, max_body);
    }
};

} // namespace cppx::http
