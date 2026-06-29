/**
 * @file tmpfs.h
 * @author theflysong
 * @brief TmpFS 内存文件系统
 * @version alpha-1.0.0
 * @date 2026-06-11
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <sus/types.h>
#include <vfs/ops.h>

#include <cstddef>
#include <string>
#include <unordered_map>

namespace tmpfs {
    class TmpFSDriver;
    class TmpFSSuperblock;

    struct TmpFSMetadata final : public IMetadata {};

    struct TmpFSNode {
        inode_t inode_id = 0;
        INodeType type   = INodeType::FILE;
        TmpFSMetadata metadata;
        std::unordered_map<std::string, inode_t> entries;
        char *content = nullptr;
        size_t content_sz = 0;
        std::string symlink_target;

        TmpFSNode() = default;
        ~TmpFSNode() {
            delete[] content;
        }

        TmpFSNode(const TmpFSNode &)            = delete;
        TmpFSNode &operator=(const TmpFSNode &) = delete;

        TmpFSNode(TmpFSNode &&other) noexcept
            : inode_id(other.inode_id), type(other.type),
              metadata(std::move(other.metadata)),
              entries(std::move(other.entries)), content(other.content),
              content_sz(other.content_sz),
              symlink_target(std::move(other.symlink_target)) {
            other.content    = nullptr;
            other.content_sz = 0;
        }

        TmpFSNode &operator=(TmpFSNode &&other) noexcept {
            if (this == &other) {
                return *this;
            }
            delete[] content;
            inode_id       = other.inode_id;
            type           = other.type;
            metadata       = std::move(other.metadata);
            entries        = std::move(other.entries);
            content        = other.content;
            content_sz     = other.content_sz;
            symlink_target = std::move(other.symlink_target);
            other.content  = nullptr;
            other.content_sz = 0;
            return *this;
        }
    };

    class TmpFSFile final : public IFile {
    private:
        TmpFSSuperblock *_sb;
        TmpFSNode *_node;

    public:
        TmpFSFile(TmpFSSuperblock &sb, TmpFSNode &node) noexcept;
        ~TmpFSFile() final = default;

        [[nodiscard]]
        Result<size_t> read(off_t offset, void *buf, size_t len) override;
        [[nodiscard]]
        Result<size_t> write(off_t offset, const void *buf,
                             size_t len) override;
        [[nodiscard]]
        Result<size_t> size() override;
        [[nodiscard]]
        Result<void> truncate(size_t new_size) override;
        [[nodiscard]]
        Result<void> ioctl(size_t cmd, syscall::UBuffer &&arg) override;
        [[nodiscard]]
        Result<void> sync() override;
        [[nodiscard]]
        Result<void> getattr(AttrSet &out) const override;
        [[nodiscard]]
        Result<void> setattr(AttrMask mask, const AttrSet &attrs) override;
        [[nodiscard]]
        IMetadata &metadata() override;
        [[nodiscard]]
        inode_t inode_id() const override;
        [[nodiscard]]
        INodeCachePolicy inode_cache() const override;
        [[nodiscard]]
        FileCachePolicy file_cache() const override;
    };

    class TmpFSDirectory final : public IDirectory {
    private:
        TmpFSSuperblock *_sb;
        TmpFSNode *_node;

    public:
        TmpFSDirectory(TmpFSSuperblock &sb, TmpFSNode &node) noexcept;
        ~TmpFSDirectory() final = default;

        [[nodiscard]]
        Result<inode_t> lookup(std::string_view name) override;
        [[nodiscard]]
        Result<inode_t> mkfile(std::string_view name,
                               const char *options) override;
        [[nodiscard]]
        Result<inode_t> mkdir(std::string_view name,
                              const char *options) override;
        [[nodiscard]]
        Result<inode_t> symlink(std::string_view name,
                                std::string_view target) override;
        [[nodiscard]]
        Result<void> link(std::string_view name, inode_t target) override;
        [[nodiscard]]
        Result<void> rename(std::string_view old_name, IDirectory &new_parent,
                            std::string_view new_name) override;
        [[nodiscard]]
        Result<void> unlink(std::string_view name) override;
        [[nodiscard]]
        Result<void> rmdir(std::string_view name) override;
        [[nodiscard]]
        Result<size_t> entry_count() override;
        [[nodiscard]]
        Result<DirectoryEntryInfo> entry_at(size_t index) override;
        [[nodiscard]]
        Result<void> sync() override;
        [[nodiscard]]
        Result<void> getattr(AttrSet &out) const override;
        [[nodiscard]]
        Result<void> setattr(AttrMask mask, const AttrSet &attrs) override;
        [[nodiscard]]
        IMetadata &metadata() override;
        [[nodiscard]]
        inode_t inode_id() const override;
        [[nodiscard]]
        INodeCachePolicy inode_cache() const override;
    };

    class TmpFSSuperblock final : public ISuperblock {
    private:
        TmpFSDriver *_fs;
        size_t _sb_id;
        inode_t _next_inode;
        std::unordered_map<inode_t, TmpFSNode> _nodes;
        TmpFSMetadata _metadata;

        [[nodiscard]]
        Result<TmpFSNode *> lookup_node(inode_t inode_id) noexcept;
        [[nodiscard]]
        Result<inode_t> create_entry(TmpFSNode &parent, std::string_view name,
                                     INodeType type);

    public:
        TmpFSSuperblock(TmpFSDriver &fs, size_t sb_id);
        ~TmpFSSuperblock() final = default;

        [[nodiscard]]
        IFsDriver &fs() final;
        [[nodiscard]]
        Result<void> sync() final;
        [[nodiscard]]
        Result<inode_t> root() final;
        [[nodiscard]]
        Result<util::owner<IINode *>> get_inode(inode_t inode_id) final;
        [[nodiscard]]
        Result<uint16_t> inode_mode(inode_t inode_id) final;
        [[nodiscard]]
        Result<bool> is_symlink(inode_t inode_id) final;
        [[nodiscard]]
        Result<std::string> readlink(inode_t inode_id) final;
        [[nodiscard]]
        Result<inode_t> alloc_inode(INodeType type) final;
        [[nodiscard]]
        Result<void> free_inode(inode_t id) final;
        [[nodiscard]]
        IMetadata &metadata() final;
        [[nodiscard]]
        size_t sb_id() const final;

        friend class TmpFSFile;
        friend class TmpFSDirectory;
    };

    class TmpFSDriver final : public IPesudoFsDriver {
    private:
        size_t _next_sb_id      = 1;
        TmpFSSuperblock *_mount = nullptr;

    public:
        ~TmpFSDriver() final = default;

        [[nodiscard]]
        const char *name() const final;
        [[nodiscard]]
        Result<void> probe(const char *name, const char *options) final;
        [[nodiscard]]
        Result<util::owner<ISuperblock *>> mount(const char *name,
                                                 const char *options) final;
        [[nodiscard]]
        Result<void> unmount(ISuperblock *sb) final;
    };
}  // namespace tmpfs
