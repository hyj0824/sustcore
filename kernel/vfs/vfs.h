/**
 * @file vfs.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief Virtual File System
 * @version alpha-1.0.0
 * @date 2026-02-04
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cap/capability.h>
#include <cap/cholder.h>
#include <bio/blk.h>
#include <logger.h>
#include <sus/nonnull.h>
#include <sus/owner.h>
#include <sus/path.h>
#include <sus/refcount.h>
#include <sustcore/errcode.h>
#include <sustcore/files.h>
#include <vfs/ops.h>

#include <string>
#include <unordered_map>

class VFsDriver;
class VSuperblock;
class VINode;
class VFile;
class VDirectory;
class VFS;

class VFsDriver : public util::refc<VFsDriver> {
public:
    constexpr void on_death() {}
    constexpr bool closable() const {
        return !alive();
    }

private:
    util::owner<IFsDriver *> _fsd;

public:
    constexpr VFsDriver(util::owner<IFsDriver *> fsd) : _fsd(fsd) {}
    constexpr virtual ~VFsDriver() {
        delete _fsd;
    }
    constexpr IFsDriver *fsd() const {
        return _fsd.get();
    }
};

class VSuperblock : public util::refc<VSuperblock> {
public:
    constexpr bool closable() const {
        return !alive();
    }

private:
    util::owner<ISuperblock *> _sb;
    util::refc_ptr<VFsDriver> _fsd;
    std::unordered_map<inode_t, VINode *> _inode_cache;

public:
    VSuperblock(util::owner<ISuperblock *> sb, VFsDriver &fsd)
        : _sb(sb), _fsd(&fsd) {}
    virtual ~VSuperblock();
    constexpr ISuperblock *sb() const {
        return _sb.get();
    }
    constexpr VFsDriver &vfsd() const {
        return *_fsd;
    }
    Result<util::refc_ptr<VINode>> get_vnode(inode_t inode_id);
    void on_death();
};

class VINode : public util::refc<VINode> {
public:
    static constexpr PayloadType IDENTIFIER = PayloadType::VFILE;

private:
    util::owner<IINode *> _inode;
    util::refc_ptr<VFsDriver> _fsd;
    util::refc_ptr<VSuperblock> _vsb;

public:
    void on_death() {
        delete this;
    }

    constexpr IINode *inode() const {
        return _inode.get();
    }
    constexpr VFsDriver &vfsd() const {
        return *_fsd;
    }
    constexpr VSuperblock &superblock() const {
        return *_vsb;
    }

    constexpr VINode(util::owner<IINode *> inode, VFsDriver &fsd,
                     VSuperblock &vsb)
        : _inode(inode), _fsd(&fsd), _vsb(&vsb) {}
    constexpr virtual ~VINode() {
        delete _inode;
    }
};

class VFile : public cap::_PayloadHelper<PayloadType::VFILE> {
private:
    util::refc_ptr<VINode> _vind;
    util::Path _mount_path;
    VFS *_vfs;

public:
    VFile(VINode &vind, const util::Path &mount_path, VFS &vfs);
    ~VFile() override = default;
    void destruct() override;

    [[nodiscard]]
    util::nonnull<VINode *> vinode() const {
        return util::nnullforce(_vind.get());
    }

    [[nodiscard]]
    const util::Path &mount_path() const {
        return _mount_path;
    }
};

class VDirectory : public cap::_PayloadHelper<PayloadType::VDIR> {
private:
    util::refc_ptr<VINode> _vind;
    util::Path _mount_path;
    VFS *_vfs;

public:
    VDirectory(VINode &vind, const util::Path &mount_path, VFS &vfs);
    ~VDirectory() override = default;
    void destruct() override;

    [[nodiscard]]
    util::nonnull<VINode *> vinode() const {
        return util::nnullforce(_vind.get());
    }

    [[nodiscard]]
    const util::Path &mount_path() const {
        return _mount_path;
    }
};

enum class MountFlags { NONE = 0 };

class VFS {
private:
    struct MountRecord {
        util::owner<VSuperblock *> superblock;
        size_t devno = 0;
        bool is_block_mount = false;
        size_t active_files = 0;
    };

    std::unordered_map<std::string, util::owner<VFsDriver *>> fs_table;
    std::unordered_map<util::Path, MountRecord> mount_table;

    [[nodiscard]]
    Result<util::refc_ptr<VINode>> _resolve_from(
        util::refc_ptr<VINode> base, const util::Path &base_path,
        const util::Path &path, VSuperblock *vsb) const;

    [[nodiscard]]
    Result<VFile *> _open_file_at(VINode &parent, const util::Path &mount_path,
                                  const char *relpath);

    [[nodiscard]]
    Result<VDirectory *> _open_dir_at(VINode &parent,
                                      const util::Path &mount_path,
                                      const char *relpath);

    [[nodiscard]]
    Result<VFile *> _open_file(const char *filepath);

    [[nodiscard]]
    Result<VDirectory *> _open_dir(const char *filepath);

    [[nodiscard]]
    Result<std::pair<util::Path, VSuperblock *>> _resolve_mount(
        const util::Path &path);

    [[nodiscard]]
    Result<MountRecord *> _lookup_mount_record(const util::Path &mount_path);

    [[nodiscard]]
    Result<util::refc_ptr<VINode>> _resolve_inode(const util::Path &path,
                                                  util::Path &mount_path);

    void _on_vfile_destroy(const util::Path &mount_path) noexcept;

public:
    static void init();
    static bool initialized();
    static VFS &inst();

    VFS() = default;
    ~VFS();

    VFS(const VFS &)            = delete;
    VFS &operator=(const VFS &) = delete;
    VFS(VFS &&)                 = delete;
    VFS &operator=(VFS &&)      = delete;

    // 注册文件系统
    Result<void> register_fs(util::owner<IFsDriver *> &&driver);
    Result<void> unregister_fs(const char *fs_name);
    // 挂载文件系统
    Result<void> mount(const char *fs_name, size_t devno,
                       const char *mountpoint, MountFlags flags,
                       const char *options);
    Result<void> mount(const char *fs_name, const char *mountpoint,
                       const char *options);
    Result<void> umount(const char *mountpoint);
    // 打开文件并直接插入到指定 holder 中
    Result<CapIdx> open(const char *filepath, cap::CHolder &holder);
    [[nodiscard]]
    Result<CapIdx> open(cap::Capability &parent_dir_cap, const char *relpath,
                        flags::oflg_t oflags, cap::CHolder &holder);
    [[nodiscard]]
    Result<CapIdx> opendir(cap::Capability &parent_dir_cap, const char *relpath,
                           flags::oflg_t oflags, cap::CHolder &holder);
    [[nodiscard]]
    Result<CapIdx> mkfile(cap::Capability &parent_dir_cap, const char *relpath,
                          flags::oflg_t oflags, cap::CHolder &holder);
    [[nodiscard]]
    Result<CapIdx> mkdir(cap::Capability &parent_dir_cap, const char *relpath,
                         flags::oflg_t oflags, cap::CHolder &holder);
    [[nodiscard]]
    Result<CapIdx> open_dir(const char *filepath, cap::CHolder &holder,
                            b64 perm);
    // 仅供测试代码使用的调试接口
    Result<VFile *> __debug_open(const char *filepath);

public:
    // 读取文件内容到buf中, 返回实际读取的字节数
    Result<size_t> read(VFile &vfile, off_t offset, void *buf, size_t len) const;
    // 将buf中的内容写入文件, 返回实际写入的字节数
    Result<size_t> write(VFile &vfile, off_t offset, const void *buf,
                         size_t len) const;
    // 获取文件大小
    Result<size_t> size(VFile &vfile) const;
    // 刷新文件内容到存储设备
    Result<void> sync(VFile &vfile) const;
    Result<void> sync(VDirectory &vdir) const;

    friend class VFile;
    friend class VDirectory;
};
