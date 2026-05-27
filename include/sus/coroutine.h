/**
 * @file coroutine.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 协程辅助函数/类定义
 * @version alpha-1.0.0
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <coroutine>
#include <utility>

namespace util {
    template <typename T>
    class cotask;

    template <>
    class cotask<void>;

    /**
     * @brief 协程的 promise 类型定义
     *
     * @tparam T 协程返回值类型
     */
    template <typename T>
    struct promise {
        using handle_type = std::coroutine_handle<promise<T>>;

        T value{};
        bool detached                        = false;
        bool destroy_on_final_suspend        = false;
        std::coroutine_handle<> continuation = nullptr;

        cotask<T> get_return_object();

        std::suspend_never initial_suspend() noexcept {
            return {};
        }

        struct final_awaiter {
            bool await_ready() const noexcept {
                return false;
            }

            std::coroutine_handle<> await_suspend(
                handle_type handle) const noexcept {
                auto &promise = handle.promise();
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

    /**
     * @brief void 协程的 promise 类型定义.
     */
    template <>
    struct promise<void> {
        using handle_type = std::coroutine_handle<promise<void>>;

        bool detached                        = false;
        bool destroy_on_final_suspend        = false;
        std::coroutine_handle<> continuation = nullptr;

        cotask<void> get_return_object();

        std::suspend_never initial_suspend() noexcept {
            return {};
        }

        struct final_awaiter {
            bool await_ready() const noexcept {
                return false;
            }

            std::coroutine_handle<> await_suspend(
                handle_type handle) const noexcept {
                auto &promise = handle.promise();
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

    /**
     * @brief 协程类型定义
     *
     * @tparam T 协程返回值类型
     */
    template <typename T>
    class cotask {
    public:
        using promise_type = promise<T>;
        using handle_type  = typename promise_type::handle_type;

        explicit cotask(handle_type handle)
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
            return _handle == nullptr || (_handle != nullptr && _handle.done());
        }

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

            bool await_ready() const noexcept {
                return task == nullptr || task->_handle == nullptr ||
                       task->_handle.done();
            }

            bool await_suspend(
                std::coroutine_handle<> continuation) const noexcept {
                if (task == nullptr || task->_handle == nullptr ||
                    task->_handle.done())
                {
                    return false;
                }
                task->_handle.promise().continuation = continuation;
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

    /**
     * @brief void 返回值协程类型定义.
     */
    template <>
    class cotask<void> {
    public:
        using promise_type = promise<void>;
        using handle_type  = typename promise_type::handle_type;

        explicit cotask(handle_type handle)
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
            return _handle == nullptr || (_handle != nullptr && _handle.done());
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

            bool await_ready() const noexcept {
                return task == nullptr || task->_handle == nullptr ||
                       task->_handle.done();
            }

            bool await_suspend(
                std::coroutine_handle<> continuation) const noexcept {
                if (task == nullptr || task->_handle == nullptr ||
                    task->_handle.done())
                {
                    return false;
                }
                task->_handle.promise().continuation = continuation;
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
    cotask<T> promise<T>::get_return_object() {
        return cotask<T>(handle_type::from_promise(*this));
    }

    inline cotask<void> promise<void>::get_return_object() {
        return cotask<void>(handle_type::from_promise(*this));
    }
}  // namespace util
