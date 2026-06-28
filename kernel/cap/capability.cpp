/**
 * @file capability.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief capability definition
 * @version alpha-1.0.0
 * @date 2026-02-25
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <cap/capability.h>
#include <cap/permission.h>
#include <logger.h>
#include <mem/alloc.h>
#include <storage.h>
#include <sus/list.h>
#include <sustcore/capability.h>

namespace cap {
    namespace kop {
        Storage<KOP<Capability>> capability_raw;
        Storage<KOP<CGroup>> cgroup_raw;
        Storage<LockedObject<IrqSaveGuardedLock, KOP<Capability>>>
            capability_storage;
        Storage<LockedObject<IrqSaveGuardedLock, KOP<CGroup>>> cgroup_storage;

        [[nodiscard]]
        LockedObject<IrqSaveGuardedLock, KOP<Capability>> &capability() {
            return capability_storage.ref();
        }

        [[nodiscard]]
        LockedObject<IrqSaveGuardedLock, KOP<CGroup>> &cgroup() {
            return cgroup_storage.ref();
        }
    }  // namespace kop

    void init_kop() {
        kop::capability_raw.construct();
        kop::cgroup_raw.construct();
        kop::capability_storage.construct(kop::capability_raw.get());
        kop::cgroup_storage.construct(kop::cgroup_raw.get());
    }

    void *Capability::operator new(size_t size) {
        assert(size == sizeof(Capability));
        return kop::capability().get()->alloc();
    }

    void Capability::operator delete(void *ptr) {
        kop::capability().get()->free(static_cast<Capability *>(ptr));
    }

    void *CGroup::operator new(size_t size) {
        assert(size == sizeof(CGroup));
        return kop::cgroup().get()->alloc();
    }

    void CGroup::operator delete(void *ptr) {
        kop::cgroup().get()->free(static_cast<CGroup *>(ptr));
    }
}  // namespace cap
