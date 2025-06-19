#include <exception>
#include <format>
#include <print>
#include <coroutine>
#include <deque>
#include <source_location>
#include "app_timer.hpp"

namespace co::scheduler {
struct IPollable { // NOLINT(cppcoreguidelines-special-member-functions) virtual interface
	using pollable_handle = std::coroutine_handle<IPollable>;
	[[nodiscard]]
	virtual pollable_handle promise_handle() const = 0;
	[[nodiscard]]
	virtual bool poll_ready() const = 0;
	virtual ~IPollable()            = default;
};

struct simple_scheduler {
	using any_handle      = std::coroutine_handle<>;
	using pollable_handle = IPollable::pollable_handle;
	/**
	 * @note Specialization std::coroutine_handle<void> erases the promise type.
	 * It is convertible from other specializations.
	 * @see https://en.cppreference.com/w/cpp/coroutine/coroutine_handle
	 */

	simple_scheduler() = default;

	void push_back(pollable_handle pollable) {
		_pollables.emplace_back(pollable);
	}

	/**
	 * @brief iterate over the pollables
	 */
	void poll_once() {
		auto it = _pollables.begin();
		while (it != _pollables.end()) {
			auto &pollable = (*it).promise();
			if (pollable.poll_ready()) {
				pollable.promise_handle().resume();
				it = _pollables.erase(it);
			} else {
				++it;
			}
		}
	}

	[[nodiscard]]
	bool empty() const noexcept { return _pollables.empty(); }

	/** properties */
	std::deque<pollable_handle> _pollables;
};
}


namespace app::global {
inline co::scheduler::simple_scheduler scheduler;
}


namespace co {
/**
 * we haven't touch awaitable yet
 * @see https://devblogs.microsoft.com/oldnewthing/20191209-00/?p=103195
 */


/**
 * @brief a `nothing`, just for the sake of being able to use `co_await`
 * @see https://en.cppreference.com/w/cpp/language/coroutines.html
 */
struct delay_task {
	struct promise_type : scheduler::IPollable {
		using any_handle      = std::coroutine_handle<>;
		using pollable_handle = std::coroutine_handle<IPollable>;
		using time_point      = app::timer::monotonic_clock::time_point;

		std::suspend_never initial_suspend() { return {}; }
		std::suspend_never final_suspend() noexcept { return {}; }
		void return_void() {}
		delay_task get_return_object() { return {}; }
		void unhandled_exception() { std::terminate(); }

		[[nodiscard]]
		pollable_handle promise_handle() const override {
			return pollable_handle::from_promise(
				const_cast<promise_type &>(*this)); // NOLINT(cppcoreguidelines-pro-type-const-cast)
		}
		[[nodiscard]]
		bool poll_ready() const override { return app::timer::monotonic_clock::now() >= deadline; }

		/** properties */
		time_point deadline;
	};
};

struct delay_awaitable {
	using any_handle      = std::coroutine_handle<>;
	using pollable_handle = scheduler::IPollable::pollable_handle;
	using concrete_handle = std::coroutine_handle<delay_task::promise_type>;
	using time_point      = app::timer::monotonic_clock::time_point;
	using IPollable       = scheduler::IPollable;
	using scheduler       = co::scheduler::simple_scheduler;

	[[nodiscard]]
	bool is_ready() const {
		return app::timer::monotonic_clock::now() >= deadline;
	}

	bool await_ready() const { return is_ready(); }

	bool await_suspend(concrete_handle hdl) {
		if (is_ready()) {
			return false;
		}
		// assign the deadline to the promise
		// otherwise, the `promise_type` wouldn't even know
		// its deadline
		//
		// awaitable just live in `co_await` realm?
		hdl.promise().deadline = deadline;
		app::global::scheduler.push_back(std::coroutine_handle<IPollable>::from_address(hdl.address()));
		return true;
	}

	void await_resume() const {}

	/** properties */
	time_point deadline;
};

template <typename Rep, typename Period>
auto delay(std::chrono::duration<Rep, Period> ms) -> delay_awaitable {
	return delay_awaitable{app::timer::monotonic_clock::now() + std::chrono::duration_cast<app::timer::monotonic_clock::duration>(ms)};
}

delay_task fake_blink() {
	using namespace std::chrono_literals;
	std::println("s0");
	constexpr auto log = [](uint32_t line) {
		constexpr auto TASK_IDX = 0;
		std::println("[{}ms] {} ({})",
					 app::timer::monotonic_clock::now().time_since_epoch().count(),
					 line,
					 TASK_IDX);
	};
	co_await delay(1'000ms);
	log(std::source_location::current().line());
	co_await delay(1'000ms);
	log(std::source_location::current().line());
	co_await delay(500ms);
	log(std::source_location::current().line());
	co_await delay(250ms);
	log(std::source_location::current().line());
	co_await delay(250ms);
	log(std::source_location::current().line());
	co_await delay(3'000ms);
	log(std::source_location::current().line());
}

delay_task fake_blink_2() {
	using namespace std::chrono_literals;
	std::println("s1");
	constexpr auto log = [](uint32_t line) {
		constexpr auto TASK_IDX = 1;
		std::println("[{}ms] {} ({})",
					 app::timer::monotonic_clock::now().time_since_epoch().count(),
					 line,
					 TASK_IDX);
	};
	co_await delay(2'000ms);
	log(std::source_location::current().line());
	co_await delay(500ms);
	log(std::source_location::current().line());
	co_await delay(2'000ms);
	log(std::source_location::current().line());
	co_await delay(1'000ms);
	log(std::source_location::current().line());
	co_await delay(250ms);
	log(std::source_location::current().line());
	co_await delay(3'000ms);
	log(std::source_location::current().line());
}

}

int main() {
	auto &scheduler = app::global::scheduler;
	co::fake_blink();
	co::fake_blink_2();
	std::println("start");

	// keep looping until the scheduler becomes truly empty
	while (not scheduler.empty()) {
		scheduler.poll_once();
	}

	std::println("done");
}
