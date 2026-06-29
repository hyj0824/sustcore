/**
 * @file rtc.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief RTC 通用接口
 * @version alpha-1.0.0
 * @date 2026-06-29
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#pragma once

#include <sus/units.h>

namespace rtc
{
    class IRPC
    {
    public:
        virtual ~IRPC() = default;

        [[nodiscard]]
        virtual units::time now() = 0;
    };

    class IRTC
        : public IRPC
    {
    public:
        virtual ~IRTC() = default;

        [[nodiscard]]
        virtual units::time read_time() = 0;

        [[nodiscard]]
        units::time now() override {
            return read_time();
        }
    };
}
