/**
 * @file metahelper.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 使用静态反射帮助生成胶水代码
 * @version alpha-1.0.0
 * @date 2026-05-21
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <rpc/session.h>
#include <rpc/typeparse.h>

#include <concepts>
#include <meta>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace rpc {
    template <typename T>
    concept call_helper_argument =
        !std::same_as<std::remove_cvref_t<T>, void> && std::is_pod_v<std::remove_cvref_t<T>>;

    template <std::meta::info Method, typename FuncType>
    struct call_helper_traits;

    template <typename FuncType>
    struct call_helper_return;

    template <std::meta::info Method, typename FuncType>
    struct meta_client_call_traits;

    template <std::meta::info Method, typename R, typename... Args>
    struct call_helper_base {
        using args_type = std::tuple<std::remove_cvref_t<Args>...>;
        using index_seq = typename std::make_index_sequence<sizeof...(Args)>::type;

        static_assert((call_helper_argument<Args> && ...),
                      "rpc::call_helper only supports POD parameters");

        template <size_t I, typename Arg>
        static Result<void> read_one(const ByteBuffer &argbuf, args_type &args) {
            // ByteBuffer stores raw bytes, so arguments are read as their
            // cv/ref-stripped storage type and later passed to the function.
            using StoredArg = std::remove_cvref_t<Arg>;
            auto read_res   = argbuf.template read<StoredArg>();
            if (!read_res.has_value()) {
                propagate_return(read_res);
            }
            std::get<I>(args) = read_res.value();
            void_return();
        }

        template <size_t... I>
        static Result<void> read_args(const ByteBuffer &argbuf, args_type &args,
                                      std::index_sequence<I...>) {
            Result<void> res{};

            // Expand one read per parameter. The fold stops evaluating after
            // the first failed read because && short-circuits.
            bool ok = ((res = read_one<I, Args>(argbuf, args), res.has_value()) && ...);
            if (!ok) {
                propagate_return(res);
            }
            void_return();
        }

        template <typename This, size_t... I>
        static Result<R> invoke(This *self, args_type &args, std::index_sequence<I...>) {
            if constexpr (std::same_as<R, void>) {
                // [:Method:] splices the reflected member function back into
                // an expression that can be called on the service instance.
                self->[:Method:](std::get<I>(args)...);
                void_return();
            } else {
                // std::get<I>(args)... expands the tuple back into the
                // original member-function argument list.
                return self->[:Method:](std::get<I>(args)...);
            }
        }

        template <typename This>
        static Result<R> call(This *self, const ByteBuffer &argbuf) {
            args_type args{};
            auto read_res = read_args(argbuf, args, index_seq{});
            propagate(read_res);
            return invoke(self, args, index_seq{});
        }
    };

    template <std::meta::info Method, typename R, typename... Args>
    struct call_helper_traits<Method, R(Args...)> : call_helper_base<Method, R, Args...> {};

    template <std::meta::info Method, typename R, typename... Args>
    struct call_helper_traits<Method, R(Args...) const> : call_helper_base<Method, R, Args...> {};

    template <typename R, typename... Args>
    struct call_helper_return<R(Args...)> {
        using type = R;
    };

    template <typename R, typename... Args>
    struct call_helper_return<R(Args...) const> {
        using type = R;
    };

    template <std::meta::info Method, typename This>
    auto call_helper(This *self, const ByteBuffer &argbuf) {
        // The reflected function entity is converted to its function type,
        // which is then decomposed by call_helper_traits.
        using FuncType = typename[:std::meta::type_of(Method):];
        return call_helper_traits<Method, FuncType>::call(self, argbuf);
    }

    struct service_name_t {};
    inline constexpr service_name_t service_name{};

    struct service_magic_t {};
    inline constexpr service_magic_t service_magic{};

    struct expose {
        sus_u32 function_id;
        constexpr expose(sus_u32 function_id) : function_id(function_id) {}
    };

    template <typename Interface>
    consteval const char *meta_service_name() {
        const char *name = nullptr;

        template for (constexpr auto member : std::meta::static_data_members_of(
                          ^^Interface, std::meta::access_context::unchecked())) {
            if constexpr (util::is_annotated_entity_by<member, service_name_t>()) {
                name = [:member:];
            }
        }

        return name;
    }

    template <typename Interface>
    consteval sus_u32 meta_service_magic() {
        sus_u32 magic = 0;

        template for (constexpr auto member : std::meta::static_data_members_of(
                          ^^Interface, std::meta::access_context::unchecked())) {
            if constexpr (util::is_annotated_entity_by<member, service_magic_t>()) {
                magic = [:member:];
            }
        }

        return magic;
    }

    template <std::meta::info Method>
    consteval bool meta_method_exposed() {
        return util::is_annotated_entity_by<Method, expose>();
    }

    template <std::meta::info Method>
    consteval sus_u32 meta_method_id() {
        return util::get_entity_annotation<Method, expose>().function_id;
    }

    template <typename Interface, typename Impl>
    class MetaServer : public Server {
        static_assert(meta_service_name<Interface>() != nullptr,
                      "rpc::MetaServer requires a [[=rpc::service_name]] static member");

        class MetaServerSession : public ServerSession {
            MetaServer &_meta_server;

            template <std::meta::info Method>
            Result<void> handle_exposed_call(const CallPacket &msg, CallResult &result) {
                using FuncType   = typename[:std::meta::type_of(Method):];
                using ReturnType = typename call_helper_return<FuncType>::type;

                if (!do_arguments_match<FuncType>(msg.types)) {
                    result.is_error = true;
                    void_return();
                }

                auto *impl    = _meta_server.impl();
                auto call_res = call_helper<Method>(impl, msg.argbuf);
                if (!call_res.has_value()) {
                    result.is_error = true;
                    void_return();
                }

                result.return_type = get_type_id<ReturnType>();
                if constexpr (std::same_as<ReturnType, void>) {
                    result.retbuf = ByteBuffer(0);
                } else {
                    result.retbuf  = ByteBuffer(sizeof(ReturnType));
                    auto write_res = result.retbuf.write(call_res.value());
                    if (!write_res.has_value()) {
                        result.is_error = true;
                    }
                }
                void_return();
            }

            Result<void> handle_call(const CallPacket &msg, CallResult &result) override {
                template for (constexpr auto method : std::meta::members_of(
                                  ^^Interface, std::meta::access_context::unchecked())) {
                    if constexpr (meta_method_exposed<method>()) {
                        if (msg.function_id == meta_method_id<method>()) {
                            return handle_exposed_call<method>(msg, result);
                        }
                    }
                }

                result.is_error = true;
                void_return();
            }

        public:
            MetaServerSession(MetaServer &server, sus_u32 session_number)
                : ServerSession(server, session_number), _meta_server(server) {}
        };

        Impl *impl() {
            return static_cast<Impl *>(this);
        }

    public:
        explicit MetaServer(CapIdx server_endpoint)
            : Server(server_endpoint, std::string_view(meta_service_name<Interface>()),
                     meta_service_magic<Interface>()) {}

        ServerSession *create_session(sus_u32 session_number) override {
            return new MetaServerSession(*this, session_number);
        }
    };

    template <std::meta::info Method, typename R, typename... Args>
    struct meta_client_call_base {
        static_assert(meta_method_exposed<Method>(),
                      "rpc::MetaClient::call requires a [[=rpc::expose(id)]] method");
        static_assert((call_helper_argument<Args> && ...),
                      "rpc::MetaClient::call only supports POD parameters");

        template <typename FormalArg, typename CallArg>
        static Result<void> write_one(ByteBuffer &argbuf, CallArg &&arg) {
            using StoredArg = std::remove_cvref_t<FormalArg>;
            StoredArg stored = static_cast<StoredArg>(arg);
            auto write_res = argbuf.write(stored);
            propagate(write_res);
            void_return();
        }

        static std::vector<sus_u32> make_type_list() {
            std::vector<sus_u32> types;
            (types.push_back(get_type_id<std::remove_cvref_t<Args>>()), ...);
            return types;
        }

        template <typename... CallArgs>
        static Result<ByteBuffer> make_arg_buffer(CallArgs &&...args) {
            static_assert(sizeof...(CallArgs) == sizeof...(Args),
                          "rpc::MetaClient::call argument count mismatch");
            ByteBuffer argbuf((sizeof(std::remove_cvref_t<Args>) + ... + 0));

            // Write each runtime argument in the reflected parameter order.
            Result<void> res{};
            bool ok = ((res = write_one<Args>(argbuf, std::forward<CallArgs>(args)),
                        res.has_value()) &&
                       ...);
            if (!ok) {
                propagate_return(res);
            }
            return argbuf;
        }

        template <typename ClientType, typename... CallArgs>
        static Result<R> call(ClientType &client, CallArgs &&...args) {
            auto session_res = client.start();
            propagate(session_res);
            auto &session = session_res.value().get();

            auto argbuf_res = make_arg_buffer(std::forward<CallArgs>(args)...);
            propagate(argbuf_res);
            auto argbuf = argbuf_res.value();

            auto response_res =
                session.call(meta_method_id<Method>(), make_type_list(), argbuf,
                             get_type_id<R>());
            propagate(response_res);

            if constexpr (std::same_as<R, void>) {
                void_return();
            } else {
                return response_res.value().retbuf.template read<R>();
            }
        }
    };

    template <std::meta::info Method, typename R, typename... Args>
    struct meta_client_call_traits<Method, R(Args...)>
        : meta_client_call_base<Method, R, Args...> {};

    template <std::meta::info Method, typename R, typename... Args>
    struct meta_client_call_traits<Method, R(Args...) const>
        : meta_client_call_base<Method, R, Args...> {};

    template <typename Interface>
    class MetaClient : public Client {
        static_assert(meta_service_name<Interface>() != nullptr,
                      "rpc::MetaClient requires a [[=rpc::service_name]] static member");

    public:
        explicit MetaClient(CapIdx server_endpoint)
            : Client(server_endpoint, std::string_view(meta_service_name<Interface>()),
                     meta_service_magic<Interface>()) {}

        template <std::meta::info Method, typename... Args>
        auto call(Args &&...args) {
            using FuncType = typename[:std::meta::type_of(Method):];
            return meta_client_call_traits<Method, FuncType>::call(
                *this, std::forward<Args>(args)...);
        }
    };
}  // namespace rpc
