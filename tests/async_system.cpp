// Deterministic tests for cppx.async.system using scripted fakes.

import cppx.async;
import cppx.async.test;
import cppx.async.system.test;
import cppx.http;
import cppx.test;
import std;

cppx::test::context tc;
using namespace std::chrono_literals;

namespace async_test = cppx::async::test;
namespace system_test = cppx::async::system::test;

auto to_bytes(std::string_view text) -> std::vector<std::byte> {
    auto bytes = std::vector<std::byte>{};
    bytes.reserve(text.size());
    for (auto ch : text)
        bytes.push_back(static_cast<std::byte>(ch));
    return bytes;
}

auto from_bytes(std::span<std::byte const> bytes) -> std::string {
    auto text = std::string{};
    text.reserve(bytes.size());
    for (auto byte : bytes)
        text.push_back(static_cast<char>(byte));
    return text;
}

// ---- round-trip ----------------------------------------------------------

auto scripted_roundtrip(system_test::test_network& network)
    -> cppx::async::task<bool> {
    auto scope = network.activate();
    auto listener_res = co_await system_test::test_listener::bind("127.0.0.1", 0);
    if (!listener_res)
        co_return false;

    auto listener = std::move(*listener_res);
    auto client_res = co_await system_test::test_stream::connect(
        "127.0.0.1", listener.local_port());
    if (!client_res)
        co_return false;

    auto client = std::move(*client_res);
    auto server_res = co_await listener.accept();
    if (!server_res)
        co_return false;

    auto server = std::move(*server_res);
    auto payload = to_bytes("async hello");
    auto send_res = co_await client.send(payload);
    if (!send_res || *send_res != payload.size())
        co_return false;

    auto buffer = std::vector<std::byte>(32);
    auto recv_res = co_await server.recv(buffer);
    if (!recv_res)
        co_return false;

    co_return from_bytes(std::span{buffer}.first(*recv_res)) == "async hello";
}

void test_deterministic_roundtrip() {
    system_test::test_network network;
    auto passed = async_test::run_test(
        [&](async_test::test_executor&) -> cppx::async::task<bool> {
            return scripted_roundtrip(network);
        });
    tc.check(passed, "deterministic async round-trip");
}

// ---- scripted errors -----------------------------------------------------

auto connect_refused(system_test::test_network& network)
    -> cppx::async::task<bool> {
    auto scope = network.activate();
    auto result = co_await system_test::test_stream::connect("127.0.0.1", 9000);
    co_return !result
        && result.error() == cppx::http::net_error::connect_refused;
}

void test_connect_refused() {
    system_test::test_network network;
    auto refused = async_test::run_test(
        [&](async_test::test_executor&) -> cppx::async::task<bool> {
            return connect_refused(network);
        });
    tc.check(refused, "connect refused is deterministic");
}

auto bind_failed(system_test::test_network& network)
    -> cppx::async::task<bool> {
    auto scope = network.activate();
    network.fail_next_bind();
    auto result = co_await system_test::test_listener::bind("127.0.0.1", 0);
    co_return !result && result.error() == cppx::http::net_error::bind_failed;
}

void test_bind_failed() {
    system_test::test_network network;
    auto failed = async_test::run_test(
        [&](async_test::test_executor&) -> cppx::async::task<bool> {
            return bind_failed(network);
        });
    tc.check(failed, "bind failure can be scripted");
}

// ---- accept wake-up ------------------------------------------------------

auto accept_wakeup_server(
    system_test::test_network& network,
    std::vector<std::string>& log,
    std::uint16_t& port) -> cppx::async::task<void> {
    auto scope = network.activate();
    auto listener_res = co_await system_test::test_listener::bind("127.0.0.1", 0);
    if (!listener_res)
        co_return;

    auto listener = std::move(*listener_res);
    port = listener.local_port();
    log.push_back("listening");

    auto server = co_await listener.accept();
    if (server)
        log.push_back("accepted");
}

auto accept_wakeup_client(
    system_test::test_network& network,
    std::vector<std::string>& log,
    std::uint16_t const& port) -> cppx::async::task<void> {
    auto scope = network.activate();
    while (port == 0)
        co_await async_test::delay{1ms};

    co_await async_test::delay{25ms};
    auto client = co_await system_test::test_stream::connect("127.0.0.1", port);
    if (client)
        log.push_back("connected");
}

