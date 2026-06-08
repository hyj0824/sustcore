/**
 * @file wait.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief thread waiting reasons
 * @version alpha-1.0.0
 * @date 2026-05-14
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <sus/coroutine.h>
#include <sus/list.h>
#include <sus/map.h>
#include <sustcore/errcode.h>
#include <task/task_struct.h>

#include <atomic>
#include <concepts>
#include <coroutine>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace task::wait {
    class FutureAwaiter;

    template <typename T>
    class cotask;

    template <typename T>
    class Future;

    template <typename T>
    class Promise;

    struct promise_base;

    size_t alloc_reason();

    enum class FutureState {
        PENDING,
        COMPLETE,
        ERROR,
        CANCLED,
        CONSUMED,
    };

    struct WaitContext {
        FutureState state                      = FutureState::COMPLETE;
        size_t wait_reason               = 0;
        WaitPredicate wait_predicate           = {};
        WaitReadyPredicate ready_predicate     = {};
        std::coroutine_handle<> suspended_leaf = nullptr;

        [[nodiscard]]
        bool pending() const noexcept {
            return state == FutureState::PENDING && wait_reason != 0;
        }

        void clear() noexcept {
            state           = FutureState::COMPLETE;
            wait_reason     = 0;
            wait_predicate  = {};
            ready_predicate = {};
            suspended_leaf  = nullptr;
        }

        static const WaitContext EMPTY;
    };

    struct promise_base {
        bool detached                        = false;
        bool destroy_on_final_suspend        = false;
        std::coroutine_handle<> continuation = nullptr;
        promise_base *wait_parent            = nullptr;
        WaitContext wait_context_storage     = {};

        [[nodiscard]]
        WaitContext &wait_context() noexcept {
            return wait_context_storage;
        }

        [[nodiscard]]
        const WaitContext &wait_context() const noexcept {
            return wait_context_storage;
        }

        void propagate_wait_context_chain() noexcept {
            auto *cursor = wait_parent;
            while (cursor != nullptr) {
                cursor->wait_context_storage = wait_context_storage;
                cursor                       = cursor->wait_parent;
            }
        }

        void clear_wait_context_chain() noexcept {
            wait_context_storage.clear();
            auto *cursor = wait_parent;
            while (cursor != nullptr) {
                cursor->wait_context_storage.clear();
                cursor = cursor->wait_parent;
            }
        }

        auto await_transform(FutureAwaiter awaiter) noexcept;

        template <typename T>
        auto await_transform(cotask<T> &task) noexcept {
            return task.operator co_await();
        }

        template <typename T>
        auto await_transform(const cotask<T> &task) noexcept {
            return task.operator co_await();
        }

        template <typename T>
        auto await_transform(cotask<T> &&task) noexcept {
            return std::move(task).operator co_await();
        }

        template <typename T>
        auto await_transform(Future<T> &future) noexcept {
            return future.operator co_await();
        }

        template <typename T>
        auto await_transform(const Future<T> &future) = delete;

        template <typename T>
        auto await_transform(Future<T> &&future) noexcept {
            return std::move(future).operator co_await();
        }

        template <typename Awaitable>
            requires(!std::same_as<std::remove_cvref_t<Awaitable>,
                                   FutureAwaiter>)
        auto await_transform(Awaitable &&awaitable);
    };

    namespace detail {
        template <typename T>
        struct is_result_type : std::false_type {};

        template <typename T>
        struct is_result_type<std::expected<T, ErrCode>> : std::true_type {};

        template <typename T>
        inline constexpr bool is_result_type_v = is_result_type<T>::value;

        template <typename T>
        struct future_wait_result {
            using type = Result<T>;
        };

        template <typename T>
        struct future_wait_result<std::expected<T, ErrCode>> {
            using type = Result<T>;
        };

        template <typename T>
        using future_wait_result_t = typename future_wait_result<T>::type;

        template <typename T>
        struct future_value_storage {
            using type = Optional<T>;
        };

        template <>
        struct future_value_storage<void> {
            struct type {};
        };

        template <typename T>
        using future_value_storage_t = typename future_value_storage<T>::type;

        template <typename Promise>
        concept HasWaitContextAccessor = requires(Promise &promise) {
            {
                promise.wait_context()
            } -> std::same_as<WaitContext &>;
        };

        template <typename T>
        struct is_wait_cotask : std::false_type {};

        template <typename T>
        struct is_wait_cotask<task::wait::cotask<T>> : std::true_type {};

        template <typename T>
        struct is_wait_future : std::false_type {};

        template <typename T>
        struct is_wait_future<task::wait::Future<T>> : std::true_type {};

        template <typename Awaitable>
        inline constexpr bool is_wait_cotask_v =
            is_wait_cotask<std::remove_cvref_t<Awaitable>>::value;

        template <typename Awaitable>
        inline constexpr bool is_wait_future_v =
            is_wait_future<std::remove_cvref_t<Awaitable>>::value;

        template <typename Awaitable>
        decltype(auto) get_awaiter(Awaitable &&awaitable) {
            if constexpr (requires {
                              std::forward<Awaitable>(awaitable)
                                  .operator co_await();
                          })
            {
                return std::forward<Awaitable>(awaitable).operator co_await();
            } else {
                return std::forward<Awaitable>(awaitable);
            }
        }

        template <typename Awaiter>
        class ClearWaitContextAwaiter {
        private:
            Awaiter _awaiter;
            promise_base *_promise = nullptr;

        public:
            explicit ClearWaitContextAwaiter(Awaiter awaiter,
                                             promise_base *promise) noexcept
                : _awaiter(std::move(awaiter)), _promise(promise) {}

            [[nodiscard]]
            bool await_ready() {
                return _awaiter.await_ready();
            }

            template <typename Promise>
            decltype(auto) await_suspend(
                std::coroutine_handle<Promise> continuation) {
                if (_promise != nullptr) {
                    _promise->clear_wait_context_chain();
                }
                using SuspendRet =
                    decltype(_awaiter.await_suspend(continuation));
                if constexpr (std::is_void_v<SuspendRet>) {
                    _awaiter.await_suspend(continuation);
                } else {
                    return _awaiter.await_suspend(continuation);
                }
            }

            decltype(auto) await_resume() {
                return _awaiter.await_resume();
            }
        };

        template <typename T>
        struct AsyncState {
            std::atomic<FutureState> state = FutureState::PENDING;
            size_t wait_reason = 0;
            std::atomic<size_t> ref_count = 0;
            ErrCode error = ErrCode::FUTURE_ERROR;
            future_value_storage_t<T> value{};
            std::function<Result<void>()> cancel_callback{};

            AsyncState() = default;

            explicit AsyncState(size_t reason) noexcept
                : state(FutureState::PENDING),
                  wait_reason(reason),
                  ref_count(0),
                  error(ErrCode::FUTURE_ERROR),
                  value(),
                  cancel_callback() {}
        };

        template <typename T>
        [[nodiscard]]
        constexpr bool future_ready_state(FutureState state) noexcept {
            return state == FutureState::COMPLETE ||
                   state == FutureState::ERROR ||
                   state == FutureState::CANCLED ||
                   state == FutureState::CONSUMED;
        }
    }  // namespace detail

    template <typename T>
    struct promise : public promise_base {
        using handle_type = std::coroutine_handle<promise<T>>;

        T value{};

        cotask<T> get_return_object();

        std::suspend_never initial_suspend() noexcept {
            return {};
        }

        struct final_awaiter {
            [[nodiscard]]
            bool await_ready() const noexcept {
                return false;
            }

            std::coroutine_handle<> await_suspend(
                handle_type handle) const noexcept {
                auto &promise = handle.promise();
                promise.clear_wait_context_chain();
                if (promise.continuation) {
                    return promise.continuation;
                }
                if (promise.destroy_on_final_suspend) {
                    handle.destroy();
                }
                return std::noop_coroutine();
            }

            void await_resume() const noexcept {}
        };

        final_awaiter final_suspend() noexcept {
            return {};
        }

        void return_value(T ret) {
            value = std::move(ret);
        }

        void unhandled_exception() {
            value = {};
        }
    };

    template <>
    struct promise<void> : public promise_base {
        using handle_type = std::coroutine_handle<promise<void>>;

        cotask<void> get_return_object();

        std::suspend_never initial_suspend() noexcept {
            return {};
        }

        struct final_awaiter {
            [[nodiscard]]
            bool await_ready() const noexcept {
                return false;
            }

            std::coroutine_handle<> await_suspend(
                handle_type handle) const noexcept {
                auto &promise = handle.promise();
                promise.clear_wait_context_chain();
                if (promise.continuation) {
                    return promise.continuation;
                }
                if (promise.destroy_on_final_suspend) {
                    handle.destroy();
                }
                return std::noop_coroutine();
            }

            void await_resume() const noexcept {}
        };

        final_awaiter final_suspend() noexcept {
            return {};
        }

        void return_void() noexcept {}

        void unhandled_exception() noexcept {}
    };

    template <typename T>
    class cotask {
    public:
        using promise_type = promise<T>;
        using handle_type  = std::coroutine_handle<promise_type>;

        explicit cotask(handle_type handle) noexcept
            : _handle(handle), _owns_handle(handle != nullptr) {}

        cotask(const cotask &)            = delete;
        cotask &operator=(const cotask &) = delete;

        cotask(cotask &&other) noexcept
            : _handle(other._handle), _owns_handle(other._owns_handle) {
            other._handle      = nullptr;
            other._owns_handle = false;
        }

        cotask &operator=(cotask &&other) noexcept {
            if (this != &other) {
                reset();
                _handle            = other._handle;
                _owns_handle       = other._owns_handle;
                other._handle      = nullptr;
                other._owns_handle = false;
            }
            return *this;
        }

        ~cotask() {
            reset();
        }

        [[nodiscard]]
        bool valid() const noexcept {
            return _handle != nullptr;
        }

        [[nodiscard]]
        bool done() const noexcept {
            return _handle == nullptr || _handle.done();
        }

        [[nodiscard]]
        T result() const {
            if (_handle == nullptr) {
                return {};
            }
            return _handle.promise().value;
        }

        void resume() const {
            if (_handle != nullptr && !_handle.done()) {
                _handle.resume();
            }
        }

        [[nodiscard]]
        std::coroutine_handle<> handle() const noexcept {
            return _handle;
        }

        [[nodiscard]]
        const WaitContext &wait_context() const noexcept {
            if (_handle == nullptr) {
                return WaitContext::EMPTY;
            }
            return _handle.promise().wait_context();
        }

        void detach() {
            if (_handle == nullptr) {
                return;
            }
            _handle.promise().detached                 = true;
            _handle.promise().destroy_on_final_suspend = true;
            if (_handle.done()) {
                _handle.destroy();
                _handle = nullptr;
            }
            _owns_handle = false;
        }

        struct awaiter {
            cotask *task = nullptr;

            [[nodiscard]]
            bool await_ready() const noexcept {
                return task == nullptr || task->_handle == nullptr ||
                       task->_handle.done();
            }

            template <typename ParentPromise>
            bool await_suspend(std::coroutine_handle<ParentPromise>
                                   continuation) const noexcept {
                if (task == nullptr || task->_handle == nullptr ||
                    task->_handle.done())
                {
                    return false;
                }

                auto &child_promise        = task->_handle.promise();
                child_promise.continuation = continuation;
                if constexpr (detail::HasWaitContextAccessor<ParentPromise>) {
                    child_promise.wait_parent =
                        static_cast<promise_base *>(&continuation.promise());
                    if (child_promise.wait_context().pending()) {
                        child_promise.propagate_wait_context_chain();
                    } else {
                        child_promise.clear_wait_context_chain();
                    }
                } else {
                    child_promise.wait_parent = nullptr;
                    child_promise.wait_context().clear();
                }
                return true;
            }

            T await_resume() const {
                if (task == nullptr || task->_handle == nullptr) {
                    return {};
                }
                auto handle = task->_handle;
                auto value  = handle.promise().value;
                if (handle.promise().detached && !task->_owns_handle) {
                    handle.destroy();
                    task->_handle = nullptr;
                }
                return value;
            }
        };

        awaiter operator co_await() noexcept {
            return awaiter{this};
        }

        awaiter operator co_await() const noexcept {
            return awaiter{const_cast<cotask *>(this)};
        }

    private:
        void reset() {
            if (_handle != nullptr && _owns_handle) {
                _handle.destroy();
            }
            _handle      = nullptr;
            _owns_handle = false;
        }

        handle_type _handle = nullptr;
        bool _owns_handle   = false;
    };

    template <>
    class cotask<void> {
    public:
        using promise_type = promise<void>;
        using handle_type  = std::coroutine_handle<promise_type>;

        explicit cotask(handle_type handle) noexcept
            : _handle(handle), _owns_handle(handle != nullptr) {}

        cotask(const cotask &)            = delete;
        cotask &operator=(const cotask &) = delete;

        cotask(cotask &&other) noexcept
            : _handle(other._handle), _owns_handle(other._owns_handle) {
            other._handle      = nullptr;
            other._owns_handle = false;
        }

        cotask &operator=(cotask &&other) noexcept {
            if (this != &other) {
                reset();
                _handle            = other._handle;
                _owns_handle       = other._owns_handle;
                other._handle      = nullptr;
                other._owns_handle = false;
            }
            return *this;
        }

        ~cotask() {
            reset();
        }

        [[nodiscard]]
        bool valid() const noexcept {
            return _handle != nullptr;
        }

        [[nodiscard]]
        bool done() const noexcept {
            return _handle == nullptr || _handle.done();
        }

        void resume() const {
            if (_handle != nullptr && !_handle.done()) {
                _handle.resume();
            }
        }

        [[nodiscard]]
        std::coroutine_handle<> handle() const noexcept {
            return _handle;
        }

        [[nodiscard]]
        const WaitContext &wait_context() const noexcept {
            if (_handle == nullptr) {
                return WaitContext::EMPTY;
            }
            return _handle.promise().wait_context();
        }

        void detach() {
            if (_handle == nullptr) {
                return;
            }
            _handle.promise().detached                 = true;
            _handle.promise().destroy_on_final_suspend = true;
            if (_handle.done()) {
                _handle.destroy();
                _handle = nullptr;
            }
            _owns_handle = false;
        }

        struct awaiter {
            cotask *task = nullptr;

            [[nodiscard]]
            bool await_ready() const noexcept {
                return task == nullptr || task->_handle == nullptr ||
                       task->_handle.done();
            }

            template <typename ParentPromise>
            bool await_suspend(std::coroutine_handle<ParentPromise>
                                   continuation) const noexcept {
                if (task == nullptr || task->_handle == nullptr ||
                    task->_handle.done())
                {
                    return false;
                }

                auto &child_promise        = task->_handle.promise();
                child_promise.continuation = continuation;
                if constexpr (detail::HasWaitContextAccessor<ParentPromise>) {
                    child_promise.wait_parent =
                        static_cast<promise_base *>(&continuation.promise());
                    if (child_promise.wait_context().pending()) {
                        child_promise.propagate_wait_context_chain();
                    } else {
                        child_promise.clear_wait_context_chain();
                    }
                } else {
                    child_promise.wait_parent = nullptr;
                    child_promise.wait_context().clear();
                }
                return true;
            }

            void await_resume() const noexcept {
                if (task == nullptr || task->_handle == nullptr) {
                    return;
                }
                auto handle = task->_handle;
                if (handle.promise().detached && !task->_owns_handle) {
                    handle.destroy();
                    task->_handle = nullptr;
                }
            }
        };

        awaiter operator co_await() noexcept {
            return awaiter{this};
        }

        awaiter operator co_await() const noexcept {
            return awaiter{const_cast<cotask *>(this)};
        }

    private:
        void reset() {
            if (_handle != nullptr && _owns_handle) {
                _handle.destroy();
            }
            _handle      = nullptr;
            _owns_handle = false;
        }

        handle_type _handle = nullptr;
        bool _owns_handle   = false;
    };

    template <typename T>
    class Future {
    public:
        using value_type       = T;
        using wait_result_type = detail::future_wait_result_t<T>;

        Future() noexcept = default;

        explicit Future(detail::AsyncState<T> *state) noexcept : _state(state) {
            acquire();
        }

        Future(const Future &other) noexcept : _state(other._state) {
            acquire();
        }

        Future &operator=(const Future &other) noexcept {
            if (this != &other) {
                release();
                _state = other._state;
                acquire();
            }
            return *this;
        }

        Future(Future &&other) noexcept : _state(other._state) {
            other._state = nullptr;
        }

        Future &operator=(Future &&other) noexcept {
            if (this != &other) {
                release();
                _state = other._state;
                other._state = nullptr;
            }
            return *this;
        }

        ~Future() {
            release();
        }

        [[nodiscard]]
        bool valid() const noexcept {
            return _state != nullptr;
        }

        [[nodiscard]]
        FutureState state() const noexcept {
            return _state != nullptr ? _state->state : FutureState::ERROR;
        }

        [[nodiscard]]
        bool completed() const noexcept {
            return _state != nullptr && _state->state == FutureState::COMPLETE;
        }

        [[nodiscard]]
        bool consumed() const noexcept {
            return _state != nullptr && _state->state == FutureState::CONSUMED;
        }

        [[nodiscard]]
        bool readable() const noexcept {
            return _state != nullptr &&
                   detail::future_ready_state<T>(_state->state);
        }

        [[nodiscard]]
        size_t wait_reason() const noexcept {
            return _state != nullptr ? _state->wait_reason : 0;
        }

        Result<void> cancle() noexcept;
        wait_result_type value();

        struct awaiter {
        private:
            Future *future = nullptr;

        public:
            explicit awaiter(Future *future) noexcept : future(future) {}

            [[nodiscard]]
            bool await_ready() const noexcept {
                return future == nullptr || future->readable();
            }

            template <typename Promise>
            bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
                if (future == nullptr || future->readable()) {
                    return false;
                }
                if constexpr (!detail::HasWaitContextAccessor<Promise>) {
                    return false;
                } else {
                    auto &promise                = handle.promise();
                    auto &wait_context           = promise.wait_context();
                    wait_context.state           = FutureState::PENDING;
                    wait_context.wait_reason     = future->wait_reason();
                    wait_context.wait_predicate  = {};
                    wait_context.ready_predicate = [future = future]() noexcept {
                        return future != nullptr && future->readable();
                    };
                    wait_context.suspended_leaf = handle;
                    promise.propagate_wait_context_chain();
                    return true;
                }
            }

            wait_result_type await_resume();
        };

        awaiter operator co_await() noexcept {
            return awaiter(this);
        }

        awaiter operator co_await() const = delete;

    private:
        void acquire() noexcept {
            if (_state != nullptr) {
                ++_state->ref_count;
            }
        }

        void release() noexcept {
            if (_state == nullptr) {
                return;
            }
            assert(_state->ref_count > 0);
            --_state->ref_count;
            if (_state->ref_count == 0) {
                delete _state;
            }
            _state = nullptr;
        }

        detail::AsyncState<T> *_state = nullptr;
    };

    template <typename T>
    class Promise {
    public:
        Promise() : _state(new detail::AsyncState<T>(alloc_reason())) {
            assert(_state != nullptr);
            _state->ref_count = 1;
        }

        Promise(const Promise &)            = delete;
        Promise &operator=(const Promise &) = delete;

        Promise(Promise &&other) noexcept : _state(other._state) {
            other._state = nullptr;
        }

        Promise &operator=(Promise &&other) noexcept {
            if (this != &other) {
                release();
                _state = other._state;
                other._state = nullptr;
            }
            return *this;
        }

        ~Promise() {
            release();
        }

        [[nodiscard]]
        Future<T> future() const noexcept {
            return Future<T>(_state);
        }

        [[nodiscard]]
        size_t wait_reason() const noexcept {
            return _state != nullptr ? _state->wait_reason : 0;
        }

        Result<void> set_value(T value);
        Result<void> set_error(ErrCode error) noexcept;
        Result<void> set_cancled() noexcept;
        Result<void> set_cancel_callback(
            std::function<Result<void>()> callback) noexcept {
            if (_state == nullptr) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            _state->cancel_callback = std::move(callback);
            void_return();
        }

    private:
        void release() noexcept {
            if (_state == nullptr) {
                return;
            }
            assert(_state->ref_count > 0);
            --_state->ref_count;
            if (_state->ref_count == 0) {
                delete _state;
            }
            _state = nullptr;
        }

        detail::AsyncState<T> *_state = nullptr;
    };

    template <>
    class Future<void> {
    public:
        using value_type = void;
        using wait_result_type = Result<void>;

        Future() noexcept = default;

        explicit Future(detail::AsyncState<void> *state) noexcept
            : _state(state) {
            acquire();
        }

        Future(const Future &other) noexcept : _state(other._state) {
            acquire();
        }

        Future &operator=(const Future &other) noexcept {
            if (this != &other) {
                release();
                _state = other._state;
                acquire();
            }
            return *this;
        }

        Future(Future &&other) noexcept : _state(other._state) {
            other._state = nullptr;
        }

        Future &operator=(Future &&other) noexcept {
            if (this != &other) {
                release();
                _state = other._state;
                other._state = nullptr;
            }
            return *this;
        }

        ~Future() {
            release();
        }

        [[nodiscard]]
        bool valid() const noexcept {
            return _state != nullptr;
        }

        [[nodiscard]]
        bool readable() const noexcept {
            return _state != nullptr &&
                   detail::future_ready_state<void>(_state->state);
        }

        [[nodiscard]]
        size_t wait_reason() const noexcept {
            return _state != nullptr ? _state->wait_reason : 0;
        }

        Result<void> cancle() noexcept;
        Result<void> value();

    private:
        void acquire() noexcept {
            if (_state != nullptr) {
                ++_state->ref_count;
            }
        }

        void release() noexcept {
            if (_state == nullptr) {
                return;
            }
            assert(_state->ref_count > 0);
            --_state->ref_count;
            if (_state->ref_count == 0) {
                delete _state;
            }
            _state = nullptr;
        }

        detail::AsyncState<void> *_state = nullptr;
    };

    template <>
    class Promise<void> {
    public:
        Promise() : _state(new detail::AsyncState<void>(alloc_reason())) {
            assert(_state != nullptr);
            _state->ref_count = 1;
        }

        Promise(const Promise &)            = delete;
        Promise &operator=(const Promise &) = delete;

        Promise(Promise &&other) noexcept : _state(other._state) {
            other._state = nullptr;
        }

        Promise &operator=(Promise &&other) noexcept {
            if (this != &other) {
                release();
                _state = other._state;
                other._state = nullptr;
            }
            return *this;
        }

        ~Promise() {
            release();
        }

        [[nodiscard]]
        Future<void> future() const noexcept {
            return Future<void>(_state);
        }

        Result<void> set_value();
        Result<void> set_error(ErrCode error) noexcept;
        Result<void> set_cancled() noexcept;
        Result<void> set_cancel_callback(
            std::function<Result<void>()> callback) noexcept {
            if (_state == nullptr) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            _state->cancel_callback = std::move(callback);
            void_return();
        }

    private:
        void release() noexcept {
            if (_state == nullptr) {
                return;
            }
            assert(_state->ref_count > 0);
            --_state->ref_count;
            if (_state->ref_count == 0) {
                delete _state;
            }
            _state = nullptr;
        }

        detail::AsyncState<void> *_state = nullptr;
    };

    /**
     * @brief syscall 协程通用等待点.
     *
     * 该 awaiter 只负责在挂起时写入 promise 的等待上下文, 真正的线程等待
     * 由最外层 syscall 路径统一处理.
     */
    class FutureAwaiter {
    private:
        size_t _reason = 0;
        WaitPredicate _predicate;
        WaitReadyPredicate _ready_predicate;
        Result<void> _result{};

    public:
        explicit FutureAwaiter(size_t reason,
                               WaitPredicate predicate            = {},
                               WaitReadyPredicate ready_predicate = {}) noexcept
            : _reason(reason),
              _predicate(std::move(predicate)),
              _ready_predicate(std::move(ready_predicate)),
              _result{} {}

        [[nodiscard]]
        bool ready() const noexcept {
            return _ready_predicate && _ready_predicate();
        }

        [[nodiscard]]
        bool await_ready() const noexcept {
            return ready();
        }

        template <typename Promise>
        bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
            if constexpr (!detail::HasWaitContextAccessor<Promise>) {
                _result = std::unexpected(ErrCode::INVALID_PARAM);
                return false;
            } else {
                if (_ready_predicate && _ready_predicate()) {
                    return false;
                }
                if (_reason == 0) {
                    _result = std::unexpected(ErrCode::INVALID_PARAM);
                    return false;
                }

                auto &promise                = handle.promise();
                auto &wait_context           = promise.wait_context();
                wait_context.state           = FutureState::PENDING;
                wait_context.wait_reason     = _reason;
                wait_context.wait_predicate  = std::move(_predicate);
                wait_context.ready_predicate = std::move(_ready_predicate);
                wait_context.suspended_leaf  = handle;
                promise.propagate_wait_context_chain();
                _result = {};
                return true;
            }
        }

        [[nodiscard]]
        Result<void> await_resume() const noexcept {
            return _result;
        }
    };

    inline auto promise_base::await_transform(FutureAwaiter awaiter) noexcept {
        return awaiter;
    }

    template <typename Awaitable>
        requires(!std::same_as<std::remove_cvref_t<Awaitable>, FutureAwaiter>)
    inline auto promise_base::await_transform(Awaitable &&awaitable) {
        if constexpr (detail::is_wait_cotask_v<std::remove_cvref_t<Awaitable>> ||
                      detail::is_wait_future_v<std::remove_cvref_t<Awaitable>>)
        {
            return std::forward<Awaitable>(awaitable).operator co_await();
        } else {
            auto awaiter =
                detail::get_awaiter(std::forward<Awaitable>(awaitable));
            return detail::ClearWaitContextAwaiter<decltype(awaiter)>(
                std::move(awaiter), this);
        }
    }

    template <typename T>
    inline cotask<T> promise<T>::get_return_object() {
        return cotask<T>(handle_type::from_promise(*this));
    }

    inline cotask<void> promise<void>::get_return_object() {
        return cotask<void>(handle_type::from_promise(*this));
    }

    template <typename T>
    typename detail::future_wait_result_t<T> take_wait_result(Future<T> &future) {
        return future.value();
    }

    // 等待队列
    struct WaitQueue {
        // 等待队列对应的等待id
        size_t id;
        util::IntrusiveList<TCB, &TCB::wait_head> threads;

        // 从wait id中创建一个等待队列
        explicit WaitQueue(size_t id) : id(id), threads() {}
    };

    // 等待原因管理器, 负责管理所有的等待队列
    class WaitReasonManager {
    private:
        // 编号分配
        std::atomic<size_t> _next_reason = 1;
        // wait_id -> wait_queue
        std::unordered_map<size_t, WaitQueue *> _queues;

        // 获取等待队列, 如果不存在则创建一个新的
        Result<WaitQueue *> queue_for_wait(size_t id);
        // 获取等待队列, 如果不存在则返回错误
        Result<WaitQueue *> queue_if_exists(size_t id);

    public:
        // 分配一个 wait reason 号
        size_t alloc_reason();
        // 将当前线程加入等待队列
        Result<void> enqueue(size_t id, TCB *tcb);
        Result<void> enqueue(size_t id, TCB *tcb,
                             WaitPredicate predicate);
        Result<TCB *> peek_one(size_t id);
        // 从等待队列中弹出一个线程
        Result<TCB *> pop_one(size_t id);
        Result<void> remove(TCB *tcb);
        // 从等待队列中唤醒一个线程, 返回被唤醒线程的数量(0或1)
        Result<size_t> wake_one(size_t id);
        // 从等待队列中唤醒所有线程, 返回被唤醒线程的数量
        Result<size_t> wake_all(size_t id);
        // 判断是否有线程在等待队列中
        bool has_waiting(size_t id);

        // 初始化等待原因管理器的单例实例
        static void init();
        static bool initialized();
        // 获取等待原因管理器的单例实例
        static WaitReasonManager &inst();
    };

    size_t alloc_reason();
    [[deprecated("use co_await wait_current(...) in syscall coroutine paths")]]
    Result<void> deprecated_wait_current(size_t id);
    [[deprecated("use co_await wait_current(...) in syscall coroutine paths")]]
    Result<void> deprecated_wait_current(size_t id,
                                         WaitPredicate predicate);
    /**
     * @brief 等待指定事件变为就绪.
     *
     * 该接口面向普通线程阻塞场景: 在每次进入等待前以及被唤醒返回后
     * 都会重新检查 ready_predicate, 直到其返回 true.
     *
     * @param id 等待原因号, 必须非 0.
     * @param ready_predicate 事件就绪判断函数, 必须非空.
     * @return Result<void> 成功返回空结果, 失败返回错误码.
     */
    Result<void> wait_event(size_t id,
                            WaitReadyPredicate ready_predicate) noexcept;
    Result<TCB *> peek_one(size_t id);
    Result<size_t> wake_one(size_t id);
    Result<size_t> wake_all(size_t id);
    bool has_waiting(size_t id);

    template <typename T>
    typename detail::future_wait_result_t<T> wait_for(Future<T> &future);

    template <typename T>
    typename detail::future_wait_result_t<T> kthread_wait_for(Future<T> &future);
}  // namespace task::wait
