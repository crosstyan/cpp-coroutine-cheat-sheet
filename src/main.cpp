#include <format>
#include <future>
#include <coroutine>
#include <iostream>
#include <variant>


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
		 */
		std::suspend_always final_suspend() noexcept { return {}; }

		/**
		 * @see https://devblogs.microsoft.com/oldnewthing/20210407-00/?p=105061
		 * @see https://devblogs.microsoft.com/oldnewthing/20210406-00/?p=105057
		 */
		void return_value(T value) {
			std::println("return_value: {}", value);
			inner = value;
		}
		auto get_return_object() -> box<T> {
			std::println("get_return_object");
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

box<int> f() {
	co_return 42;
}
}


int main() {
	auto b = co::f();
	std::println("coroutine return");
	std::println("{}", b.get().value_or(0));
}
