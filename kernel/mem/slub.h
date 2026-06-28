/**
 * @file slub.h
 * @author jeromeyao (yaoshengqi726@outlook.com)
 * theflysong(song_of_the_fly@163.com)
 * @brief SLUB Allocator
 * @version alpha-1.0.0
 * @date 2026-02-13
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <driver/int/guard.h>
#include <guard.h>
#include <logger.h>
#include <mem/alloc_def.h>
#include <mem/gfp.h>
#include <spinlock.h>
#include <storage.h>
#include <sus/list.h>
#include <sustcore/addr.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace slub {
    template <typename ObjType>
    class SlubCollection;

    constexpr size_t PAGES_PER_SLAB = 1;
    constexpr size_t SLAB_BYTES     = PAGESIZE * PAGES_PER_SLAB;
    constexpr size_t ALIGN          = 16;
    constexpr int SLAB_KMAX         = 2048;

    struct SlubStats {
        size_t total_slabs;
        size_t objects_inuse;
        size_t objects_total;
        size_t memory_usage_bytes;
    };

    void init_chrono_overhead();

    static constexpr uintptr_t align_down(uintptr_t addr, uintptr_t align) {
        return addr & ~(align - 1);
    }

    static constexpr uintptr_t align_up(uintptr_t addr, uintptr_t align) {
        return (addr + align - 1) & ~(align - 1);
    }

    static constexpr bool is_pow2(size_t n) {
        return (n > 0) && ((n & (n - 1)) == 0);
    }

    struct SlabHeader {
        enum class SlabState { EMPTY, PARTIAL, FULL };
        util::ListHead<SlabHeader> list_head{};
        void *freelist{};
        size_t inuse{};
        size_t total{};
        SlabState state{};

        SlabHeader()
            : list_head({}),
              freelist(nullptr),
              inuse(0),
              total(0),
              state(SlabState::EMPTY) {}
    };

    static_assert(
        util::IntrusiveListNodeTrait<SlabHeader, &SlabHeader::list_head>,
        "SlabHeader fails to be a valid intrusive list node");

    template <typename ObjType>
    struct size_of_type : public std::size_constant<sizeof(ObjType)> {};

    template <typename ObjType>
    struct align_of_type : public std::size_constant<alignof(ObjType)> {};

    template <typename ObjType>
    concept HugeObjectType = (size_of_type<ObjType>::value >= SLAB_KMAX);

    template <typename ObjType>
    class Slub {
    protected:
        static constexpr size_t raw_obj_size_  = size_of_type<ObjType>::value;
        static constexpr size_t raw_obj_align_ = align_of_type<ObjType>::value;

        static_assert(is_pow2(raw_obj_align_),
                      "raw_obj_align_ must be power-of-two");

        static constexpr size_t ptr_size_  = sizeof(void *);
        static constexpr size_t ptr_align_ = alignof(void *);

        static constexpr size_t round_up_pow2(size_t n, size_t align) {
            return (n + align - 1) & ~(align - 1);
        }

        static constexpr size_t max(size_t x, size_t y) {
            return (x > y) ? x : y;
        }

        static constexpr size_t obj_align_ = max(raw_obj_align_, ptr_align_);
        static constexpr size_t obj_size_ =
            round_up_pow2(max(raw_obj_size_, ptr_size_), obj_align_);

        constexpr static size_t pages_      = PAGES_PER_SLAB;
        constexpr static size_t slab_bytes_ = SLAB_BYTES;

        static_assert(is_pow2(obj_align_), "obj_align_ must be power-of-two");

    public:
        Slub() = default;

        ObjType *alloc();
        void free(ObjType *ptr);

        [[nodiscard]]
        SlubStats get_stats() const {
            size_t total_slabs   = partial.size() + full.size() + empty.size();
            size_t objects_total = 0;
            if (total_slabs > 0) {
                auto base = static_cast<uintptr_t>(0);
                auto cur  = align_up(base + sizeof(SlabHeader), obj_align_);
                auto end  = base + slab_bytes_;
                size_t per_slab = 0;
                while (cur + obj_size_ <= end) {
                    per_slab++;
                    cur += obj_size_;
                }
                objects_total = total_slabs * per_slab;
            }
            return {.total_slabs=total_slabs,
                    .objects_inuse=inuse_objects_,
                    .objects_total=objects_total,
                    .memory_usage_bytes=total_slabs * slab_bytes_};
        }

    private:
        util::IntrusiveList<SlabHeader> partial{};
        util::IntrusiveList<SlabHeader> full{};
        util::IntrusiveList<SlabHeader> empty{};
        size_t inuse_objects_ = 0;

        SlabHeader *new_slab();
        void init_slab_headers(SlabHeader *slab);
        SlabHeader *slab_of(void *p);

        void to_empty(SlabHeader *slab);
        void to_partial(SlabHeader *slab);
        void to_full(SlabHeader *slab);
        void inner_free(void *ptr);
    };

    template <HugeObjectType ObjType>
    class Slub<ObjType> {
    private:
        static constexpr size_t obj_pages =
            page_align_up(sizeof(ObjType)) / PAGESIZE;
        size_t inuse_objects_ = 0;

    public:
        Slub() = default;

        ObjType *alloc() {
            auto gfp_res = GFP::get_free_page(obj_pages);
            if (!gfp_res.has_value()) {
                loggers::SLUB::ERROR("无法分配大对象内存");
                return nullptr;
            }
            auto p = gfp_res.value();
            assert(p.nonnull());
            inuse_objects_++;
            return convert<KpaAddr>(p).as<ObjType>();
        }

        void free(ObjType *ptr) {
            if (!ptr) {
                loggers::SLUB::WARN("can't free null pointer");
                return;
            }

            auto paddr = convert<PhyAddr>((KpaAddr)ptr);
            GFP::put_page(paddr, obj_pages);
            inuse_objects_--;
        }

        [[nodiscard]]
        SlubStats get_stats() const {
            return {
                .total_slabs=inuse_objects_,
                .objects_inuse=inuse_objects_,
                .objects_total=inuse_objects_,
                .memory_usage_bytes=inuse_objects_ * obj_pages * PAGESIZE,
            };
        }
    };

    template <typename ObjType>
    SlabHeader *Slub<ObjType>::slab_of(void *p) {
        auto ptr  = reinterpret_cast<uintptr_t>(p);
        auto base = align_down(ptr, slab_bytes_);
        return reinterpret_cast<SlabHeader *>(base);
    }

    template <typename ObjType>
    void Slub<ObjType>::init_slab_headers(SlabHeader *slab) {
        auto base       = reinterpret_cast<uintptr_t>(slab);
        auto slab_start = align_up(base + sizeof(SlabHeader), obj_align_);

        constexpr size_t total =
            (slab_bytes_ - align_up(sizeof(SlabHeader), obj_align_)) /
            obj_size_;
        static_assert(total > 0, "每个 slab 至少应包含一个对象");

        slab->total = total;
        slab->inuse = 0;

        void *head = nullptr;
        for (size_t i = total; i > 0; i--) {
            void *obj =
                reinterpret_cast<void *>(slab_start + (i - 1) * obj_size_);
            *reinterpret_cast<void **>(obj) = head;
            head                            = obj;
        }
        slab->freelist = head;
    }

    template <typename ObjType>
    SlabHeader *Slub<ObjType>::new_slab() {
        auto gfp_res = GFP::get_free_page(pages_);
        if (!gfp_res.has_value()) {
            loggers::SLUB::ERROR("无法分配新的 slab 内存");
            return nullptr;
        }
        auto paddr  = gfp_res.value();
        auto kpaddr = convert<KpaAddr>(paddr);
        auto *slab  = new (kpaddr.addr()) SlabHeader{};
        init_slab_headers(slab);
        return slab;
    }

    template <typename ObjType>
    void Slub<ObjType>::to_empty(SlabHeader *slab) {
        if (slab->state == SlabHeader::SlabState::PARTIAL) {
            partial.erase(typename decltype(partial)::iterator(slab));
        } else if (slab->state == SlabHeader::SlabState::FULL) {
            full.erase(typename decltype(full)::iterator(slab));
        }
        slab->state = SlabHeader::SlabState::EMPTY;
        empty.push_back(*slab);
    }

    template <typename ObjType>
    void Slub<ObjType>::to_partial(SlabHeader *slab) {
        if (slab->state == SlabHeader::SlabState::EMPTY) {
            empty.erase(typename decltype(empty)::iterator(slab));
        } else if (slab->state == SlabHeader::SlabState::FULL) {
            full.erase(typename decltype(full)::iterator(slab));
        }
        slab->state = SlabHeader::SlabState::PARTIAL;
        partial.push_back(*slab);
    }

    template <typename ObjType>
    void Slub<ObjType>::to_full(SlabHeader *slab) {
        if (slab->state == SlabHeader::SlabState::PARTIAL) {
            partial.erase(typename decltype(partial)::iterator(slab));
        } else if (slab->state == SlabHeader::SlabState::EMPTY) {
            empty.erase(typename decltype(empty)::iterator(slab));
        }
        slab->state = SlabHeader::SlabState::FULL;
        full.push_back(*slab);
    }

    template <typename ObjType>
    ObjType *Slub<ObjType>::alloc() {
        SlabHeader *slab = nullptr;
        if (!partial.empty()) {
            slab = &partial.back();
        } else if (!empty.empty()) {
            slab = &empty.back();
            to_partial(slab);
        } else {
            slab = new_slab();
            if (slab == nullptr) {
                loggers::SLUB::ERROR("无法分配新的 slab");
                return nullptr;
            }
            slab->state = SlabHeader::SlabState::PARTIAL;
            partial.push_back(*slab);
        }

        assert(slab != nullptr);
        assert(slab->freelist != nullptr);
        void *obj      = slab->freelist;
        slab->freelist = *reinterpret_cast<void **>(obj);
        slab->inuse++;
        inuse_objects_++;
        if (slab->inuse == slab->total) {
            to_full(slab);
        }
        return static_cast<ObjType *>(obj);
    }

    template <typename ObjType>
    void Slub<ObjType>::inner_free(void *ptr) {
        InterruptGuard guard;
        guard.enter();

        if (!ptr) {
            loggers::SLUB::WARN("can't free null pointer");
            return;
        }
        auto *slab_header             = slab_of(ptr);
        *reinterpret_cast<void **>(ptr) = slab_header->freelist;
        slab_header->freelist         = ptr;
        slab_header->inuse--;
        inuse_objects_--;
        if (slab_header->inuse == 0) {
            to_empty(slab_header);
        } else if (slab_header->inuse == slab_header->total - 1) {
            to_partial(slab_header);
        }
    }

    template <typename ObjType>
    void Slub<ObjType>::free(ObjType *ptr) {
        if (!ptr) {
            loggers::SLUB::WARN("can't free null pointer");
            return;
        }
        inner_free(ptr);
    }

    template <size_t sz>
    class SizedSlub {
    private:
        class Object {
            char data[sz];
        };

        Storage<Slub<Object>> _raw_slub;
        Storage<LockedObject<IrqSaveGuardedLock, Slub<Object>>> _slub_storage;

        [[nodiscard]]
        LockedObject<IrqSaveGuardedLock, Slub<Object>> &slub() {
            return _slub_storage.ref();
        }

    public:
        SizedSlub() {
            static_assert(is_pow2(sz));
            _raw_slub.construct();
            _slub_storage.construct(_raw_slub.get());
        }

        void *malloc() {
            return static_cast<void *>(slub().get()->alloc());
        }

        void free(void *ptr) {
            slub().get()->free(static_cast<Object *>(ptr));
        }
    };

    class SlubMalloc {
    private:
        static constexpr size_t KMAX = 2048;
        static constexpr size_t KMIN = 8;

        struct AllocRecord {
            void *ptr;
            size_t size;
            util::ListHead<AllocRecord> list_head{};
            static Slub<AllocRecord> *ALLOC_RECORD_SLUB;

            constexpr AllocRecord(void *ptr, size_t size)
                : ptr(ptr), size(size) {}
            constexpr AllocRecord() : AllocRecord(nullptr, 0) {}

            void *operator new(size_t sz);
            void operator delete(void *ptr);
        };

        SizedSlub<8> _slub8{};
        SizedSlub<16> _slub16{};
        SizedSlub<32> _slub32{};
        SizedSlub<64> _slub64{};
        SizedSlub<128> _slub128{};
        SizedSlub<256> _slub256{};
        SizedSlub<512> _slub512{};
        SizedSlub<1024> _slub1024{};
        SizedSlub<2048> _slub2048{};

        Slub<AllocRecord> _alloc_record_slub{};
        util::IntrusiveList<AllocRecord> _alloc_records{};

        static Storage<SlubMalloc> _INSTANCE_STORAGE;
        static Storage<LockedObject<IrqSaveGuardedLock, SlubMalloc>>
            _INSTANCE_LOCKED_STORAGE;
        static bool _initialized;

        [[nodiscard]]
        bool contains_small_size(size_t sz) const {
            switch (sz) {
                case 8:
                case 16:
                case 32:
                case 64:
                case 128:
                case 256:
                case 512:
                case 1024:
                case 2048: return true;
                default:   return false;
            }
        }

        [[nodiscard]]
        static size_t up2pow(size_t n) {
            n--;
            n |= n >> 1;
            n |= n >> 2;
            n |= n >> 4;
            n |= n >> 8;
            n |= n >> 16;
            if constexpr (sizeof(size_t) == 8) {
                n |= n >> 32;
            }
            n++;
            return n;
        }

        [[nodiscard]]
        static constexpr size_t get_pages(size_t rsz) {
            return std::max(static_cast<size_t>(1),
                            (rsz + PAGESIZE - 1) / PAGESIZE);
        }

        bool add_record(void *ptr, size_t rsz) {
            auto *record  = new AllocRecord(ptr, rsz);
            size_t before = _alloc_records.size();
            auto inserted = _alloc_records.insert(_alloc_records.end(), *record);
            size_t after  = _alloc_records.size();
            if (inserted == _alloc_records.end()) {
                loggers::MEMORY::ERROR(
                    "无法记录分配: ptr=%p size=%u record=%p (size before=%lu after=%lu)",
                    ptr, static_cast<unsigned int>(rsz), record,
                    (unsigned long)before, (unsigned long)after);
                delete record;
                return false;
            }
            return true;
        }

        AllocRecord *get_record(void *ptr) {
            size_t checked = 0;
            for (auto &rec : _alloc_records) {
                checked++;
                if (rec.ptr == ptr) {
                    return &rec;
                }
            }
            auto sz = _alloc_records.size();
            if (sz != checked) {
                loggers::MEMORY::INFO(
                    "alloc_records size mismatch: expected=%lu iterated=%lu",
                    (unsigned long)sz, (unsigned long)checked);
            }
            return nullptr;
        }

        void remove_record(AllocRecord *record) {
            size_t before = _alloc_records.size();
            _alloc_records.remove(*record);
            size_t after = _alloc_records.size();
            if (before == after) {
                loggers::MEMORY::ERROR(
                    "remove_record: record=%p NOT found in list (size=%lu)",
                    (void *)record, (unsigned long)before);
            }
            delete record;
        }

        void *large_malloc(size_t rsz) {
            loggers::MEMORY::DEBUG("转交到large_malloc途径分配");
            assert(is_pow2(rsz));
            const size_t pages = get_pages(rsz);
            auto gfp_res       = GFP::get_free_page(pages);
            if (!gfp_res.has_value()) {
                loggers::SLUB::ERROR("无法分配大对象内存");
                return nullptr;
            }
            return convert<KpaAddr>(gfp_res.value()).addr();
        }

        void *small_malloc(size_t rsz) {
            assert(contains_small_size(rsz));
            switch (rsz) {
                case 8:    return _slub8.malloc();
                case 16:   return _slub16.malloc();
                case 32:   return _slub32.malloc();
                case 64:   return _slub64.malloc();
                case 128:  return _slub128.malloc();
                case 256:  return _slub256.malloc();
                case 512:  return _slub512.malloc();
                case 1024: return _slub1024.malloc();
                case 2048: return _slub2048.malloc();
                default:
                    loggers::SLUB::ERROR("不支持的对象大小: %d", rsz);
                    return nullptr;
            }
        }

        void free_inner(void *ptr, size_t rsz) {
            if (rsz >= KMAX) {
                auto pages = get_pages(rsz);
                GFP::put_page(convert<PhyAddr>((KpaAddr)ptr), pages);
                return;
            }

            assert(contains_small_size(rsz));
            switch (rsz) {
                case 8:    _slub8.free(ptr); return;
                case 16:   _slub16.free(ptr); return;
                case 32:   _slub32.free(ptr); return;
                case 64:   _slub64.free(ptr); return;
                case 128:  _slub128.free(ptr); return;
                case 256:  _slub256.free(ptr); return;
                case 512:  _slub512.free(ptr); return;
                case 1024: _slub1024.free(ptr); return;
                case 2048: _slub2048.free(ptr); return;
                default:
                    loggers::SLUB::ERROR("不支持的对象大小: %d", rsz);
                    return;
            }
        }

    public:
        SlubMalloc() {
            AllocRecord::ALLOC_RECORD_SLUB = &_alloc_record_slub;
            loggers::MEMORY::INFO(
                "SlubMalloc 初始化完成: records=%lu alloc_record_inuse=%lu",
                (unsigned long)_alloc_records.size(),
                (unsigned long)_alloc_record_slub.get_stats().objects_inuse);
        }

        static LockedObject<IrqSaveGuardedLock, SlubMalloc> &INSTANCE() {
            assert(_initialized);
            return _INSTANCE_LOCKED_STORAGE.ref();
        }

        static void init() {
            if (_initialized) {
                return;
            }
            _INSTANCE_STORAGE.construct();
            _INSTANCE_LOCKED_STORAGE.construct(_INSTANCE_STORAGE.get());
            _initialized = true;
        }

        void *malloc(size_t sz) {
            const size_t rsz = std::max(up2pow(sz), KMIN);
            assert(contains_small_size(rsz) || rsz >= KMAX);

            void *ptr = (rsz >= KMAX) ? large_malloc(rsz) : small_malloc(rsz);
            if (ptr == nullptr) {
                loggers::MEMORY::ERROR("无法分配内存!");
                return nullptr;
            }
            if (!add_record(ptr, rsz)) {
                free_inner(ptr, rsz);
                assert(false);
                return nullptr;
            }
            return ptr;
        }

        void free(void *ptr) {
            auto *record = get_record(ptr);
            if (record == nullptr) {
                loggers::MEMORY::ERROR(
                    "未查询到地址%p分配记录 (list size=%lu)", ptr,
                    (unsigned long)_alloc_records.size());
                return;
            }
            free_inner(ptr, record->size);
            remove_record(record);
        }
    };

    static_assert(KOPTrait<Slub<int>, int>, "KOP 不满足 KOPTrait");
}  // namespace slub
