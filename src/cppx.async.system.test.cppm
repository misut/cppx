// Deterministic test doubles for cppx.async.system. These fakes run on
// cppx.async.test::test_executor and let tests script bind/connect/accept,
// in-memory send/recv, and virtual-time delays without touching the OS.

export module cppx.async.system.test;

import cppx.async;
import cppx.async.test;
import cppx.net;
import cppx.net.async;
import std;

export namespace cppx::async::system::test {

using duration = cppx::async::test::test_executor::duration;

class test_network;
class test_stream;
class test_listener;

namespace detail {

inline thread_local test_network* current_network = nullptr;

struct waiter {
    cppx::async::test::test_executor* executor = nullptr;
    std::coroutine_handle<> handle = nullptr;
};

inline void wake(waiter entry) {
    if (!entry.executor || !entry.handle)
        return;
    entry.executor->schedule(entry.handle);
}

inline void wake_one(std::deque<waiter>& waiters) {
    if (waiters.empty())
        return;
    auto entry = waiters.front();
    waiters.pop_front();
    wake(entry);
}

inline void wake_all(std::deque<waiter>& waiters) {
    while (!waiters.empty())
        wake_one(waiters);
}

struct suspend_until_woken {
    std::deque<waiter>& waiters;

    auto await_ready() const noexcept -> bool {
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle) const {
        waiters.push_back({
            .executor = cppx::async::test::current_executor(),
            .handle = handle,
        });
    }

    void await_resume() const noexcept {}
};

struct byte_queue {
    std::deque<std::byte> bytes;
    bool closed = false;
    std::deque<waiter> recv_waiters;
};

struct stream_state {
    std::shared_ptr<byte_queue> incoming;
    std::shared_ptr<byte_queue> outgoing;
    bool closed = false;
};

struct listener_state {
    test_network* network = nullptr;
    std::uint16_t port = 0;
    bool closed = false;
    std::deque<std::shared_ptr<stream_state>> pending_streams;
    std::deque<waiter> accept_waiters;
};

} // namespace detail

class test_network {
public:
    using net_error = cppx::net::net_error;

    class activation {
    public:
        explicit activation(test_network& network)
            : previous_{std::exchange(detail::current_network, &network)} {}

        activation(activation const&) = delete;
        auto operator=(activation const&) -> activation& = delete;
        activation(activation&&) = delete;
        auto operator=(activation&&) -> activation& = delete;

        ~activation() {
            detail::current_network = previous_;
        }

    private:
        test_network* previous_ = nullptr;
    };

    auto activate() -> activation {
        return activation{*this};
    }

    void set_bind_delay(duration value) { bind_delay_ = value; }
    void set_connect_delay(duration value) { connect_delay_ = value; }
    void set_accept_delay(duration value) { accept_delay_ = value; }
    void set_send_delay(duration value) { send_delay_ = value; }
    void set_recv_delay(duration value) { recv_delay_ = value; }

    void fail_next_bind(net_error error = net_error::bind_failed) {
        next_bind_error_ = error;
    }

    void fail_next_connect(net_error error = net_error::connect_refused) {
        next_connect_error_ = error;
    }

    void fail_next_accept(net_error error = net_error::accept_failed) {
        next_accept_error_ = error;
    }

    void fail_next_send(net_error error = net_error::send_failed) {
        next_send_error_ = error;
    }

    void fail_next_recv(net_error error = net_error::recv_failed) {
        next_recv_error_ = error;
    }

private:
    friend class test_stream;
    friend class test_listener;

    auto take_next_bind_error() -> std::optional<net_error> {
        return take_error(next_bind_error_);
    }

    auto take_next_connect_error() -> std::optional<net_error> {
        return take_error(next_connect_error_);
    }

    auto take_next_accept_error() -> std::optional<net_error> {
        return take_error(next_accept_error_);
    }

    auto take_next_send_error() -> std::optional<net_error> {
        return take_error(next_send_error_);
    }

    auto take_next_recv_error() -> std::optional<net_error> {
        return take_error(next_recv_error_);
    }

