#ifndef AFC139B3_97A0_419E_BB84_1242D6517CB8
#define AFC139B3_97A0_419E_BB84_1242D6517CB8
#include <chrono>

namespace app::timer {
/**
 * @brief a monotonic clock that returns the current time in milliseconds
 * @see https://en.cppreference.com/w/cpp/named_req/TrivialClock.html
 * @see https://en.cppreference.com/w/cpp/named_req/Clock.html
 */
struct monotonic_clock {
	using rep                       = uint64_t;
	using period                    = std::milli;
	using duration                  = std::chrono::duration<rep, period>;
	using time_point                = std::chrono::time_point<monotonic_clock>;
	static constexpr bool is_steady = true;
	static time_point now();
};
}

#endif /* AFC139B3_97A0_419E_BB84_1242D6517CB8 */
