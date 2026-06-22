/**
 * @file tmpfs.cpp
 * @author theflysong
 * @brief TmpFS 内存文件系统实现
 * @version alpha-1.0.0
 * @date 2026-06-11
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <guard.h>
#include <vfs/tmpfs.h>

#include <algorithm>
#include <cstring>

namespace tmpfs {
    namespace {
        [[nodiscard]]
        Result<void> validate_entry_name(std::string_view name) {
            if (name.empty() || name == "." || name == "..") {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            for (char ch : name) {
                if (ch == '/') {
                    unexpect_return(ErrCode::INVALID_PARAM);
                }
            }
            void_return();
        }
    }  // namespace

    TmpFSFile::TmpFSFile(TmpFSSuperblock &sb, TmpFSNode &node) noexcept
        : _sb(&sb), _node(&node) {}

    Result<size_t> TmpFSFile::read(off_t offset, void *buf, size_t len) {
        if (offset < 0 || buf == nullptr) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        const size_t start = static_cast<size_t>(offset);
        if (start >= _node->content.size()) {
            return 0;
        }

        const size_t readable = std::min(len, _node->content.size() - start);
        memcpy(buf, _node->content.data() + start, readable);
        return readable;
    }

    Result<size_t> TmpFSFile::write(off_t offset, const void *buf, size_t len) {
        if (offset < 0 || (len != 0 && buf == nullptr)) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        const size_t start = static_cast<size_t>(offset);
        const size_t end   = start + len;
        if (end < start) {
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }

        if (end > _node->content.size()) {
            _node->content.resize(end, 0);
        }
        if (len != 0) {
            memcpy(_node->content.data() + start, buf, len);
        }
        return len;
    }

    Result<size_t> TmpFSFile::size() {
        return _node->content.size();
    }

    Result<void> TmpFSFile::sync() {
        void_return();
    }

    IMetadata &TmpFSFile::metadata() {
        return _node->metadata;
    }

    inode_t TmpFSFile::inode_id() const {
        return _node->inode_id;
    }

    INodeCachePolicy TmpFSFile::inode_cache() const {
        return INodeCachePolicy::SHARED;
    }

    FileCachePolicy TmpFSFile::file_cache() const {
        return FileCachePolicy::PERMANENT;
    }

    TmpFSDirectory::TmpFSDirectory(TmpFSSuperblock &sb,
                                   TmpFSNode &node) noexcept
        : _sb(&sb), _node(&node) {}

    Result<inode_t> TmpFSDirectory::lookup(std::string_view name) {
        if (name.empty()) {
            return _node->inode_id;
        }

        auto it = _node->entries.find(std::string(name));
        if (it == _node->entries.end()) {
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }
        return it->second;
    }

    Result<inode_t> TmpFSDirectory::mkfile(std::string_view name,
                                           const char *options) {
        (void)options;
        return _sb->create_entry(*_node, name, INodeType::FILE);
    }

    Result<inode_t> TmpFSDirectory::mkdir(std::string_view name,
                                          const char *options) {
        (void)options;
        return _sb->create_entry(*_node, name, INodeType::DIRECTORY);
    }

    Result<inode_t> TmpFSDirectory::symlink(std::string_view name,
                                            std::string_view target) {
        if (target.empty()) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        auto inode_res = _sb->create_entry(*_node, name, INodeType::SYMLINK);
        propagate(inode_res);
        auto node_res = _sb->lookup_node(inode_res.value());
        propagate(node_res);
        node_res.value()->symlink_target.assign(target.data(), target.size());
        return inode_res.value();
    }

    Result<void> TmpFSDirectory::unlink(std::string_view name) {
        auto lookup_res = lookup(name);
        propagate(lookup_res);

        auto node_res = _sb->lookup_node(lookup_res.value());
        propagate(node_res);
        auto *target = node_res.value();
        if (target->type == INodeType::DIRECTORY) {
            unexpect_return(ErrCode::NOT_SUPPORTED);
        }

        auto erase_count = _node->entries.erase(std::string(name));
        if (erase_count == 0) {
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }
        auto free_res = _sb->free_inode(target->inode_id);
        propagate(free_res);
        void_return();
    }

    Result<void> TmpFSDirectory::rmdir(std::string_view name) {
        auto lookup_res = lookup(name);
        propagate(lookup_res);

        auto node_res = _sb->lookup_node(lookup_res.value());
        propagate(node_res);
        auto *target = node_res.value();
        if (target->type != INodeType::DIRECTORY) {
            unexpect_return(ErrCode::NOT_SUPPORTED);
        }
        if (!target->entries.empty()) {
            unexpect_return(ErrCode::BUSY);
        }

        auto erase_count = _node->entries.erase(std::string(name));
        if (erase_count == 0) {
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }
        auto free_res = _sb->free_inode(target->inode_id);
        propagate(free_res);
        void_return();
    }

    Result<size_t> TmpFSDirectory::entry_count() {
        return _node->entries.size();
    }

    Result<DirectoryEntryInfo> TmpFSDirectory::entry_at(size_t index) {
        if (index >= _node->entries.size()) {
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }

        auto it = _node->entries.begin();
        for (size_t i = 0; i < index; ++i) {
            ++it;
        }

        return DirectoryEntryInfo{
            .name = it->first,
        };
    }

    Result<void> TmpFSDirectory::sync() {
        void_return();
    }

    IMetadata &TmpFSDirectory::metadata() {
        return _node->metadata;
    }

    inode_t TmpFSDirectory::inode_id() const {
        return _node->inode_id;
    }

    INodeCachePolicy TmpFSDirectory::inode_cache() const {
        return INodeCachePolicy::SHARED;
    }

    TmpFSSuperblock::TmpFSSuperblock(TmpFSDriver &fs, size_t sb_id)
        : _fs(&fs), _sb_id(sb_id), _next_inode(1) {
        _nodes.insert_or_assign(0, TmpFSNode{
                                       .inode_id = 0,
                                       .type     = INodeType::DIRECTORY,
                                       .metadata = {},
                                       .entries  = {},
                                       .content  = {},
                                       .symlink_target = {},
                                   });
    }

    Result<TmpFSNode *> TmpFSSuperblock::lookup_node(
        inode_t inode_id) noexcept {
        auto node_res = _nodes.at_nt(inode_id);
        if (!node_res.has_value()) {
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }
        return node_res.value();
    }

    Result<inode_t> TmpFSSuperblock::create_entry(TmpFSNode &parent,
                                                  std::string_view name,
                                                  INodeType type) {
        auto name_res = validate_entry_name(name);
        propagate(name_res);

        if (parent.type != INodeType::DIRECTORY) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        const std::string entry_name(name);
        if (parent.entries.contains(entry_name)) {
            unexpect_return(ErrCode::KEY_DUPLICATED);
        }

        auto inode_res = alloc_inode(type);
        propagate(inode_res);
        const inode_t inode_id = inode_res.value();
        auto remove_guard      = util::Guard([this, inode_id]() {
            auto free_res = free_inode(inode_id);
            if (!free_res.has_value()) {
                loggers::VFS::WARN("TmpFS 回滚 inode 失败: inode=%u err=%s",
                                        static_cast<unsigned>(inode_id),
                                        to_cstring(free_res.error()));
            }
        });

        parent.entries.insert_or_assign(entry_name, inode_id);
        remove_guard.release();
        return inode_id;
    }

    IFsDriver &TmpFSSuperblock::fs() {
        return *_fs;
    }

    Result<void> TmpFSSuperblock::sync() {
        void_return();
    }

    Result<inode_t> TmpFSSuperblock::root() {
        return 0;
    }

    Result<util::owner<IINode *>> TmpFSSuperblock::get_inode(inode_t inode_id) {
        auto node_res = lookup_node(inode_id);
        propagate(node_res);
        TmpFSNode *node = node_res.value();

        if (node->type == INodeType::DIRECTORY) {
            auto *dir = new TmpFSDirectory(*this, *node);
            if (dir == nullptr) {
                unexpect_return(ErrCode::OUT_OF_MEMORY);
            }
            return util::owner<IINode *>(dir);
        }

        if (node->type == INodeType::SYMLINK) {
            class TmpFSSymlink final : public ISymlink {
            private:
                TmpFSNode *_node;

            public:
                explicit TmpFSSymlink(TmpFSNode &node) noexcept : _node(&node) {}

                [[nodiscard]]
                Result<std::string> target() override {
                    return _node->symlink_target;
                }
                [[nodiscard]]
                IMetadata &metadata() override {
                    return _node->metadata;
                }
                [[nodiscard]]
                inode_t inode_id() const override {
                    return _node->inode_id;
                }
                [[nodiscard]]
                INodeCachePolicy inode_cache() const override {
                    return INodeCachePolicy::SHARED;
                }
            };

            auto *symlink = new TmpFSSymlink(*node);
            if (symlink == nullptr) {
                unexpect_return(ErrCode::OUT_OF_MEMORY);
            }
            return util::owner<IINode *>(symlink);
        }

        auto *file = new TmpFSFile(*this, *node);
        if (file == nullptr) {
            unexpect_return(ErrCode::OUT_OF_MEMORY);
        }
        return util::owner<IINode *>(file);
    }

    Result<bool> TmpFSSuperblock::is_symlink(inode_t inode_id) {
        auto node_res = lookup_node(inode_id);
        propagate(node_res);
        return node_res.value()->type == INodeType::SYMLINK;
    }

    Result<std::string> TmpFSSuperblock::readlink(inode_t inode_id) {
        auto node_res = lookup_node(inode_id);
        propagate(node_res);
        if (node_res.value()->type != INodeType::SYMLINK) {
            unexpect_return(ErrCode::TYPE_NOT_MATCHED);
        }
        return node_res.value()->symlink_target;
    }

    Result<inode_t> TmpFSSuperblock::alloc_inode(INodeType type) {
        const inode_t inode_id = _next_inode++;
        _nodes.insert_or_assign(inode_id, TmpFSNode{
                                              .inode_id = inode_id,
                                              .type     = type,
                                              .metadata = {},
                                              .entries  = {},
                                              .content  = {},
                                              .symlink_target = {},
                                          });
        return inode_id;
    }

    Result<void> TmpFSSuperblock::free_inode(inode_t id) {
        if (id == 0 || !_nodes.contains(id)) {
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }
        _nodes.erase(id);
        void_return();
    }

    IMetadata &TmpFSSuperblock::metadata() {
        return _metadata;
    }

    size_t TmpFSSuperblock::sb_id() const {
        return _sb_id;
    }

    const char *TmpFSDriver::name() const {
        return "tmpfs";
    }

    Result<void> TmpFSDriver::probe(const char *name, const char *options) {
        (void)options;
        if (name == nullptr || std::string_view(name) != "tmpfs") {
            unexpect_return(ErrCode::NOT_SUPPORTED);
        }
        void_return();
    }

    Result<util::owner<ISuperblock *>> TmpFSDriver::mount(const char *name,
                                                          const char *options) {
        (void)options;
        if (name == nullptr || std::string_view(name) != "tmpfs") {
            unexpect_return(ErrCode::NOT_SUPPORTED);
        }
        if (_mount != nullptr) {
            unexpect_return(ErrCode::BUSY);
        }

        auto *sb = new TmpFSSuperblock(*this, _next_sb_id++);
        if (sb == nullptr) {
            unexpect_return(ErrCode::OUT_OF_MEMORY);
        }
        _mount = sb;
        return util::owner<ISuperblock *>(sb);
    }

    Result<void> TmpFSDriver::unmount(ISuperblock *sb) {
        if (_mount == sb) {
            _mount = nullptr;
        }
        void_return();
    }
}  // namespace tmpfs
