#ifndef _WIN32
#define _POSIX_C_SOURCE 199309L
#endif

#include "app_timer.hpp"
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace app::timer {
static std::atomic<uint64_t> _millis_c = 0;

/**
 * @note
 * In real embedded systems, a hardware timer (e.g., SysTick) is used to generate periodic interrupts,
 * typically firing every 1ms to increment a global "tick" counter.
 * This is the foundation for system time, task scheduling, and timeouts.
 *
 * Here, we **emulate** this mechanism using an OS timer callback, which plays a similar role:
 * the callback function is conceptually equivalent to registering an ISR (Interrupt Service Routine)
 * for the timer interrupt on bare metal.
 *
 * @see https://www.sciencedirect.com/topics/engineering/systick-timer
 *
 * ---
 *
 * In a SoC/MCU **without** a built-in timer peripheral, a common workaround is to use a hardware timer circuit
 * (e.g., a 555 timer chip) to generate periodic pulses on a GPIO pin. The GPIO interrupt is then used to
 * simulate a timer interrupt in software.
 *
 * (However, practically **every** modern MCU has at least one hardware timer or SysTick peripheral!)
 *
 * If no hardware interrupts are available at all (no timer, no external interrupt sources), the only fallback
 * is to implement a "software timer" using CPU cycle counting (e.g., a busy-wait loop / spinlock), which is
 * highly inaccurate and wastes CPU cycles, but technically possible.
 */

#ifdef _WIN32
static VOID CALLBACK timer_callback(PVOID lpParameter, BOOLEAN TimerOrWaitFired) {
	std::ignore = lpParameter;
	std::ignore = TimerOrWaitFired;
	_millis_c++;
}
#endif

/**
 * @note This is a C++ static constructor pattern: the lambda runs once before `main()`,
 * functionally equivalent to the `init` function in Go.
 *
 * In standard hosted C++, static (global/namespace-scope) variable initialization is guaranteed
 * to run before entering `main()`. This allows "setup code" to be executed automatically,
 * without an explicit call from user code.
 *
 * **Embedded systems note:**
 * On some embedded toolchains (bare-metal/RTOS), static/global initializers **may be omitted**
 * if the linker script/startup code does not properly handle `.init_array` or equivalent sections.
 * Always verify startup files and linker scripts ensure that C++ global constructors are called.
 *
 * @see https://go.dev/doc/effective_go#init
 * @see https://developer.arm.com/documentation/dui0808/b/Chdfiffc
 * @see https://docs.oracle.com/cd/E19683-01/816-7777/6mdorm6is/index.html
 */
static std::monostate _ = [] -> std::monostate {
#ifdef _WIN32
	HANDLE hTimer      = nullptr;
	HANDLE hTimerQueue = CreateTimerQueue();
	if (hTimerQueue == nullptr) {
		std::abort();
	}

	if (not CreateTimerQueueTimer(&hTimer, hTimerQueue, timer_callback, nullptr, 0, 1, 0)) {
		DeleteTimerQueue(hTimerQueue);
		std::abort();
	}

	return {};
#else
	struct sigaction sa;
	constexpr auto timer_callback = [](int sig) {
		std::ignore = sig;
		_millis_c++;
	};
	sa.sa_handler = timer_callback;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGALRM, &sa, nullptr) == -1) {
		std::abort();
	}

	struct itimerval timer;
	timer.it_value.tv_sec     = 0;
	timer.it_value.tv_usec    = 1000;
	timer.it_interval.tv_sec  = 0;
	timer.it_interval.tv_usec = 1000;

	if (setitimer(ITIMER_REAL, &timer, nullptr) == -1) {
		std::abort();
	}

	return {};
#endif
}();

static uint64_t millis() {
	return _millis_c;
}

monotonic_clock::time_point monotonic_clock::now() {
	return time_point(duration(static_cast<rep>(millis())));
}
}
