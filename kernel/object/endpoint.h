/**
 * @file endpoint.h
 * @brief Endpoint capability object
 */

#pragma once

#include <cap/capability.h>
#include <perm/endpoint.h>
#include <sus/list.h>
#include <sustcore/capability.h>
#include <task/task_struct.h>

#include <cstddef>

namespace cap {
    struct EndpointMessage {
        pid_t sender_pid = 0;
        char msgbuf[MAX_MSG_SIZE]{};
        size_t msgsz = 0;
        Capability *caps[MAX_MSG_CAPS]{};
        size_t capsz = 0;
        util::ListHead<EndpointMessage> list_head{};

        EndpointMessage() = default;
        ~EndpointMessage();
    };

    struct EndpointPayload : public _PayloadHelper<PayloadType::ENDPOINT> {
        util::IntrusiveList<EndpointMessage, &EndpointMessage::list_head> messages = {};
        WaitReasonId send_wait_reason;
        WaitReasonId recv_wait_reason;

        EndpointPayload();
        ~EndpointPayload() override;
    };

    class EndpointObject : public CapObj<EndpointPayload> {
    public:
        explicit EndpointObject(util::nonnull<Capability *> cap)
            : CapObj<EndpointPayload>(cap) {}

        Result<bool> send(pid_t sender_pid, const char *msgbuf, size_t msgsz,
                          Capability **caps, size_t capsz, bool blocking);
        Result<EndpointMessage *> recv_async();
        Result<bool> recv_sync(task::wait::WakePostAction action);
    };
}  // namespace cap
