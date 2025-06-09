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
std::future<int> f() {
	co_return 42;
}
}


int main() {
	auto f = co::f();
	std::println("{}", f.get());
}
