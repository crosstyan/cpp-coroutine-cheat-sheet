#include <algorithm>
#include <exception>
#include <format>
#include <print>
#include <future>
#include <coroutine>
#include <deque>
#include <type_traits>
#include <vector>
#include <memory>
#include "app_timer.hpp"

namespace std {
template <typename R, typename... Args>
struct coroutine_traits<std::future<R>, Args...> {
	struct promise_type {
		std::promise<R> p;

		/** like a constructor */
		std::suspend_never initial_suspend() noexcept { return {}; }
		/** like a destructor */
		std::suspend_never final_suspend() noexcept { return {}; }
		/** when the `co_return` is called */
		void return_value(R value) {
			p.set_value(value);
		}
		/** construct the return object (the first parameter of `coroutine_traits`) */
		auto get_return_object() -> std::future<R> {
			return p.get_future();
		}
		/** exception CANNOT leak from the coroutine */
		void unhandled_exception() {
			p.set_exception(std::current_exception());
		}
	};
};
}

namespace co {
/**
 * @see https://github.com/GorNishanov/await/blob/327fe6a49cc91079b5fa4d6c5ea79b0d7dde53a4/2018_CppCon/src/coro_infra.h#L8-L34
 */
struct scheduler_queue {
	static constexpr const int N = 256;
	using coro_handle            = std::coroutine_handle<>;

	uint32_t head = 0;
	uint32_t tail = 0;
	coro_handle arr[N];

	void push_back(coro_handle h) {
		arr[head] = h;
		head      = (head + 1) % N;
	}

	coro_handle pop_front() {
		auto result = arr[tail];
		tail        = (tail + 1) % N;
		return result;
	}
	/**
	 * @note `coro_handle` might be nullptr
	 */
	auto try_pop_front() { return head != tail ? pop_front() : coro_handle{}; }

	void run() {
		while (auto h = try_pop_front()) {
			h.resume();
		}
	}
};

struct simple_scheduler {
	using coro_handle = std::coroutine_handle<>;
	/**
	 * @note Specialization std::coroutine_handle<void> erases the promise type.
	 * It is convertible from other specializations.
	 * @see https://en.cppreference.com/w/cpp/coroutine/coroutine_handle
	 */
	std::deque<coro_handle> _m_conts;

	// generic event list
	struct poll_event { // NOLINT(cppcoreguidelines-special-member-functions) virtual interface
		coro_handle handle;
		[[nodiscard]]
		virtual bool poll_ready() const = 0; // true when event is ready
		virtual ~poll_event()           = default;
	};
	std::vector<std::unique_ptr<poll_event>> _events;

	void add_event(std::unique_ptr<poll_event> ev) {
		_events.emplace_back(std::move(ev));
	}

	void poll_events() {
		auto it = _events.begin();
		while (it != _events.end()) {
			if ((*it)->poll_ready()) {
				// move ready coroutine to run queue
				_m_conts.emplace_back((*it)->handle);
				it = _events.erase(it);
			} else {
				++it;
			}
		}
	}

	[[nodiscard]]
	bool events_empty() const noexcept { return _events.empty(); }

	simple_scheduler() = default;

	void push_back(coro_handle h) {
		_m_conts.emplace_back(h);
	}

	coro_handle try_pop_front() {
		if (_m_conts.empty()) {
			return coro_handle{};
		}
		auto result = _m_conts.front();
		_m_conts.pop_front();
		return result;
	}

	/**
	 * @brief check if there are pending coroutine handles in the queue
	 */
	[[nodiscard]]
	bool empty() const noexcept { return _m_conts.empty(); }

	/**
	 * @note this function would run every coroutine in the queue once, the
	 * queue push (won't check if it's done or not, the coroutine resume would
	 * execute else where)
	 */
	void run_and_empty() {
		while (auto h = try_pop_front()) {
			h.resume();
		}
	};

	[[nodiscard]]
	bool done() const noexcept { return _m_conts.empty() && _events.empty(); }
};


inline simple_scheduler scheduler;


template <typename T>
	requires std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>
struct box {
	struct promise_type;
	using Handle = std::coroutine_handle<promise_type>;

	/** constructor etc. */
	explicit box(const Handle coroutine) : _m_coroutine(coroutine) {}
	box() = default;
	~box() {
		if (_m_coroutine) {
			_m_coroutine.destroy();
		}
	}
	box(const box &)            = delete;
	box &operator=(const box &) = delete;
	box(box &&other) noexcept : _m_coroutine{other._m_coroutine} {
		other._m_coroutine = {};
	}
	box &operator=(box &&other) noexcept {
		if (this != &other) {
			if (_m_coroutine) {
				_m_coroutine.destroy();
			}
			_m_coroutine       = other._m_coroutine;
			other._m_coroutine = {};
		}
		return *this;
	}

