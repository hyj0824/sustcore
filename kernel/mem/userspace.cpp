/**
 * @file userspace.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 用户地址空间相关
 * @version alpha-1.0.0
 * @date 2026-05-21
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <env.h>
#include <mem/userspace.h>
#include <mem/vma.h>

namespace key {
    /**
     * @brief 允许用户空间访问逻辑临时更新当前地址空间记录.
     */
    struct uspace : public env::key::tmm {
    public:
        uspace() = default;
    };
}  // namespace key

namespace {
    /**
     * @brief 在作用域内临时切换到指定用户地址空间页表.
     *
     * 构造时保存当前 env::tmm 与硬件页表, 切换到目标用户地址空间;
     * 析构时恢复原地址空间并刷新 TLB.
     */
    class UspaceSwitchGuard {
    private:
        TaskMemoryManager *_origin_tmm;
        PhyAddr _origin_pgd;

    public:
        /**
         * @brief 切换到目标用户地址空间页表.
         *
         * @param target_tmm 目标用户地址空间.
         */
        explicit UspaceSwitchGuard(TaskMemoryManager *target_tmm)
            : _origin_tmm(env::inst().tmm()), _origin_pgd(env::inst().pgd()) {
            env::inst().tmm(key::uspace()) = target_tmm;
            PageMan(target_tmm->pgd()).switch_root();
            PageMan::flush_tlb();
        }

        ~UspaceSwitchGuard() {
            env::inst().tmm(key::uspace()) = _origin_tmm;
            PageMan(_origin_pgd).switch_root();
            PageMan::flush_tlb();
        }

        UspaceSwitchGuard(const UspaceSwitchGuard &)            = delete;
        UspaceSwitchGuard &operator=(const UspaceSwitchGuard &) = delete;
        UspaceSwitchGuard(UspaceSwitchGuard &&)                 = delete;
        UspaceSwitchGuard &operator=(UspaceSwitchGuard &&)      = delete;
    };
}  // namespace

namespace uspace {
    /**
     * @brief 判断用户内存管理器是否对应当前页表.
     */
    bool is_current_uspace(TaskMemoryManager *utmm) {
        if (utmm == nullptr) {
            return false;
        }
        if (utmm == env::inst().tmm()) {
            return true;
        }
        return utmm->pgd().nonnull() && utmm->pgd() == env::inst().pgd();
    }

    /**
     * @brief 通过临时切换页表复制非当前用户地址空间的数据.
     */
    Result<void> umemcpy_other(CpyDir dir, TaskMemoryManager *utmm, void *kbuf,
                               VirAddr uaddr, size_t len) {
        // 参数检查
        if (utmm == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        if (kbuf == nullptr && len != 0) {
            unexpect_return(ErrCode::NULLPTR);
        }
        if (!utmm->pgd().nonnull()) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        if (len == 0) {
            return {};
        }

        // 切换到目标地址空间后通过 SUM 访问用户地址
        UspaceSwitchGuard switch_guard(utmm);
        SumGuard sum_guard;
        sum_guard.open();
        switch (dir) {
            case CpyDir::U2K: memcpy(kbuf, uaddr.addr(), len); break;
            case CpyDir::K2U: memcpy(uaddr.addr(), kbuf, len); break;
            default:          unexpect_return(ErrCode::INVALID_PARAM);
        }
        return {};
    }

    /**
     * @brief 通过临时切换页表填充非当前用户地址空间的数据.
     */
    Result<void> umemset_other(TaskMemoryManager *utmm, VirAddr uaddr,
                               int value, size_t len) {
        // 参数检查
        if (utmm == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        if (!utmm->pgd().nonnull()) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        if (len == 0) {
            return {};
        }

        // 切换到目标地址空间后通过 SUM 访问用户地址
        UspaceSwitchGuard switch_guard(utmm);
        SumGuard sum_guard;
        sum_guard.open();
        memset(uaddr.addr(), value, len);
        return {};
    }

    /**
     * @brief 向用户地址空间填充指定字节值.
     */
    Result<void> umemset(TaskMemoryManager *utmm, VirAddr uaddr, int value,
                         size_t len) {
        // 参数检查
        if (utmm == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }

        // 当前地址空间快路径
        if (is_current_uspace(utmm)) {
            SumGuard guard;
            guard.open();
            memset(uaddr.addr(), value, len);
            return {};
        }

        // 非当前地址空间预留路径
        return umemset_other(utmm, uaddr, value, len);
    }
}  // namespace uspace
