// HTTP server tests. Uses real system sockets for a localhost
// round-trip: start a server on an ephemeral port, GET with the
// HTTP client, verify the response.

import cppx.http;
import cppx.http.client;
import cppx.http.server;
import cppx.http.system;
import cppx.test;
import std;

cppx::test::context tc;

void test_route_handler() {
    // Start server with a custom route
    cppx::http::server<cppx::http::system::listener,
                       cppx::http::system::stream> srv;

    srv.route(cppx::http::method::GET, "/api/health",
              [](cppx::http::request const&) -> cppx::http::response {
        return {
            .stat = {200},
            .hdrs = {},
            .body = cppx::http::as_bytes(R"({"status":"ok"})"),
        };
    });

    // Bind to ephemeral port to discover the actual port
    auto listener = cppx::http::system::listener::bind("127.0.0.1", 0);
    tc.check(listener.has_value(), "server bind");
    if (!listener) return;
    auto port = listener->local_port();
    listener->close(); // close so the server can re-bind

    // Run server in background thread
    auto server_done = std::atomic<bool>{false};
    auto server_thread = std::thread{[&] {
        cppx::http::server<cppx::http::system::listener,
                           cppx::http::system::stream> s;
        s.route(cppx::http::method::GET, "/api/health",
                [](cppx::http::request const&) -> cppx::http::response {
            return {
                .stat = {200},
                .hdrs = {},
                .body = cppx::http::as_bytes(R"({"status":"ok"})"),
            };
        });
        // Run for one request then stop
        auto l = cppx::http::system::listener::bind("127.0.0.1", port);
        if (!l) return;
        auto conn = l->accept();
        if (!conn) return;
        cppx::http::detail::handle_connection(
            std::move(*conn), {
                {cppx::http::method::GET, "/api/health",
                 [](cppx::http::request const&) -> cppx::http::response {
                    return {
                        .stat = {200},
                        .hdrs = {},
                        .body = cppx::http::as_bytes(R"({"status":"ok"})"),
                    };
                }}
            },
            [](cppx::http::request const&) -> cppx::http::response {
                return {.stat = {404}, .hdrs = {}, .body = {}};
            });
        l->close();
        server_done.store(true);
    }};

    // Give server a moment to start listening
    std::this_thread::sleep_for(std::chrono::milliseconds{50});

    // Client request
    using client_t = cppx::http::client<cppx::http::system::stream,
                                        cppx::http::system::tls>;
    auto url = std::format("http://127.0.0.1:{}/api/health", port);
    auto resp = client_t{}.get(url);

    tc.check(resp.has_value(), "client GET succeeds");
    if (resp) {
        tc.check(resp->stat.code == 200, "status 200");
        tc.check(resp->body_string() == R"({"status":"ok"})",
              "response body matches");
    }

    server_thread.join();
    tc.check(server_done.load(), "server completed");
}

void test_static_file_serving() {
    // Create a temp directory with a test file
    auto tmp = std::filesystem::temp_directory_path() /
               "cppx_http_server_test";
    std::filesystem::create_directories(tmp);
    {
        auto f = std::ofstream{tmp / "hello.txt"};
        f << "hello world";
    }
    {
        auto f = std::ofstream{tmp / "index.html"};
        f << "<h1>home</h1>";
    }

    // Bind ephemeral port
    auto listener = cppx::http::system::listener::bind("127.0.0.1", 0);
    tc.check(listener.has_value(), "static server bind");
    if (!listener) { std::filesystem::remove_all(tmp); return; }
    auto port = listener->local_port();
    listener->close();

    // Server thread — handle 2 requests then stop
    auto server_thread = std::thread{[&] {
        auto l = cppx::http::system::listener::bind("127.0.0.1", port);
        if (!l) return;

        // Create the static file handler
        cppx::http::handler_fn fallback = [&tmp](
            cppx::http::request const& req) -> cppx::http::response {
            auto path = req.target.path;
            if (path.empty() || path == "/") path = "/index.html";
            if (path.find("..") != std::string::npos)
                return {.stat = {403}, .hdrs = {}, .body = {}};
            auto file_path = tmp / path.substr(1);
            std::error_code ec;
            if (!std::filesystem::is_regular_file(file_path, ec))
                return {.stat = {404}, .hdrs = {},
                        .body = cppx::http::as_bytes("Not Found")};
            auto size = std::filesystem::file_size(file_path, ec);
            auto in = std::ifstream{file_path, std::ios::binary};
            auto body = std::vector<std::byte>(size);
            in.read(reinterpret_cast<char*>(body.data()),
                    static_cast<std::streamsize>(size));
            auto ext = file_path.extension().string();
            cppx::http::response resp;
            resp.stat = {200};
            resp.hdrs.set("content-type", cppx::http::mime_type(ext));
            resp.body = std::move(body);
            return resp;
        };

        for (int i = 0; i < 2; ++i) {
            auto conn = l->accept();
            if (!conn) break;
            cppx::http::detail::handle_connection(
                std::move(*conn), {}, fallback);
        }
        l->close();
    }};

    std::this_thread::sleep_for(std::chrono::milliseconds{50});

    using client_t = cppx::http::client<cppx::http::system::stream,
                                        cppx::http::system::tls>;

    // Request 1: /hello.txt
    auto url1 = std::format("http://127.0.0.1:{}/hello.txt", port);
    auto r1 = client_t{}.get(url1);
    tc.check(r1.has_value(), "GET /hello.txt succeeds");
    if (r1) {
        tc.check(r1->stat.code == 200, "hello.txt 200");
        tc.check(r1->body_string() == "hello world", "hello.txt body");
        tc.check(r1->hdrs.get("content-type") == "text/plain",
              "hello.txt MIME type");
    }

    // Request 2: / (should map to index.html)
    auto url2 = std::format("http://127.0.0.1:{}/", port);
    auto r2 = client_t{}.get(url2);
    tc.check(r2.has_value(), "GET / succeeds");
    if (r2) {
        tc.check(r2->stat.code == 200, "index.html 200");
        tc.check(r2->body_string() == "<h1>home</h1>", "index.html body");
    }

    server_thread.join();
    std::filesystem::remove_all(tmp);
}

void test_mime_types() {
    tc.check(cppx::http::mime_type(".html") == "text/html", ".html");
    tc.check(cppx::http::mime_type(".css") == "text/css", ".css");
    tc.check(cppx::http::mime_type(".js") == "application/javascript", ".js");
    tc.check(cppx::http::mime_type(".wasm") == "application/wasm", ".wasm");
    tc.check(cppx::http::mime_type(".json") == "application/json", ".json");
    tc.check(cppx::http::mime_type(".png") == "image/png", ".png");
    tc.check(cppx::http::mime_type(".svg") == "image/svg+xml", ".svg");
    tc.check(cppx::http::mime_type(".xyz") == "application/octet-stream",
          "unknown extension");
}

int main() {
    test_mime_types();
    test_route_handler();
    test_static_file_serving();
    return tc.summary("cppx.http.server");
}
