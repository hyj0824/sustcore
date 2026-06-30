/**
 * @file task.cpp
 * @brief PCB/TCB capability objects
 */

#include <logger.h>
#include <object/perm.h>
#include <object/task.h>
#include <syscall/syscall.h>
#include <task/scheduler.h>
#include <task/task.h>
#include <task/task_struct.h>
#include <task/wait.h>
#include <vfs/procfs.h>

namespace cap {
    namespace {
        /**
         * @brief 获取当前正在驱动对象操作的线程.
         * 统一退化为调度器当前线程.
         *
         * @return task::TCB* 当前线程指针.
         */
        [[nodiscard]]
        task::TCB *current_object_tcb() noexcept {
            return schd::Scheduler::inst().current_tcb();
        }

        [[nodiscard]]
        Result<task::TCB *> main_signal_target(task::PCB *pcb) noexcept {
            if (pcb == nullptr) {
                unexpect_return(ErrCode::NULLPTR);
            }
            for (auto &tcb : pcb->threads) {
                if (tcb.basic_entity.state != ThreadState::DYING) {
                    return &tcb;
                }
            }
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }

        [[nodiscard]]
        size_t first_signal_index(uint64_t bits) noexcept {
            size_t index = 0;
            while ((bits & 1) == 0) {
                bits >>= 1;
                index++;
            }
            return index;
        }

        constexpr int LINUX_SIG_BLOCK   = 0;
        constexpr int LINUX_SIG_UNBLOCK = 1;
        constexpr int LINUX_SIG_SETMASK = 2;

        [[nodiscard]]
        constexpr uint64_t unmaskable_signal_mask() noexcept {
            constexpr size_t LINUX_SIGKILL = 9;
            constexpr size_t LINUX_SIGSTOP = 19;
            return (uint64_t(1) << LINUX_SIGKILL) |
                   (uint64_t(1) << LINUX_SIGSTOP);
        }
    }  // namespace

    // 无人引用的 PCB 对象会被放入 TaskManager 的回收队列中等待销毁
    void PCBPayload::destruct() {
        loggers::TASK::DEBUG("PCBPayload destruct: pid=%d", pcb->pid);
        task::TaskManager::inst().enqueue_recycle(pcb);
        delete this;
    }

