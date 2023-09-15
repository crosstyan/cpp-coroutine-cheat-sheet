#include <iostream>
#include <coroutine>
#include <variant>


/// https://en.cppreference.com/w/cpp/coroutine/coroutine_traits
/* std::coroutine_traits<co_ret_t, ...> */
struct co_ret_t {
    struct co_promise_t {
        co_ret_t get_return_object() {
            return {std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        // return an awaitable
        // await_ready()
        // await_suspend(std::coroutine_handle<co_promise_t> handle)
        // await_resume()
        std::suspend_always initial_suspend() { return {}; }

        // start ↑ / shutdown ↓

        void return_void() {}

        void unhandled_exception() {}

        // return an awaitable
        std::suspend_always final_suspend() noexcept { return {}; }
    };

    /// trait
    using promise_type = co_promise_t;

    // my convention
    using handle_t = std::coroutine_handle<promise_type>;
    handle_t handle;
    co_ret_t(handle_t h):handle(h){};

    void resume(){
        handle.resume();
    }
};

co_ret_t hello_coroutine() {
    std::cout << "Hello coroutine!" << std::endl;
    co_return;
}

// co_await
// The unary operator co_await suspends a coroutine and returns control to the caller.
int main() {
    std::cout << "creation" << std::endl;
    auto hello = hello_coroutine();
    std::cout << "suspend" << std::endl;
    std::cout << "resume" << std::endl;
    hello.resume();
    return 0;
}
