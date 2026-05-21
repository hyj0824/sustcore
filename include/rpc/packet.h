/**
 * @file packet.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief RPC 消息包
 * @version alpha-1.0.0
 * @date 2026-05-18
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <rpc/buffer.h>
#include <sus/types.h>
#include <sustcore/capability.h>
#include <sustcore/errcode.h>
#include <sustcore/msg.h>

#include <cstddef>
#include <cstring>
#include <vector>

namespace rpc {
    inline constexpr sus_u32 RPC_REQUEST_MAGIC =
        (static_cast<sus_u32>('R') << 0) | (static_cast<sus_u32>('P') << 8) |
        (static_cast<sus_u32>('C') << 16) | (static_cast<sus_u32>('C') << 24);

    inline constexpr sus_u32 RPC_RESPONSE_MAGIC =
        (static_cast<sus_u32>('R') << 0) | (static_cast<sus_u32>('P') << 8) |
        (static_cast<sus_u32>('C') << 16) | (static_cast<sus_u32>('R') << 24);

    enum class PacketType : sus_u32 {
        CALL             = 1,
        RESPONSE         = 2,
        SESSION          = 3,
        SESSION_RESPONSE = 4,
        CLOSE            = 5,
        CLOSE_RESPONSE   = 6,
        ERROR            = 7,
    };

    enum class PrimitiveTypeId : sus_u32 {
        u8        = 0,
        u16       = 1,
        u32       = 2,
        u64       = 3,
        i8        = 4,
        i16       = 5,
        i32       = 6,
        i64       = 7,
        f32       = 8,
        f64       = 9,
        size      = 10,
        boolean   = 11,
        void_type = 12
    };

    inline constexpr sus_u32 prim_typeid(PrimitiveTypeId t) {
        return static_cast<sus_u32>(t);
    }

    enum class RPCErrorCode : sus_u32 {
        SUCCESS       = 0,
        UNKNOWN_ERROR = 1,
        INVALID_MAGIC = 2
    };

    struct SessionPacket {
        sus_u32 service_magic{};
    };

    struct SessionResponsePacket {
        sus_u32 service_magic{};
        sus_u32 session_id{};
    };

    struct ClosePacket {
        sus_u32 service_magic{};
        sus_u32 session_id{};
    };

    struct CloseResponsePacket {
        sus_u32 service_magic{};
        sus_u32 session_id{};
    };

    struct ErrorPacket {
        sus_u32 service_magic{};
        sus_u32 session_id{};
        sus_u32 function_id{};
        RPCErrorCode code{};
    };

    struct CallPacket {
        sus_u32 service_magic{};
        sus_u32 session_id{};
        sus_u32 function_id{};
        std::vector<sus_u32> types;
        ByteBuffer argbuf;
    };

    struct ResponsePacket {
        sus_u32 service_magic{};
        sus_u32 session_id{};
        sus_u32 function_id{};
        sus_u32 return_type{};
        ByteBuffer retbuf;
    };

    Result<MsgPacket> encode_session(const SessionPacket &packet);
    Result<MsgPacket> encode_session_response(
        const SessionResponsePacket &packet);
    Result<MsgPacket> encode_close(const ClosePacket &packet);
    Result<MsgPacket> encode_close_response(const CloseResponsePacket &packet);
    Result<MsgPacket> encode_call(const CallPacket &packet);
    Result<MsgPacket> encode_response(const ResponsePacket &packet);
    Result<MsgPacket> encode_error(const ErrorPacket &packet);

    bool is_rpc_message(const MsgPacket &msg);
    PacketType peek_type(const MsgPacket &msg);
    Result<SessionPacket> decode_session(const MsgPacket &msg);
    Result<SessionResponsePacket> decode_session_response(const MsgPacket &msg);
    Result<ClosePacket> decode_close(const MsgPacket &msg);
    Result<CloseResponsePacket> decode_close_response(const MsgPacket &msg);
    Result<CallPacket> decode_call(const MsgPacket &msg);
    Result<ResponsePacket> decode_response(const MsgPacket &msg);
    Result<ErrorPacket> decode_error(const MsgPacket &msg);
}  // namespace rpc