	/** promise type implementation */
	struct promise_type {
		std::suspend_never initial_suspend() noexcept { return {}; }
		/**
		 * @brief  https://stackoverflow.com/questions/70532488/what-is-the-best-way-to-return-value-from-promise-type
		 * @note if both initial_suspend and final_suspend are suspend_never, the promise_type would be destroyed (by who?)
		 * why the `std::promise` equivalent is not destroyed?
		 */
		std::suspend_always final_suspend() noexcept { return {}; }

		/**
		 * @see https://devblogs.microsoft.com/oldnewthing/20210407-00/?p=105061
		 * @see https://devblogs.microsoft.com/oldnewthing/20210406-00/?p=105057
		 * @note `return_value` is executed after `get_return_object`
		 */
		void return_value(T value) {
			inner = value;
		}
		auto get_return_object() -> box<T> {
			return box{Handle::from_promise(*this)};
		};
		[[noreturn]]
		void unhandled_exception() { std::terminate(); }

		/** properties */
		std::optional<T> inner;
	};

	std::optional<T> get() {
		if (not _m_coroutine) {
			throw std::logic_error{"no handle"};
		}
		return _m_coroutine.promise().inner;
	}

private:
	/** properties */
	Handle _m_coroutine;
	/** end of properties */
};

/**
 * @brief same as `box` but use `shared_ptr` to store
 * the content of value, so that the coroutine frame could be auto-destroyed
 * with `suspend_never` in `final_suspend`
 */
template <typename T>
	requires std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>
struct sbox {
	struct promise_type;
	using Handle = std::coroutine_handle<promise_type>;

	/** constructor etc. */
	explicit sbox(std::shared_ptr<T> content) : _content(std::move(content)) {}
	~sbox()                       = default;
	sbox(const sbox &)            = delete;
	sbox &operator=(const sbox &) = delete;
	sbox(sbox &&other) noexcept : _content(std::move(other._content)) {}
	sbox &operator=(sbox &&other) noexcept {
		if (this != &other) {
			_content = std::move(other._content);
		}
		return *this;
	}

	/** promise type implementation */
	struct promise_type {
		std::suspend_never initial_suspend() noexcept { return {}; }
		/**
		 * @note automatic cleanup
		 */
		std::suspend_never final_suspend() noexcept { return {}; }

		void return_value(T value) {
			*_promise_content = value;
		}
		auto get_return_object() -> sbox<T> {
			_promise_content = std::make_shared<T>();
			return sbox{_promise_content};
		};
		[[noreturn]]
		void unhandled_exception() { std::terminate(); }

		/** properties */

		/**
		 * @note when the coroutine frame inited, it defaults to a nullptr
		 * would be initialized by `get_return_object`
		 */
		std::shared_ptr<T> _promise_content;
	};

	std::optional<T> get() {
		if (not _content) {
			return std::nullopt;
		}
		return *_content;
	}

private:
	/** properties */
	std::shared_ptr<T> _content;
	/** end of properties */
};


/**
 * we haven't touch awaitable yet
 * @see https://devblogs.microsoft.com/oldnewthing/20191209-00/?p=103195
 */


struct delay_awaitable {
	using Handle     = std::coroutine_handle<>;
	using time_point = app::timer::monotonic_clock::time_point;

	struct timer_event : simple_scheduler::poll_event {
		time_point deadline;
		timer_event(time_point d, Handle h) : deadline(d) {
			handle = h;
		}
		[[nodiscard]]
		bool poll_ready() const override {
			return app::timer::monotonic_clock::now() >= deadline;
		}
	};

	// instance state
	time_point deadline;

	[[nodiscard]]
	bool is_ready() const {
		return app::timer::monotonic_clock::now() >= deadline;
	}

	bool await_ready() const { return is_ready(); }

	bool await_suspend(Handle h) {
		if (is_ready()) {
			return false;
		}
		// register timer event with scheduler
		co::scheduler.add_event(std::make_unique<timer_event>(deadline, h));
		return true; // suspend
	}

