// Pure tests for cppx.http. No network I/O — exercises URL parsing,
// header operations, request/response serialization, and incremental
// parser round-trips.

import cppx.bytes;
import cppx.http;
import cppx.test;
import std;

cppx::test::context tc;

// ---- URL parsing ---------------------------------------------------------

void test_url_parse_https() {
    auto u = cppx::http::url::parse("https://api.github.com/repos/misut/cppx");
    tc.check(u.has_value(), "parse https URL");
    tc.check(u->scheme == "https", "scheme is https");
    tc.check(u->host == "api.github.com", "host");
    tc.check(u->port == 0, "port is default");
    tc.check(u->effective_port() == 443, "effective port 443");
    tc.check(u->path == "/repos/misut/cppx", "path");
    tc.check(u->query.empty(), "no query");
    tc.check(u->is_tls(), "is_tls true");
}

void test_url_parse_http_with_port() {
    auto u = cppx::http::url::parse("http://localhost:8080/api/health?v=1");
    tc.check(u.has_value(), "parse http URL with port and query");
    tc.check(u->scheme == "http", "scheme is http");
    tc.check(u->host == "localhost", "host");
    tc.check(u->port == 8080, "port 8080");
    tc.check(u->effective_port() == 8080, "effective port 8080");
    tc.check(u->path == "/api/health", "path");
    tc.check(u->query == "v=1", "query");
    tc.check(!u->is_tls(), "is_tls false");
}

void test_url_parse_no_path() {
    auto u = cppx::http::url::parse("http://example.com");
    tc.check(u.has_value(), "parse URL without path");
    tc.check(u->path == "/", "default path /");
}

void test_url_parse_invalid() {
    auto u = cppx::http::url::parse("not-a-url");
    tc.check(!u.has_value(), "invalid URL → error");
}

void test_url_to_string() {
    auto u = cppx::http::url::parse("https://example.com:8443/api?q=1");
    tc.check(u.has_value(), "parse for to_string");
    tc.check(u->to_string() == "https://example.com:8443/api?q=1",
          "round-trip to_string");
}

// ---- headers -------------------------------------------------------------

void test_headers_case_insensitive() {
    cppx::http::headers h;
    h.set("Content-Type", "text/html");
    tc.check(h.get("content-type") == "text/html", "lowercase lookup");
    tc.check(h.get("CONTENT-TYPE") == "text/html", "uppercase lookup");
    tc.check(h.contains("Content-Type"), "contains mixed case");
}

void test_headers_set_overwrites() {
    cppx::http::headers h;
    h.set("Host", "a.com");
    h.set("Host", "b.com");
    tc.check(h.get("host") == "b.com", "set overwrites");
    tc.check(h.size() == 1, "no duplicates after set");
}

void test_headers_append() {
    cppx::http::headers h;
    h.append("Set-Cookie", "a=1");
    h.append("Set-Cookie", "b=2");
    tc.check(h.size() == 2, "append creates duplicates");
}

// ---- method --------------------------------------------------------------

void test_method_to_string() {
    tc.check(cppx::http::to_string(cppx::http::method::GET) == "GET", "GET");
    tc.check(cppx::http::to_string(cppx::http::method::POST) == "POST", "POST");
    tc.check(cppx::http::to_string(cppx::http::method::DELETE_) == "DELETE",
          "DELETE");
}

void test_method_from_string() {
    tc.check(cppx::http::method_from_string("GET") == cppx::http::method::GET,
          "from GET");
    tc.check(!cppx::http::method_from_string("INVALID").has_value(),
          "from INVALID");
}

// ---- status --------------------------------------------------------------

void test_status() {
    auto ok = cppx::http::status{200};
    tc.check(ok.ok(), "200 is ok");
    tc.check(ok.reason() == "OK", "200 reason");

    auto nf = cppx::http::status{404};
    tc.check(!nf.ok(), "404 is not ok");
    tc.check(nf.reason() == "Not Found", "404 reason");
}

// ---- serialization -------------------------------------------------------

