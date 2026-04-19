import cppx.async;
import cppx.async.test;
import cppx.http;
import cppx.net;
import cppx.net.async;
import cppx.test;
import std;

cppx::test::context tc;

static_assert(std::same_as<cppx::http::net_error, cppx::net::net_error>);

void test_net_error_strings() {
    tc.check(cppx::net::to_string(cppx::net::net_error::connect_refused)
                 == "connect_refused",
             "cppx.net to_string");
    tc.check(cppx::http::to_string(cppx::http::net_error::bind_failed)
                 == "bind_failed",
             "cppx.http net_error compatibility alias");
}

void test_null_sync_helpers() {
    auto stream = cppx::net::null_stream::connect("example.com", 80);
    tc.check(!stream.has_value(), "null_stream connect fails");
    tc.check(stream.error() == cppx::net::net_error::connect_refused,
             "null_stream error");

    auto listener = cppx::net::null_listener::bind("127.0.0.1", 0);
    tc.check(!listener.has_value(), "null_listener bind fails");
    tc.check(listener.error() == cppx::net::net_error::bind_failed,
             "null_listener error");
}

void test_null_async_helpers() {
    auto connect_failed = cppx::async::test::run_test(
        [](cppx::async::test::test_executor&)
            -> cppx::async::task<bool> {
            auto stream = co_await cppx::net::async::null_stream::connect(
                "example.com", 80);
            co_return !stream.has_value()
                && stream.error() == cppx::net::net_error::connect_refused;
        });
    tc.check(connect_failed, "async null_stream connect fails");

    auto bind_failed = cppx::async::test::run_test(
        [](cppx::async::test::test_executor&)
            -> cppx::async::task<bool> {
            auto listener = co_await cppx::net::async::null_listener::bind(
                "127.0.0.1", 0);
            co_return !listener.has_value()
                && listener.error() == cppx::net::net_error::bind_failed;
        });
    tc.check(bind_failed, "async null_listener bind fails");
}

int main() {
    test_net_error_strings();
    test_null_sync_helpers();
    test_null_async_helpers();
    return tc.summary("cppx.net");
}