	void await_resume() const {}
};

auto delay(std::chrono::milliseconds ms) -> delay_awaitable {
	return delay_awaitable{app::timer::monotonic_clock::now() + ms};
}

/**
 * @brief a dumb accumulator that accumulate value each yield
 * @note this is NOT a generic generator
 */
template <typename T>
	requires std::is_arithmetic_v<T> &&
			 std::is_default_constructible_v<T> &&
			 std::is_trivially_copyable_v<T>
struct accumulator {
	struct promise_type;
	using Handle = std::coroutine_handle<promise_type>;

	/** constructor etc. */
	explicit accumulator(std::shared_ptr<T> content, Handle handle) : _content(std::move(content)), _m_coroutine(handle) {}
	~accumulator() {
		if (_m_coroutine) {
			_m_coroutine.destroy();
		}
	};
	accumulator(const accumulator &)            = delete;
	accumulator &operator=(const accumulator &) = delete;
	accumulator(accumulator &&other) noexcept : _content(std::move(other._content)), _m_coroutine(other._m_coroutine) {
		other._m_coroutine = {};
		other._content     = {};
	}
	accumulator &operator=(accumulator &&other) noexcept {
		if (this != &other) {
			if (_m_coroutine) {
				_m_coroutine.destroy();
			}
			_m_coroutine       = other._m_coroutine;
			_content           = std::move(other._content);
			other._m_coroutine = {};
			other._content     = {};
		}
		return *this;
	}

	/** promise type implementation */
	struct promise_type {
		std::suspend_never initial_suspend() noexcept { return {}; }
		/**
		 * @note coroutine frame won't be destroyed automatically
		 */
		std::suspend_always final_suspend() noexcept { return {}; }

		template <std::convertible_to<T> From>
		std::suspend_always yield_value(From &&from) {
			*_promise_content += std::forward<From>(from);
			return {};
		}

		/**
		 * filled after `get_return_object` is called
		 */
		void return_value(T value) {
			*_promise_content += value;
		}

		/**
		 * when `co_return` is used, the `get_return_object` is called
		 *
		 * after that the `return_value` would be called
		 * (the target of `co_return` would be the parameter of `return_value`)
		 *
		 * so first the `return_object` needs to be contructed
		 * (which hasn't existed yet)
		 */
		auto get_return_object() -> accumulator<T> {
			_promise_content = std::make_shared<T>();
			return accumulator{_promise_content, Handle::from_promise(*this)};
		};
		[[noreturn]]
		void unhandled_exception() { std::terminate(); }

		/** properties */

		/**
		 * @note when the coroutine frame inited, it defaults to a nullptr
		 * would be initialized by `get_return_object`
		 */
		std::shared_ptr<T> _promise_content;
	};

	std::optional<T> get() {
		if (not _content) {
			return std::nullopt;
		}
		return *_content;
	}
	/**
	 * @brief resume the coroutine
	 * @return true if the coroutine is not done
	 * @return false if the coroutine is done
	 */
	bool resume() {
		if (not _m_coroutine) {
			return false;
		}
		if (_m_coroutine.done()) {
			_m_coroutine.destroy();
			_m_coroutine = {};
			return false;
		}
		_m_coroutine.resume();
		return true;
	}

private:
	/** properties */
	std::shared_ptr<T> _content;
	Handle _m_coroutine;
	/** end of properties */
};


/**
 * @brief a `nothing`, just for the sake of being able to use `co_await`
 * @see https://en.cppreference.com/w/cpp/language/coroutines.html
 */
struct void_task {
	struct promise_type {
		void_task get_return_object() { return {}; }
		std::suspend_never initial_suspend() { return {}; }
		std::suspend_never final_suspend() noexcept { return {}; }
		void return_void() {}
		void unhandled_exception() {}
	};
};

accumulator<int> f() {
	co_yield 1;
	co_yield 2;
	co_yield 3;
	co_yield 4;
	co_yield 5;
	co_yield 6;
	co_yield 7;
	co_yield 8;
	co_yield 9;
	co_yield 10;
	co_return 42;
}

void_task fake_blink() {
	std::println("i");
	co_await delay(std::chrono::milliseconds{1'000});
	std::println("1");
	co_await delay(std::chrono::milliseconds{1'000});
	std::println("2");
	co_await delay(std::chrono::milliseconds{500});
	std::println("3");
	co_await delay(std::chrono::milliseconds{250});
	std::println("4");
	co_await delay(std::chrono::milliseconds{250});
	std::println("5");
	co_await delay(std::chrono::milliseconds{3'000});
}

}

int main() {
	co::fake_blink();
	std::println("start");

	// Keep looping until the scheduler becomes truly empty
	while (not co::scheduler.done()) {
		co::scheduler.poll_events();
		co::scheduler.run_and_empty();
	}

	std::println("done");
}
