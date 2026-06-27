/**
 * @file execve.h
 * @author theflysong
 * @brief create_process / execve 请求结构
 * @version alpha-1.0.0
 * @date 2026-06-27
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <sustcore/bootstrap.h>
#include <sustcore/capability.h>

struct ExecveRequest {
    CapIdx image_cap;
    const char *execfn;
    CapIdx *caps;
    const char **argv;
    const char **envp;
    const char **bsargv;
};
