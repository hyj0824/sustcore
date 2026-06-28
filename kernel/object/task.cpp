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
        if (sz == 0 || offset > payload->memsz || sz > payload->memsz - offset) {
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }

        VMA::Type type =
            (protflg & VMA::PROT_SHARE) != 0 ? VMA::Type::SHARE
                                             : VMA::Type::DATA;
        auto add_res = _obj->pcb->tmm->add_vma(
            type, MemoryGrowth::FIXED, VirArea(vaddr, vaddr + sz), payload,
            protflg, offset);
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
        if ((vaddr.arith() % PAGESIZE) != 0 || (sz % PAGESIZE) != 0 || sz == 0) {
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
            offset, max_entries,
            [expose_mem_cap](const VMA &) {
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

        auto *runtime_tcb = schd::Scheduler::inst().current_tcb();
        tcb->basic_entity.state = ThreadState::DYING;
        tcb->basic_entity
            .template flags_set<schd::SchedMeta::FLAGS_NEED_RESCHED>();
        if (runtime_tcb == tcb) {
            schd::Scheduler::inst().schedule();
        }
        void_return();
    }
}  // namespace cap