void test_serialize_request() {
    cppx::http::request req;
    req.verb = cppx::http::method::GET;
    req.target = *cppx::http::url::parse("http://example.com/index.html");
    req.hdrs.set("Accept", "text/html");

    auto bytes = cppx::http::serialize(req);
    auto str = std::string{
        reinterpret_cast<char const*>(bytes.data()), bytes.size()};

    tc.check(str.contains("GET /index.html HTTP/1.1\r\n"),
          "request line");
    tc.check(str.contains("accept: text/html\r\n"), "accept header");
    tc.check(str.contains("host: example.com\r\n"), "auto host header");
    tc.check(str.contains("\r\n\r\n"), "double CRLF terminator");
}

void test_request_response_body_surface() {
    static_assert(std::same_as<decltype(cppx::http::request{}.body),
                               cppx::bytes::byte_buffer>);
    static_assert(std::same_as<decltype(cppx::http::response{}.body),
                               cppx::bytes::byte_buffer>);

    cppx::http::request req;
    req.body = cppx::http::as_bytes("abc");
    tc.check(req.body.size() == 3, "request body uses byte_buffer");
    tc.check(req.body.subview(1, 8).size() == 2, "request body subview clamps");

    cppx::http::response res;
    res.body = cppx::http::as_bytes("abc");
    tc.check(res.body_string() == "abc", "response body_string works on byte_buffer");
}

void test_serialize_response() {
    cppx::http::response res;
    res.stat = {200};
    res.hdrs.set("Content-Type", "text/plain");
    res.body = cppx::http::as_bytes("hello");

    auto bytes = cppx::http::serialize(res);
    auto str = std::string{
        reinterpret_cast<char const*>(bytes.data()), bytes.size()};

    tc.check(str.contains("HTTP/1.1 200 OK\r\n"), "status line");
    tc.check(str.contains("content-type: text/plain\r\n"), "content-type");
    tc.check(str.contains("content-length: 5\r\n"), "auto content-length");
    tc.check(str.ends_with("hello"), "body at end");
}

// ---- response parser -----------------------------------------------------

void test_parse_simple_response() {
    auto raw = std::string{
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 15\r\n"
        "\r\n"
        "{\"status\":\"ok\"}"};

    cppx::http::response_parser parser;
    auto data = cppx::http::as_bytes(raw);
    auto state = parser.feed(data.view());
    tc.check(state.has_value() && *state == cppx::http::parse_state::complete,
          "single-feed complete");

    auto res = std::move(parser).finish();
    tc.check(res.stat.code == 200, "status 200");
    tc.check(res.stat.ok(), "status ok");
    tc.check(res.hdrs.get("content-type") == "application/json",
          "content-type header");
    tc.check(res.body_string() == R"({"status":"ok"})", "body matches");
}

void test_parse_incremental_response() {
    auto part1 = std::string{"HTTP/1.1 200 OK\r\nContent-Len"};
    auto part2 = std::string{"gth: 5\r\n\r\nhello"};

    cppx::http::response_parser parser;
    auto s1 = parser.feed(cppx::http::as_bytes(part1).view());
    tc.check(s1.has_value() && *s1 == cppx::http::parse_state::need_more,
          "part1 needs more");

    auto s2 = parser.feed(cppx::http::as_bytes(part2).view());
    tc.check(s2.has_value() && *s2 == cppx::http::parse_state::complete,
          "part2 complete");

    auto res = std::move(parser).finish();
    tc.check(res.body_string() == "hello", "body assembled from 2 parts");
}

void test_parse_chunked_response() {
    auto raw = std::string{
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "6\r\n world\r\n"
        "0\r\n\r\n"};

    cppx::http::response_parser parser;
    auto state = parser.feed(cppx::http::as_bytes(raw).view());
    tc.check(state.has_value() && *state == cppx::http::parse_state::complete,
          "chunked complete");

    auto res = std::move(parser).finish();
    tc.check(res.body_string() == "hello world", "chunked body assembled");
}

