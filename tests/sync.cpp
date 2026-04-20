import cppx.sync;
import cppx.test;
import std;

cppx::test::context tc;

void test_work_queue_single_producer_single_consumer() {
    cppx::sync::work_queue<int> queue;
    tc.check(!queue.closed(), "fresh work_queue starts open");
    tc.check(queue.empty(), "fresh work_queue starts empty");
    tc.check_eq(queue.size(), std::size_t{0}, "fresh work_queue size is zero");

    tc.check(queue.push(10), "push first item succeeds");
    tc.check(queue.push(20), "push second item succeeds");
    tc.check_eq(queue.size(), std::size_t{2}, "work_queue size tracks queued items");

    auto first = queue.try_pop();
    tc.check(first.has_value(), "try_pop returns first item");
    if (first)
        tc.check_eq(*first, 10, "work_queue preserves FIFO order");

    auto second = queue.wait_pop();
    tc.check(second.has_value(), "wait_pop returns second item");
    if (second)
        tc.check_eq(*second, 20, "wait_pop drains remaining item");

    tc.check(queue.empty(), "work_queue empty after draining");
    tc.check_eq(queue.size(), std::size_t{0}, "work_queue size drops after draining");
}

void test_work_queue_close_while_waiting() {
    cppx::sync::work_queue<int> queue;
    auto ready = std::promise<void>{};
    auto popped = std::promise<std::optional<int>>{};
    auto popped_future = popped.get_future();

    auto waiter = std::thread{[&] {
        ready.set_value();
        popped.set_value(queue.wait_pop());
    }};

    ready.get_future().wait();
    queue.close();

    auto result = popped_future.get();
    tc.check(!result.has_value(), "wait_pop returns nullopt after close while waiting");
    waiter.join();
    tc.check(queue.closed(), "work_queue reports closed after close()");
}

void test_work_queue_rejects_push_after_close() {
    cppx::sync::work_queue<std::string> queue;
    queue.close();

    tc.check(!queue.push("later"), "push after close is rejected");
    tc.check(queue.closed(), "work_queue remains closed");
    tc.check(queue.empty(), "closed work_queue stays empty when push is rejected");
}

void test_work_queue_multi_threaded_stress() {
    cppx::sync::work_queue<int> queue;

    constexpr int producer_count = 4;
    constexpr int consumer_count = 4;
    constexpr int items_per_producer = 250;
    constexpr int total_items = producer_count * items_per_producer;

    auto seen = std::vector<std::atomic<int>>(total_items);
    for (auto& slot : seen)
        slot.store(0);

    auto consumed = std::atomic<int>{0};
    auto producers = std::vector<std::thread>{};
    auto consumers = std::vector<std::thread>{};

    for (int producer = 0; producer < producer_count; ++producer) {
        producers.emplace_back([&, producer] {
            auto base = producer * items_per_producer;
            for (int offset = 0; offset < items_per_producer; ++offset) {
                auto pushed = queue.push(base + offset);
                if (!pushed)
                    return;
            }
        });
    }

    for (int consumer = 0; consumer < consumer_count; ++consumer) {
        consumers.emplace_back([&] {
            for (;;) {
                auto item = queue.wait_pop();
                if (!item)
                    break;
                seen.at(static_cast<std::size_t>(*item)).fetch_add(1);
                consumed.fetch_add(1);
            }
        });
    }

    for (auto& producer : producers)
        producer.join();

    queue.close();

    for (auto& consumer : consumers)
        consumer.join();

    tc.check_eq(consumed.load(), total_items,
                "multi-threaded stress consumes every queued item exactly once");

    bool every_item_seen_once = true;
    for (auto const& slot : seen) {
        if (slot.load() != 1) {
            every_item_seen_once = false;
            break;
        }
    }
    tc.check(every_item_seen_once,
             "multi-threaded stress marks every item exactly once");
}

