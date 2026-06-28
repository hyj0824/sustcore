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

#include <fwd.h>
#include <arch/description.h>
#include <cstring>
#include <device/cpu.h>
#include <device/int.h>
#include <mem/vma.h>
#include <schd/schdbase.h>
#include <sustcore/boot.h>
#include <task/task_struct.h>
#include <sustcore/addr.h>

namespace env {
    register HartContext *hart_ctx asm("tp");

    // passkey
    namespace key {
        struct set {
        public:
            constexpr set() noexcept = default;
        };
    }  // namespace key

    /**
     * @brief `/proc/meminfo` 使用的全局内存统计缓存.
     *
     * 内部统一按页数维护，读取 `/proc/meminfo` 时再换算为 kB。
     */
    struct SystemMemoryInfo {
        size_t mem_total_pages = 0;
        size_t mem_free_pages = 0;
        size_t buffer_pages = 0;
        size_t page_cache_pages = 0;
        size_t active_file_pages = 0;
        size_t inactive_file_pages = 0;
        size_t kernel_stack_pages = 0;
        size_t page_table_pages = 0;
        size_t anon_pages = 0;
        size_t mapped_pages = 0;
        size_t dirty_pages = 0;
        size_t writeback_pages = 0;
        size_t committed_pages = 0;
        size_t directmap_4k_pages = 0;
        size_t directmap_2m_pages = 0;
        size_t directmap_1g_pages = 0;
    };

    class Environment {
    private:
        alignas(PAGESIZE) byte _bootinfo_storage[::MAX_BOOTINFO_SIZE] = {};
        size_t _bootinfo_size                                = 0;
        PhyAddr _main_kernel_pgd = PhyAddr::null;
        SystemMemoryInfo _system_memory_info{};

    public:
        constexpr static size_t MAX_BOOTINFO_SIZE = ::MAX_BOOTINFO_SIZE;
        constexpr Environment();

        // readers
        [[nodiscard]]
        TaskMemoryManager *tmm() const noexcept;
        [[nodiscard]]
        TaskMemoryManager *&tmm(key::set) noexcept;

        [[nodiscard]]
        PhyAddr main_kernel_pgd() const noexcept;
        [[nodiscard]]
        PhyAddr &main_kernel_pgd(key::set) noexcept;

        [[nodiscard]]
        Context *trap_context() const noexcept;
        [[nodiscard]]
        Context *&trap_context(key::set) noexcept;

        [[nodiscard]]
        PhyAddr pgd() const noexcept;