void test_parse_no_body_response() {
    auto raw = std::string{
        "HTTP/1.1 204 No Content\r\n"
        "\r\n"};

    cppx::http::response_parser parser;
    auto state = parser.feed(cppx::http::as_bytes(raw).view());
    tc.check(state.has_value() && *state == cppx::http::parse_state::complete,
          "204 no body complete");
    auto res = std::move(parser).finish();
    tc.check(res.body.empty(), "no body");
}

// ---- request parser ------------------------------------------------------

void test_parse_simple_request() {
    auto raw = std::string{
        "GET /api/health HTTP/1.1\r\n"
        "Host: localhost:3000\r\n"
        "\r\n"};

    cppx::http::request_parser parser;
    auto state = parser.feed(cppx::http::as_bytes(raw).view());
    tc.check(state.has_value() && *state == cppx::http::parse_state::complete,
          "request parse complete");

    auto req = std::move(parser).finish();
    tc.check(req.verb == cppx::http::method::GET, "method GET");
    tc.check(req.target.path == "/api/health", "path");
    tc.check(req.hdrs.get("host") == "localhost:3000", "host header");
    tc.check(req.body.empty(), "no body");
}

void test_parse_request_with_body() {
    auto raw = std::string{
        "POST /api/echo HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 11\r\n"
        "\r\n"
        "hello world"};

    cppx::http::request_parser parser;
    auto state = parser.feed(cppx::http::as_bytes(raw).view());
    tc.check(state.has_value() && *state == cppx::http::parse_state::complete,
          "POST parse complete");

    auto req = std::move(parser).finish();
    tc.check(req.verb == cppx::http::method::POST, "method POST");
    tc.check(std::string(reinterpret_cast<char const*>(req.body.data()),
                      req.body.size()) == "hello world",
          "body");
}

void test_parse_request_with_query() {
    auto raw = std::string{
        "GET /search?q=hello&page=1 HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "\r\n"};

    cppx::http::request_parser parser;
    auto state = parser.feed(cppx::http::as_bytes(raw).view());
    tc.check(state.has_value(), "query request parsed");

    auto req = std::move(parser).finish();
    tc.check(req.target.path == "/search", "path without query");
    tc.check(req.target.query == "q=hello&page=1", "query string");
}

// ---- round-trip ----------------------------------------------------------

void test_serialize_parse_roundtrip() {
    // Build a response, serialize it, parse it back
    cppx::http::response original;
    original.stat = {201};
    original.hdrs.set("X-Custom", "test-value");
    original.body = cppx::http::as_bytes("created");

    auto bytes = cppx::http::serialize(original);

    cppx::http::response_parser parser;
    auto state = parser.feed(bytes.view());
    tc.check(state.has_value() && *state == cppx::http::parse_state::complete,
          "roundtrip parse complete");

    auto parsed = std::move(parser).finish();
    tc.check(parsed.stat.code == 201, "roundtrip status");
    tc.check(parsed.hdrs.get("x-custom") == "test-value", "roundtrip header");
    tc.check(parsed.body_string() == "created", "roundtrip body");
}

// ---- concepts (compile-time) ---------------------------------------------

void test_concepts() {
    // These are compile-time checks via static_assert in the module.
    // If we got here, null_stream/null_listener/null_tls satisfy their
    // respective concepts.
    tc.check(true, "concepts compile-time verified");
}

// ---- main ----------------------------------------------------------------

int main() {
    test_url_parse_https();
    test_url_parse_http_with_port();
    test_url_parse_no_path();
    test_url_parse_invalid();
    test_url_to_string();
    test_headers_case_insensitive();
    test_headers_set_overwrites();
    test_headers_append();
    test_method_to_string();
    test_method_from_string();
    test_status();
    test_serialize_request();
    test_request_response_body_surface();
    test_serialize_response();
    test_parse_simple_response();
    test_parse_incremental_response();
    test_parse_chunked_response();
    test_parse_no_body_response();
    test_parse_simple_request();
    test_parse_request_with_body();
    test_parse_request_with_query();
    test_serialize_parse_roundtrip();
    test_concepts();
    return tc.summary("cppx.http");
}
