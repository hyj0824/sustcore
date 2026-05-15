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
#include <logger.h>
#include <perm/permission.h>
#include <sus/list.h>
#include <sus/queue.h>
#include <sustcore/capability.h>

namespace cap {
    namespace kop {
        KOP<Capability> capability;
        KOP<CGroup> cgroup;
    }  // namespace kop

    void init_kop() {
        new (&kop::capability) KOP<Capability>();
        new (&kop::cgroup) KOP<CGroup>();
    }

    void *Capability::operator new(size_t size) {
        assert(size == sizeof(Capability));
        return kop::capability.alloc();
    }

    void Capability::operator delete(void *ptr) {
        kop::capability.free(static_cast<Capability *>(ptr));
    }

    void *CGroup::operator new(size_t size) {
        assert(size == sizeof(CGroup));
        return kop::cgroup.alloc();
    }

    void CGroup::operator delete(void *ptr) {
        kop::cgroup.free(static_cast<CGroup *>(ptr));
    }
}  // namespace cap
