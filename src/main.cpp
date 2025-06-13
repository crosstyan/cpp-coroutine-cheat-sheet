#include <algorithm>
#include <format>
#include <future>
#include <coroutine>
#include <iostream>
#include <type_traits>
#include <variant>
#include <vector>
#include "app_timer.hpp"


/** std::coroutine_traits<co_ret_t, ...>
 * @see https://en.cppreference.com/w/cpp/coroutine/coroutine_traits
 */
template <typename R, typename... Args>
struct std::coroutine_traits<std::future<R>, Args...> {
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

namespace co {
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
		void unhandled_exception() { throw; }

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
		 * @note automatic cleanup
		 * @note use `suspend_always` if you need the coroutine frame not being
		 * destroyed
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
		void unhandled_exception() { throw; }

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

/**
 * we haven't touch awaitable yet
 * @see https://devblogs.microsoft.com/oldnewthing/20191209-00/?p=103195
 */

struct simple_scheduler {
	/**
	 * @note Specialization std::coroutine_handle<void> erases the promise type.
	 * It is convertible from other specializations.
	 * @see https://en.cppreference.com/w/cpp/coroutine/coroutine_handle
	 */
	std::vector<std::coroutine_handle<>> _m_conts;

	simple_scheduler() = default;

	void schedule(std::coroutine_handle<> cont) {
		_m_conts.emplace_back(cont);
	}

	/**
	 * @note should be called in a loop
	 */
	void iterate() {
		for (auto &cont : _m_conts) {
			if (cont.done()) {
				_m_conts.erase(std::ranges::remove(_m_conts, cont).begin(), _m_conts.end());
			} else {
				cont.resume();
			}
		}
	};
};
}

int main() {
	auto b = co::f();
	std::println("{}", b.get().value_or(0));
	while (b.resume()) {
		std::println("{}", b.get().value_or(0));
	}
	std::println("done");
}