    Result<size_t> PCBObject::get_pid() const {
        if (!imply(perm::pcb::GETPID)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (_obj->pcb == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        return _obj->pcb->pid;
    }

    Result<void> PCBObject::kill(int exit_code) const {
        if (!imply(perm::pcb::KILL)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (_obj->pcb == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }

        task::PCB *pcb    = _obj->pcb;
        auto *current_tcb = current_object_tcb();
        return task::TaskManager::inst().kill_pcb_impl(pcb, current_tcb,
                                                       exit_code);
    }

    Result<void> PCBObject::map(MemoryObject &memory, size_t offset,
                                VirAddr vaddr, size_t sz,
                                VMA::Prot protflg) const {
        if (!imply(perm::pcb::VMCONTEXT)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (_obj->pcb == nullptr || _obj->pcb->tmm == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        if (!memory.cap()->imply(perm::memory::MAP)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if ((protflg & VMA::PROT_R) != 0 &&
            !memory.cap()->imply(perm::memory::READ))
        {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if ((protflg & VMA::PROT_W) != 0 &&
            !memory.cap()->imply(perm::memory::WRITE))
        {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if ((protflg & VMA::PROT_X) != 0 &&
            !memory.cap()->imply(perm::memory::EXEC))
        {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }

        auto *payload = memory.obj();
        if (payload == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        if ((offset % PAGESIZE) != 0 || (vaddr.arith() % PAGESIZE) != 0 ||
            (sz % PAGESIZE) != 0)
        {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        if (sz == 0 || offset > payload->memsz || sz > payload->memsz - offset)
        {
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }

        VMA::Type type = (protflg & VMA::PROT_SHARE) != 0 ? VMA::Type::SHARE
                                                          : VMA::Type::DATA;
        auto add_res   = _obj->pcb->tmm->add_vma(type, MemoryGrowth::FIXED,
                                                 VirArea(vaddr, vaddr + sz),
                                                 payload, protflg, offset);
        propagate(add_res);
        void_return();
    }

    Result<void> PCBObject::unmap(VirAddr vaddr, size_t sz) const {
        if (!imply(perm::pcb::VMCONTEXT)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (_obj->pcb == nullptr || _obj->pcb->tmm == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        if ((vaddr.arith() % PAGESIZE) != 0 || (sz % PAGESIZE) != 0 || sz == 0)
        {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        return _obj->pcb->tmm->remove_vma_range(VirArea(vaddr, vaddr + sz));
    }

    Result<cap::VMAInfo> PCBObject::query_vaddr(VirAddr vaddr,
                                                CapIdx mem_cap) const {
        if (!imply(perm::pcb::VMCONTEXT)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (_obj->pcb == nullptr || _obj->pcb->tmm == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        auto query_res = _obj->pcb->tmm->query_vaddr(vaddr, mem_cap);
        propagate(query_res);
        auto result = query_res.value();
        return cap::VMAInfo{
            .vma_type  = static_cast<b64>(result.type),
            .vma_prot  = result.prot,
            .vma_start = result.start.addr(),
            .vma_size  = result.size,
            .mem_cap   = result.mem_cap,
        };
    }

    Result<std::vector<cap::VMAInfo>> PCBObject::query_vspace(
        size_t offset, size_t max_entries, bool expose_mem_cap) const {
        if (!imply(perm::pcb::VMCONTEXT)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (_obj->pcb == nullptr || _obj->pcb->tmm == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        auto entries = _obj->pcb->tmm->query_vspace(
            offset, max_entries, [expose_mem_cap](const VMA &) {
                return expose_mem_cap ? cap::null : cap::error;
            });
        std::vector<cap::VMAInfo> results{};
        results.reserve(entries.size());
        for (size_t i = 0; i < entries.size(); ++i) {
            results.push_back(cap::VMAInfo{
                .vma_type  = static_cast<b64>(entries[i].type),
                .vma_prot  = entries[i].prot,
                .vma_start = entries[i].start.addr(),
                .vma_size  = entries[i].size,
                .mem_cap   = entries[i].mem_cap,
            });
        }
        return results;
    }

    Result<task::PCB *> PCBObject::require_new_thread() const {
        if (!imply(perm::pcb::NEW_THREAD)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (_obj->pcb == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        return _obj->pcb;
    }

    Result<task::PCB *> PCBObject::require_new_process() const {
        if (!imply(perm::pcb::NEW_PROCESS)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (_obj->pcb == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        return _obj->pcb;
    }

    Result<task::PCB *> PCBObject::require_execute() const {
        if (!imply(perm::pcb::EXECUTE)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (_obj->pcb == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        return _obj->pcb;
    }

    Result<task::PCB *> PCBObject::require_new_process_execute() const {
        if (!imply(perm::pcb::NEW_PROCESS | perm::pcb::EXECUTE)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (_obj->pcb == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        return _obj->pcb;
    }

    Result<task::PCB *> PCBObject::require_procfs() const {
        if (!imply(perm::pcb::PROCFS)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (_obj->pcb == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        return _obj->pcb;
    }

    Result<CapIdx> PCBObject::get_procfs_cap(std::string_view name,
                                             CHolder &holder) const {
        auto pcb_res = require_procfs();
        propagate(pcb_res);
        return procfs::ProcFSDriver::inst().get(pcb_res.value()->pid, name,
                                                holder);
    }

    Result<void> PCBObject::redirect_procfs(std::string_view name,
                                            std::string_view target) const {
        auto pcb_res = require_procfs();
        propagate(pcb_res);
        return procfs::ProcFSDriver::inst().redirect(pcb_res.value()->pid, name,
                                                     target);
    }

    Result<void> PCBObject::sigaction(size_t signo,
                                      const task::SigAction *action,
                                      task::SigAction *old_action) const {
        if (!imply(perm::pcb::SIGACTION)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (_obj->pcb == nullptr || signo >= task::SignalState::MAX_SIGNALS) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        auto &state = _obj->pcb->signal_state;
        if (old_action != nullptr) {
            *old_action = state.actions[signo];
        }
        if (action != nullptr) {
            state.actions[signo] = *action;
        }
        void_return();
    }

    Result<void> PCBObject::sigmask(int how, const uint64_t *set,
                                    uint64_t *oldset) const {
        if (!imply(perm::pcb::SIGACTION)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (_obj->pcb == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }

        auto &state       = _obj->pcb->signal_state;
        uint64_t old_mask = state.blocked_mask.load();
        if (oldset != nullptr) {
            *oldset = old_mask;
        }

        if (set == nullptr) {
            void_return();
        }

        uint64_t requested_mask = *set & ~unmaskable_signal_mask();
        switch (how) {
            case LINUX_SIG_BLOCK:
                state.blocked_mask = old_mask | requested_mask;
                break;
            case LINUX_SIG_UNBLOCK:
                state.blocked_mask = old_mask & ~requested_mask;
                break;
            case LINUX_SIG_SETMASK: state.blocked_mask = requested_mask; break;
            default:                unexpect_return(ErrCode::INVALID_PARAM);
        }
        void_return();
    }

    Result<void> PCBObject::signal(size_t signo) const {
        if (!imply(perm::pcb::SIGNAL)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (_obj->pcb == nullptr || signo >= task::SignalState::MAX_SIGNALS) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        auto &state         = _obj->pcb->signal_state;
        state.pending_mask |= (uint64_t(1) << signo);

        auto target_res = main_signal_target(_obj->pcb);
        if (target_res.has_value()) {
            auto *target = target_res.value();
            task::mark_tcb_signal_interrupt(*target, signo);
            if (target->basic_entity.state ==
                ThreadState::INTERRUPTIBLE_WAITING)
            {
                (void)schd::Scheduler::inst().wakeup_waiting(target);
            }
        } else if (target_res.error() != ErrCode::ENTRY_NOT_FOUND) {
            propagate_return(target_res);
        }
        auto wake_res = wait::wake_all(state.waitsig_wd);
        if (!wake_res.has_value() &&
            wake_res.error() != ErrCode::ENTRY_NOT_FOUND &&
            wake_res.error() != ErrCode::FAILURE)
        {
            propagate_return(wake_res);
        }
        void_return();
    }

    Result<size_t> PCBObject::waitsig(uint64_t mask, size_t timeout_ns,
                                      size_t options) const {
        if (!imply(perm::pcb::WAITSIG)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (_obj->pcb == nullptr || options != 0) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        auto &state = _obj->pcb->signal_state;
        auto pending = state.pending_mask.load() & mask;
        if (pending != 0) {
            size_t signo        = first_signal_index(pending);
            state.pending_mask &= ~(uint64_t(1) << signo);
            return signo;
        }
        if (timeout_ns == 0) {
            unexpect_return(ErrCode::TIMEOUT);
        }

        auto wait_res = timeout_wait_event_int(
            state.waitsig_wd, timeout_ns, ({
                auto __pending = state.pending_mask.load() & mask;
                __pending != 0;
            }));
        if (!wait_res.has_value()) {
            if (wait_res.error() != ErrCode::INTERRUPTED) {
                propagate_return(wait_res);
            }
        }

        pending = state.pending_mask.load() & mask;
        if (pending != 0) {
            size_t signo        = first_signal_index(pending);
            state.pending_mask &= ~(uint64_t(1) << signo);
            return signo;
        }

        if (!wait_res.has_value()) {
            propagate_return(wait_res);
        }

        unexpect_return(ErrCode::FAILURE);
    }

    Result<task::TCB *> TCBObject::require_current() const {
        if (_obj->tcb == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        auto *current = current_object_tcb();
        if (current == nullptr || current != _obj->tcb) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        return _obj->tcb;
    }

    Result<void> TCBObject::kill(int exit_code) const {
        (void)exit_code;
        auto current_res = require_current();
        propagate(current_res);

        task::TCB *tcb = current_res.value();
        if (tcb->task == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }

        auto *runtime_tcb       = schd::Scheduler::inst().current_tcb();
        tcb->basic_entity.state = ThreadState::DYING;
        tcb->basic_entity
            .template flags_set<schd::SchedMeta::FLAGS_NEED_RESCHED>();
        if (runtime_tcb == tcb) {
            schd::Scheduler::inst().schedule();
        }
        void_return();
    }
}  // namespace cap
