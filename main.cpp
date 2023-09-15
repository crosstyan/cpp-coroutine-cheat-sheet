#include <iostream>
#include <coroutine>
#include <variant>


namespace co {
/// https://en.cppreference.com/w/cpp/coroutine/coroutine_traits
/* std::coroutine_traits<co_ret_t, ...> */
    struct ret_t {
        struct answer_awaitable;

        struct promise_t {
            int val = 0;


            // optional yield
            answer_awaitable yield_value(int v) {
                return {v};
            }


            /** some traits **/

            ret_t get_return_object() {
                return {std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            // return an awaitable
            // await_ready()
            // await_suspend(std::coroutine_handle<promise_t> handle)
            // await_resume()
            answer_awaitable initial_suspend() {
                std::cout << "initial_suspend; " << "val: " << val << std::endl;
                return {val};
            }


            // start ↑ / shutdown ↓

            void return_void() {}

            void unhandled_exception() {}

            // return an awaitable
            answer_awaitable final_suspend() noexcept {
                std::cout << "final_suspend; " << "val: " << val << std::endl;
                return {val};
            }

        };

        struct answer_awaitable {
            int val;

            answer_awaitable(int v) : val(v) {
                std::cout << "answer_awaitable: " << val << std::endl;
            }

            // https://en.cppreference.com/w/cpp/coroutine/suspend_always
            // true: suspend never
            // false: suspend always
            bool await_ready() noexcept { return false; }

            void await_suspend(std::coroutine_handle<promise_t> handle) noexcept {
                std::cout << "await_suspend" << std::endl;
                handle.promise().val = this->val;
            }

            void await_resume() noexcept {}
        };

        /// trait
        using promise_type = promise_t;

        using handle_t = std::coroutine_handle<promise_type>;
        handle_t handle;


        ret_t(handle_t h) : handle(h) {};

        int val() {
            return handle.promise().val;
        }
    };
}

co::ret_t hello_coroutine() {
    // this expression is still from caller?
    std::cout << "Hello coroutine!" << std::endl;
    co_await co::ret_t::answer_awaitable{42};
    // co_yield 42;
}

// co_await
// The unary operator co_await suspends a coroutine and returns control to the caller.
int main() {
    std::cout << "creation" << std::endl;
    auto hello = hello_coroutine();
    std::cout << "suspend" << std::endl;
    std::cout << "resume" << std::endl;
    auto v = hello.val();
    std::cout << "val: " << v << std::endl;
    return 0;
}
