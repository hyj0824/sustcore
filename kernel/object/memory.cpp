/**
 * @file memory.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief Memory capability object
 * @version alpha-1.0.0
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <mem/gfp.h>
#include <mem/vma.h>
#include <object/memory.h>
#include <object/perm.h>
#include <sustcore/errcode.h>

#include <cstring>

namespace {
    /**
     * @brief 将 Memory 内偏移向下对齐到页边界. 
     */
    size_t page_offset(size_t offset) {
        return page_align_down(offset);
    }

    /**
     * @brief 判断请求的增长/收缩方式是否被 Memory 属性允许. 
     */
    bool growth_allows(cap::MemoryGrowth owned, cap::MemoryGrowth requested) {
        if (requested == cap::MemoryGrowth::FIXED) {
            return owned == cap::MemoryGrowth::FIXED;
        }
        return (static_cast<b64>(requested) & ~static_cast<b64>(owned)) == 0;
    }

    /**
     * @brief 为 continuity Memory 预分配连续物理页. 
     *
     * 已分配过物理页或大小为 0 时不做任何操作. 
     */
    Result<void> ensure_contiguous(cap::MemoryPayload *memory) {
        if (memory == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        if (!memory->continuity || memory->allocated_size() != 0) {
            void_return();
        }
        size_t pages = page_align_up(memory->memsz) / PAGESIZE;
        if (pages == 0) {
            void_return();
        }
        auto paddr_res = GFP::get_free_page(pages);
        propagate(paddr_res);
        PhyAddr base = paddr_res.value();
        for (size_t i = 0; i < pages; ++i) {
            memory->phy_pages.push_back({base + i * PAGESIZE, i * PAGESIZE});
        }
        void_return();
    }
}  // namespace

namespace cap {
    MemoryPayload::MemoryPayload(size_t memsz, bool shared, bool continuity,
                                 MemoryGrowth growth)
        : memsz(memsz),
          shared(shared),
          continuity(continuity),
          growth(growth),
          phy_pages() {}

    void MemoryPayload::destruct() {
        for (auto &page : phy_pages) {
            GFP::put_page(page.addr, 1);
        }
        delete this;
    }

    Payload *MemoryPayload::clone_payload() {
        if (shared) {
            return this;
        }

        auto *cloned = new MemoryPayload(memsz, shared, continuity, growth);
        for (auto &page : phy_pages) {
            GFP::keep_page(page.addr, 1);
            cloned->phy_pages.push_back(page);
        }
        return cloned;
    }

    Result<PhyAddr> MemoryPayload::lookup_page(size_t offset) const {
        size_t poff = page_offset(offset);
        if (poff >= memsz) {
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }
        for (auto &page : phy_pages) {
            if (page.offset == poff) {
                return page.addr;
            }
        }
        unexpect_return(ErrCode::PAGE_NOT_PRESENT);
    }

    Result<PhyAddr> MemoryPayload::ensure_page(size_t offset) {
        auto lookup_res = lookup_page(offset);
        if (lookup_res.has_value()) {
            return lookup_res.value();
        }
        if (lookup_res.error() != ErrCode::PAGE_NOT_PRESENT) {
            propagate_return(lookup_res);
        }

        size_t poff   = page_offset(offset);
        auto page_res = GFP::get_free_page(1);
        propagate(page_res);
        PhyAddr paddr = page_res.value();
        memset(convert<KpaAddr>(paddr).addr(), 0, PAGESIZE);
        phy_pages.push_back({paddr, poff});
        return paddr;
    }

    Result<void> MemoryPayload::replace_page(size_t offset, PhyAddr new_addr) {
        size_t poff = page_offset(offset);
        for (auto &page : phy_pages) {
            if (page.offset == poff) {
                GFP::put_page(page.addr, 1);
                page.addr = new_addr;
                void_return();
            }
        }
        unexpect_return(ErrCode::PAGE_NOT_PRESENT);
    }

    void MemoryPayload::release_pages_from(size_t offset) {
        size_t poff = page_offset(offset);
        for (auto it = phy_pages.begin(); it != phy_pages.end();) {
            if (it->offset >= poff) {
                GFP::put_page(it->addr, 1);
                it = phy_pages.erase(it);
            } else {
                ++it;
            }
        }
    }

