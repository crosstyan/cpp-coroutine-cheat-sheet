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
	bool has_value;
	T _value;

	struct promise_type {
		std::optional<T> inner;

		std::suspend_never initial_suspend() noexcept { return {}; }
		std::suspend_never final_suspend() noexcept { return {}; }

		void return_value(T value) {
			this->inner = value;
		}
		auto get_return_object() -> box<T> {
			if (this->inner) {
				return box{true, this->inner.value()};
			}
			return box{false, T{}};
		};
		void unhandled_exception() { /** do nothing */ }
	};

	T &value() {
		if (not has_value) {
			throw std::logic_error{"not found"};
		}
		return _value;
	}
};

box<int> f() {
	co_return 42;
}
}


int main() {
	auto f = co::f();
	std::println("{}", f.value());
}
