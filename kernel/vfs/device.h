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

#include <bio/blk.h>
#include <cap/capability.h>
#include <sus/owner.h>
#include <vfs/ops.h>

#include <string>
#include <unordered_map>

class VFile;

namespace devfs {
    constexpr const char *SYSFS_MOUNT_PATH = "/sys/";
    constexpr const char *DEVFS_MOUNT_PATH = "/sys/dev/";

    class CharDevFile;
    class DevFSDirectory;
    class DevFSUnboundFile;
    class DevFSSuperblock;
    class DevFSDriver;

    struct CharFactory {
        void *ctx;
        Result<util::owner<IINode *>> (*create)(void *ctx, inode_t inode_id);
    };

    class DevFileMetadata : public IMetadata {
    public:
        bool is_blk;
        constexpr DevFileMetadata(bool is_blk) : is_blk(is_blk) {}
    };

    class CharDevFile : public IFile {
    protected:
        inode_t _inode_id;
        DevFileMetadata _metadata = {false};

        explicit CharDevFile(inode_t inode_id) : _inode_id(inode_id) {}

    public:
        virtual ~CharDevFile() = default;

        [[nodiscard]]
        virtual Result<size_t> read(void *buf, size_t len);

        [[nodiscard]]
        virtual Result<size_t> write(const void *buf, size_t len) = 0;

        [[nodiscard]]
        Result<size_t> read(off_t offset, void *buf, size_t len) override;

        [[nodiscard]]
        Result<size_t> write(off_t offset, const void *buf,
                             size_t len) override;

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

    class BlockDevFile : public IFile {
    private:
        inode_t _inode_id;
        size_t _devno;

        DevFileMetadata _metadata = {true};

    public:
        explicit BlockDevFile(inode_t inode_id, size_t devno)
            : _inode_id(inode_id), _devno(devno) {}

        virtual ~BlockDevFile() = default;

        [[nodiscard]]
        Result<size_t> read(off_t offset, void *buf, size_t len) override;

        [[nodiscard]]
        Result<size_t> write(off_t offset, const void *buf,
                             size_t len) override;

        [[nodiscard]]
        Result<size_t> size() override;

        [[nodiscard]]
        Result<void> sync() override;

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

        [[nodiscard]]
        size_t devno() const {
            return _devno;
        }
    };

    class DevFSDirectory final : public IDirectory {
    private:
        DevFSSuperblock *_sb;
        inode_t _inode_id;
        struct EmptyMetadata : public IMetadata {
        } _metadata;

    public:
        DevFSDirectory(DevFSSuperblock &sb, inode_t inode_id) noexcept;
        ~DevFSDirectory() final = default;

        [[nodiscard]]
        Result<inode_t> lookup(std::string_view name) final;
        [[nodiscard]]
        Result<inode_t> mkfile(std::string_view name,
                               const char *options) final;
        [[nodiscard]]
        Result<inode_t> mkdir(std::string_view name, const char *options) final;
        [[nodiscard]]
        Result<size_t> entry_count() final;
        [[nodiscard]]
        Result<DirectoryEntryInfo> entry_at(size_t index) final;
        [[nodiscard]]
        Result<void> sync() final;
        [[nodiscard]]
        IMetadata &metadata() final;
        [[nodiscard]]
        inode_t inode_id() const final;
        [[nodiscard]]
        INodeCachePolicy inode_cache() const final;
    };

    class DevFSUnboundFile final : public IFile {
    private:
        inode_t _inode_id;
        struct EmptyMetadata : public IMetadata {
        } _metadata;

    public:
        explicit DevFSUnboundFile(inode_t inode_id) noexcept;
        ~DevFSUnboundFile() final = default;

        [[nodiscard]]
        Result<size_t> read(off_t offset, void *buf, size_t len) override;
        [[nodiscard]]
        Result<size_t> write(off_t offset, const void *buf,
                             size_t len) override;
        [[nodiscard]]
        Result<size_t> size() override;
        [[nodiscard]]
        Result<void> sync() override;
        [[nodiscard]]
        IMetadata &metadata() override;
        [[nodiscard]]
        inode_t inode_id() const override;
        [[nodiscard]]
        INodeCachePolicy inode_cache() const override;
        [[nodiscard]]
        FileCachePolicy file_cache() const override;
    };

    class DevFSSuperblock final : public ISuperblock {
    public:
        enum class NodeState {
            DIRECTORY,
            UNBOUND_FILE,
            CHAR_DEVICE,
            BLOCK_DEVICE
        };

        struct NodeRecord {
            inode_t inode_id;
            NodeState state;
            std::string name;
            inode_t parent_inode;
            std::unordered_map<std::string, inode_t> entries;
            CharFactory char_factory{};
            bool has_factory = false;
            size_t block_devno = 0;
            bool has_block_devno = false;
            struct EmptyMetadata : public IMetadata {
            } metadata;
        };

    private:
        DevFSDriver *_fs;
        size_t _sb_id;
        inode_t _next_inode = 1;
        std::unordered_map<inode_t, NodeRecord> _nodes;
        struct EmptyMetadata : public IMetadata {
        } _metadata;

        [[nodiscard]]
        Result<NodeRecord *> lookup_record(inode_t inode_id);
        [[nodiscard]]
        Result<const NodeRecord *> lookup_record(inode_t inode_id) const;
        [[nodiscard]]
        Result<inode_t> create_node(inode_t parent_inode, std::string_view name,
                                    NodeState state);

    public:
        DevFSSuperblock(DevFSDriver *fs, size_t sb_id);
        ~DevFSSuperblock() final = default;

        [[nodiscard]]
        IFsDriver &fs() final;
        [[nodiscard]]
        Result<void> sync() final;
        [[nodiscard]]
        Result<inode_t> root() final;
        [[nodiscard]]
        Result<util::owner<IINode *>> get_inode(inode_t inode_id) final;
        [[nodiscard]]
        IMetadata &metadata() final;
        [[nodiscard]]
        size_t sb_id() const final;
        [[nodiscard]]
        Result<inode_t> mkdir(inode_t parent_inode, std::string name);
        [[nodiscard]]
        Result<inode_t> mkfile(inode_t parent_inode, std::string name);
        [[nodiscard]]
        Result<void> link_char(cap::Capability &filecap, CharFactory factory);
        [[nodiscard]]
        Result<void> link_block(cap::Capability &filecap, size_t devno);
        [[nodiscard]]
        Result<inode_t> alloc_inode(INodeType type) final;
        [[nodiscard]]
        Result<void> free_inode(inode_t id) final;
        [[nodiscard]]
        Result<inode_t> lookup(inode_t parent_inode, std::string_view name);
        [[nodiscard]]
        Result<IMetadata *> inode_metadata(inode_t inode_id);

        friend class DevFSDirectory;
    };

    class DevFSDriver final : public IPesudoFsDriver {
    private:
        size_t _next_sb_id        = 1;
        DevFSSuperblock *_mounted = nullptr;

    public:
        ~DevFSDriver() final = default;

        [[nodiscard]]
        const char *name() const final;
        [[nodiscard]]
        Result<void> probe(const char *name, const char *options) final;
        [[nodiscard]]
        Result<util::owner<ISuperblock *>> mount(const char *name,
                                                 const char *options) final;
        [[nodiscard]]
        Result<void> unmount(ISuperblock *sb) final;
        [[nodiscard]]
        Result<DevFSSuperblock *> mounted_superblock() const;
    };
}  // namespace devfs
