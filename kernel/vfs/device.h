/**
 * @file device.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 设备文件系统
 * @version alpha-1.0.0
 * @date 2026-06-05
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <logger.h>
#include <sus/owner.h>
#include <vfs/ops.h>

#include <string>
#include <unordered_map>

namespace devfs {
    class CharDevFile;
    class DevFSDirectory;
    class DevFSSuperblock;
    class DevFSDriver;

    struct CharFactory {
        void *ctx;
        Result<util::owner<IINode *>> (*create)(void *ctx, inode_t inode_id);
    };

    class CharDevFile : public IFile {
    protected:
        inode_t _inode_id;
        struct EmptyMetadata : public IMetadata {
        } _metadata;

        explicit CharDevFile(inode_t inode_id) : _inode_id(inode_id) {}

    public:
        virtual ~CharDevFile() = default;

        [[nodiscard]]
        virtual Result<size_t> read(void *buf, size_t len) {
            (void)buf;
            (void)len;
            unexpect_return(ErrCode::NOT_SUPPORTED);
        }

        [[nodiscard]]
        virtual Result<size_t> write(const void *buf, size_t len) = 0;

        [[nodiscard]]
        Result<size_t> read(off_t offset, void *buf, size_t len) override {
            if (offset != 0) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            return read(buf, len);
        }

        [[nodiscard]]
        Result<size_t> write(off_t offset, const void *buf,
                             size_t len) override {
            if (offset != 0) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            return write(buf, len);
        }

        [[nodiscard]]
        Result<size_t> size() override {
            return 0;
        }

        [[nodiscard]]
        Result<void> sync() override {
            void_return();
        }

        [[nodiscard]]
        inode_t inode_id() const override {
            return _inode_id;
        }

        [[nodiscard]]
        INodeCachePolicy inode_cache() const final {
            return INodeCachePolicy::PERMANENT;
        }

        [[nodiscard]]
        FileCachePolicy file_cache() const final {
            return FileCachePolicy::NONE;
        }

        [[nodiscard]]
        IMetadata &metadata() override {
            return _metadata;
        }
    };

    class DevFSDirectory final : public IDirectory {
    private:
        inode_t _inode_id;
        std::string _name;
        std::unordered_map<std::string, inode_t> _entries;
        struct EmptyMetadata : public IMetadata {
        } _metadata;

    public:
        ~DevFSDirectory() final = default;

        explicit DevFSDirectory(inode_t inode_id, std::string name)
            : _inode_id(inode_id), _name(std::move(name)) {}

        [[nodiscard]]
        Result<inode_t> lookup(std::string_view name) final {
            auto it = _entries.find(std::string(name));
            if (it == _entries.end()) {
                unexpect_return(ErrCode::ENTRY_NOT_FOUND);
            }
            return it->second;
        }

        [[nodiscard]]
        Result<void> sync() final {
            void_return();
        }

        [[nodiscard]]
        Result<inode_t> mkfile(std::string_view name,
                               const char *options) override {
            unexpect_return(ErrCode::NOT_SUPPORTED);
        }
        [[nodiscard]]
        Result<inode_t> mkdir(std::string_view name,
                              const char *options) override {
            unexpect_return(ErrCode::NOT_SUPPORTED);
        }

        [[nodiscard]]
        Result<void> add_entry(std::string name, inode_t inode_id) {
            if (_entries.contains(name)) {
                unexpect_return(ErrCode::KEY_DUPLICATED);
            }
            _entries.insert_or_assign(std::move(name), inode_id);
            void_return();
        }

        [[nodiscard]]
        IMetadata &metadata() final {
            return _metadata;
        }

        [[nodiscard]]
        inode_t inode_id() const final {
            return _inode_id;
        }

        [[nodiscard]]
        INodeCachePolicy inode_cache() const final {
            return INodeCachePolicy::PERMANENT;
        }
    };

    class DevFSSuperblock final : public ISuperblock {
    private:
        DevFSDriver *_fs;
        size_t _sb_id;
        inode_t _next_inode = 1;
        DevFSDirectory *_root;
        std::unordered_map<inode_t, util::owner<DevFSDirectory *>> _directories;
        std::unordered_map<inode_t, CharFactory> _char_factories;
        struct EmptyMetadata : public IMetadata {
        } _metadata;

    public:
        DevFSSuperblock(DevFSDriver *fs, size_t sb_id);

        ~DevFSSuperblock() final = default;

        [[nodiscard]]
        IFsDriver &fs() final;

        [[nodiscard]]
        Result<void> sync() final {
            void_return();
        }

        [[nodiscard]]
        Result<inode_t> root() final {
            return _root->inode_id();
        }

        [[nodiscard]]
        Result<util::owner<IINode *>> get_inode(inode_t inode_id) final;

        [[nodiscard]]
        IMetadata &metadata() final {
            return _metadata;
        }

        [[nodiscard]]
        size_t sb_id() const final {
            return _sb_id;
        }

        [[nodiscard]]
        Result<inode_t> mkdir(inode_t parent_inode, std::string name);

        [[nodiscard]]
        Result<inode_t> register_char(inode_t parent_inode, std::string name,
                                      CharFactory factory);

        [[nodiscard]]
        Result<inode_t> ensure_dir(inode_t parent_inode, std::string_view name);

        [[nodiscard]]
        Result<inode_t> alloc_inode(INodeType type) final {
            unexpect_return(ErrCode::NOT_SUPPORTED);
        }

        [[nodiscard]]
        Result<void> free_inode(inode_t id) final {
            unexpect_return(ErrCode::NOT_SUPPORTED);
        }
    };

    class DevFSDriver final : public IPesudoFsDriver {
    private:
        size_t _next_sb_id        = 1;
        DevFSSuperblock *_mounted = nullptr;

    public:
        ~DevFSDriver() final = default;

        [[nodiscard]]
        const char *name() const final {
            return "devfs";
        }

        [[nodiscard]]
        Result<void> probe(const char *name, const char *options) final {
            (void)options;
            if (std::string_view(name) != "devfs") {
                unexpect_return(ErrCode::NOT_SUPPORTED);
            }
            void_return();
        }

        [[nodiscard]]
        Result<util::owner<ISuperblock *>> mount(const char *name,
                                                 const char *options) final {
            (void)options;
            if (std::string_view(name) != "devfs") {
                unexpect_return(ErrCode::NOT_SUPPORTED);
            }
            if (_mounted != nullptr) {
                unexpect_return(ErrCode::BUSY);
            }
            auto *sb = new DevFSSuperblock(this, _next_sb_id++);
            if (sb == nullptr) {
                unexpect_return(ErrCode::OUT_OF_MEMORY);
            }
            _mounted = sb;
            return util::owner<ISuperblock *>(sb);
        }

        [[nodiscard]]
        Result<void> unmount(ISuperblock *sb) final {
            if (_mounted == sb) {
                _mounted = nullptr;
            }
            void_return();
        }

        [[nodiscard]]
        Result<DevFSSuperblock *> mounted_superblock() const {
            if (_mounted == nullptr) {
                unexpect_return(ErrCode::ENTRY_NOT_FOUND);
            }
            return _mounted;
        }
    };

    inline DevFSSuperblock::DevFSSuperblock(DevFSDriver *fs, size_t sb_id)
        : _fs(fs), _sb_id(sb_id), _root(new DevFSDirectory(0, "/")) {
        _directories.insert_or_assign(0, util::owner(_root));
    }

    inline IFsDriver &DevFSSuperblock::fs() {
        return *_fs;
    }

    inline Result<util::owner<IINode *>> DevFSSuperblock::get_inode(
        inode_t inode_id) {
        auto dir_it = _directories.find(inode_id);
        if (dir_it != _directories.end()) {
            return util::owner<IINode *>(dir_it->second.get());
        }

        auto char_it = _char_factories.find(inode_id);
        if (char_it != _char_factories.end()) {
            return char_it->second.create(char_it->second.ctx, inode_id);
        }

        unexpect_return(ErrCode::ENTRY_NOT_FOUND);
    }

    inline Result<inode_t> DevFSSuperblock::mkdir(inode_t parent_inode,
                                                  std::string name) {
        auto parent_it = _directories.find(parent_inode);
        if (parent_it == _directories.end()) {
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }

        inode_t inode_id = _next_inode++;
        auto dir         = util::owner(new DevFSDirectory(inode_id, name));
        auto add_res     = parent_it->second->add_entry(name, inode_id);
        propagate(add_res);
        _directories.insert_or_assign(inode_id, dir);
        return inode_id;
    }

    inline Result<inode_t> DevFSSuperblock::register_char(inode_t parent_inode,
                                                          std::string name,
                                                          CharFactory factory) {
        auto parent_it = _directories.find(parent_inode);
        if (parent_it == _directories.end()) {
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }

        inode_t inode_id = _next_inode++;
        auto add_res     = parent_it->second->add_entry(name, inode_id);
        propagate(add_res);
        _char_factories.insert_or_assign(inode_id, std::move(factory));
        return inode_id;
    }

    inline Result<inode_t> DevFSSuperblock::ensure_dir(inode_t parent_inode,
                                                       std::string_view name) {
        auto parent_it = _directories.find(parent_inode);
        if (parent_it == _directories.end()) {
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }
        auto lookup_res = parent_it->second->lookup(name);
        if (lookup_res.has_value()) {
            return lookup_res.value();
        }
        if (lookup_res.error() != ErrCode::ENTRY_NOT_FOUND) {
            propagate_return(lookup_res);
        }
        return mkdir(parent_inode, std::string(name));
    }
}  // namespace devfs
