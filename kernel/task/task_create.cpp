/**
 * @file task_create.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief task 创建、装配与运行期切换
 * @version alpha-1.0.0
 * @date 2026-06-21
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <cap/permission.h>
#include <env.h>
#include <guard.h>
#include <kinit.h>
#include <logger.h>
#include <object/memory.h>
#include <object/task.h>
#include <sus/raii.h>
#include <task/scheduler.h>
#include <task/startup.h>
#include <task/task.h>
#include <vfs/vfs.h>

#include <cassert>
#include <unordered_set>
#include <utility>
#include <vector>

namespace task {
    namespace {
        extern "C" [[noreturn]] void new_utask_trampoline();

        void init_user_context(Context *ctx, void *entrypoint, void *stack_top,
                               void *kstack_top,
                               umb_t linuxproc_entrypoint = 0) noexcept {
            assert(ctx != nullptr);
            *ctx = {};
            ctx->setup_regs<SetupCase::USER_THREAD>();
            ctx->pc()         = reinterpret_cast<umb_t>(entrypoint);
            ctx->sp()         = reinterpret_cast<umb_t>(stack_top);
            ctx->linux_ra()   = linuxproc_entrypoint;
            ctx->kstack_top() = reinterpret_cast<umb_t>(kstack_top);
        }

        template <SetupCase setcase>
        void init_kernel_context(Context *ctx, void *entrypoint, void *arg0,
                                 void *stack_top) noexcept {
            assert(ctx != nullptr);
            *ctx = {};
            ctx->setup_regs<setcase>();
            ctx->sp()           = reinterpret_cast<umb_t>(stack_top);
            ctx->ra()           = reinterpret_cast<umb_t>(entrypoint);
            ctx->kthread_arg0() = reinterpret_cast<umb_t>(arg0);
        }

        void reset_thread_runtime(util::nonnull<TCB *> tcb) noexcept {
            tcb->basic_entity.state   = ThreadState::EMPTY;
            tcb->basic_entity.rq_head = {};
            tcb->basic_entity.flags   = 0;
            tcb->rr_entity            = {};
            tcb->syscall_info.reset();
        }

        void build_user_contexts(util::nonnull<TCB *> tcb, void *entrypoint,
                                 void *user_stack_top,
                                 umb_t linuxproc_entrypoint = 0) noexcept {
            tcb->reset_kstack();
            auto *user_ctx = tcb->push<Context>();
            init_user_context(user_ctx, entrypoint, user_stack_top,
                              tcb->kstack_top(), linuxproc_entrypoint);
            init_kernel_context<SetupCase::UTHREAD_TRAMPOLINE>(
                tcb->kernel_context_ptr(),
                reinterpret_cast<void *>(&new_utask_trampoline), user_ctx,
                tcb->kstack_top());
        }

        void build_kernel_context(util::nonnull<TCB *> tcb, void *entrypoint,
                                  void *arg0) noexcept {
            tcb->reset_kstack();
            init_kernel_context<SetupCase::KTHREAD>(
                tcb->kernel_context_ptr(), entrypoint, arg0, tcb->kstack_top());
        }

        void prepare_user_thread(util::nonnull<TCB *> tcb, void *entrypoint,
                                 void *user_stack_top,
                                 schd::ClassType schd_class,
                                 umb_t linuxproc_entrypoint = 0) noexcept {
            tcb->is_kernel  = false;
            tcb->boot_role  = BootThreadRole::NONE;
            tcb->schd_class = schd_class;
            reset_thread_runtime(tcb);
            build_user_contexts(tcb, entrypoint, user_stack_top,
                                linuxproc_entrypoint);
        }

        void prepare_kernel_thread(util::nonnull<TCB *> tcb, void *entrypoint,
                                   void *arg0,
                                   schd::ClassType schd_class) noexcept {
            tcb->is_kernel  = true;
            tcb->boot_role  = BootThreadRole::NONE;
            tcb->schd_class = schd_class;
            reset_thread_runtime(tcb);
            build_kernel_context(tcb, entrypoint, arg0);
        }

        void prepare_forked_thread(util::nonnull<TCB *> tcb,
                                   const Context &parent_ctx,
                                   schd::ClassType parent_schd_class,
                                   BootThreadRole parent_boot_role) noexcept {
            tcb->is_kernel = false;
            reset_thread_runtime(tcb);
            tcb->reset_kstack();
            auto *child_user_ctx = tcb->push<Context>();
            *child_user_ctx      = parent_ctx;
            child_user_ctx->kstack_top() =
                reinterpret_cast<umb_t>(tcb->kstack_top());
            write_ret(*child_user_ctx,
                      syscall::RetPack{
                          .processed = true,
                          .ret0      = 0,
                          .ret1      = static_cast<b64>(ErrCode::SUCCESS),
                      });
            init_kernel_context<SetupCase::UTHREAD_TRAMPOLINE>(
                tcb->kernel_context_ptr(),
                reinterpret_cast<void *>(&new_utask_trampoline), child_user_ctx,
                tcb->kstack_top());
            tcb->boot_role = parent_schd_class == schd::ClassType::INIT
                                 ? BootThreadRole::NONE
                                 : parent_boot_role;
            tcb->schd_class = parent_schd_class == schd::ClassType::INIT
                                  ? schd::ClassType::FCFS
                                  : parent_schd_class;
        }

        schd::ClassType exec_sched_class(PCB *pcb, TCB *current_tcb,
                                         bool target_current) {
            if (target_current && current_tcb != nullptr) {
                return current_tcb->schd_class;
            }
            if (pcb != nullptr && !pcb->threads.empty()) {
                schd::ClassType cls = pcb->threads.front().schd_class;
                if (cls != schd::ClassType::IDLE && cls != schd::ClassType::BOT)
                {
                    return cls;
                }
            }
            return schd::ClassType::FCFS;
        }

        void kthread_idle() {
            while (true) {
                Idle::idle();
            }
        }

        [[noreturn]]
        void kthread_exit() {
            auto *tcb = schd::Scheduler::inst().current_tcb();
            if (tcb == nullptr || !tcb->is_kernel) {
                panic("非法内核线程退出");
            }
            tcb->basic_entity.state = ThreadState::WAITING;
            tcb->basic_entity
                .template flags_set<schd::SchedMeta::FLAGS_NEED_RESCHED>();
            schd::Scheduler::inst().schedule();
            panic("内核线程退出后仍被调度");
            while (true);
        }

        [[noreturn]]
        void kthread_trampoline() {
            auto *tcb = schd::Scheduler::inst().current_tcb();
            if (tcb == nullptr || !tcb->is_kernel || tcb->kentry == nullptr) {
                panic("内核线程入口无效");
            }
            tcb->kentry(tcb->karg);
            kthread_exit();
        }

        void kthread_noarg_entry(void *arg) {
            auto entry = reinterpret_cast<void (*)()>(arg);
            entry();
        }

        Result<CapIdx> insert_pcb_cap(PCB *pcb) {
            if (pcb == nullptr || pcb->cholder == nullptr) {
                unexpect_return(ErrCode::NULLPTR);
            }
            auto *payload = new cap::PCBPayload(pcb);
            if (payload == nullptr) {
                unexpect_return(ErrCode::OUT_OF_MEMORY);
            }
            auto insert_res = pcb->cholder->insert_to_free(payload);
            if (!insert_res.has_value()) {
                delete payload;
                propagate_return(insert_res);
            }
            return insert_res.value();
        }

        struct ReservedCapSet {
            std::unordered_set<CapIdx> caps{};

            [[nodiscard]]
            bool contains(CapIdx idx) const {
                return caps.contains(idx);
            }
        };

        Result<ReservedCapSet> collect_reserved_caps(
            cap::CHolder *holder, CapIdx pcb_cap, const CapIdx *reserved_caps,
            size_t reserved_count) {
            if (holder == nullptr) {
                unexpect_return(ErrCode::NULLPTR);
            }

            auto pcb_cap_res = holder->lookup(pcb_cap);
            propagate(pcb_cap_res);

            ReservedCapSet set{};
            set.caps.insert(pcb_cap);
            for (size_t i = 0; i < reserved_count; ++i) {
                CapIdx idx   = reserved_caps[i];
                auto cap_res = holder->lookup(idx);
                propagate(cap_res);
                set.caps.insert(idx);
            }
            return set;
        }

        Result<void> remove_unreserved_caps(cap::CHolder *holder,
                                            const ReservedCapSet &reserved) {
            if (holder == nullptr) {
                unexpect_return(ErrCode::NULLPTR);
            }

            ErrCode err = ErrCode::SUCCESS;
            holder->space().foreach ([&](CapIdx idx, cap::Capability *) {
                if (err != ErrCode::SUCCESS) {
                    return;
                }
                if (reserved.contains(idx)) {
                    return;
                }
                auto remove_res = holder->remove(idx);
                if (!remove_res.has_value()) {
                    err = remove_res.error();
                }
            });
            if (err != ErrCode::SUCCESS) {
                unexpect_return(err);
            }
            void_return();
        }

        TaskSpec make_empty_task_spec() {
            return TaskSpec{
                .tmm          = util::owner<TaskMemoryManager *>(nullptr),
                .holder       = nullptr,
                .entrypoint   = VirAddr(static_cast<addr_t>(0)),
                .linuxproc_entrypoint = VirAddr(static_cast<addr_t>(0)),
                .startup_blob = util::owner<char *>(nullptr),
            };
        }

        void cleanup_task_spec(TaskSpec &spec) {
            if (spec.tmm.get() != nullptr) {
                PhyAddr pgd = spec.tmm->pgd();
                delete spec.tmm;
                GFP::put_page(pgd, 1);
                spec.tmm = util::owner<TaskMemoryManager *>(nullptr);
            }
            if (spec.startup_blob != nullptr) {
                delete[] spec.startup_blob.get();
                spec.startup_blob      = util::owner<char *>(nullptr);
                spec.startup_blob_size = 0;
            }
        }

        class TaskSpecGuard {
        public:
            explicit TaskSpecGuard(TaskSpec &spec) noexcept : _spec(&spec) {}

            TaskSpecGuard(const TaskSpecGuard &)            = delete;
            TaskSpecGuard &operator=(const TaskSpecGuard &) = delete;

            ~TaskSpecGuard() {
                if (_spec != nullptr) {
                    cleanup_task_spec(*_spec);
                }
            }

            void release() noexcept {
                _spec = nullptr;
            }

        private:
            TaskSpec *_spec;
        };

        template <typename LoadSpecFn, typename... Args>
        Result<TaskSpec> load_task_spec_impl(TaskManager &manager,
                                             LoadSpecFn load_spec_fn,
                                             Args &&...args) {
            TaskSpec spec = make_empty_task_spec();
            LoadPrm load_prm{};
            auto load_spec_res = (manager.*load_spec_fn)(
                std::forward<Args>(args)..., spec, load_prm);
            propagate(load_spec_res);
            return spec;
        }
    }  // namespace

    Result<VirAddr> TaskManager::build_user_stack(
        cap::MemoryPayload &stack_mem, VirAddr stack_top,
        const task::StartupInfo &startup_info, const void *startup_blob,
        size_t startup_blob_size) {
        if (!stack_top.nonnull()) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        if (startup_blob_size != 0 && startup_blob == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }

        constexpr size_t STACK_ALIGN = 16;
        addr_t stack_arith           = stack_top.arith();
        size_t total_size =
            sizeof(size_t) + sizeof(task::StartupInfo) + startup_blob_size;
        if (stack_arith < total_size) {
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }

        addr_t raw_sp     = stack_arith - total_size;
        addr_t aligned_sp = raw_sp & ~(static_cast<addr_t>(STACK_ALIGN - 1));
        if (aligned_sp < USER_STACK_BOTTOM.arith()) {
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }

        VirAddr user_sp(aligned_sp);
        size_t mem_offset = user_sp - USER_STACK_BOTTOM;
        auto size_write_res =
            stack_mem.write(mem_offset, &total_size, sizeof(total_size));
        propagate(size_write_res);
        auto info_write_res = stack_mem.write(
            mem_offset + sizeof(size_t), &startup_info, sizeof(startup_info));
        propagate(info_write_res);
        if (startup_blob_size != 0) {
            auto blob_write_res = stack_mem.write(
                mem_offset + sizeof(size_t) + sizeof(startup_info),
                startup_blob, startup_blob_size);
            propagate(blob_write_res);
        }
        loggers::TASK::DEBUG(
            "build_user_stack: stack_top=%p raw_sp=%p aligned_sp=%p "
            "mem_off=%lu total=%lu blob=%lu",
            stack_top.addr(), reinterpret_cast<void *>(raw_sp), user_sp.addr(),
            mem_offset, total_size, startup_blob_size);
        return user_sp;
    }

    Result<util::nonnull<TCB *>> TaskManager::setup_user_main_thread(
        util::nonnull<PCB *> pcb, util::nonnull<TCB *> tcb, TaskSpec &spec,
        task::StartupInfo startup_info, bool link_into_pcb,
        const char *log_tag) {
        if (pcb->cholder == nullptr || pcb->tmm.get() == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }

        auto *stack_mem = new cap::MemoryPayload(MAX_INITIAL_STACK_SIZE, false,
                                                 false, VMA::Growth::GROW_DOWN);
        if (stack_mem == nullptr) {
            unexpect_return(ErrCode::OUT_OF_MEMORY);
        }
        auto stack_cap_res = pcb->cholder->insert_to_free(stack_mem);
        if (!stack_cap_res.has_value()) {
            delete stack_mem;
            propagate_return(stack_cap_res);
        }
        auto vma_res =
            pcb->tmm->add_vma(VMA::Type::STACK, VMA::Growth::GROW_DOWN,
                              VirArea(USER_STACK_BOTTOM, USER_STACK_TOP),
                              stack_mem, PageMan::RWX::RW);
        propagate(vma_res);
        loggers::TASK::INFO(
            "创建STACK VMA: pid=%lu area=[%p,%p) mem=%p memsz=%lu mem_off=%lu",
            pcb->pid, USER_STACK_BOTTOM.addr(), USER_STACK_TOP.addr(),
            stack_mem, static_cast<unsigned long>(MAX_INITIAL_STACK_SIZE), 0UL);

        auto tcb_cap_res = pcb->cholder->create<cap::TCBPayload>(tcb.get());
        propagate(tcb_cap_res);

        pcb->main_tcb_cap          = tcb_cap_res.value();
        startup_info.pcb_cap       = pcb->pcb_cap;
        startup_info.main_tcb_cap  = pcb->main_tcb_cap;
        startup_info.stack_mem_cap = stack_cap_res.value();
        auto stack_top_res =
            build_user_stack(*stack_mem, USER_STACK_TOP, startup_info,
                             spec.startup_blob.get(), spec.startup_blob_size);
        propagate(stack_top_res);
        loggers::TASK::DEBUG("栈内存分配: pid=%lu 栈内存地址=%p 已分配=%lu",
                             pcb->pid, stack_mem, stack_mem->allocated_size());
        prepare_user_thread(tcb, pcb->entrypoint.addr(),
                            stack_top_res.value().addr(), tcb->schd_class,
                            spec.linuxproc_entrypoint.arith());
        if (link_into_pcb) {
            pcb->threads.push_back(*tcb);
        }
        loggers::TASK::INFO("%s: pid=%lu entry=%p sp=%p", log_tag, pcb->pid,
                            pcb->entrypoint.addr(),
                            stack_top_res.value().addr());

        if (spec.startup_blob != nullptr) {
            delete[] spec.startup_blob.get();
            spec.startup_blob      = util::owner<char *>(nullptr);
            spec.startup_blob_size = 0;
        }
        return tcb;
    }

    Result<util::nonnull<TCB *>> TaskManager::construct_thread(
        util::nonnull<PCB *> pcb, void *entrypoint, void *stack_top,
        schd::ClassType schd_class, bool kernel_thread) {
        auto tcb_res = create_bound_tcb(pcb);
        propagate(tcb_res);
        util::nonnull<TCB *> tcb = tcb_res.value();

        if (kernel_thread) {
            prepare_kernel_thread(tcb, entrypoint, nullptr, schd_class);
        } else {
            prepare_user_thread(tcb, entrypoint, stack_top, schd_class);
        }

        pcb->threads.push_back(*tcb);
        return tcb;
    }

    Result<util::nonnull<TCB *>> TaskManager::populate_task(
        util::nonnull<PCB *> pcb, TaskSpec spec, schd::ClassType schd_class,
        TCB *reuse_main_tcb) {
        if (pcb->is_kernel) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        if (spec.tmm.get() == nullptr || spec.holder == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }

        pcb->tmm                   = spec.tmm;
        pcb->cholder               = spec.holder;
        pcb->entrypoint            = spec.entrypoint;
        pcb->linuxproc_entrypoint  = spec.linuxproc_entrypoint;
        pcb->linux_subsystem_entry = spec.entrypoint;
        pcb->is_linux_process      = spec.linuxproc_entrypoint.nonnull();

        if (pcb->pcb_cap == cap::null) {
            auto pcb_cap_res = insert_pcb_cap(pcb);
            propagate(pcb_cap_res);
            pcb->pcb_cap = pcb_cap_res.value();
        } else {
            auto pcb_cap_res = pcb->cholder->lookup(pcb->pcb_cap);
            propagate(pcb_cap_res);
        }

        task::StartupInfo startup_info{
            .heap_vaddr    = spec.heap_vaddr,
            .stack_vaddr   = USER_STACK_BOTTOM,
            .entrypoint    = spec.entrypoint,
            .pcb_cap       = pcb->pcb_cap,
            .main_tcb_cap  = cap::null,
            .heap_mem_cap  = spec.heap_mem_cap,
            .stack_mem_cap = cap::null,
        };

        if (reuse_main_tcb == nullptr) {
            auto tcb_res = create_bound_tcb(pcb);
            propagate(tcb_res);
            auto tcb = tcb_res.value();
            auto tcb_guard =
                util::Guard([this, tcb]() { (void)recycle_tcb(tcb); });
            tcb->is_kernel  = false;
            tcb->boot_role  = BootThreadRole::NONE;
            tcb->schd_class = schd_class;
            auto setup_res = setup_user_main_thread(
                pcb, tcb, spec, startup_info, true, "构造用户主线程");
            propagate(setup_res);
            tcb_guard.release();
            return setup_res.value();
        }

        auto tcb        = util::nnullforce(reuse_main_tcb);
        tcb->task       = pcb;
        tcb->is_kernel  = false;
        tcb->boot_role  = BootThreadRole::NONE;
        tcb->schd_class = schd_class;
        return setup_user_main_thread(pcb, tcb, spec, startup_info, true,
                                      "复用主线程上下文");
    }

    Result<util::nonnull<PCB *>> TaskManager::commit_loaded_task(
        TaskSpec spec, schd::ClassType schd_class, bool wakeup) {
        util::nonnull<PCB *> pcb = alloc_pcb();
        auto pcb_guard           = delete_guard(util::owner(pcb.get()));

        auto init_res = init_pcb(pcb);
        if (!init_res.has_value()) {
            loggers::SUSTCORE::ERROR("初始化PCB失败! 错误码: %s",
                                     to_cstring(init_res.error()));
            propagate_return(init_res);
        }

        auto main_thread_res = populate_task(pcb, spec, schd_class);
        if (!main_thread_res.has_value()) {
            loggers::SUSTCORE::ERROR("构造主线程失败! 错误码: %s",
                                     to_cstring(main_thread_res.error()));
            propagate_return(main_thread_res);
        }
        if (schd_class == schd::ClassType::INIT) {
            main_thread_res.value()->boot_role = BootThreadRole::INIT_USER;
        }

        if (wakeup) {
            TCB *main_tcb = main_thread_res.value();
            if (!schd::Scheduler::inst().wakeup_new(main_tcb)) {
                loggers::SUSTCORE::ERROR("唤醒新进程失败");
                unexpect_return(ErrCode::CREATION_FAILED);
            }
        }

        _pid_map[pcb->pid] = pcb.get();
        pcb_guard.release();
        return pcb;
    }

    Result<util::nonnull<PCB *>> TaskManager::create_loaded_user_task(
        TaskSpec spec, schd::ClassType schd_class, bool wakeup) {
        return commit_loaded_task(spec, schd_class, wakeup);
    }

    Result<util::nonnull<PCB *>> TaskManager::create_kernel_task() {
        if (_kernel_pcb != nullptr) {
            return util::nnullforce(_kernel_pcb);
        }

        util::nonnull<PCB *> pcb = alloc_pcb();
        auto pcb_guard           = delete_guard(util::owner(pcb.get()));
        auto init_res            = init_pcb(pcb);
        propagate(init_res);

        pcb->is_kernel = true;
        auto tmm_res =
            TaskMemoryManager::from_existing_pgd(env::inst().main_kernel_pgd());
        if (!tmm_res.has_value()) {
            loggers::SUSTCORE::FATAL("无法绑定主内核页表: %s",
                                     to_cstring(tmm_res.error()));
            propagate_return(tmm_res);
        }
        auto tmm_guard  = delete_guard(tmm_res.value());
        pcb->tmm        = tmm_res.value();
        auto create_res = cap::CHolderManager::inst().create_holder();
        propagate(create_res);
        pcb->cholder      = create_res.value();
        pcb->entrypoint   = VirAddr(nullptr);
        pcb->pcb_cap      = cap::null;
        pcb->main_tcb_cap = cap::null;

        _kernel_pcb        = pcb.get();
        _pid_map[pcb->pid] = pcb.get();
        pcb_guard.release();
        tmm_guard.release();
        return pcb;
    }

    Result<util::nonnull<TCB *>> TaskManager::create_kernel_thread(
        void (*entry)(), schd::ClassType schd_class) {
        return create_kernel_thread(
            &kthread_noarg_entry, reinterpret_cast<void *>(entry), schd_class);
    }

    Result<util::nonnull<TCB *>> TaskManager::create_kernel_thread(
        KThreadEntry entry, void *arg, schd::ClassType schd_class) {
        if (entry == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        if (schd_class == schd::ClassType::BOT) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        auto pcb_res = kernel_pcb();
        propagate(pcb_res);
        auto tcb_res = create_bound_tcb(pcb_res.value());
        propagate(tcb_res);
        auto tcb = tcb_res.value();
        auto tcb_guard = util::Guard([this, tcb]() { (void)recycle_tcb(tcb); });

        tcb->kentry = entry;
        tcb->karg   = arg;
        prepare_kernel_thread(tcb,
                              reinterpret_cast<void *>(&kthread_trampoline),
                              nullptr, schd_class);
        pcb_res.value()->threads.push_back(*tcb);

        tcb_guard.release();
        return tcb;
    }

    Result<util::nonnull<TCB *>> TaskManager::create_idle_thread() {
        return create_kernel_thread(&kthread_idle, schd::ClassType::IDLE);
    }

    Result<util::nonnull<TCB *>> TaskManager::create_kinit_thread() {
        auto tcb_res =
            create_kernel_thread(&kinit_runtime_entry, schd::ClassType::INIT);
        propagate(tcb_res);
        tcb_res.value()->boot_role = BootThreadRole::KINIT;
        return tcb_res.value();
    }

    Result<util::nonnull<PCB *>> TaskManager::create_init_task(
        TaskSpec spec /* ... args*/) {
        constexpr schd::ClassType INIT_SCHED_CLASS = schd::ClassType::INIT;
        return create_loaded_user_task(spec, INIT_SCHED_CLASS, false);
    }

    Result<util::nonnull<PCB *>> TaskManager::create_task(
        TaskSpec spec, schd::ClassType schd_class /* ... args*/) {
        if (schd_class == schd::ClassType::INIT ||
            schd_class == schd::ClassType::IDLE ||
            schd_class == schd::ClassType::BOT)
        {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        return create_loaded_user_task(spec, schd_class, true);
    }

    Result<util::nonnull<PCB *>> TaskManager::load_task_image(
        CapIdx image_cap, cap::CHolder *holder, schd::ClassType schd_class,
        bool wakeup, const void *startup_blob, size_t startup_blob_size) {
        auto spec_res =
            load_task_spec_impl(*this, &TaskManager::load_task_spec, image_cap,
                                holder, startup_blob, startup_blob_size);
        propagate(spec_res);
        TaskSpec spec = std::move(spec_res.value());
        TaskSpecGuard spec_guard(spec);
        auto task_res = commit_loaded_task(spec, schd_class, wakeup);
        propagate(task_res);
        spec_guard.release();
        return task_res.value();
    }

    Result<util::nonnull<PCB *>> TaskManager::load_linux_task_image(
        CapIdx image_cap, cap::CHolder *holder, CapIdx subsystem_image_cap,
        schd::ClassType schd_class, bool wakeup, const void *startup_blob,
        size_t startup_blob_size) {
        auto spec_res = load_task_spec_impl(
            *this, &TaskManager::load_linux_task_spec, image_cap, holder,
            subsystem_image_cap, startup_blob, startup_blob_size);
        propagate(spec_res);
        TaskSpec spec = std::move(spec_res.value());
        TaskSpecGuard spec_guard(spec);
        auto task_res = commit_loaded_task(spec, schd_class, wakeup);
        propagate(task_res);
        spec_guard.release();
        return task_res.value();
    }

    Result<util::nonnull<PCB *>> TaskManager::load_elf(
        CapIdx image_cap, schd::ClassType schd_class) {
        return load_task_image(image_cap, nullptr, schd_class, true);
    }

    Result<util::nonnull<PCB *>> TaskManager::load_elf_into(
        CapIdx image_cap, cap::CHolder *holder, schd::ClassType schd_class,
        const void *startup_blob, size_t startup_blob_size) {
        return load_task_image(image_cap, holder, schd_class, true,
                               startup_blob, startup_blob_size);
    }

    Result<util::nonnull<PCB *>> TaskManager::load_linux_elf_into(
        CapIdx image_cap, cap::CHolder *holder, CapIdx subsystem_image_cap,
        schd::ClassType schd_class, const void *startup_blob,
        size_t startup_blob_size) {
        return load_linux_task_image(image_cap, holder, subsystem_image_cap,
                                     schd_class, true, startup_blob,
                                     startup_blob_size);
    }

    Result<util::nonnull<PCB *>> TaskManager::load_init(const char *path) {
        constexpr schd::ClassType INIT_SCHED_CLASS = schd::ClassType::INIT;
        if (path == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        auto create_res = cap::CHolderManager::inst().create_holder();
        if (!create_res.has_value()) {
            unexpect_return(ErrCode::CREATION_FAILED);
        }
        auto *holder      = create_res.value();
        auto holder_guard = util::Guard([holder]() {
            auto rm_res =
                cap::CHolderManager::inst().remove_holder(holder->id());
            assert(rm_res.has_value());
        });
        auto image_res = VFS::inst().open(path, *holder);
        propagate(image_res);
        auto root_res =
            VFS::inst().open_dir("/", *holder,
                                 perm::vdir::READ | perm::vdir::WRITE |
                                     perm::vdir::EXEC | perm::basic::CLONE);
        propagate(root_res);
        std::vector<BootstrapCapPathView> dir_records{};
        std::vector<BootstrapCapPathView> file_records{};
        dir_records.push_back(BootstrapCapPathView{
            .cap  = root_res.value(),
            .path = "/",
        });

        TaskSpec spec = make_empty_task_spec();
        TaskSpecGuard spec_guard(spec);
        auto bootstrap_res =
            build_bootstrap_blob(nullptr, 0, dir_records, file_records, spec);
        propagate(bootstrap_res);
        auto loaded_spec_res = load_task_spec_impl(
            *this, &TaskManager::load_task_spec, image_res.value(), holder,
            spec.startup_blob.get(), spec.startup_blob_size);
        propagate(loaded_spec_res);
        spec_guard.release();

        TaskSpec loaded_spec = std::move(loaded_spec_res.value());
        TaskSpecGuard loaded_spec_guard(loaded_spec);
        auto load_res =
            commit_loaded_task(loaded_spec, INIT_SCHED_CLASS, false);
        propagate(load_res);
        loaded_spec_guard.release();
        holder_guard.release();
        return load_res.value();
    }

    Result<util::nonnull<TCB *>> TaskManager::populate_forked_task(
        util::nonnull<PCB *> child_pcb, PCB *parent_pcb, TCB *parent_tcb,
        Context *parent_ctx, CapIdx ret_slot) {
        if (parent_pcb == nullptr || parent_tcb == nullptr ||
            parent_ctx == nullptr || parent_pcb->tmm == nullptr ||
            parent_pcb->cholder == nullptr)
        {
            unexpect_return(ErrCode::NULLPTR);
        }

        auto holder_res = cap::CHolderManager::inst().create_holder();
        propagate(holder_res);
        cap::CHolder *child_holder = holder_res.value();
        auto holder_guard          = util::Guard([child_holder]() {
            auto rm_res =
                cap::CHolderManager::inst().remove_holder(child_holder->id());
            assert(rm_res.has_value());
        });

        auto pgd_res = GFP::get_free_page(1);
        propagate(pgd_res);
        auto child_tmm = util::owner(new TaskMemoryManager(pgd_res.value()));
        auto tmm_guard = util::Guard([&child_tmm]() {
            if (child_tmm.get() == nullptr) {
                return;
            }
            PhyAddr pgd = child_tmm->pgd();
            delete child_tmm;
            GFP::put_page(pgd, 1);
        });

        auto clone_mem_res = parent_pcb->tmm->clone_to_cow(*child_tmm);
        propagate(clone_mem_res);

        ErrCode clone_caps_err = ErrCode::SUCCESS;
        parent_pcb->cholder->space().foreach (
            [&](CapIdx idx, cap::Capability *parent_cap) {
                if (clone_caps_err != ErrCode::SUCCESS) {
                    return;
                }
                cap::Payload *payload = parent_cap->payload();
                auto *memory = parent_cap->payload_as<cap::MemoryPayload>();
                if (memory != nullptr) {
                    auto child_memory_res =
                        parent_pcb->tmm->cloned_memory_for(memory, *child_tmm);
                    if (child_memory_res.has_value()) {
                        payload = child_memory_res.value();
                    } else {
                        payload = memory->clone_payload();
                    }
                }
                auto insert_res =
                    child_holder->insert(idx, payload, parent_cap->perm());
                if (!insert_res.has_value()) {
                    clone_caps_err = insert_res.error();
                }
            });
        if (clone_caps_err != ErrCode::SUCCESS) {
            unexpect_return(clone_caps_err);
        }

        child_pcb->tmm          = child_tmm;
        child_pcb->cholder      = child_holder;
        child_pcb->entrypoint   = parent_pcb->entrypoint;
        child_pcb->pcb_cap      = ret_slot;
        child_pcb->main_tcb_cap = parent_pcb->main_tcb_cap;

        auto child_tcb_res = create_bound_tcb(child_pcb);
        propagate(child_tcb_res);
        auto child_tcb = child_tcb_res.value();
        auto tcb_guard =
            util::Guard([this, child_tcb]() { (void)recycle_tcb(child_tcb); });

        prepare_forked_thread(child_tcb, *parent_ctx, parent_tcb->schd_class,
                              parent_tcb->boot_role);
        child_pcb->threads.push_back(*child_tcb);
        tcb_guard.release();

        auto *pcb_payload = new cap::PCBPayload(child_pcb.get());
        if (pcb_payload == nullptr) {
            unexpect_return(ErrCode::OUT_OF_MEMORY);
        }
        auto insert_parent_res =
            parent_pcb->cholder->insert(ret_slot, pcb_payload, perm::allperm());
        if (!insert_parent_res.has_value()) {
            delete pcb_payload;
            propagate_return(insert_parent_res);
        }
        auto insert_child_res =
            child_holder->insert(ret_slot, pcb_payload, perm::allperm());
        if (!insert_child_res.has_value()) {
            auto remove_res = parent_pcb->cholder->remove(ret_slot);
            assert(remove_res.has_value());
            propagate_return(insert_child_res);
        }

        tmm_guard.release();
        holder_guard.release();
        return child_tcb;
    }

    Result<ForkResult> TaskManager::fork_current(CapIdx ret_slot) {
        auto *parent_tcb = schd::Scheduler::inst().current_tcb();
        auto *parent_ctx = env::inst().trap_context();
        if (parent_tcb == nullptr || parent_tcb->task == nullptr ||
            parent_ctx == nullptr)
        {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        PCB *parent_pcb = parent_tcb->task;
        if (parent_pcb->is_kernel) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        if (parent_pcb->tmm == nullptr || parent_pcb->cholder == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }

        util::nonnull<PCB *> child_pcb = alloc_pcb();
        auto pcb_guard = delete_guard(util::owner(child_pcb.get()));
        auto init_res  = init_pcb(child_pcb);
        propagate(init_res);
        auto fill_res = populate_forked_task(child_pcb, parent_pcb, parent_tcb,
                                             parent_ctx, ret_slot);
        propagate(fill_res);
        TCB *child_tcb = fill_res.value();

        _pid_map[child_pcb->pid] = child_pcb.get();
        if (!schd::Scheduler::inst().wakeup_new(child_tcb)) {
            _pid_map.erase(child_pcb->pid);
            unexpect_return(ErrCode::CREATION_FAILED);
        }

        ForkResult result{ret_slot, child_pcb->pid};
        pcb_guard.release();
        return result;
    }

    Result<CapIdx> TaskManager::create_thread_current(VirAddr entry,
                                                      VirAddr stack_addr,
                                                      size_t stack_size) {
        auto *current_tcb = schd::Scheduler::inst().current_tcb();
        if (current_tcb == nullptr || current_tcb->task == nullptr) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        PCB *pcb = current_tcb->task;
        if (pcb->is_kernel) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        if (pcb->tmm == nullptr || pcb->cholder == nullptr ||
            !entry.nonnull() || !stack_addr.nonnull() || stack_size == 0)
        {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        if (current_tcb->schd_class == schd::ClassType::IDLE) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        addr_t stack_base = stack_addr.arith();
        if (stack_base > MAX_ADDR - stack_size) {
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }
        VirAddr stack_top(stack_base + stack_size);
        VirArea stack_area(stack_addr, stack_top);
        auto range_res = pcb->tmm->locate_range(stack_area);
        propagate(range_res);

        auto con_res =
            construct_thread(util::nnullforce(pcb), entry.addr(),
                             stack_top.addr(), current_tcb->schd_class);
        propagate(con_res);
        auto tcb       = con_res.value();
        auto tcb_guard = util::Guard([this, tcb]() { (void)recycle_tcb(tcb); });

        auto cap_res = pcb->cholder->create<cap::TCBPayload>(tcb.get());
        propagate(cap_res);
        auto cap_guard = remove_guard(pcb->cholder, cap_res.value());

        if (!schd::Scheduler::inst().wakeup_new(tcb.get())) {
            unexpect_return(ErrCode::CREATION_FAILED);
        }

        cap_guard.release();
        tcb_guard.release();
        return cap_res.value();
    }

    Result<void> TaskManager::exec_current(CapIdx image_cap,
                                           const CapIdx *reserved_caps,
                                           size_t reserved_count) {
        auto *current_tcb = schd::Scheduler::inst().current_tcb();
        if (current_tcb == nullptr || current_tcb->task == nullptr) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        return exec_pcb(util::nnullforce(current_tcb->task), image_cap,
                        reserved_caps, reserved_count, nullptr, 0);
    }

    Result<void> TaskManager::exec_pcb(util::nonnull<PCB *> target,
                                       CapIdx image_cap,
                                       const CapIdx *reserved_caps,
                                       size_t reserved_count,
                                       const void *startup_blob,
                                       size_t startup_blob_size) {
        PCB *pcb = target.get();
        if (pcb->is_kernel) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        if (pcb->tmm == nullptr || pcb->cholder == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        if (startup_blob_size != 0 && startup_blob == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        auto *current_tcb = schd::Scheduler::inst().current_tcb();
        bool target_current =
            current_tcb != nullptr && current_tcb->task == pcb;
        schd::ClassType schd_class =
            exec_sched_class(pcb, current_tcb, target_current);
        TCB *reuse_tcb = target_current ? current_tcb : nullptr;

        auto reserved_res = collect_reserved_caps(
            pcb->cholder, pcb->pcb_cap, reserved_caps, reserved_count);
        propagate(reserved_res);
        const auto &reserved = reserved_res.value();

        TaskSpec spec = make_empty_task_spec();
        TaskSpecGuard spec_guard(spec);
        LoadPrm load_prm{};
        auto load_spec_res =
            load_task_spec(image_cap, pcb->cholder, startup_blob,
                           startup_blob_size, spec, load_prm);
        propagate(load_spec_res);

        auto prune_res = remove_unreserved_caps(pcb->cholder, reserved);
        propagate(prune_res);

        TaskMemoryManager *old_tmm = pcb->tmm;
        while (!pcb->threads.empty()) {
            TCB *tcb = &pcb->threads.front();
            pcb->threads.pop_front();
            if (tcb == reuse_tcb) {
                continue;
            }
            if (tcb->basic_entity.state == ThreadState::READY) {
                auto dequeue_res =
                    schd::Scheduler::inst().dequeue(util::nnullforce(tcb));
                propagate(dequeue_res);
            }
            auto recycle_res = recycle_tcb(util::nnullforce(tcb));
            propagate(recycle_res);
        }
        pcb->tmm          = util::owner<TaskMemoryManager *>(nullptr);
        pcb->entrypoint   = VirAddr(static_cast<addr_t>(0));
        pcb->main_tcb_cap = cap::null;

        auto populate_res =
            populate_task(util::nnullforce(pcb), spec, schd_class, reuse_tcb);
        propagate(populate_res);

        if (target_current) {
            current_tcb->basic_entity.state = ThreadState::RUNNING;
            current_tcb->basic_entity.flags = 0;
            current_tcb->rr_entity          = {};
        } else if (!schd::Scheduler::inst().wakeup_new(populate_res.value())) {
            unexpect_return(ErrCode::CREATION_FAILED);
        }

        spec_guard.release();

        if (target_current) {
            schd::switch_pgd(pcb->tmm);
        }

        TaskSpec old_spec{
            .tmm        = util::owner(old_tmm),
            .holder     = nullptr,
            .entrypoint = VirAddr(static_cast<addr_t>(0)),
        };
        cleanup_task_spec(old_spec);

        loggers::SUSTCORE::DEBUG("execve成功: image_cap=%p pid=%d", image_cap,
                                 pcb->pid);
        void_return();
    }
}  // namespace task