        [[nodiscard]]
        const BootInfoHeader *bootinfo() const noexcept;
        [[nodiscard]]
        BootInfoHeader *bootinfo(key::set) noexcept;
        [[nodiscard]]
        const byte *bootinfo_storage() const noexcept;
        [[nodiscard]]
        byte *bootinfo_storage(key::set) noexcept;
        [[nodiscard]]
        size_t bootinfo_size() const noexcept;
        [[nodiscard]]
        size_t &bootinfo_size(key::set) noexcept;
        [[nodiscard]]
        const SystemMemoryInfo &system_memory_info() const noexcept;
        [[nodiscard]]
        SystemMemoryInfo &system_memory_info(key::set) noexcept;
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
        constexpr TaskMemoryManager *&tmm(key::set) noexcept {
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
        constexpr Context *&trap_context(key::set) noexcept {
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
         * @brief 获取当前 hart 的定时器.
         *
         * @return device::Alarm* 当前 的定时器
         */
        [[nodiscard]]
        constexpr device::Alarm *alarm() const noexcept {
            return _alarm;
        }

        /**
         * @brief 获取当前 hart 的定时器引用槽.
         *
         * @return device::Alarm*& 当前 hart 的定时器引用槽
         */
        [[nodiscard]]
        constexpr device::Alarm *&alarm() noexcept {
            return _alarm;
        }

        /**
         * @brief 获取当前 hart 的 TimeKeeper.
         *
         * @return device::TimeKeeper* 当前 hart 的 TimeKeeper.
         */
        [[nodiscard]]
        constexpr device::TimeKeeper *time_keeper() const noexcept {
            return _time_keeper;
        }

        /**
         * @brief 获取当前 hart 的 TimeKeeper 引用槽.
         *
         * @return device::TimeKeeper*& 当前 hart 的 TimeKeeper 引用槽.
         */
        [[nodiscard]]
        constexpr device::TimeKeeper *&time_keeper() noexcept {
            return _time_keeper;
        }

        /**
         * @brief 获取当前 hart 绑定的 CPU 对象.
         *
         * @return device::Cpu* 当前 hart 的 CPU 对象.
         */
        [[nodiscard]]
        constexpr device::Cpu *cpu() const noexcept {
            return _cpu;
        }

        /**
         * @brief 获取当前 hart 绑定 CPU 对象的引用槽.
         *
         * @return device::Cpu*& 当前 hart 的 CPU 引用槽.
         */
        [[nodiscard]]
        constexpr device::Cpu *&cpu() noexcept {
            return _cpu;
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
            _cpu          = nullptr;
            if (_alarm != nullptr) {
                delete _alarm;
                _alarm = nullptr;
            }
            if (_time_keeper != nullptr) {
                delete _time_keeper;
                _time_keeper = nullptr;
            }
        }

    private:
        size_t _hart_id = 0;
        schd::RQ _rq{};
        task::TCB *_current_tcb       = nullptr;
        task::PCB *_current_pcb       = nullptr;
        TaskMemoryManager *_tmm       = nullptr;
        Context *_trap_context        = nullptr;
        device::Cpu *_cpu             = nullptr;
        device::Alarm *_alarm         = nullptr;
        device::TimeKeeper *_time_keeper = nullptr;
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

    inline constexpr Environment::Environment() = default;

    inline TaskMemoryManager *Environment::tmm() const noexcept {
        return hart_ctx->tmm();
    }

    inline TaskMemoryManager *&Environment::tmm(key::set) noexcept {
        return hart_ctx->tmm_ref();
    }

    inline PhyAddr Environment::main_kernel_pgd() const noexcept {
        return _main_kernel_pgd;
    }

    inline PhyAddr &Environment::main_kernel_pgd(key::set) noexcept {
        return _main_kernel_pgd;
    }

    inline Context *Environment::trap_context() const noexcept {
        return hart_ctx->trap_context();
    }

    inline Context *&Environment::trap_context(key::set) noexcept {
        return hart_ctx->trap_context_ref();
    }

    inline PhyAddr Environment::pgd() const noexcept {
        return PageMan::read_root();
    }

    inline const BootInfoHeader *Environment::bootinfo() const noexcept {
        if (_bootinfo_size == 0) {
            return nullptr;
        }
        return reinterpret_cast<const BootInfoHeader *>(_bootinfo_storage);
    }

    inline BootInfoHeader *Environment::bootinfo(key::set) noexcept {
        if (_bootinfo_size == 0) {
            return nullptr;
        }
        return reinterpret_cast<BootInfoHeader *>(_bootinfo_storage);
    }

    inline const byte *Environment::bootinfo_storage() const noexcept {
        return _bootinfo_storage;
    }

    inline byte *Environment::bootinfo_storage(key::set) noexcept {
        return _bootinfo_storage;
    }

    inline size_t Environment::bootinfo_size() const noexcept {
        return _bootinfo_size;
    }

    inline size_t &Environment::bootinfo_size(key::set) noexcept {
        return _bootinfo_size;
    }

    inline const SystemMemoryInfo &Environment::system_memory_info()
        const noexcept {
        return _system_memory_info;
    }

    inline SystemMemoryInfo &Environment::system_memory_info(
        key::set) noexcept {
        return _system_memory_info;
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
