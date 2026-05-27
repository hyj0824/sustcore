/**
 * @file units.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 单位
 * @version alpha-1.0.0
 * @date 2026-02-08
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>

namespace units {
    // 实际上, 这个结构体只是一个uint64_t
    struct frequency {
    protected:
        uint64_t milihertz;
        explicit constexpr frequency(uint64_t mili_hz) : milihertz(mili_hz) {}

    public:
        explicit constexpr frequency() : milihertz(0) {}
        constexpr operator uint64_t() const {
            return to_hz();
        }

        constexpr uint64_t to_milihz() const {
            return milihertz;
        }
        constexpr uint64_t to_hz() const {
            return milihertz / 1'000;
        }
        constexpr uint64_t to_khz() const {
            return to_hz() / 1'000;
        }
        constexpr uint64_t to_mhz() const {
            return to_khz() / 1'000;
        }
        constexpr uint64_t to_ghz() const {
            return to_mhz() / 1'000;
        }

        static constexpr frequency from_milihz(uint64_t h) {
            return frequency(h);
        }
        static constexpr frequency from_hz(uint64_t h) {
            return from_milihz(h * 1'000);
        }
        static constexpr frequency from_khz(uint64_t kh) {
            return from_hz(kh * 1'000);
        }
        static constexpr frequency from_mhz(uint64_t mh) {
            return from_khz(mh * 1'000);
        }
        static constexpr frequency from_ghz(uint64_t gh) {
            return from_mhz(gh * 1'000);
        }

        constexpr frequency operator+(const frequency &other) const {
            return frequency(milihertz + other.milihertz);
        }

        constexpr frequency operator-(const frequency &other) const {
            return frequency(milihertz - other.milihertz);
        }

        constexpr frequency operator*(uint64_t multiplier) const {
            return frequency(milihertz * multiplier);
        }

        constexpr frequency operator/(uint64_t divisor) const {
            return frequency(milihertz / divisor);
        }

        constexpr uint64_t operator/(const frequency &other) const {
            return milihertz / other.milihertz;
        }
    };

    using tick = uint64_t;

    constexpr uint64_t NANOSECONDS_PER_MILLIHERTZ = 1'000'000'000'000ULL;

    struct time {
    protected:
        uint64_t nanoseconds;
        explicit constexpr time(uint64_t ns) : nanoseconds(ns) {}

    public:
        explicit constexpr time() : nanoseconds(0) {}
        constexpr operator uint64_t() const {
            return to_nanoseconds();
        }

        [[nodiscard]]
        constexpr uint64_t to_nanoseconds() const {
            return nanoseconds;
        }
        [[nodiscard]]
        constexpr uint64_t to_microseconds() const {
            return nanoseconds / 1'000;
        }
        [[nodiscard]]
        constexpr uint64_t to_milliseconds() const {
            return nanoseconds / 1'000'000;
        }
        [[nodiscard]]
        constexpr uint64_t to_seconds() const {
            return to_milliseconds() / 1'000;
        }

        static constexpr time from_nanoseconds(uint64_t ns) {
            return time(ns);
        }
        static constexpr time from_microseconds(uint64_t us) {
            return from_nanoseconds(us * 1'000);
        }
        static constexpr time from_milliseconds(uint64_t ms) {
            return from_microseconds(ms * 1'000);
        }
        static constexpr time from_seconds(uint64_t s) {
            return from_milliseconds(s * 1'000);
        }

        constexpr time operator+(const time &other) const {
            return time(nanoseconds + other.nanoseconds);
        }

        constexpr time operator-(const time &other) const {
            return time(nanoseconds - other.nanoseconds);
        }

        constexpr time operator*(uint64_t multiplier) const {
            return time(nanoseconds * multiplier);
        }

        constexpr time operator/(uint64_t divisor) const {
            return time(nanoseconds / divisor);
        }

        constexpr uint64_t operator/(const time &other) const {
            return nanoseconds / other.nanoseconds;
        }

        constexpr uint64_t operator*(const frequency &f) const {
            // time * frequency = (ns) * (mili Hz) = (ns) * (1000/s) = 10^(-12)
            // s
            return (nanoseconds * f.to_milihz()) / NANOSECONDS_PER_MILLIHERTZ;
        }
    };  // namespace units

    // calculate the frequenct
    constexpr frequency operator/(uint64_t count, const time &t) {
        // 1 / ns = 1 / (1e-9 s) = 1e9 Hz
        // count / (t ns) = (count / t) * 1e9 Hz = count * (1e9 / t) Hz
        return frequency::from_milihz(
            count * (NANOSECONDS_PER_MILLIHERTZ / t.to_nanoseconds()));
    };

    // calculate the time
    constexpr time operator/(uint64_t count, const frequency &f) {
        // 1 / Hz = 1 / (1/s) = s
        // count / (f Hz) = count * (1 / f) s = count * (1e9 / f) ns
        return time::from_nanoseconds(
            count * (NANOSECONDS_PER_MILLIHERTZ / f.to_milihz()));
    };
}  // namespace units

constexpr units::frequency operator""_mHz(unsigned long long h) {
    return units::frequency::from_milihz(h);
}

constexpr units::frequency operator""_Hz(unsigned long long h) {
    return units::frequency::from_hz(h);
}

constexpr units::frequency operator""_kHz(unsigned long long kh) {
    return units::frequency::from_khz(kh);
}

constexpr units::frequency operator""_MHz(unsigned long long mh) {
    return units::frequency::from_mhz(mh);
}

constexpr units::frequency operator""_GHz(unsigned long long gh) {
    return units::frequency::from_ghz(gh);
}

constexpr units::time operator""_ns(unsigned long long ns) {
    return units::time::from_nanoseconds(ns);
}