/**
 * @file slub.cpp
 * @author jeromeyao (yaoshengqi726@outlook.com)
 * theflysong(song_of_the_fly@163.com)
 * @brief SLUB Allocator
 * @version alpha-1.0.0
 * @date 2026-02-13
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <mem/slub.h>

namespace slub {
    Storage<SlubMalloc> SlubMalloc::_INSTANCE_STORAGE;
    Storage<LockedObject<IrqSaveGuardedLock, SlubMalloc>>
        SlubMalloc::_INSTANCE_LOCKED_STORAGE;
    bool SlubMalloc::_initialized = false;
    Slub<SlubMalloc::AllocRecord> *SlubMalloc::AllocRecord::ALLOC_RECORD_SLUB =
        nullptr;

    void *SlubMalloc::AllocRecord::operator new(size_t sz) {
        assert(sz == sizeof(AllocRecord));
        assert(ALLOC_RECORD_SLUB != nullptr);
        return ALLOC_RECORD_SLUB->alloc();
    }

    void SlubMalloc::AllocRecord::operator delete(void *ptr) {
        assert(ALLOC_RECORD_SLUB != nullptr);
        ALLOC_RECORD_SLUB->free(static_cast<AllocRecord *>(ptr));
    }
}  // namespace slub
