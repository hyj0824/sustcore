/**
 * @file bootstrap.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief task bootstrap / preload / spec 装载
 * @version alpha-1.0.0
 * @date 2026-06-21
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <cap/permission.h>
#include <exe/elfloader.h>
#include <kmod/bootstrap.h>
#include <logger.h>
#include <mem/alloc.h>
#include <task/task.h>

#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace task {
    namespace {
        [[nodiscard]]
        bool valid_bootstrap_cap_path(const BootstrapCapPathView &record) {
            return record.cap != cap::null && record.cap != cap::error &&
                   record.path != nullptr && record.path[0] == '/';
        }

        [[nodiscard]]
        size_t bootstrap_cap_path_record_size(
            const BootstrapCapPathView &record) {
            return sizeof(BootstrapRecordHeader) + sizeof(CapIdx) +
                   strlen(record.path) + 1;
        }

        [[nodiscard]]
        Result<void> append_bootstrap_record(char *dst, size_t blob_size,
                                             size_t &offset, uint32_t type,
                                             const void *data,
                                             size_t data_size) {
            if (dst == nullptr || data == nullptr) {
                unexpect_return(ErrCode::NULLPTR);
            }
            size_t record_size = sizeof(BootstrapRecordHeader) + data_size;
            if (offset > blob_size || blob_size - offset < record_size) {
                unexpect_return(ErrCode::OUT_OF_BOUNDARY);
            }

            auto *header =
                reinterpret_cast<BootstrapRecordHeader *>(dst + offset);
            header->next = 0;
            header->type = type;
            memcpy(dst + offset + sizeof(BootstrapRecordHeader), data,
                   data_size);
            offset += record_size;
            void_return();
        }

        [[nodiscard]]
        Result<void> append_bootstrap_cap_path_record(
            char *dst, size_t blob_size, size_t &offset, uint32_t type,
            const BootstrapCapPathView &record) {
            if (!valid_bootstrap_cap_path(record)) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }

            size_t path_len = strlen(record.path) + 1;
            size_t record_size =
                sizeof(BootstrapRecordHeader) + sizeof(CapIdx) + path_len;
            if (offset > blob_size || blob_size - offset < record_size) {
                unexpect_return(ErrCode::OUT_OF_BOUNDARY);
            }

            auto *header =
                reinterpret_cast<BootstrapRecordHeader *>(dst + offset);
            header->next = 0;
            header->type = type;
            memcpy(dst + offset + sizeof(BootstrapRecordHeader), &record.cap,
                   sizeof(record.cap));
            memcpy(dst + offset + sizeof(BootstrapRecordHeader) +
                       sizeof(record.cap),
                   record.path, path_len);
            offset += record_size;
            void_return();
        }

        void finalize_bootstrap_record_next(
            char *blob, const std::vector<size_t> &offsets) {
            for (size_t i = 0; i < offsets.size(); ++i) {
                auto *header = reinterpret_cast<BootstrapRecordHeader *>(
                    blob + offsets[i]);
                header->next = i + 1 < offsets.size() ? offsets[i + 1] : 0;
            }
        }
    }  // namespace

    Result<CapIdx> TaskManager::preload(CapIdx image_cap, TaskSpec &spec,
                                        LoadPrm &prm) {
        auto create_res = cap::CHolderManager::inst().create_holder();
        if (!create_res.has_value()) {
            loggers::SUSTCORE::ERROR("创建CHolder失败! 错误码: %s",
                                     to_cstring(create_res.error()));
            unexpect_return(ErrCode::CREATION_FAILED);
        }
        auto holder = create_res.value();

        auto preload_res = preload_into(image_cap, holder, spec, prm);
        if (!preload_res.has_value()) {
            auto rm_res =
                cap::CHolderManager::inst().remove_holder(holder->id());
            assert(rm_res.has_value());
            propagate_return(preload_res);
        }
        return preload_res;
    }

    Result<CapIdx> TaskManager::preload_into(CapIdx image_cap,
                                             cap::CHolder *holder,
                                             TaskSpec &spec, LoadPrm &prm) {
        if (holder == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }

        auto gfp_res = GFP::get_free_page(1);
        if (!gfp_res.has_value()) {
            loggers::SUSTCORE::ERROR("无法为程序页表分配物理页");
            unexpect_return(ErrCode::CREATION_FAILED);
        }
        auto tmm = util::owner(new TaskMemoryManager(gfp_res.value()));

        auto cap_res = holder->lookup(image_cap);
        propagate(cap_res);
        if (cap_res.value()->payload()->type_id() != PayloadType::VFILE) {
            unexpect_return(ErrCode::TYPE_NOT_MATCHED);
        }
        if (!cap_res.value()->imply(perm::vfile::EXEC)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }

        spec.holder = holder;
        spec.tmm    = tmm;
        prm.src_path       = "<cap>";
        prm.image_file_cap = image_cap;
        return image_cap;
    }

    Result<void> TaskManager::validate_bootstrap_blob(
        const void *startup_blob, size_t startup_blob_size) {
        if (startup_blob_size == 0) {
            void_return();
        }
        if (startup_blob == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }

        bool valid  = true;
        bool walked = bootstrap_foreach_record(
            startup_blob, startup_blob_size,
            [&](const BootstrapRecordView &view) {
                if (!valid) {
                    return;
                }
                if (view.header->type == BOOTSTRAP_TYPE_FILECAPEXPLAIN ||
                    view.header->type == BOOTSTRAP_TYPE_DIRCAPEXPLAIN)
                {
                    BootstrapCapPathView cap_path{};
                    valid = bootstrap_parse_cap_path(view, cap_path);
                    if (!valid || !valid_bootstrap_cap_path(cap_path)) {
                        return;
                    }
                    return;
                }
                if (view.header->type == 0) {
                    valid = false;
                }
            });
        if (!walked || !valid) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        void_return();
    }

    Result<void> TaskManager::build_bootstrap_blob(
        const void *startup_blob, size_t startup_blob_size,
        const std::vector<BootstrapCapPathView> &dir_records,
        const std::vector<BootstrapCapPathView> &file_records, TaskSpec &spec) {
        auto validate_res =
            validate_bootstrap_blob(startup_blob, startup_blob_size);
        propagate(validate_res);

        size_t total_size = startup_blob_size;
        for (const auto &record : dir_records) {
            if (!valid_bootstrap_cap_path(record)) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            total_size += bootstrap_cap_path_record_size(record);
        }
        for (const auto &record : file_records) {
            if (!valid_bootstrap_cap_path(record)) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            total_size += bootstrap_cap_path_record_size(record);
        }

        if (total_size == 0) {
            if (spec.startup_blob != nullptr) {
                delete[] spec.startup_blob.get();
                spec.startup_blob      = util::owner<char *>(nullptr);
                spec.startup_blob_size = 0;
            }
            void_return();
        }

        auto merged = util::owner(new char[total_size]);
        if (merged.get() == nullptr) {
            unexpect_return(ErrCode::OUT_OF_MEMORY);
        }

        size_t offset = 0;
        std::vector<size_t> record_offsets{};
        if (startup_blob_size != 0) {
            bool copied = true;
            bool walked = bootstrap_foreach_record(
                startup_blob, startup_blob_size,
                [&](const BootstrapRecordView &view) {
                    if (!copied) {
                        return;
                    }
                    record_offsets.push_back(offset);
                    auto append_res = append_bootstrap_record(
                        merged.get(), total_size, offset, view.header->type,
                        view.data, view.data_size);
                    copied = append_res.has_value();
                });
            if (!walked || !copied) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
        }

        for (const auto &record : dir_records) {
            record_offsets.push_back(offset);
            auto append_res = append_bootstrap_cap_path_record(
                merged.get(), total_size, offset, BOOTSTRAP_TYPE_DIRCAPEXPLAIN,
                record);
            propagate(append_res);
        }
        for (const auto &record : file_records) {
            record_offsets.push_back(offset);
            auto append_res = append_bootstrap_cap_path_record(
                merged.get(), total_size, offset, BOOTSTRAP_TYPE_FILECAPEXPLAIN,
                record);
            propagate(append_res);
        }

        finalize_bootstrap_record_next(merged.get(), record_offsets);
        if (spec.startup_blob != nullptr) {
            delete[] spec.startup_blob.get();
        }
        spec.startup_blob      = std::move(merged);
        spec.startup_blob_size = total_size;
        void_return();
    }

    Result<void> TaskManager::load_task_spec(CapIdx image_cap,
                                             cap::CHolder *holder,
                                             const void *startup_blob,
                                             size_t startup_blob_size,
                                             TaskSpec &spec, LoadPrm &prm) {
        std::vector<BootstrapCapPathView> dir_records{};
        std::vector<BootstrapCapPathView> file_records{};
        auto bootstrap_res = build_bootstrap_blob(
            startup_blob, startup_blob_size, dir_records, file_records, spec);
        propagate(bootstrap_res);

        auto preload_res = holder == nullptr
                               ? preload(image_cap, spec, prm)
                               : preload_into(image_cap, holder, spec, prm);
        if (!preload_res.has_value()) {
            loggers::SUSTCORE::ERROR("预加载程序资源失败! 错误码: %s",
                                     to_cstring(preload_res.error()));
            propagate_return(preload_res);
        }

        auto load_res = loader::elf::load(spec, prm);
        if (!load_res.has_value()) {
            loggers::SUSTCORE::ERROR("加载ELF程序失败! 错误码: %s",
                                     to_cstring(load_res.error()));
            unexpect_return(ErrCode::CREATION_FAILED);
        }
        void_return();
    }

    Result<void> TaskManager::load_linux_task_spec(
        CapIdx image_cap, cap::CHolder *holder, CapIdx subsystem_image_cap,
        const void *startup_blob, size_t startup_blob_size, TaskSpec &spec,
        LoadPrm &prm) {
        if (holder == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }

        std::vector<BootstrapCapPathView> dir_records{};
        std::vector<BootstrapCapPathView> file_records{};
        auto bootstrap_res = build_bootstrap_blob(
            startup_blob, startup_blob_size, dir_records, file_records, spec);
        propagate(bootstrap_res);

        auto preload_res = preload_into(image_cap, holder, spec, prm);
        if (!preload_res.has_value()) {
            loggers::SUSTCORE::ERROR("预加载POSIX程序失败! 错误码: %s",
                                     to_cstring(preload_res.error()));
            propagate_return(preload_res);
        }

        auto load_linux_res = loader::elf::load_segments(spec, prm, true);
        if (!load_linux_res.has_value()) {
            loggers::SUSTCORE::ERROR("加载POSIX程序失败! 错误码: %s",
                                     to_cstring(load_linux_res.error()));
            unexpect_return(ErrCode::CREATION_FAILED);
        }

        VirAddr linux_entry = spec.entrypoint;
        LoadPrm subsystem_prm{
            .image_file_cap = subsystem_image_cap,
            .src_path       = "<cap>",
        };
        spec.entrypoint           = VirAddr(static_cast<addr_t>(0));
        spec.linuxproc_entrypoint = VirAddr(static_cast<addr_t>(0));
        auto load_subsystem_res =
            loader::elf::load_segments(spec, subsystem_prm, false);
        if (!load_subsystem_res.has_value()) {
            loggers::SUSTCORE::ERROR("加载POSIX子系统失败! 错误码: %s",
                                     to_cstring(load_subsystem_res.error()));
            unexpect_return(ErrCode::CREATION_FAILED);
        }

        spec.linuxproc_entrypoint = linux_entry;
        void_return();
    }
}  // namespace task
