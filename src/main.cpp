#include <cassert>
#include <exception>
#include <format>
#include <print>
#include <coroutine>
#include <deque>
#include <source_location>
#include "app_instant.hpp"
#include "app_timer.hpp"

namespace co::allocator {
/**
 * @brief a simple memory pool
 * @tparam S the size of maximum single element
 * @tparam N the number of elements in the pool
 * @note the total size of the pool is `S * N`
 */
template <std::size_t S, std::size_t N>
struct pool {
	constexpr static auto el_size  = S;
	constexpr static auto el_count = N;

	struct element {
		std::byte data[S];
		bool occupied;
	};

	pool() {
		for (auto &e : _elements) {
			e.occupied = false;
		}
	}

	/**
	 * @brief allocate the memory
	 * @note this is not thread-safe
	 * @note would return nullptr if the pool is full
	 * @note debug mode coroutine size should be 168 bytes, release mode
	 * coroutine size should be 48 bytes
	 */
	void *allocate(std::size_t size) noexcept {
		if (size > el_size) {
			return nullptr;
		}
		size_t i{};
		for (auto &e : _elements) {
			if (not e.occupied) {
				std::println("[allocate] size={} (limit={}, index={})", size, el_size, i);
				e.occupied = true;
				return reinterpret_cast<void *>(e.data);
			}
			i += 1;
		}
		return nullptr;
	}

	/**
	 * @brief deallocate the memory
	 * @note this is not thread-safe
	 * @note would terminate if the pointer is not allocated by this pool
	 */
	void deallocate(void *ptr) noexcept {
		size_t i{};
		for (auto &e : _elements) {
			auto p = reinterpret_cast<void *>(e.data);
			if (p == ptr) {
				e.occupied = false;
				std::println("[deallocate] index={}", i);
				return;
			}
			i += 1;
		}
		std::terminate();
	}

private:
	std::array<element, N> _elements;
};
}

namespace app::global {
/**
 * @brief the global coroutine memory pool
 */
inline co::allocator::pool<256, 8> pool{};
}

namespace co::scheduler {
struct IPollable { // NOLINT(cppcoreguidelines-special-member-functions) virtual interface
	[[nodiscard]]
	virtual bool poll() const = 0;
	virtual ~IPollable()      = default;
};
}

namespace co {
/**
 * @brief a `nothing`, just for the sake of being able to use `co_await`
 * @see https://en.cppreference.com/w/cpp/language/coroutines.html
 */
struct pollable_task {
	struct promise_type : scheduler::IPollable {
		using any_handle      = std::coroutine_handle<>;
		using pollable_handle = std::coroutine_handle<pollable_task::promise_type>;

		std::suspend_never initial_suspend() { return {}; }
		std::suspend_never final_suspend() noexcept { return {}; }
		void return_void() {}
		pollable_task get_return_object() { return {}; }
		void unhandled_exception() { std::terminate(); }

		[[nodiscard]]
		pollable_handle coroutine_handle() const {
			return pollable_handle::from_promise(
				const_cast<promise_type &>(*this)); // NOLINT(cppcoreguidelines-pro-type-const-cast)
		}

		[[nodiscard]]
		bool poll() const override { return pollable->poll(); }

		void *operator new(std::size_t size) noexcept {
			auto p = app::global::pool.allocate(size);
			assert(p != nullptr);
			return p;
		}

		void operator delete(void *ptr) noexcept {
			app::global::pool.deallocate(ptr);
		}

		/** properties */
		std::unique_ptr<IPollable> pollable;
	};
};

struct timer_pollable : scheduler::IPollable {
	using time_point = app::timer::monotonic_clock::time_point;
	time_point deadline;

	timer_pollable(time_point deadline) : deadline(deadline) {}

	[[nodiscard]]
	bool poll() const override { return app::timer::monotonic_clock::now() >= deadline; }
};
using pollable_co_handle = std::coroutine_handle<pollable_task::promise_type>;
}


namespace co::scheduler {
struct simple_scheduler {
	/**
	 * @note Specialization std::coroutine_handle<void> erases the promise type.
	 * It is convertible from other specializations.
	 * @see https://en.cppreference.com/w/cpp/coroutine/coroutine_handle
	 */
	using any_handle         = std::coroutine_handle<>;
	using pollable_co_handle = co::pollable_co_handle;