void test_coalescing_queue_duplicate_suppression() {
    cppx::sync::coalescing_queue<std::string, int> queue;

    tc.check(queue.push("alpha", 1), "first coalescing push succeeds");
    tc.check(!queue.push("alpha", 2), "duplicate queued key is suppressed");
    tc.check(queue.push("beta", 3), "distinct key still queues");
    tc.check_eq(queue.size(), std::size_t{2}, "coalescing_queue size counts unique queued keys");

    auto first = queue.wait_pop();
    tc.check(first.has_value(), "first coalescing wait_pop succeeds");
    if (first) {
        tc.check_eq(first->key, std::string{"alpha"}, "first coalescing pop returns key");
        tc.check_eq(first->value, 1, "first coalescing pop returns original value");
    }

    auto second = queue.try_pop();
    tc.check(second.has_value(), "second coalescing try_pop succeeds");
    if (second) {
        tc.check_eq(second->key, std::string{"beta"}, "second coalescing pop preserves FIFO");
        tc.check_eq(second->value, 3, "second coalescing pop preserves payload");
    }
}

void test_coalescing_queue_releases_key_after_pop() {
    cppx::sync::coalescing_queue<std::string, int> queue;

    tc.check(queue.push("alpha", 1), "coalescing_queue accepts initial key");
    auto first = queue.wait_pop();
    tc.check(first.has_value(), "coalescing_queue returns popped item");
    tc.check(queue.push("alpha", 2), "popped key can be queued again immediately");

    queue.close();
    auto second = queue.wait_pop();
    tc.check(second.has_value(), "closed coalescing_queue still drains queued work");
    if (second) {
        tc.check_eq(second->key, std::string{"alpha"}, "re-queued key stays visible in pop result");
        tc.check_eq(second->value, 2, "re-queued value is preserved");
    }

    auto drained = queue.wait_pop();
    tc.check(!drained.has_value(), "closed coalescing_queue becomes terminal after draining");
    tc.check(!queue.push("alpha", 3), "coalescing_queue rejects push after close");
}

void test_background_worker_close_and_join() {
    cppx::sync::work_queue<int> queue;
    auto entered = std::promise<void>{};
    auto exited = std::atomic<bool>{false};

    cppx::sync::background_worker worker{
        [&](std::stop_token) {
            entered.set_value();
            auto item = queue.wait_pop();
            (void)item;
            exited.store(true);
        },
        {.on_stop = [&] { queue.close(); }},
    };

    entered.get_future().wait();
    worker.close();

    tc.check(exited.load(), "background_worker close wakes queue wait and joins");
    tc.check(!worker.joinable(), "background_worker is not joinable after close");
    tc.check(worker.stop_requested(), "background_worker stop token is requested during close");
}

void test_background_worker_destructor_teardown() {
    cppx::sync::work_queue<int> queue;
    auto entered = std::promise<void>{};
    auto exited = std::atomic<bool>{false};

    {
        cppx::sync::background_worker worker{
            [&](std::stop_token) {
                entered.set_value();
                auto item = queue.wait_pop();
                (void)item;
                exited.store(true);
            },
            {.on_stop = [&] { queue.close(); }},
        };

        entered.get_future().wait();
    }

    tc.check(exited.load(), "background_worker destructor closes and joins safely");
}

void test_background_worker_exception_capture_and_reporting() {
    auto callback_called = std::atomic<bool>{false};
    auto callback_exception = std::exception_ptr{};

    cppx::sync::background_worker worker{
        [](std::stop_token) {
            throw std::runtime_error{"worker failure"};
        },
        {.on_exception = [&](std::exception_ptr failure) {
            callback_called.store(true);
            callback_exception = failure;
        }},
    };

    worker.join();

    tc.check(callback_called.load(), "background_worker reports worker exception");
    tc.check(callback_exception != nullptr, "background_worker passes exception to callback");
    tc.check(worker.exception() != nullptr, "background_worker stores worker exception");

    bool threw = false;
    try {
        worker.rethrow_if_failed();
    } catch (std::runtime_error const& error) {
        threw = true;
        tc.check_eq(std::string_view{error.what()},
                    std::string_view{"worker failure"},
                    "background_worker preserves exception message");
    }
    tc.check(threw, "background_worker rethrows stored worker exception");
}

int main() {
    test_work_queue_single_producer_single_consumer();
    test_work_queue_close_while_waiting();
    test_work_queue_rejects_push_after_close();
    test_work_queue_multi_threaded_stress();
    test_coalescing_queue_duplicate_suppression();
    test_coalescing_queue_releases_key_after_pop();
    test_background_worker_close_and_join();
    test_background_worker_destructor_teardown();
    test_background_worker_exception_capture_and_reporting();
    return tc.summary("cppx.sync");
}
