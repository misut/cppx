// Opt-in smoke tests for cppx.async.system with real event-loop I/O.

import cppx.async;
import cppx.async.system;
import cppx.net;
import cppx.test;
import std;

cppx::test::context tc;

#if !defined(__wasi__)
using namespace std::chrono_literals;

auto smoke_enabled() -> bool {
    auto const* value = std::getenv("CPPX_RUN_ASYNC_SYSTEM_SMOKE");
    return value && std::string_view{value} == "1";
}

cppx::async::task<void> tcp_roundtrip_test(bool& passed) {
    auto listener_res = co_await cppx::async::system::async_listener::bind(
        "127.0.0.1", 0);
    if (!listener_res) {
        passed = false;
        co_return;
    }
    auto listener = std::move(*listener_res);
    auto port = listener.local_port();

    auto client_res = co_await cppx::async::system::async_stream::connect(
        "127.0.0.1", port);
    if (!client_res) {
        listener.close();
        passed = false;
        co_return;
    }
    auto client = std::move(*client_res);

    auto server_res = co_await listener.accept();
    if (!server_res) {
        client.close();
        listener.close();
        passed = false;
        co_return;
    }
    auto server = std::move(*server_res);

    auto payload = std::string{"async hello"};
    auto bytes = std::vector<std::byte>{};
    for (auto ch : payload)
        bytes.push_back(static_cast<std::byte>(ch));

    auto send_res = co_await client.send(bytes);
    if (!send_res) {
        passed = false;
        client.close();
        server.close();
        listener.close();
        co_return;
    }

    auto buffer = std::vector<std::byte>(64);
    auto recv_res = co_await server.recv(buffer);
    if (!recv_res) {
        passed = false;
        client.close();
        server.close();
        listener.close();
        co_return;
    }

    auto received = std::string{
        reinterpret_cast<char const*>(buffer.data()), *recv_res};
    passed = received == payload;

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

cppx::async::task<void> connect_refused_test(bool& got_error) {
    auto res = co_await cppx::async::system::async_stream::connect(
        "127.0.0.1", 1);
    got_error = !res.has_value()
        && res.error() == cppx::net::net_error::connect_refused;
}

void test_connect_refused() {
    bool got_error = false;
    auto t = connect_refused_test(got_error);
    cppx::async::system::run(t);
    tc.check(got_error, "async connect refused");
}

void test_event_loop_stops() {
    cppx::async::system::event_loop loop;
    bool done = false;
    auto coro = [](bool& flag) -> cppx::async::task<void> {
        flag = true;
        co_return;
    };
    auto t = coro(done);
    loop.schedule(t.handle());
    loop.run();
    t.result();
    tc.check(done, "event loop stops when no work remains");
}

cppx::async::task<void> sleep_for_test(
    bool& resumed,
    std::chrono::milliseconds& elapsed) {
    auto const start = std::chrono::steady_clock::now();
    co_await cppx::async::system::sleep_for(50ms);
    elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    resumed = true;
}

void test_sleep_for() {
    bool resumed = false;
    auto elapsed = std::chrono::milliseconds{0};
    auto t = sleep_for_test(resumed, elapsed);
    cppx::async::system::event_loop loop;
    loop.schedule(t.handle());
    loop.run();
    t.result();
    tc.check(resumed, "sleep_for resumes coroutine");
    tc.check(elapsed >= 30ms,
             "sleep_for waits for approximately the requested duration");
}

#endif // !__wasi__

int main() {
#if !defined(__wasi__)
    if (!smoke_enabled()) {
        std::println(
            "cppx.async.system smoke skipped (set CPPX_RUN_ASYNC_SYSTEM_SMOKE=1 to enable)");
        return 0;
    }

    test_tcp_roundtrip();
    test_connect_refused();
    test_event_loop_stops();
    test_sleep_for();
#endif
    return tc.summary("cppx.async.system.smoke");
}