    Result<void> MemoryPayload::resize(size_t newsz) {
        if (newsz == memsz) {
            void_return();
        }
        if (shared) {
            unexpect_return(ErrCode::NOT_SUPPORTED);
        }

        bool grow   = newsz > memsz && ((growth & MemoryGrowth::GROW_UP) ||
                                      (growth & MemoryGrowth::GROW_DOWN));
        bool shrink = newsz < memsz && ((growth & MemoryGrowth::SHRINK_UP) ||
                                        (growth & MemoryGrowth::SHRINK_DOWN));
        if (!grow && !shrink) {
            unexpect_return(ErrCode::NOT_SUPPORTED);
        }

        if (continuity) {
            size_t old_pages = page_align_up(memsz) / PAGESIZE;
            size_t new_pages = page_align_up(newsz) / PAGESIZE;
            if (new_pages == 0) {
                release_pages_from(0);
                memsz = newsz;
                void_return();
            }

            auto new_base_res = GFP::get_free_page(new_pages);
            propagate(new_base_res);
            PhyAddr new_base = new_base_res.value();
            memset(convert<KpaAddr>(new_base).addr(), 0, new_pages * PAGESIZE);

            size_t copy_pages = old_pages < new_pages ? old_pages : new_pages;
            for (size_t i = 0; i < copy_pages; ++i) {
                auto old_page_res = lookup_page(i * PAGESIZE);
                if (old_page_res.has_value()) {
                    memcpy(convert<KpaAddr>(new_base + i * PAGESIZE).addr(),
                           convert<KpaAddr>(old_page_res.value()).addr(),
                           PAGESIZE);
                }
            }

            for (auto &page : phy_pages) {
                GFP::put_page(page.addr, 1);
            }
            phy_pages.clear();
            for (size_t i = 0; i < new_pages; ++i) {
                phy_pages.push_back({new_base + i * PAGESIZE, i * PAGESIZE});
            }
        } else if (newsz < memsz) {
            release_pages_from(newsz);
        }
        memsz = newsz;
        void_return();
    }

    size_t MemoryPayload::allocated_size() const {
        return phy_pages.size() * PAGESIZE;
    }

    Result<void> MemoryObject::map_into(TaskMemoryManager &tmm, VirAddr vaddr,
                                        PageMan::RWX rwx,
                                        MemoryGrowth req_growth) const {
        if (!imply(perm::memory::MAP)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (!PageMan::is_readable(rwx)) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        if (!imply(perm::memory::READ)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (PageMan::is_writable(rwx) && !imply(perm::memory::WRITE)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (PageMan::is_executable(rwx) && !imply(perm::memory::EXEC)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if ((req_growth & MemoryGrowth::GROW_UP ||
             req_growth & MemoryGrowth::SHRINK_UP) &&
            !imply(perm::memory::FLEXUP))
        {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if ((req_growth & MemoryGrowth::GROW_DOWN ||
             req_growth & MemoryGrowth::SHRINK_DOWN) &&
            !imply(perm::memory::FLEXDOWN))
        {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (!growth_allows(_obj->growth, req_growth)) {
            unexpect_return(ErrCode::NOT_SUPPORTED);
        }

        auto cont_res = ensure_contiguous(_obj);
        propagate(cont_res);

        VirArea area(vaddr, vaddr + _obj->memsz);
        auto add_res =
            tmm.add_vma(VMA::Type::SHARE_RW, req_growth, area, _obj, rwx);
        propagate(add_res);
        void_return();
    }

    Result<void> MemoryObject::unmap_from(TaskMemoryManager &tmm,
                                          VirAddr vaddr) const {
        if (!imply(perm::memory::MAP)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        auto locate_res = tmm.locate_memory(_obj, vaddr);
        propagate(locate_res);
        return tmm.remove_vma(locate_res.value());
    }

    Result<void> MemoryObject::resize_in(TaskMemoryManager *tmm,
                                         size_t newsz) const {
        if (!imply(perm::memory::RESIZE)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (tmm != nullptr && newsz < _obj->memsz) {
            tmm->unmap_memory_tail(_obj, newsz);
        }
        auto resize_res = _obj->resize(newsz);
        propagate(resize_res);
        if (tmm != nullptr) {
            auto sync_res = tmm->sync_memory_vmas(_obj);
            propagate(sync_res);
        }
        void_return();
    }

    Result<MemoryObject::QueryResult> MemoryObject::query() const {
        if (!imply(perm::memory::QUERY)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        return QueryResult{_obj->memsz, _obj->allocated_size()};
    }
}  // namespace cap
