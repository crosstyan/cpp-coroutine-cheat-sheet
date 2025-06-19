//
// Created by Kurosu Chan on 2023/12/4.
//

#ifndef COMMON_INSTANT_H
#define COMMON_INSTANT_H

#include <chrono>
#include "app_timer.hpp"

namespace app::timer {
/**
 * @brief A measurement of a monotonically nondecreasing clock.
 * @tparam T the data type of the counter
 * @sa https://doc.rust-lang.org/std/time/struct.Instant.html
 */
class Instant {
public:
	using clock        = monotonic_clock;
	using time_point   = clock::time_point;
	using duration     = clock::duration;
	using milliseconds = std::chrono::milliseconds;
	using ms_rep       = milliseconds::rep;

	Instant() {
		this->time_ = clock::now();
	}

	static Instant now() {
		return Instant{};
	}

	[[nodiscard]]
	duration elapsed() const {
		const auto now = clock::now();
		// overflow
		if (now < time_) {
			const auto c = now.time_since_epoch().count() +
						   (std::numeric_limits<time_point::rep>::max() - time_.time_since_epoch().count());
			return duration{c};
		}
		return duration{now - time_};
	}

	[[nodiscard]]
	ms_rep elapsed_ms() const {
		return std::chrono::duration_cast<milliseconds>(elapsed()).count();
	}

	[[nodiscard]]
	bool has_elapsed_ms(const ms_rep ms) const {
		return this->elapsed_ms() >= ms;
	}

	/**
	 * @brief Checks if the specified time interval has elapsed since the last reset.
	 *
	 * This method checks if the elapsed time since the last reset is greater than or equal to the input parameter `ms`.
	 * If it is, the method resets the internal timer and returns true. If not, it simply returns false.
	 * The `[[nodiscard]]` attribute indicates that the compiler will issue a warning if the return value of this function is not used.
	 *
	 * @param ms The time interval in milliseconds.
	 * @return Returns true if the elapsed time is greater than or equal to the input time interval, otherwise returns false.
	 */
	[[nodiscard]]
	bool mut_every_ms(const ms_rep ms) {
		const bool gt = this->elapsed_ms() >= ms;
		if (gt) {
			this->mut_reset();
			return true;
		}
		return false;
	}

	template <typename Rep, typename Period>
	[[nodiscard]]
	bool has_elapsed(const std::chrono::duration<Rep, Period> duration) const {
		return this->elapsed() >= duration;
	}

	template <typename Rep, typename Period>
	[[nodiscard]]
	bool mut_every(const std::chrono::duration<Rep, Period> duration) {
		const bool gt = this->has_elapsed(duration);
		if (gt) {
			this->mut_reset();
			return true;
		}
		return false;
	}

	void mut_reset() {
		this->time_ = clock::now();
	}

	/**
	 * @deprecated use `mut_reset` instead
	 */
	[[deprecated("use `mut_reset` instead")]]
	void reset() {
		mut_reset();
	}

	[[nodiscard]]
	duration mut_elapsed_and_reset() {
		auto now = clock::now();
		duration diff;
		if (now < time_) {
			// overflow
			const auto c = now.time_since_epoch().count() +
						   (std::numeric_limits<time_point::rep>::max() - time_.time_since_epoch().count());
			diff = duration{c};
		} else {
			diff = now - time_;
		}
		return diff;
	}

	[[nodiscard]]
	time_point count() const {
		return time_;
	}

private:
	time_point time_;
};
}

#endif // COMMON_INSTANT_H