	simple_scheduler() = default;

	void push_back(pollable_co_handle pollable) {
		_coroutines.emplace_back(pollable);
	}

	/**
	 * @brief iterate over the pollables
	 */
	void poll_once() {
		using namespace std::chrono_literals;
		static auto instant = app::timer::Instant::now();
		if (instant.mut_every(1'000ms)) {
			std::println("[{}ms] len(coro)={}",
						 app::timer::monotonic_clock::now().time_since_epoch().count(),
						 _coroutines.size());
		}
		auto it = _coroutines.begin();
		while (it != _coroutines.end()) {
			auto &coroutine = *it;
			auto &pollable  = coroutine.promise();
			if (pollable.poll()) {
				coroutine.resume();
				// should be re-pushed by awaitable
				it = _coroutines.erase(it);
			} else {
				++it;
			}
		}
	}

	[[nodiscard]]
	bool empty() const noexcept { return _coroutines.empty(); }

	/** properties */
	std::deque<pollable_co_handle> _coroutines;
};
}


namespace app::global {
inline co::scheduler::simple_scheduler scheduler;
}


namespace co {
/**
 * @note this awaitable only works with `co::pollable_task` coroutine
 * @see co::timer_pollable
 * @see co::pollable_task
 * @see https://devblogs.microsoft.com/oldnewthing/20191209-00/?p=103195
 */
struct timer_awaitable {
	using time_point = app::timer::monotonic_clock::time_point;

	[[nodiscard]]
	bool is_ready() const {
		return app::timer::monotonic_clock::now() >= deadline;
	}

	bool await_ready() const { return is_ready(); }

	bool await_suspend(pollable_co_handle hdl) {
		if (is_ready()) {
			return false;
		}
		/**
		 * assign the deadline to the promise
		 * otherwise, the `promise_type` wouldn't even know
		 * its deadline
		 *
		 * awaitable just live in `co_await` realm?
		 */
		hdl.promise().pollable = std::make_unique<co::timer_pollable>(deadline);
		app::global::scheduler.push_back(hdl);
		return true;
	}

	void await_resume() const {}

	/** properties */
	time_point deadline;
};

/**
 * @brief create a timer awaitable
 */
template <typename Rep, typename Period>
auto delay(std::chrono::duration<Rep, Period> duration) -> timer_awaitable {
	return timer_awaitable{app::timer::monotonic_clock::now() +
						   std::chrono::duration_cast<app::timer::monotonic_clock::duration>(duration)};
}

pollable_task fake_blink() {
	using namespace std::chrono_literals;
	static constexpr auto TASK_IDX = 0;
	std::println("s{}", TASK_IDX);
	constexpr auto log = [](uint32_t line) {
		std::println("[{}ms] {} ({})",
					 app::timer::monotonic_clock::now().time_since_epoch().count(),
					 line,
					 TASK_IDX);
	};
	co_await delay(1s);
	log(std::source_location::current().line());
	co_await delay(1s);
	log(std::source_location::current().line());
	co_await delay(500ms);
	log(std::source_location::current().line());
	co_await delay(250ms);
	log(std::source_location::current().line());
	co_await delay(250ms);
	log(std::source_location::current().line());
	co_await delay(3s);
	log(std::source_location::current().line());
	std::println("e{}", TASK_IDX);
}

pollable_task fake_blink_2() {
	using namespace std::chrono_literals;
	static constexpr auto TASK_IDX = 1;
	std::println("s{}", TASK_IDX);
	constexpr auto log = [](uint32_t line) {
		std::println("[{}ms] {} ({})",
					 app::timer::monotonic_clock::now().time_since_epoch().count(),
					 line,
					 TASK_IDX);
	};
	co_await delay(2s);
	log(std::source_location::current().line());
	co_await delay(500ms);
	log(std::source_location::current().line());
	co_await delay(2s);
	log(std::source_location::current().line());
	co_await delay(1s);
	log(std::source_location::current().line());
	co_await delay(250ms);
	log(std::source_location::current().line());
	co_await delay(3s);
	log(std::source_location::current().line());
	std::println("e{}", TASK_IDX);
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
