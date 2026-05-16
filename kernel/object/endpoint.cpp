/**
 * @file endpoint.cpp
 * @brief Endpoint capability object
 */

#include <device/int.h>
#include <logger.h>
#include <object/endpoint.h>
#include <perm/perm.h>
#include <task/wait.h>

#include <cstring>

namespace cap {
    EndpointMessage::~EndpointMessage() {
        for (size_t i = 0; i < capsz; ++i) {
            delete caps[i];
            caps[i] = nullptr;
        }
        capsz = 0;
    }

    EndpointPayload::EndpointPayload()
        :
          send_wait_reason(task::wait::alloc_reason()),
          recv_wait_reason(task::wait::alloc_reason())
          {}

    EndpointPayload::~EndpointPayload() {
        while (!messages.empty()) {
            EndpointMessage *msg = &messages.front();
            messages.pop_front();
            delete msg;
        }
    }

    static Result<void> check_msg_bounds(size_t msgsz, size_t capsz) {
        if (msgsz > MAX_MSG_SIZE || capsz > MAX_MSG_CAPS) {
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }
        void_return();
    }

    Result<bool> EndpointObject::send(pid_t sender_pid, const char *msgbuf,
                                      size_t msgsz, Capability **caps,
                                      size_t capsz, bool blocking) {
        propagate(check_msg_bounds(msgsz, capsz));
        if (!imply(perm::endpoint::WRITE)) {
            loggers::CAPABILITY::ERROR("Endpoint WRITE权限不足");
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (capsz != 0 && !imply(perm::endpoint::GRANT)) {
            loggers::CAPABILITY::ERROR("Endpoint GRANT权限不足");
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }

        util::owner<EndpointMessage*> msg = util::owner(new EndpointMessage());
        auto msg_guard = util::Guard([&]() { delete msg; });

        if (msg == nullptr) {
            unexpect_return(ErrCode::OUT_OF_MEMORY);
        }

        msg->sender_pid = sender_pid;
        msg->msgsz      = msgsz;
        msg->capsz      = capsz;
        if (msgsz != 0) {
            memcpy(msg->msgbuf, msgbuf, msgsz);
        }
        for (size_t i = 0; i < capsz; ++i) {
            if (caps[i] == nullptr ||
                !caps[i]->imply(perm::basic::CLONE)) {
                unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
            }
            msg->caps[i] = caps[i]->clone();
            if (msg->caps[i] == nullptr) {
                unexpect_return(ErrCode::OUT_OF_MEMORY);
            }
        }

        InterruptGuard guard;
        guard.enter();

        if (task::wait::has_waiting(_obj->recv_wait_reason)) {
            _obj->messages.push_back(*msg);
            msg_guard.release();
            auto wake_res = task::wait::wake_one(_obj->recv_wait_reason);
            return wake_res.transform(always(true));
        }

        // no waiting receiver, decide whether to block or not
        if (!blocking) {
            return false;
        }

        _obj->messages.push_back(*msg);
        auto wait_res = task::wait::wait_current(_obj->send_wait_reason);
        if (!wait_res.has_value()) {
            _obj->messages.remove(*msg);
            propagate_return(wait_res);
        }
        msg_guard.release();
        return true;
    }

    Result<EndpointMessage *> EndpointObject::recv_async() {
        if (!imply(perm::endpoint::READ)) {
            loggers::CAPABILITY::ERROR("Endpoint READ权限不足");
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }

        InterruptGuard guard;
        guard.enter();

        if (_obj->messages.empty()) {
            return static_cast<EndpointMessage *>(nullptr);
        }

        EndpointMessage *msg = &_obj->messages.front();
        _obj->messages.pop_front();
        auto wake_res = task::wait::wake_one(_obj->send_wait_reason);
        propagate(wake_res);
        return msg;
    }

    Result<bool> EndpointObject::recv_sync(task::wait::WakePostAction action) {
        if (!imply(perm::endpoint::READ)) {
            loggers::CAPABILITY::ERROR("Endpoint READ权限不足");
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (!action) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        InterruptGuard guard;
        guard.enter();

        if (!_obj->messages.empty()) {
            return action(nullptr);
        }

        loggers::CAPABILITY::INFO("Endpoint没有消息可接收, 线程进入等待");

        // TODO: wait_current() 的语义是将线程调度状态改为阻塞态
        // 但是我们希望在线程调度状态回到就绪态时进行后半部分的处理(即从消息队列中取出消息并返回)
        // 因此引入一个编译期协程框架可能会对其实现有所裨益
        // 目前先通过向 wait_current() 传入一个回调函数来实现这个功能
        auto wait_res = task::wait::wait_current(_obj->recv_wait_reason,
                                                 std::move(action));
        return wait_res.transform(always(true));
    }
}  // namespace cap
