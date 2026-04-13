// Pure HTTP server. Templated over listener_engine and stream_engine
// concepts — no platform dependency. When given real engines (from
// cppx.http.system), it serves real HTTP. When given fakes, it's
// testable without sockets.

export module cppx.http.server;
import std;
import cppx.http;

export namespace cppx::http {

// ---- MIME type detection -------------------------------------------------

inline auto mime_type(std::string_view extension) -> std::string_view {
    if (extension == ".html" || extension == ".htm") return "text/html";
    if (extension == ".css") return "text/css";
    if (extension == ".js" || extension == ".mjs") return "application/javascript";
    if (extension == ".json") return "application/json";
    if (extension == ".wasm") return "application/wasm";
    if (extension == ".xml") return "application/xml";
    if (extension == ".txt") return "text/plain";
    if (extension == ".png") return "image/png";
    if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
    if (extension == ".gif") return "image/gif";
    if (extension == ".svg") return "image/svg+xml";
    if (extension == ".ico") return "image/x-icon";
    if (extension == ".woff") return "font/woff";
    if (extension == ".woff2") return "font/woff2";
    if (extension == ".ttf") return "font/ttf";
    if (extension == ".pdf") return "application/pdf";
    if (extension == ".gz" || extension == ".tar.gz") return "application/gzip";
    if (extension == ".zip") return "application/zip";
    return "application/octet-stream";
}

// ---- handler type --------------------------------------------------------

using handler_fn = std::function<response(request const&)>;

// ---- route entry ---------------------------------------------------------

namespace detail {

struct route_entry {
    method verb;
    std::string pattern;
    handler_fn handler;
};

// Simple path matching. Exact match only for v1.
inline bool path_matches(std::string_view pattern, std::string_view path) {
    return pattern == path;
}

// Send all bytes from a buffer, retrying on partial sends.
template <stream_engine S>
auto server_send_all(S& s, std::span<std::byte const> data)
    -> std::expected<void, net_error>
{
    while (!data.empty()) {
        auto n = s.send(data);
        if (!n) return std::unexpected(n.error());
        data = data.subspan(*n);
    }
    return {};
}

// Handle one HTTP connection: read request, dispatch, write response.
template <stream_engine Stream>
void handle_connection(Stream conn,
                       std::vector<route_entry> const& routes,
                       handler_fn const& fallback)
{
    // Read the request
    request_parser parser;
    auto buf = std::array<std::byte, 8192>{};
    for (;;) {
        auto n = conn.recv(buf);
        if (!n) return; // client disconnected
        auto chunk = std::span<std::byte const>{buf.data(), *n};
        auto state = parser.feed(chunk);
        if (!state) return; // parse error → drop connection
        if (*state == parse_state::complete) break;
    }
    auto req = std::move(parser).finish();

    // Match routes
    for (auto const& route : routes) {
        if (route.verb == req.verb &&
            path_matches(route.pattern, req.target.path)) {
            auto resp = route.handler(req);
            auto wire = serialize(resp);
            server_send_all(conn, wire);
            conn.close();
            return;
        }
    }

    // Fallback handler (static files or 404)
    auto resp = fallback(req);
    auto wire = serialize(resp);
    server_send_all(conn, wire);
    conn.close();
}

} // namespace detail

// ---- server template -----------------------------------------------------

template <class Listener, stream_engine Stream>
    requires listener_engine<Listener, Stream>
class server {
    std::vector<detail::route_entry> routes_;
    handler_fn fallback_ = [](request const&) -> response {
        return {
            .stat = {404},
            .hdrs = {},
            .body = as_bytes("Not Found"),
        };
    };
    std::atomic<bool> running_{false};

public:
    // Register a route handler for a specific method + path.
    void route(method verb, std::string_view pattern, handler_fn h) {
        routes_.push_back({verb, std::string{pattern}, std::move(h)});
    }

    // Serve static files from `root` directory under `prefix` URL path.
    // Example: serve_static("/", "./public") maps /index.html → ./public/index.html
    void serve_static(std::string_view prefix, std::filesystem::path root) {
        auto prefix_str = std::string{prefix};
        if (!prefix_str.empty() && prefix_str.back() == '/')
            prefix_str.pop_back();

        fallback_ = [prefix_str, root = std::move(root)](
                        request const& req) -> response {
            if (req.verb != method::GET && req.verb != method::HEAD)
                return {.stat = {405}, .hdrs = {}, .body = as_bytes("Method Not Allowed")};

            auto path = req.target.path;
            // Strip prefix
            if (!prefix_str.empty()) {
                if (!path.starts_with(prefix_str))
                    return {.stat = {404}, .hdrs = {}, .body = as_bytes("Not Found")};
                path = path.substr(prefix_str.size());
            }
            if (path.empty() || path == "/") path = "/index.html";

            // Security: reject path traversal
            if (path.find("..") != std::string::npos)
                return {.stat = {403}, .hdrs = {}, .body = as_bytes("Forbidden")};

            auto file_path = root / path.substr(1); // skip leading /
            std::error_code ec;
            if (!std::filesystem::is_regular_file(file_path, ec))
                return {.stat = {404}, .hdrs = {}, .body = as_bytes("Not Found")};

            // Read file
            auto size = std::filesystem::file_size(file_path, ec);
            if (ec)
                return {.stat = {500}, .hdrs = {}, .body = as_bytes("Internal Server Error")};

            auto in = std::ifstream{file_path, std::ios::binary};
            if (!in)
                return {.stat = {500}, .hdrs = {}, .body = as_bytes("Internal Server Error")};

            auto body = std::vector<std::byte>(size);
            in.read(reinterpret_cast<char*>(body.data()),
                    static_cast<std::streamsize>(size));

            // MIME type
            auto ext = file_path.extension().string();
            auto content_type = mime_type(ext);

            response resp;
            resp.stat = {200};
            resp.hdrs.set("content-type", content_type);
            resp.hdrs.set("content-length", std::to_string(body.size()));
            if (req.verb == method::HEAD)
                resp.body = {};
            else
                resp.body = std::move(body);
            return resp;
        };
    }

    // Start the server. Blocks until stop() is called.
    auto run(std::string_view addr, std::uint16_t port)
        -> std::expected<void, net_error>
    {
        auto listener = Listener::bind(addr, port);
        if (!listener) return std::unexpected(listener.error());

        running_.store(true);
        std::vector<std::thread> threads;

        while (running_.load()) {
            auto conn = listener->accept();
            if (!conn) {
                if (!running_.load()) break; // stop() was called
                continue; // transient error, retry
            }
            threads.emplace_back([this, c = std::move(*conn)]() mutable {
                detail::handle_connection(std::move(c), routes_, fallback_);
            });
        }

        // Join all worker threads
        for (auto& t : threads)
            if (t.joinable()) t.join();

        listener->close();
        return {};
    }

    // Signal the server to stop accepting new connections.
    void stop() { running_.store(false); }
};

} // namespace cppx::http