    auto allocate_port() -> std::uint16_t {
        while (listeners_.contains(next_port_))
            ++next_port_;
        return next_port_++;
    }

    auto find_listener(std::uint16_t port)
        -> std::shared_ptr<detail::listener_state> {
        auto found = listeners_.find(port);
        if (found == listeners_.end())
            return {};
        return found->second;
    }

    static auto take_error(std::optional<net_error>& slot)
        -> std::optional<net_error> {
        auto value = slot;
        slot.reset();
        return value;
    }

    duration bind_delay_{};
    duration connect_delay_{};
    duration accept_delay_{};
    duration send_delay_{};
    duration recv_delay_{};

    std::optional<net_error> next_bind_error_;
    std::optional<net_error> next_connect_error_;
    std::optional<net_error> next_accept_error_;
    std::optional<net_error> next_send_error_;
    std::optional<net_error> next_recv_error_;

    std::uint16_t next_port_ = 41000;
    std::unordered_map<std::uint16_t, std::shared_ptr<detail::listener_state>>
        listeners_;
};

auto current_network() noexcept -> test_network* {
    return detail::current_network;
}

class test_stream {
public:
    using net_error = cppx::net::net_error;

    test_stream() = default;

    test_stream(test_stream const&) = delete;
    auto operator=(test_stream const&) -> test_stream& = delete;

    test_stream(test_stream&& other) noexcept
        : network_{std::exchange(other.network_, nullptr)},
          state_{std::move(other.state_)} {}

    auto operator=(test_stream&& other) noexcept -> test_stream& {
        if (this != &other) {
            close();
            network_ = std::exchange(other.network_, nullptr);
            state_ = std::move(other.state_);
        }
        return *this;
    }

    ~test_stream() {
        close();
    }

    static auto connect(std::string_view, std::uint16_t port)
        -> cppx::async::task<std::expected<test_stream, net_error>> {
        auto* network = current_network();
        if (!network)
            co_return std::unexpected(net_error::connect_refused);

        if (network->connect_delay_ > duration::zero())
            co_await cppx::async::test::delay{network->connect_delay_};

        if (auto error = network->take_next_connect_error())
            co_return std::unexpected(*error);

        auto listener = network->find_listener(port);
        if (!listener || listener->closed)
            co_return std::unexpected(net_error::connect_refused);

        auto client_to_server = std::make_shared<detail::byte_queue>();
        auto server_to_client = std::make_shared<detail::byte_queue>();

        auto client = std::make_shared<detail::stream_state>(
            detail::stream_state{
                .incoming = server_to_client,
                .outgoing = client_to_server,
                .closed = false,
            });
        auto server = std::make_shared<detail::stream_state>(
            detail::stream_state{
                .incoming = client_to_server,
                .outgoing = server_to_client,
                .closed = false,
            });

        listener->pending_streams.push_back(server);
        detail::wake_one(listener->accept_waiters);
        co_return test_stream{network, std::move(client)};
    }

    auto send(std::span<std::byte const> data)
        -> cppx::async::task<std::expected<std::size_t, net_error>> {
        if (!network_ || !state_ || state_->closed)
            co_return std::unexpected(net_error::send_failed);

        if (auto error = network_->take_next_send_error())
            co_return std::unexpected(*error);

        if (network_->send_delay_ > duration::zero())
            co_await cppx::async::test::delay{network_->send_delay_};

        if (state_->closed || !state_->outgoing || state_->outgoing->closed)
            co_return std::unexpected(net_error::send_failed);

        for (auto byte : data)
            state_->outgoing->bytes.push_back(byte);

        detail::wake_all(state_->outgoing->recv_waiters);
        co_return data.size();
    }

