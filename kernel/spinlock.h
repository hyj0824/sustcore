/**
 * @file spinlock.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 自旋锁
 * @version alpha-1.0.0
 * @date 2026-06-09
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#pragma once

#include <arch/riscv64/spinlock.h>

class SpinLocker
{
private:
    raw_spinlock_t _lock = 0;
public:
    SpinLocker() = default;

    void lock()
    {
        // TODO: 将当前线程标记为不可抢断
        // 更准确地说, 添加一个方法, 记录不可抢断的次数
        // 次数归零时取消不可抢断
        __raw_spin_lock(&_lock);
    }

    void unlock()
    {
        // TODO: 将当前线程取消不可抢断的标记
        __raw_spin_unlock(&_lock);
    }
};

class GuardedLock
{
private:
    SpinLocker &_lock;
    bool locked = false;
public:
    GuardedLock(SpinLocker &spinlock)
        : _lock(spinlock)
    {
        lock();
    }

    ~GuardedLock()
    {
        unlock();
    }

    void lock()
    {
        if (! locked)
        {
            _lock.lock();
            locked = true;
        }
    }

    void unlock()
    {
        if (locked)
        {
            _lock.unlock();
            locked = false;
        }
    }
};