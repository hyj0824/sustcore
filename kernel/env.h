/**
 * @file env.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief environment
 * @version alpha-1.0.0
 * @date 2026-04-06
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <arch/riscv64/ctxlayout.h>
#include <arch/riscv64/description.h>
#include <device/int.h>
#include <mem/vma.h>
#include <schd/schdbase.h>
#include <task/task_struct.h>
#include <sustcore/addr.h>

namespace env {
    class HartContext;
    register HartContext *hart_ctx asm("tp");

    // passkey
    namespace key {
        struct tags {
        protected:
            tags() = default;
        };

        struct unmodifiable : public tags {
        private:
            unmodifiable() = default;
        };

        struct tmm : public tags {};
        struct trap_context : public tags {};
        struct pgd : public unmodifiable {};
        struct meminfo : public tags {};
    }  // namespace key

    struct MemInfo {
        constexpr static size_t MAX_REGIONS = 128;
        MemRegion regions[MAX_REGIONS];
        size_t region_cnt = 0;
        PhyAddr lowpm     = PhyAddr::null;
        PhyAddr uppm      = PhyAddr::null;
        VirAddr lowvm     = VirAddr::null;

        constexpr MemInfo() {
            memset((void *)regions, 0, sizeof(regions));
        }
    };

    class Environment {
    private:
        MemInfo _meminfo;

    public:
        constexpr Environment();

        // readers
        [[nodiscard]]
        TaskMemoryManager *tmm() const noexcept;
        [[nodiscard]]
        TaskMemoryManager *&tmm(key::tmm) noexcept;

        [[nodiscard]]
        Context *trap_context() const noexcept;
        [[nodiscard]]
        Context *&trap_context(key::trap_context) noexcept;

        [[nodiscard]]
        PhyAddr pgd() const noexcept;

        [[nodiscard]]
        const MemInfo &meminfo() const;
        [[nodiscard]]
        MemInfo &meminfo(key::meminfo);
    };

    void construct();
    bool initialized();
    Environment &inst();

    /**
     * @brief 每个 hart 私有的最小上下文对象.
     */
    class HartContext {
    public:
        /**
         * @brief 构造一个默认 Hart 上下文.
         */
        constexpr HartContext() noexcept = default;

        /**
         * @brief 构造指定 hart 的上下文.
         *
         * @param hart_id 当前 hart 的 ID
         */
        constexpr explicit HartContext(size_t hart_id) noexcept
            : _hart_id(hart_id) {}

        /**
         * @brief 获取该上下文所属的 hart ID.
         *
         * @return size_t hart ID
         */
        [[nodiscard]]
        constexpr size_t hart_id() const noexcept {
            return _hart_id;
        }

        /**
         * @brief 设置该上下文所属的 hart ID.
         *
         * @param hart_id 当前 hart 的 ID
         */
        constexpr void set_hart_id(size_t hart_id) noexcept {
            _hart_id = hart_id;
        }

        /**
         * @brief 获取当前 hart 的运行队列.
         *
         * @return schd::RQ* 当前 hart 的运行队列
         */
        [[nodiscard]]
        constexpr schd::RQ *rq() noexcept {
            return &_rq;
        }

        /**
         * @brief 获取当前 hart 正在运行的线程.
         *
         * @return task::TCB* 当前线程
         */
        [[nodiscard]]
        constexpr task::TCB *current_tcb() const noexcept {
            return _current_tcb;
        }

        /**
         * @brief 获取当前 hart 正在运行的线程引用槽.
         *
         * @return task::TCB*& 当前线程引用槽
         */
        [[nodiscard]]
        constexpr task::TCB *&current_tcb() noexcept {
            return _current_tcb;
        }

        /**
         * @brief 获取当前 hart 正在运行的进程.
         *
         * @return task::PCB* 当前进程
         */
        [[nodiscard]]
        constexpr task::PCB *current_pcb() const noexcept {
            return _current_pcb;
        }

        /**
         * @brief 获取当前 hart 正在运行的进程引用槽.
         *
         * @return task::PCB*& 当前进程引用槽
         */
        [[nodiscard]]
        constexpr task::PCB *&current_pcb() noexcept {
            return _current_pcb;
        }

        /**
         * @brief 获取当前 hart 绑定的任务地址空间.
         *
         * @return TaskMemoryManager* 当前任务地址空间
         */
        [[nodiscard]]
        constexpr TaskMemoryManager *tmm() const noexcept {
            return _tmm;
        }

        /**
         * @brief 获取当前 hart 绑定的任务地址空间引用槽.
         *
         * @return TaskMemoryManager*& 当前任务地址空间引用槽
         */
        [[nodiscard]]
        constexpr TaskMemoryManager *&tmm(key::tmm) noexcept {
            return _tmm;
        }

        /**
         * @brief 获取当前 hart 绑定任务地址空间的引用槽.
         *
         * @return TaskMemoryManager*& 当前任务地址空间引用槽
         */
        [[nodiscard]]
        constexpr TaskMemoryManager *&tmm_ref() noexcept {
            return _tmm;
        }

        /**
         * @brief 获取当前 hart 保存的 trap 上下文.
         *
         * @return Context* 当前 trap 上下文
         */
        [[nodiscard]]
        constexpr Context *trap_context() const noexcept {
            return _trap_context;
        }

        /**
         * @brief 获取当前 hart 保存的 trap 上下文引用槽.
         *
         * @return Context*& 当前 trap 上下文引用槽
         */
        [[nodiscard]]
        constexpr Context *&trap_context(key::trap_context) noexcept {
            return _trap_context;
        }

        /**
         * @brief 获取当前 hart trap 上下文的引用槽.
         *
         * @return Context*& 当前 trap 上下文引用槽
         */
        [[nodiscard]]
        constexpr Context *&trap_context_ref() noexcept {
            return _trap_context;
        }

        /**
         * @brief 获取当前 hart 当前激活的页表根.
         *
         * @return PhyAddr 当前页表根
         */
        [[nodiscard]]
        constexpr PhyAddr pgd() const noexcept {
            return PageMan::read_root();
        }

        /**
         * @brief 获取当前 hart 的 CLINT 定时器.
         *
         * @return device::ClintTimer* 当前 hart 的定时器
         */
        [[nodiscard]]
        constexpr device::ClintTimer *timer() const noexcept {
            return _timer;
        }

        /**
         * @brief 获取当前 hart 的 CLINT 定时器引用槽.
         *
         * @return device::ClintTimer*& 当前 hart 的定时器引用槽
         */
        [[nodiscard]]
        constexpr device::ClintTimer *&timer() noexcept {
            return _timer;
        }

        /**
         * @brief 重置当前 hart 的运行时状态.
         */
        void reset_runtime() noexcept {
            _rq           = schd::RQ{};
            _current_tcb  = nullptr;
            _current_pcb  = nullptr;
            _tmm          = nullptr;
            _trap_context = nullptr;
            if (_timer != nullptr) {
                delete _timer;
                _timer = nullptr;
            }
        }

    private:
        size_t _hart_id = 0;
        schd::RQ _rq{};
        task::TCB *_current_tcb       = nullptr;
        task::PCB *_current_pcb       = nullptr;
        TaskMemoryManager *_tmm       = nullptr;
        Context *_trap_context        = nullptr;
        device::ClintTimer *_timer    = nullptr;
    };

    /**
     * @brief 固定大小的 Hart 上下文存储槽.
     */
    struct PaddedHartContext {
        HartContext ctx;
        char padding[HART_CONTEXT_SIZE - sizeof(HartContext)];
    };
    static_assert(sizeof(PaddedHartContext) == HART_CONTEXT_SIZE,
                  "PaddedHartContext size must be equal to HART_CONTEXT_SIZE");
    static_assert(offsetof(PaddedHartContext, ctx) == 0,
                  "HartContext must be the first field in PaddedHartContext");

    inline constexpr Environment::Environment() : _meminfo() {}

    inline TaskMemoryManager *Environment::tmm() const noexcept {
        return hart_ctx->tmm();
    }

    inline TaskMemoryManager *&Environment::tmm(key::tmm) noexcept {
        return hart_ctx->tmm_ref();
    }

    inline Context *Environment::trap_context() const noexcept {
        return hart_ctx->trap_context();
    }

    inline Context *&Environment::trap_context(key::trap_context) noexcept {
        return hart_ctx->trap_context_ref();
    }

    inline PhyAddr Environment::pgd() const noexcept {
        return PageMan::read_root();
    }

    inline const MemInfo &Environment::meminfo() const {
        return _meminfo;
    }

    inline MemInfo &Environment::meminfo(key::meminfo) {
        return _meminfo;
    }

    /**
     * @brief 初始化指定 hart 的 Hart 上下文槽.
     *
     * @param hart_id 要初始化的 hart ID
     */
    void construct(size_t hart_id);

    /**
     * @brief 初始化当前 hart 的运行时状态与定时器.
     */
    void init_hart();

    /**
     * @brief 获取当前 hart 的 Hart 上下文指针.
     *
     * @return HartContext* 当前 hart 的 Hart 上下文指针
     */
}  // namespace env
