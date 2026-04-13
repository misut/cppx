// Smoke tests for cppx.async.system — event loop with real I/O.
// Uses async_stream and async_listener for a localhost TCP round-trip.

import cppx.async;
import cppx.async.system;
import cppx.http;
import cppx.test;
import std;

cppx::test::context tc;

#if !defined(__wasi__) && !defined(_WIN32)

// ---- TCP round-trip via event loop ---------------------------------------

cppx::async::task<void> tcp_roundtrip_test(bool& passed) {
    // Bind listener on ephemeral port.
    auto listener_res = co_await cppx::async::system::async_listener::bind(
        "127.0.0.1", 0);
    if (!listener_res) {
        passed = false;
        co_return;
    }
    auto listener = std::move(*listener_res);
    auto port = listener.local_port();

    // Connect client.
    auto client_res = co_await cppx::async::system::async_stream::connect(
        "127.0.0.1", port);
    if (!client_res) {
        listener.close();
        passed = false;
        co_return;
    }
    auto client = std::move(*client_res);

    // Accept server-side connection.
    auto server_res = co_await listener.accept();
    if (!server_res) {
        client.close();
        listener.close();
        passed = false;
        co_return;
    }
    auto server = std::move(*server_res);

    // Send from client.
    auto payload = std::string{"async hello"};
    auto bytes = std::vector<std::byte>{};
    for (auto c : payload)
        bytes.push_back(static_cast<std::byte>(c));
    auto send_res = co_await client.send(bytes);
    if (!send_res) {
        passed = false;
        client.close();
        server.close();
        listener.close();
        co_return;
    }

    // Receive on server.
    auto buf = std::vector<std::byte>(64);
    auto recv_res = co_await server.recv(buf);
    if (!recv_res) {
        passed = false;
        client.close();
        server.close();
        listener.close();
        co_return;
    }

    auto received = std::string{
        reinterpret_cast<char const*>(buf.data()), *recv_res};
    passed = (received == payload);

    client.close();
    server.close();
    listener.close();
}

void test_tcp_roundtrip() {
    bool passed = false;
    auto t = tcp_roundtrip_test(passed);
    cppx::async::system::run(t);
    tc.check(passed, "async TCP round-trip");
}

// ---- connect refused -----------------------------------------------------

cppx::async::task<void> connect_refused_test(bool& got_error) {
    auto res = co_await cppx::async::system::async_stream::connect(
        "127.0.0.1", 1);
    got_error = !res.has_value() &&
        res.error() == cppx::http::net_error::connect_refused;
}

void test_connect_refused() {
    bool got_error = false;
    auto t = connect_refused_test(got_error);
    cppx::async::system::run(t);
    tc.check(got_error, "async connect refused");
}

// ---- event_loop stops when no work remains -------------------------------

void test_event_loop_stops() {
    cppx::async::system::event_loop loop;
    bool done = false;
    auto coro = [](bool& d) -> cppx::async::task<void> {
        d = true;
        co_return;
    };
    auto t = coro(done);
    loop.schedule(t.handle());
    loop.run(); // should return, not hang
    tc.check(done, "event loop stops when no work remains");
}

#endif // !__wasi__ && !_WIN32

int main() {
#if !defined(__wasi__) && !defined(_WIN32)
    test_tcp_roundtrip();
    test_connect_refused();
    test_event_loop_stops();
#endif
    return tc.summary("cppx.async.system");
}
