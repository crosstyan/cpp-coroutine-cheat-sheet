#include <format>
#include <future>
#include <coroutine>
#include <iostream>
#include <variant>


template <typename R, typename... Args>
struct std::coroutine_traits<std::future<R>, Args...> {
	struct promise_type {};
};

namespace co {
/// https://en.cppreference.com/w/cpp/coroutine/coroutine_traits
/* std::coroutine_traits<co_ret_t, ...> */
struct ret_t {
};


std::future<int> f() {
	std::println("hello!");
	co_return 42;
}
}


// co_await
// The unary operator co_await suspends a coroutine and returns control to the
// caller.
int main() {
	co::f();
}