    auto recv(std::span<std::byte> buffer)
        -> cppx::async::task<std::expected<std::size_t, net_error>> {
        while (true) {
            if (!network_ || !state_)
                co_return std::unexpected(net_error::connection_closed);

            if (auto error = network_->take_next_recv_error())
                co_return std::unexpected(*error);

            if (!state_->incoming->bytes.empty()) {
                if (network_->recv_delay_ > duration::zero())
                    co_await cppx::async::test::delay{network_->recv_delay_};

                auto const count =
                    std::min(buffer.size(), state_->incoming->bytes.size());
                for (auto index = std::size_t{0}; index < count; ++index) {
                    buffer[index] = state_->incoming->bytes.front();
                    state_->incoming->bytes.pop_front();
                }
                co_return count;
            }

            if (state_->closed || state_->incoming->closed)
                co_return std::unexpected(net_error::connection_closed);

            co_await detail::suspend_until_woken{state_->incoming->recv_waiters};
        }
    }

    void close() {
        if (!state_ || state_->closed)
            return;

        state_->closed = true;
        if (state_->outgoing) {
            state_->outgoing->closed = true;
            detail::wake_all(state_->outgoing->recv_waiters);
        }
    }

private:
    friend class test_listener;

    explicit test_stream(
        test_network* network,
        std::shared_ptr<detail::stream_state> state)
        : network_{network},
          state_{std::move(state)} {}

    test_network* network_ = nullptr;
    std::shared_ptr<detail::stream_state> state_;
};

class test_listener {
public:
    using net_error = cppx::net::net_error;

    test_listener() = default;

    test_listener(test_listener const&) = delete;
    auto operator=(test_listener const&) -> test_listener& = delete;

    test_listener(test_listener&& other) noexcept
        : state_{std::move(other.state_)} {}

    auto operator=(test_listener&& other) noexcept -> test_listener& {
        if (this != &other) {
            close();
            state_ = std::move(other.state_);
        }
        return *this;
    }

    ~test_listener() {
        close();
    }

    static auto bind(std::string_view, std::uint16_t port)
        -> cppx::async::task<std::expected<test_listener, net_error>> {
        auto* network = current_network();
        if (!network)
            co_return std::unexpected(net_error::bind_failed);

        if (network->bind_delay_ > duration::zero())
            co_await cppx::async::test::delay{network->bind_delay_};

        if (auto error = network->take_next_bind_error())
            co_return std::unexpected(*error);

        auto assigned_port = port == 0 ? network->allocate_port() : port;
        if (auto existing = network->find_listener(assigned_port);
            existing && !existing->closed) {
            co_return std::unexpected(net_error::bind_failed);
        }

        auto state = std::make_shared<detail::listener_state>(
            detail::listener_state{
                .network = network,
                .port = assigned_port,
                .closed = false,
            });
        network->listeners_[assigned_port] = state;
        co_return test_listener{std::move(state)};
    }

    auto accept()
        -> cppx::async::task<std::expected<test_stream, net_error>> {
        while (true) {
            if (!state_ || state_->closed)
                co_return std::unexpected(net_error::accept_failed);

            if (auto error = state_->network->take_next_accept_error())
                co_return std::unexpected(*error);

            if (!state_->pending_streams.empty()) {
                if (state_->network->accept_delay_ > duration::zero())
                    co_await cppx::async::test::delay{
                        state_->network->accept_delay_};

                auto stream = std::move(state_->pending_streams.front());
                state_->pending_streams.pop_front();
                co_return test_stream{state_->network, std::move(stream)};
            }

            co_await detail::suspend_until_woken{state_->accept_waiters};
        }
    }

    void close() {
        if (!state_ || state_->closed)
            return;

        state_->closed = true;
        if (state_->network)
            state_->network->listeners_.erase(state_->port);
        detail::wake_all(state_->accept_waiters);
    }

    auto local_port() const -> std::uint16_t {
        return state_ ? state_->port : 0;
    }

private:
    explicit test_listener(std::shared_ptr<detail::listener_state> state)
        : state_{std::move(state)} {}

    std::shared_ptr<detail::listener_state> state_;
};

static_assert(cppx::net::async::stream_engine<test_stream>);
static_assert(cppx::net::async::listener_engine<test_listener, test_stream>);

} // namespace cppx::async::system::test
