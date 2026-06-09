/**
 * @file blk.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 块设备接口
 * @version alpha-1.0.0
 * @date 2026-06-06
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <new>
#include <cassert>
#include <unordered_map>
#include <bio/block.h>
#include <bio/buffer.h>
#include <bio/request.h>
#include <task/scheduler.h>
#include <task/task.h>

namespace blk {
    struct RegisteredBlockDevice {
        util::owner<IBlockDeviceOps *> device;
        util::owner<BufferCache *> cache;
        util::owner<BlockRequestQueue *> queue;
        task::TCB *worker = nullptr;
    };

    class BlkManager {
    private:
        BlkManager() = default;
        ~BlkManager() {
            for (auto &[id, record] : _devices) {
                delete record.cache.get();
                delete record.queue.get();
                delete record.device.get();
            }
        }
        static BlkManager _INSTANCE;
        static bool _initialized;

    public:
        static BlkManager &inst() {
            assert(_initialized);
            return _INSTANCE;
        }
        static void init() {
            if (_initialized)
                return;
            _initialized = true;
            new (&_INSTANCE) BlkManager();
        }
        static bool initialized() {
            return _initialized;
        }

    private:
        std::unordered_map<size_t, RegisteredBlockDevice> _devices;
        std::unordered_map<size_t, size_t> _device_ids;
        size_t _next_id = 0;

    public:
        [[nodiscard]]
        Result<bool> contains(size_t id) const {
            return _devices.contains(id);
        }

        [[nodiscard]]
        Result<IBlockDeviceOps *> lookup(size_t id) const {
            auto it = _devices.find(id);
            if (it == _devices.end())
                unexpect_return(ErrCode::ENTRY_NOT_FOUND);
            return it->second.device.get();
        }

        [[nodiscard]]
        Result<BufferCache *> lookup_cache(size_t id) const {
            auto it = _devices.find(id);
            if (it == _devices.end())
                unexpect_return(ErrCode::ENTRY_NOT_FOUND);
            if (it->second.cache.get() == nullptr)
                unexpect_return(ErrCode::NULLPTR);
            return it->second.cache.get();
        }

        [[nodiscard]]
        Result<BlockRequestQueue *> lookup_queue(size_t id) const {
            auto it = _devices.find(id);
            if (it == _devices.end()) {
                unexpect_return(ErrCode::ENTRY_NOT_FOUND);
            }
            if (it->second.queue.get() == nullptr) {
                unexpect_return(ErrCode::NULLPTR);
            }
            return it->second.queue.get();
        }

        [[nodiscard]]
        Result<size_t> find_device_id(IBlockDeviceOps *device) const {
            if (device == nullptr) {
                unexpect_return(ErrCode::NULLPTR);
            }
            auto key = reinterpret_cast<size_t>(device);
            auto it = _device_ids.find(key);
            if (it == _device_ids.end()) {
                unexpect_return(ErrCode::ENTRY_NOT_FOUND);
            }
            return it->second;
        }

        Result<size_t> register_device(util::owner<IBlockDeviceOps *> &&device) {
            if (device.get() == nullptr) {
                unexpect_return(ErrCode::NULLPTR);
            }
            IBlockDeviceOps *raw_device = device.get();
            auto key = reinterpret_cast<size_t>(raw_device);
            if (_device_ids.contains(key)) {
                unexpect_return(ErrCode::KEY_DUPLICATED);
            }
            size_t id = _next_id++;
            auto block_sz_res = raw_device->block_sz();
            propagate(block_sz_res);
            auto *queue = new BlockRequestQueue(id);
            if (queue == nullptr) {
                unexpect_return(ErrCode::OUT_OF_MEMORY);
            }
            auto *cache = new BufferCache(id, block_sz_res.value());
            if (cache == nullptr) {
                delete queue;
                unexpect_return(ErrCode::OUT_OF_MEMORY);
            }
            auto bind_res = raw_device->bind_request_queue(util::nnullforce(queue));
            if (!bind_res.has_value()) {
                delete cache;
                delete queue;
                propagate_return(bind_res);
            }
            auto worker_res = task::TaskManager::inst().create_kernel_thread(
                [](void *arg) {
                    auto *device = static_cast<IBlockDeviceOps *>(arg);
                    if (device == nullptr) {
                        return;
                    }
                    auto run_res = device->run_request_loop();
                    if (!run_res.has_value()) {
                        loggers::SUSTCORE::ERROR(
                            "块设备 worker 退出失败: %s",
                            to_cstring(run_res.error()));
                    }
                },
                raw_device, schd::ClassType::FCFS);
            if (!worker_res.has_value()) {
                delete cache;
                delete queue;
                propagate_return(worker_res);
            }
            device = util::owner<IBlockDeviceOps *>(nullptr);
            _device_ids.insert_or_assign(key, id);
            _devices.emplace(id,
                             RegisteredBlockDevice{
                                 .device = util::owner<IBlockDeviceOps *>(raw_device),
                                 .cache = util::owner(cache),
                                 .queue = util::owner(queue),
                                 .worker = worker_res.value().get(),
                             });
            if (!schd::Scheduler::inst().wakeup_new(worker_res.value().get())) {
                auto it = _devices.find(id);
                auto rec = std::move(it->second);
                _devices.erase(it);
                _device_ids.erase(key);
                delete rec.cache.get();
                delete rec.queue.get();
                delete rec.device.get();
                unexpect_return(ErrCode::FAILURE);
            }
            return id;
        }

        Result<void> unregister_device(size_t id) {
            auto it = _devices.find(id);
            if (it == _devices.end())
                unexpect_return(ErrCode::ENTRY_NOT_FOUND);
            auto *dev = it->second.device.get();
            auto *cache = it->second.cache.get();
            auto *queue = it->second.queue.get();
            if (queue != nullptr) {
                auto stop_res =
                    queue->drain_and_stop(ErrCode::FUTURE_CANCLED);
                propagate(stop_res);
            }
            _device_ids.erase(reinterpret_cast<size_t>(dev));
            _devices.erase(it);
            delete cache;
            delete queue;
            delete dev;
            void_return();
        }
    };
}  // namespace blk