void test_accept_wakeup() {
    system_test::test_network network;
    async_test::test_executor ex;
    std::vector<std::string> log;
    std::uint16_t port = 0;

    auto server_task = accept_wakeup_server(network, log, port);
    auto client_task = accept_wakeup_client(network, log, port);

    ex.schedule(server_task.handle());
    ex.schedule(client_task.handle());
    ex.advance_until_idle();
    server_task.result();
    client_task.result();

    tc.check_eq(static_cast<int>(log.size()), 3, "accept/connect flow completed");
    tc.check_eq(log[0], std::string{"listening"}, "listener starts first");
    tc.check_eq(log[1], std::string{"connected"}, "connect wakes accept waiter");
    tc.check_eq(log[2], std::string{"accepted"}, "accept resumes after connect");
}

// ---- recv wake-up + virtual time -----------------------------------------

auto recv_wakeup_server(
    system_test::test_network& network,
    std::vector<std::string>& log,
    std::uint16_t& port,
    std::string& received) -> cppx::async::task<void> {
    auto scope = network.activate();
    auto listener_res = co_await system_test::test_listener::bind("127.0.0.1", 0);
    if (!listener_res)
        co_return;

    auto listener = std::move(*listener_res);
    port = listener.local_port();
    log.push_back("listening");

    auto server = co_await listener.accept();
    if (!server)
        co_return;

    auto buffer = std::vector<std::byte>(16);
    auto recv_res = co_await server->recv(buffer);
    if (!recv_res)
        co_return;

    received = from_bytes(std::span{buffer}.first(*recv_res));
    log.push_back("received");
}

auto recv_wakeup_client(
    system_test::test_network& network,
    std::vector<std::string>& log,
    std::uint16_t const& port) -> cppx::async::task<void> {
    auto scope = network.activate();
    while (port == 0)
        co_await async_test::delay{1ms};

    auto client = co_await system_test::test_stream::connect("127.0.0.1", port);
    if (!client)
        co_return;

    auto sent = co_await client->send(to_bytes("wake"));
    if (sent)
        log.push_back("sent");
}

void test_recv_wakeup_and_virtual_time() {
    system_test::test_network network;
    network.set_connect_delay(10ms);
    network.set_accept_delay(15ms);
    network.set_send_delay(25ms);
    network.set_recv_delay(5ms);

    async_test::test_executor ex;
    std::vector<std::string> log;
    std::uint16_t port = 0;
    std::string received;

    auto server_task = recv_wakeup_server(network, log, port, received);
    auto client_task = recv_wakeup_client(network, log, port);

    ex.schedule(server_task.handle());
    ex.schedule(client_task.handle());
    ex.advance_until_idle();
    server_task.result();
    client_task.result();

    tc.check_eq(received, std::string{"wake"}, "recv wakes after send delivery");
    tc.check_eq(log[0], std::string{"listening"}, "server binds before traffic");
    tc.check_eq(log[1], std::string{"sent"}, "client send completes first");
    tc.check_eq(log[2], std::string{"received"}, "server recv resumes after send");
    tc.check(ex.current_time() == 40ms,
             "virtual time tracks connect, accept, send, and recv delays");
}

// ---- close behavior ------------------------------------------------------

auto close_then_recv(system_test::test_network& network)
    -> cppx::async::task<bool> {
    auto scope = network.activate();
    auto listener_res = co_await system_test::test_listener::bind("127.0.0.1", 0);
    if (!listener_res)
        co_return false;

    auto listener = std::move(*listener_res);
    auto client_res = co_await system_test::test_stream::connect(
        "127.0.0.1", listener.local_port());
    if (!client_res)
        co_return false;

    auto client = std::move(*client_res);
    auto server_res = co_await listener.accept();
    if (!server_res)
        co_return false;

    auto server = std::move(*server_res);
    client.close();

    auto buffer = std::vector<std::byte>(8);
    auto recv_res = co_await server.recv(buffer);
    co_return !recv_res
        && recv_res.error() == cppx::http::net_error::connection_closed;
}

void test_close_behavior() {
    system_test::test_network network;
    auto closed = async_test::run_test(
        [&](async_test::test_executor&) -> cppx::async::task<bool> {
            return close_then_recv(network);
        });
    tc.check(closed, "closing one side wakes recv with connection_closed");
}

int main() {
    test_deterministic_roundtrip();
    test_connect_refused();
    test_bind_failed();
    test_accept_wakeup();
    test_recv_wakeup_and_virtual_time();
    test_close_behavior();
    return tc.summary("cppx.async.system");
}
