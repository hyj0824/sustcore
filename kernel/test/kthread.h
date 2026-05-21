/**
 * @file kthread.h
 * @brief Kernel thread runtime tests
 */

#pragma once

#include <sustcore/errcode.h>

namespace test::kthread {
    Result<void> start_logger_yield_test();
}
