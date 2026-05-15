/**
 * @file kop.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief kernel object pool initialization
 * @version alpha-1.0.0
 * @date 2026-05-15
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#include <cap/capability.h>
#include <task/task.h>
#include <vfs/tarfs.h>

// 在此处集中调用各模块的 kop 初始化函数
void init_kop()
{
    cap::init_kop();
    task::init_kop();
    tarfs::init_kop();
}
