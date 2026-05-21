/**
 * @file task.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 任务相关系统调用
 * @version alpha-1.0.0
 * @date 2026-05-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <cap/cholder.h>
#include <env.h>
#include <logger.h>
#include <mem/gfp.h>
#include <mem/vma.h>
#include <object/memory.h>
#include <object/perm.h>
#include <object/task.h>
#include <schd/schdbase.h>
#include <sus/nonnull.h>
#include <sus/raii.h>
#include <sustcore/capability.h>
#include <syscall/task.h>
#include <syscall/uaccess.h>
#include <task/scheduler.h>
#include <task/task.h>

#include <cassert>
#include <cstring>

namespace syscall {
    /**
     * @brief 查找当前进程 capability 空间中的 PCB payload.
     */
    static Result<cap::PCBPayload *> lookup_pcb(CapIdx idx,
                                                cap::Capability **out_cap) {
        auto cap_res = cap::CHolder::lookup(idx);
        propagate(cap_res);
        if (out_cap != nullptr) {
            *out_cap = cap_res.value();
        }
        auto *pcb = cap_res.value()->payload_as<cap::PCBPayload>();
        if (pcb == nullptr || pcb->pcb == nullptr) {
            unexpect_return(ErrCode::TYPE_NOT_MATCHED);
        }
        return pcb;
    }

    /**
     * @brief 查找当前进程 capability 空间中的 Memory payload.
     */
    static Result<cap::MemoryPayload *> lookup_memory(
        CapIdx idx, cap::Capability **out_cap) {
        auto cap_res = cap::CHolder::lookup(idx);
        propagate(cap_res);
        if (out_cap != nullptr) {
            *out_cap = cap_res.value();
        }
        auto *memory = cap_res.value()->payload_as<cap::MemoryPayload>();
        if (memory == nullptr) {
            unexpect_return(ErrCode::TYPE_NOT_MATCHED);
        }
        return memory;
    }

    /**
     * @brief 将父进程指定 capability 按相同 CapIdx 复制到子 CHolder.
     *
     * 该函数只处理 capability transfer 的 CLONE 语义; 对象自身权限检查
     * 仍由对应 CapObj 方法完成.
     */
    static Result<void> copy_initial_caps_in_place(cap::CHolder *src_holder,
                                                   TaskMemoryManager *src_tmm,
                                                   cap::CHolder *dst_holder,
                                                   VirAddr caps_uaddr,
                                                   size_t caps_sz) {
        if (src_holder == nullptr || dst_holder == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        if (caps_sz == 0) {
            void_return();
        }

        UBuffer caps_buf(caps_uaddr, caps_sz * sizeof(CapIdx));
        caps_buf.sync_from_user();
        auto *caps = reinterpret_cast<CapIdx *>(caps_buf.kbuf());
        for (size_t i = 0; i < caps_sz; ++i) {
            CapIdx idx   = caps[i];
            auto src_res = src_holder->internal_lookup(idx);
            propagate(src_res);
            cap::Capability *src_cap = src_res.value();
            if (!src_cap->imply(perm::basic::CLONE)) {
                unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
            }

            cap::Payload *src_payload = src_cap->payload();
            cap::Payload *payload     = src_payload->clone_payload();
            auto insert_res =
                dst_holder->internal_insert(idx, payload, src_cap->perm());
            if (!insert_res.has_value()) {
                if (payload != src_payload) {
                    payload->destruct();
                }
                propagate_return(insert_res);
            }

            auto *memory = src_cap->payload_as<cap::MemoryPayload>();
            if (memory != nullptr && !memory->shared && src_tmm != nullptr) {
                auto cow_res = src_tmm->protect_memory_cow(memory);
                if (!cow_res.has_value()) {
                    auto remove_res = dst_holder->internal_remove(idx);
                    assert(remove_res.has_value());
                    propagate_return(cow_res);
                }
            }
        }
        void_return();
    }

    static Result<schd::ClassType> parse_user_sched_class(size_t value) {
        switch (static_cast<schd::ClassType>(value)) {
            case schd::ClassType::RT:
            case schd::ClassType::RR:
            case schd::ClassType::FCFS:
                return static_cast<schd::ClassType>(value);
            case schd::ClassType::INIT:
            case schd::ClassType::IDLE:
            case schd::ClassType::BOT:
                unexpect_return(ErrCode::INVALID_PARAM);
            default: unexpect_return(ErrCode::INVALID_PARAM);
        }
    }

    Result<CapIdx> pcb_create_process(CapIdx pcb_cap, const UString &path,
                                      VirAddr caps_uaddr, size_t caps_sz,
                                      size_t sched_class) {
        loggers::SYSCALL::DEBUG(
            "创建进程: pcb=%p path=%s, caps_uaddr=%p, caps_sz=%u, "
            "sched_class=%u",
            pcb_cap, path.kbuf(), caps_uaddr.addr(), caps_sz, sched_class);

        cap::Capability *pcb_cap_obj = nullptr;
        auto pcb_res                 = lookup_pcb(pcb_cap, &pcb_cap_obj);
        propagate(pcb_res);
        cap::PCBObject pcb_obj(util::nnullforce(pcb_cap_obj));
        auto parent_res = pcb_obj.require_new_process_execute();
        propagate(parent_res);
        task::PCB *parent_pcb = parent_res.value();
        if (parent_pcb->cholder == nullptr || parent_pcb->tmm == nullptr) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        auto sched_res = parse_user_sched_class(sched_class);
        propagate(sched_res);

        // 1) 获取当前 CSpace 作为能力来源
        auto current_holder_res = cap::CHolder::current();
        propagate(current_holder_res);
        cap::CHolder *current_holder = current_holder_res.value();

        auto child_holder_res = cap::CHolderManager::inst().create_holder();
        propagate(child_holder_res);
        cap::CHolder *child_holder = child_holder_res.value();
        auto holder_guard          = util::Guard([child_holder]() {
            auto rm_res =
                cap::CHolderManager::inst().remove_holder(child_holder->id());
            assert(rm_res.has_value());
        });

        auto copy_res = copy_initial_caps_in_place(
            parent_pcb->cholder, parent_pcb->tmm.get(), child_holder,
            caps_uaddr, caps_sz);
        propagate(copy_res);

        // 2) 使用已预配置的 CHolder 加载子进程 ELF
        auto load_res = task::TaskManager::inst().load_elf_into(
            path.kbuf(), child_holder, sched_res.value());
        propagate(load_res);
        holder_guard.release();
        auto pcb_guard = util::Guard([&]() {
            if (load_res.has_value()) {
                auto pcb = load_res.value();
                loggers::SYSCALL::INFO("清理已加载的PCB对象: pid=%d", pcb->pid);
                // TODO: 调用 task manager 的接口来删除 PCB 对象
                // task::TaskManager::inst().remove_pcb(pcb->pid);
            }
        });

        auto pcb               = load_res.value();
        auto child_pcb_cap_res = pcb->cholder->internal_lookup(pcb->pcb_cap);
        propagate(child_pcb_cap_res);

        // 4) 返回子进程 PCB 能力给调用方
        auto ret_insert_res = current_holder->internal_insert_to_free(
            child_pcb_cap_res.value()->payload(),
            child_pcb_cap_res.value()->perm());
        propagate(ret_insert_res);

        loggers::SYSCALL::DEBUG("创建进程成功: path=%s, pid=%d", path.kbuf(),
                               pcb->pid);
        pcb_guard.release();
        return ret_insert_res.value();
    }

    Result<CapIdx> pcb_create_thread(CapIdx pcb_cap, VirAddr entry,
                                     VirAddr stack_addr, size_t stack_size) {
        cap::Capability *cap = nullptr;
        auto pcb_res         = lookup_pcb(pcb_cap, &cap);
        propagate(pcb_res);
        cap::PCBObject obj(util::nnullforce(cap));
        auto target_res = obj.require_new_thread();
        propagate(target_res);
        auto *current_tcb = schd::Scheduler::inst().current_tcb();
        if (current_tcb == nullptr || current_tcb->task != target_res.value()) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        auto thread_res = task::TaskManager::inst().create_thread_current(
            entry, stack_addr, stack_size);
        propagate(thread_res);
        return thread_res.value();
    }

    Result<size_t> pcb_fork(CapIdx pcb_cap, VirAddr child_pcb_cap_uaddr) {
        if (!child_pcb_cap_uaddr.nonnull()) {
            unexpect_return(ErrCode::NULLPTR);
        }

        cap::Capability *cap = nullptr;
        auto pcb_res         = lookup_pcb(pcb_cap, &cap);
        propagate(pcb_res);
        cap::PCBObject obj(util::nnullforce(cap));
        auto target_res = obj.require_new_process();
        propagate(target_res);
        auto *current_tcb = schd::Scheduler::inst().current_tcb();
        if (current_tcb == nullptr || current_tcb->task != target_res.value()) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        auto ret_slot_res = cap::CHolder::get_free_slot();
        propagate(ret_slot_res);
        CapIdx child_pcb_cap = ret_slot_res.value();

        UBuffer child_cap_buf(child_pcb_cap_uaddr, sizeof(CapIdx));
        child_cap_buf.sync_from_user();
        CapIdx old_child_pcb_cap = cap::null;
        memcpy(&old_child_pcb_cap, child_cap_buf.kbuf(),
               sizeof(old_child_pcb_cap));
        memcpy(child_cap_buf.kbuf(), &child_pcb_cap, sizeof(child_pcb_cap));
        child_cap_buf.sync_to_user();
        auto restore_out_guard = util::Guard([&]() {
            memcpy(child_cap_buf.kbuf(), &old_child_pcb_cap,
                   sizeof(old_child_pcb_cap));
            child_cap_buf.sync_to_user();
        });

        auto fork_res = task::TaskManager::inst().fork_current(child_pcb_cap);
        propagate(fork_res);

        restore_out_guard.release();
        return fork_res.value().child_pid;
    }

    Result<bool> pcb_kill(CapIdx pcb_cap, int exit_code) {
        cap::Capability *cap = nullptr;
        auto pcb_res         = lookup_pcb(pcb_cap, &cap);
        propagate(pcb_res);
        cap::PCBObject obj(util::nnullforce(cap));
        auto kill_res = obj.kill(exit_code);
        propagate(kill_res);
        return true;
    }

    Result<bool> pcb_map(CapIdx pcb_cap, CapIdx mem_cap, VirAddr vaddr,
                         PageMan::RWX rwx, cap::MemoryGrowth growth) {
        cap::Capability *pcb_cap_obj = nullptr;
        auto pcb_res                 = lookup_pcb(pcb_cap, &pcb_cap_obj);
        propagate(pcb_res);

        cap::Capability *mem_cap_obj = nullptr;
        auto memory_res              = lookup_memory(mem_cap, &mem_cap_obj);
        propagate(memory_res);
        cap::PCBObject pcb_obj(util::nnullforce(pcb_cap_obj));
        cap::MemoryObject mem_obj(util::nnullforce(mem_cap_obj));
        auto map_res = pcb_obj.map(mem_obj, vaddr, rwx, growth);
        propagate(map_res);
        return true;
    }

    Result<bool> pcb_execve(CapIdx pcb_cap, const UString &path,
                            VirAddr reserved_uaddr, size_t reserved_sz) {
        cap::Capability *cap = nullptr;
        auto pcb_res         = lookup_pcb(pcb_cap, &cap);
        propagate(pcb_res);
        cap::PCBObject obj(util::nnullforce(cap));
        auto target_res = obj.require_execute();
        propagate(target_res);
        UBuffer reserved_buf(reserved_uaddr, reserved_sz * sizeof(CapIdx));
        CapIdx *reserved = nullptr;
        if (reserved_sz != 0) {
            reserved_buf.sync_from_user();
            reserved = reinterpret_cast<CapIdx *>(reserved_buf.kbuf());
        }

        auto exec_res = task::TaskManager::inst().exec_pcb(
            util::nnullforce(target_res.value()), path.kbuf(), reserved,
            reserved_sz);
        propagate(exec_res);
        return true;
    }

    bool pcb_is_current(CapIdx pcb_cap) {
        auto pcb_res = lookup_pcb(pcb_cap, nullptr);
        if (!pcb_res.has_value()) {
            return false;
        }
        auto *current_tcb = schd::Scheduler::inst().current_tcb();
        return current_tcb != nullptr &&
               current_tcb->task == pcb_res.value()->pcb;
    }

    Result<size_t> get_pid(CapIdx pcb_cap) {
        auto cap_res = cap::CHolder::lookup(pcb_cap);
        propagate(cap_res);
        if (cap_res.value()->payload()->type_id() != PayloadType::PCB) {
            unexpect_return(ErrCode::TYPE_NOT_MATCHED);
        }

        auto pid_res =
            cap::PCBObject(util::nnullforce(cap_res.value())).get_pid();
        propagate(pid_res);
        return pid_res.value();
    }
}  // namespace syscall
