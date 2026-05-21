/**
 * @file packet.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief RPC 消息包
 * @version alpha-1.0.0
 * @date 2026-05-18
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <rpc/packet.h>
#include <rpc/typeparse.h>

namespace rpc {
    namespace {
        Result<MsgPacket> finish_packet(ByteBuffer &data) {
            if (data.size() > MAX_MSG_SIZE) {
                unexpect_return(ErrCode::OUT_OF_BOUNDARY);
            }

            MsgPacket packet{};
            packet.msgsz = data.size();
            packet.capsz = 0;
            if (packet.msgsz != 0) {
                memcpy(packet.msgbuf, data.data(), packet.msgsz);
            }
            return packet;
        }

        Result<void> encode_types(ByteBuffer &buf,
                                  const std::vector<sus_u32> &types) {
            auto write_res = buf.write(static_cast<sus_u32>(types.size()));
            propagate(write_res);
            for (const auto &type : types) {
                write_res = buf.write(type);
                propagate(write_res);
            }
            void_return();
        }

        Result<void> encode_data(ByteBuffer &buf, const ByteBuffer &data) {
            auto write_res = buf.write(static_cast<sus_u32>(data.size()));
            propagate(write_res);
            write_res = buf.writes(data.data(), data.size());
            propagate(write_res);
            void_return();
        }

        Result<void> encode_header(ByteBuffer &buf, sus_u32 rpc_magic,
                                   sus_u32 service_magic,
                                   PacketType packet_type) {
            auto write_res = buf.write(rpc_magic);
            propagate(write_res);
            write_res = buf.write(service_magic);
            propagate(write_res);
            write_res = buf.write(packet_type);
            propagate(write_res);
            void_return();
        }

        Result<ByteBuffer> make_reader(const MsgPacket &msg) {
            if (msg.msgsz > MAX_MSG_SIZE || msg.capsz > MAX_MSG_CAPS) {
                unexpect_return(ErrCode::OUT_OF_BOUNDARY);
            }

            auto *data = new byte[msg.msgsz];
            if (data == nullptr) {
                unexpect_return(ErrCode::ALLOCATION_FAILED);
            }
            memcpy(data, msg.msgbuf, msg.msgsz);
            return ByteBuffer(data, msg.msgsz);
        }

        Result<sus_u32> decode_header(ByteBuffer &reader, sus_u32 expected_magic,
                                      PacketType expected_type) {
            auto read_res = reader.read<sus_u32>();
            propagate(read_res);
            auto rpc_magic = read_res.value();
            if (rpc_magic != expected_magic) {
                unexpect_return(ErrCode::TYPE_NOT_MATCHED);
            }

            read_res = reader.read<sus_u32>();
            propagate(read_res);
            auto service_magic = read_res.value();

            auto type_res = reader.read<PacketType>();
            propagate(type_res);
            if (type_res.value() != expected_type) {
                unexpect_return(ErrCode::TYPE_NOT_MATCHED);
            }
            return service_magic;
        }

        Result<std::vector<sus_u32>> decode_types(ByteBuffer &reader) {
            auto read_res = reader.read<sus_u32>();
            propagate(read_res);
            auto type_count = read_res.value();

            std::vector<sus_u32> types;
            types.reserve(type_count);
            for (size_t i = 0; i < type_count; i++) {
                auto type_res = reader.read<sus_u32>();
                propagate(type_res);
                types.push_back(type_res.value());
            }
            return types;
        }

        Result<ByteBuffer> decode_data(ByteBuffer &reader) {
            auto read_res = reader.read<sus_u32>();
            propagate(read_res);
            auto data_size = read_res.value();

            auto *data = new byte[data_size];
            if (data == nullptr) {
                unexpect_return(ErrCode::ALLOCATION_FAILED);
            }

            auto reads_res = reader.reads(data, data_size);
            if (!reads_res.has_value()) {
                delete[] data;
                propagate_return(reads_res);
            }
            propagate(reads_res);

            return ByteBuffer(data, data_size);
        }
    }  // namespace

    Result<MsgPacket> encode_session(const SessionPacket &packet) {
        ByteBuffer data(sizeof(RPC_REQUEST_MAGIC) + sizeof(packet) +
                        sizeof(PacketType));
        auto write_res = encode_header(data, RPC_REQUEST_MAGIC,
                                       packet.service_magic,
                                       PacketType::SESSION);
        propagate(write_res);
        return finish_packet(data);
    }

    Result<MsgPacket> encode_session_response(
        const SessionResponsePacket &packet) {
        ByteBuffer data(sizeof(RPC_RESPONSE_MAGIC) + sizeof(packet) +
                        sizeof(PacketType));
        auto write_res = encode_header(data, RPC_RESPONSE_MAGIC,
                                       packet.service_magic,
                                       PacketType::SESSION_RESPONSE);
        propagate(write_res);
        write_res = data.write(packet.session_id);
        propagate(write_res);
        return finish_packet(data);
    }

    Result<MsgPacket> encode_close(const ClosePacket &packet) {
        ByteBuffer data(sizeof(RPC_REQUEST_MAGIC) + sizeof(packet) +
                        sizeof(PacketType));
        auto write_res = encode_header(data, RPC_REQUEST_MAGIC,
                                       packet.service_magic,
                                       PacketType::CLOSE);
        propagate(write_res);
        write_res = data.write(packet.session_id);
        propagate(write_res);
        return finish_packet(data);
    }

    Result<MsgPacket> encode_close_response(const CloseResponsePacket &packet) {
        ByteBuffer data(sizeof(RPC_RESPONSE_MAGIC) + sizeof(packet) +
                        sizeof(PacketType));
        auto write_res = encode_header(data, RPC_RESPONSE_MAGIC,
                                       packet.service_magic,
                                       PacketType::CLOSE_RESPONSE);
        propagate(write_res);
        write_res = data.write(packet.session_id);
        propagate(write_res);
        return finish_packet(data);
    }

    Result<MsgPacket> encode_call(const CallPacket &packet) {
        size_t total_size =
            sizeof(RPC_REQUEST_MAGIC) + sizeof(packet.service_magic) +
            sizeof(packet.session_id) + sizeof(packet.function_id) +
            sizeof(PacketType) + sizeof(sus_u32) +
            sizeof(sus_u32) * packet.types.size() + sizeof(sus_u32) +
            packet.argbuf.size();
        ByteBuffer data(total_size);
        auto write_res = encode_header(data, RPC_REQUEST_MAGIC,
                                       packet.service_magic,
                                       PacketType::CALL);
        propagate(write_res);
        write_res = data.write(packet.session_id);
        propagate(write_res);
        write_res = data.write(packet.function_id);
        propagate(write_res);
        write_res = encode_types(data, packet.types);
        propagate(write_res);
        write_res = encode_data(data, packet.argbuf);
        propagate(write_res);
        return finish_packet(data);
    }

    Result<MsgPacket> encode_response(const ResponsePacket &packet) {
        size_t total_size = sizeof(RPC_RESPONSE_MAGIC) +
                            sizeof(packet.service_magic) +
                            sizeof(packet.session_id) +
                            sizeof(packet.function_id) + sizeof(PacketType) +
                            sizeof(packet.return_type) + sizeof(sus_u32) +
                            packet.retbuf.size();
        ByteBuffer data(total_size);
        auto write_res = encode_header(data, RPC_RESPONSE_MAGIC,
                                       packet.service_magic,
                                       PacketType::RESPONSE);
        propagate(write_res);
        write_res = data.write(packet.session_id);
        propagate(write_res);
        write_res = data.write(packet.function_id);
        propagate(write_res);
        write_res = data.write(packet.return_type);
        propagate(write_res);
        write_res = encode_data(data, packet.retbuf);
        propagate(write_res);
        return finish_packet(data);
    }

    Result<MsgPacket> encode_error(const ErrorPacket &packet) {
        size_t total_size = sizeof(RPC_RESPONSE_MAGIC) +
                            sizeof(packet.service_magic) +
                            sizeof(packet.session_id) +
                            sizeof(packet.function_id) + sizeof(PacketType) +
                            sizeof(packet.code);
        ByteBuffer data(total_size);
        auto write_res = encode_header(data, RPC_RESPONSE_MAGIC,
                                       packet.service_magic,
                                       PacketType::ERROR);
        propagate(write_res);
        write_res = data.write(packet.session_id);
        propagate(write_res);
        write_res = data.write(packet.function_id);
        propagate(write_res);
        write_res = data.write(static_cast<sus_u32>(packet.code));
        propagate(write_res);
        return finish_packet(data);
    }

    bool is_rpc_message(const MsgPacket &msg) {
        if (msg.msgsz < sizeof(sus_u32) * 2 + sizeof(PacketType)) {
            return false;
        }
        sus_u32 magic = 0;
        memcpy(&magic, msg.msgbuf, sizeof(magic));
        return magic == RPC_REQUEST_MAGIC || magic == RPC_RESPONSE_MAGIC;
    }

    PacketType peek_type(const MsgPacket &msg) {
        if (msg.msgsz < sizeof(sus_u32) * 2 + sizeof(PacketType)) {
            return static_cast<PacketType>(0);
        }
        PacketType type{};
        memcpy(&type, msg.msgbuf + sizeof(sus_u32) * 2, sizeof(type));
        return type;
    }

    Result<SessionPacket> decode_session(const MsgPacket &msg) {
        auto reader_res = make_reader(msg);
        propagate(reader_res);
        auto reader = reader_res.value();

        auto service_magic_res =
            decode_header(reader, RPC_REQUEST_MAGIC, PacketType::SESSION);
        propagate(service_magic_res);

        return SessionPacket{service_magic_res.value()};
    }

    Result<SessionResponsePacket> decode_session_response(
        const MsgPacket &msg) {
        auto reader_res = make_reader(msg);
        propagate(reader_res);
        auto reader = reader_res.value();

        auto service_magic_res =
            decode_header(reader, RPC_RESPONSE_MAGIC,
                          PacketType::SESSION_RESPONSE);
        propagate(service_magic_res);

        sus_u32 session_id;
        auto read_res = reader.read(session_id);
        propagate(read_res);

        return SessionResponsePacket{.service_magic = service_magic_res.value(),
                                     .session_id    = session_id};
    }

    Result<ClosePacket> decode_close(const MsgPacket &msg) {
        auto reader_res = make_reader(msg);
        propagate(reader_res);
        auto reader = reader_res.value();

        auto service_magic_res =
            decode_header(reader, RPC_REQUEST_MAGIC, PacketType::CLOSE);
        propagate(service_magic_res);

        sus_u32 session_id;
        auto read_res = reader.read(session_id);
        propagate(read_res);

        return ClosePacket{.service_magic = service_magic_res.value(),
                           .session_id    = session_id};
    }

    Result<CloseResponsePacket> decode_close_response(const MsgPacket &msg) {
        auto reader_res = make_reader(msg);
        propagate(reader_res);
        auto reader = reader_res.value();

        auto service_magic_res =
            decode_header(reader, RPC_RESPONSE_MAGIC,
                          PacketType::CLOSE_RESPONSE);
        propagate(service_magic_res);

        sus_u32 session_id;
        auto read_res = reader.read(session_id);
        propagate(read_res);

        return CloseResponsePacket{.service_magic = service_magic_res.value(),
                                   .session_id    = session_id};
    }

    Result<CallPacket> decode_call(const MsgPacket &msg) {
        auto reader_res = make_reader(msg);
        propagate(reader_res);
        auto reader = reader_res.value();

        auto service_magic_res =
            decode_header(reader, RPC_REQUEST_MAGIC, PacketType::CALL);
        propagate(service_magic_res);

        sus_u32 session_id;
        auto read_res = reader.read(session_id);
        propagate(read_res);

        sus_u32 function_id;
        read_res = reader.read(function_id);
        propagate(read_res);

        auto types_res = decode_types(reader);
        propagate(types_res);

        auto args_res = decode_data(reader);
        propagate(args_res);

        return CallPacket{
            .service_magic = service_magic_res.value(),
            .session_id    = session_id,
            .function_id   = function_id,
            .types         = types_res.value(),
            .argbuf        = args_res.value(),
        };
    }

    Result<ResponsePacket> decode_response(const MsgPacket &msg) {
        auto reader_res = make_reader(msg);
        propagate(reader_res);
        auto reader = reader_res.value();

        auto service_magic_res =
            decode_header(reader, RPC_RESPONSE_MAGIC, PacketType::RESPONSE);
        propagate(service_magic_res);

        auto read_res = reader.read<sus_u32>();
        propagate(read_res);
        sus_u32 session_id = read_res.value();

        read_res = reader.read<sus_u32>();
        propagate(read_res);
        sus_u32 function_id = read_res.value();

        read_res = reader.read<sus_u32>();
        propagate(read_res);
        sus_u32 return_type = read_res.value();

        auto value_res = decode_data(reader);
        propagate(value_res);

        return ResponsePacket{
            .service_magic = service_magic_res.value(),
            .session_id    = session_id,
            .function_id   = function_id,
            .return_type   = return_type,
            .retbuf        = value_res.value(),
        };
    }

    Result<ErrorPacket> decode_error(const MsgPacket &msg) {
        auto reader_res = make_reader(msg);
        propagate(reader_res);
        auto reader = reader_res.value();

        auto service_magic_res =
            decode_header(reader, RPC_RESPONSE_MAGIC, PacketType::ERROR);
        propagate(service_magic_res);

        sus_u32 session_id;
        auto read_res = reader.read(session_id);
        propagate(read_res);

        sus_u32 function_id;
        read_res = reader.read(function_id);
        propagate(read_res);

        RPCErrorCode code;
        read_res = reader.read(code);
        propagate(read_res);

        return ErrorPacket{
            service_magic_res.value(),
            session_id,
            function_id,
            code,
        };
    }
}  // namespace rpc
