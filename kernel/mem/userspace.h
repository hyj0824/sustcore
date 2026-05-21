/**
 * @file userspace.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 用户地址空间相关
 * @version alpha-1.0.0
 * @date 2026-05-21
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <arch/description.h>
#include <sustcore/errcode.h>
#include <sustcore/addr.h>
#include <sus/types.h>

#include <cstddef>
#include <cstring>

class TaskMemoryManager;

namespace uspace {

    inline void sum_open() {
        csr_sstatus_t sstatus = csr_get_sstatus();
        sstatus.sum           = 1;  // 允许S-MODE访问U-MODE内存
        csr_set_sstatus(sstatus);
    }

    inline void sum_close() {
        csr_sstatus_t sstatus = csr_get_sstatus();
        sstatus.sum           = 0;  // 禁止S-MODE访问U-MODE内存
        csr_set_sstatus(sstatus);
    }

    struct SumGuard {
    private:
        bool opened = false;

    public:
        SumGuard() = default;
        ~SumGuard() {
            close();
        }
        void open() {
            if (!opened) {
                opened = true;
                sum_open();
            }
        }
        void close() {
            if (opened) {
                opened = false;
                sum_close();
            }
        }
        // 禁止复制和移动
        SumGuard(const SumGuard &)            = delete;
        SumGuard &operator=(const SumGuard &) = delete;
        SumGuard(SumGuard &&)                 = delete;
        SumGuard &operator=(SumGuard &&)      = delete;
        // 禁止new和delete
        void *operator new(size_t)            = delete;
        void operator delete(void *)          = delete;
    };

    /**
     * @brief 用户地址空间复制方向.
     */
    enum class CpyDir {
        U2K,  ///< 从用户地址空间复制到内核缓冲区.
        K2U,  ///< 从内核缓冲区复制到用户地址空间.
    };

    /**
     * @brief 判断用户地址空间是否为当前正在运行的地址空间.
     *
     * @param utmm 待访问的用户地址空间.
     * @return true 表示可直接通过当前页表访问.
     */
    bool is_current_uspace(TaskMemoryManager *utmm);

    /**
     * @brief 在非当前地址空间和内核缓冲区之间复制数据.
     *
     * 通过临时切换页表访问目标用户地址空间.
     *
     * @param dir 复制方向.
     * @param utmm 目标用户地址空间.
     * @param kbuf 内核缓冲区.
     * @param uaddr 用户地址.
     * @param len 复制长度.
     * @return 成功返回 SUCCESS, 失败返回错误码.
     */
    Result<void> umemcpy_other(CpyDir dir, TaskMemoryManager *utmm,
                               void *kbuf, VirAddr uaddr, size_t len);

    /**
     * @brief 在非当前用户地址空间中填充内存.
     *
     * 通过临时切换页表访问目标用户地址空间.
     *
     * @param utmm 目标用户地址空间.
     * @param uaddr 用户地址.
     * @param value 填充值.
     * @param len 填充长度.
     * @return 成功返回 SUCCESS, 失败返回错误码.
     */
    Result<void> umemset_other(TaskMemoryManager *utmm, VirAddr uaddr,
                               int value, size_t len);

    /**
     * @brief 在内核缓冲区和用户地址空间之间复制数据.
     *
     * 通过模板参数指定复制方向. 当用户地址空间为当前地址空间时, 直接打开
     * SUM 并访问用户地址; 否则转入预留的跨地址空间处理逻辑.
     *
     * @tparam Dir 复制方向.
     * @param utmm 用户地址空间.
     * @param kbuf 内核缓冲区.
     * @param uaddr 用户地址.
     * @param len 复制长度.
     * @return 成功返回 SUCCESS, 失败返回错误码.
     */
    template <CpyDir Dir>
    Result<void> umemcpy(TaskMemoryManager *utmm, void *kbuf, VirAddr uaddr,
                         size_t len) {
        // 参数检查
        if (utmm == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        if (kbuf == nullptr && len != 0) {
            unexpect_return(ErrCode::NULLPTR);
        }

        // 当前地址空间快路径
        if (is_current_uspace(utmm)) {
            SumGuard guard;
            guard.open();
            if constexpr (Dir == CpyDir::U2K) {
                memcpy(kbuf, uaddr.addr(), len);
            } else {
                memcpy(uaddr.addr(), kbuf, len);
            }
            return {};
        }

        // 非当前地址空间预留路径
        return umemcpy_other(Dir, utmm, kbuf, uaddr, len);
    }

    /**
     * @brief 在用户地址空间中填充指定字节值.
     *
     * 当用户地址空间为当前地址空间时, 直接打开 SUM 并访问用户地址; 否则转入
     * 预留的跨地址空间处理逻辑.
     *
     * @param utmm 用户地址空间.
     * @param uaddr 用户地址.
     * @param value 填充值.
     * @param len 填充长度.
     * @return 成功返回 SUCCESS, 失败返回错误码.
     */
    Result<void> umemset(TaskMemoryManager *utmm, VirAddr uaddr, int value,
                         size_t len);
}  // namespace uspace
