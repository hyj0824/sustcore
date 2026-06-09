/**
 * @file vfs.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief VFS
 * @version alpha-1.0.0
 * @date 2026-02-04
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <bio/blk.h>
#include <cap/cholder.h>
#include <sus/path.h>
#include <sustcore/errcode.h>
#include <sustcore/files.h>
#include <task/wait.h>
#include <vfs/ops.h>
#include <vfs/vfs.h>

#include <cassert>
#include <cstring>
#include <utility>

namespace {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static VFS inst_vfs;
    static bool inst_vfs_initialized = false;
}  // namespace

VSuperblock::~VSuperblock() {
    on_death();
}

Result<util::refc_ptr<VINode>> VSuperblock::get_vnode(inode_t inode_id) {
    auto cache_res = _inode_cache.at_nt(inode_id);
    if (cache_res.has_value()) {
        VINode *cached = cache_res.value().get();
        if (cached != nullptr) {
            return util::refc_ptr(cached);
        }
        _inode_cache.erase(inode_id);
    }

    auto inode_res = sb()->get_inode(inode_id);
    propagate(inode_res);

    auto *vnode = new VINode(inode_res.value(), vfsd(), *this);
    if (vnode == nullptr) {
        unexpect_return(ErrCode::OUT_OF_MEMORY);
    }

    auto policy = vnode->inode()->inode_cache();
    if (policy != INodeCachePolicy::NONE) {
        vnode->keep();
        _inode_cache.insert_or_assign(inode_id, vnode);
    }

    return util::refc_ptr(vnode);
}

void VSuperblock::on_death() {
    for (auto &entry : _inode_cache) {
        if (entry.second != nullptr) {
            entry.second->release();
        }
    }
    _inode_cache.clear();
    delete _sb.get();
    _sb = util::owner<ISuperblock *>(nullptr);
}

VFile::VFile(VINode &vind, const util::Path &mount_path, VFS &vfs)
    : _vind(&vind), _mount_path(mount_path), _vfs(&vfs) {}

void VFile::destruct() {
    if (_vfs != nullptr) {
        _vfs->_on_vfile_destroy(_mount_path);
        _vfs = nullptr;
    }
    delete this;
}

VDirectory::VDirectory(VINode &vind, const util::Path &mount_path, VFS &vfs)
    : _vind(&vind), _mount_path(mount_path), _vfs(&vfs) {}

void VDirectory::destruct() {
    if (_vfs != nullptr) {
        _vfs->_on_vfile_destroy(_mount_path);
        _vfs = nullptr;
    }
    delete this;
}

void VFS::init() {
    // call the constructor explicitly to ensure the instance is initialized
    // before use
    new (&inst_vfs) VFS();
    inst_vfs_initialized = true;
}

bool VFS::initialized() {
    return inst_vfs_initialized;
}

VFS &VFS::inst() {
    if (!initialized()) {
        panic("VFS 未初始化!");
    }
    return inst_vfs;
}

Result<IDirectory *> IINode::as_directory() {
    IDirectory *dir = this->as<IDirectory>();
    if (dir) {
        return dir;
    } else {
        unexpect_return(ErrCode::INVALID_PARAM);
    }
}

Result<IFile *> IINode::as_file() {
    IFile *file = this->as<IFile>();
    if (file) {
        return file;
    } else {
        unexpect_return(ErrCode::INVALID_PARAM);
    }
}

VFS::~VFS() = default;

Result<void> VFS::register_fs(util::owner<IFsDriver *> &&driver) {
    const char *fs_name = driver->name();
    if (fs_table.contains(fs_name)) {
        unexpect_return(ErrCode::INVALID_PARAM);
    }
    fs_table.insert_or_assign(fs_name,
                              util::owner(new VFsDriver(std::move(driver))));
    return {};
}

Result<void> VFS::unregister_fs(const char *fs_name) {
    if (!fs_table.contains(fs_name)) {
        unexpect_return(ErrCode::INVALID_PARAM);
    }

    auto get_res = fs_table.at_nt(fs_name);
    if (!get_res.has_value()) {
        unexpect_return(ErrCode::UNKNOWN_ERROR);
    }

    util::owner<VFsDriver *> driver = get_res.value().get();
    if (!driver->closable()) {
        unexpect_return(ErrCode::BUSY);
    }

    fs_table.erase(fs_name);
    delete driver;
    void_return();
}

Result<void> VFS::mount(const char *fs_name, size_t devno,
                        const char *mountpoint, MountFlags flags,
                        const char *options) {
    (void)flags;
    auto dev_res = blk::BlkManager::inst().lookup(devno);
    if (!dev_res.has_value()) {
        propagate_return(dev_res);
    }
    util::Path mnt_path = util::Path::normalize(mountpoint);
    if (mount_table.contains(mnt_path)) {
        unexpect_return(ErrCode::INVALID_PARAM);
    }

    auto lookup_result = fs_table.at_nt(fs_name);
    if (!lookup_result.has_value()) {
        unexpect_return(ErrCode::INVALID_PARAM);
    }
    VFsDriver *fsd = lookup_result.value().get();
    if (fsd->fsd()->is_pseudo()) {
        unexpect_return(ErrCode::NOT_SUPPORTED);
    }

    IFsDriver *driver = fsd->fsd();
    auto mount_result = driver->mount(devno, options);
    return mount_result.transform(
        [this, mnt_path, fsd, devno](util::owner<ISuperblock *> isb) {
            MountRecord record{
                .superblock     = util::owner(new VSuperblock(isb, *fsd)),
                .devno          = devno,
                .is_block_mount = true,
                .active_files   = 0,
            };
            this->mount_table.insert_or_assign(mnt_path, record);
        });
}

Result<void> VFS::mount(const char *fs_name, const char *mountpoint,
                        const char *options) {
    util::Path mnt_path = util::Path::normalize(mountpoint);
    if (mount_table.contains(mnt_path)) {
        unexpect_return(ErrCode::INVALID_PARAM);
    }

    auto lookup_result = fs_table.at_nt(fs_name);
    if (!lookup_result.has_value()) {
        unexpect_return(ErrCode::INVALID_PARAM);
    }
    VFsDriver *fsd = lookup_result.value().get();
    if (!fsd->fsd()->is_pseudo()) {
        unexpect_return(ErrCode::NOT_SUPPORTED);
    }

    auto *pseudo      = static_cast<IPesudoFsDriver *>(fsd->fsd());
    auto mount_result = pseudo->mount(fs_name, options);
    return mount_result.transform(
        [this, mnt_path, fsd](util::owner<ISuperblock *> isb) {
            MountRecord record{
                .superblock     = util::owner(new VSuperblock(isb, *fsd)),
                .devno          = 0,
                .is_block_mount = false,
                .active_files   = 0,
            };
            this->mount_table.insert_or_assign(mnt_path, record);
        });
}

Result<void> VFS::umount(const char *mountpoint) {
    util::Path mnt_path = util::Path::normalize(mountpoint);
    auto lookup_result  = mount_table.at_nt(mnt_path);
    if (!lookup_result.has_value()) {
        unexpect_return(ErrCode::INVALID_PARAM);
    }

    MountRecord &record = lookup_result.value().get();
    if (record.active_files != 0) {
        unexpect_return(ErrCode::BUSY);
    }

    auto super_sync_res = record.superblock->sb()->sync();
    if (!super_sync_res.has_value() &&
        super_sync_res.error() != ErrCode::NOT_SUPPORTED)
    {
        propagate_return(super_sync_res);
    }
    if (record.is_block_mount) {
        auto cache_res = blk::BlkManager::inst().lookup_cache(record.devno);
        propagate(cache_res);
        auto cache_sync_future = cache_res.value()->sync_all();
        auto cache_sync_res =
            task::wait::kthread_wait_for(cache_sync_future);
        propagate(cache_sync_res);
    }

    util::owner<VSuperblock *> vsb = record.superblock;
    this->mount_table.erase(mnt_path);

    Result<void> ret = vsb->vfsd().fsd()->unmount(vsb->sb());
    delete vsb.get();
    return ret;
}

namespace {
    [[nodiscard]]
    Result<void> validate_relpath(const char *relpath) {
        if (relpath == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        if (*relpath == '\0') {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        if (util::Path::from(relpath).is_absolute()) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        void_return();
    }

    [[nodiscard]]
    Result<void> validate_file_oflags(flags::oflg_t oflags) {
        using namespace flags;
        constexpr oflg_t valid_mask = O_READ | O_WRITE | O_EXECUTE;
        if ((oflags & ~valid_mask) != 0 || oflags == 0) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        if ((oflags & O_EXECUTE) != 0 && oflags != O_EXECUTE) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        void_return();
    }

    [[nodiscard]]
    Result<void> validate_dir_oflags(flags::oflg_t oflags) {
        using namespace flags;
        constexpr oflg_t valid_mask = O_READ | O_WRITE | O_EXECUTE;
        if ((oflags & ~valid_mask) != 0 || oflags == 0) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        void_return();
    }

    [[nodiscard]]
    b64 file_perm_from_oflags(flags::oflg_t oflags) {
        b64 perm = 0;
        if ((oflags & flags::O_READ) != 0) {
            perm |= perm::vfile::READ;
        }
        if ((oflags & flags::O_WRITE) != 0) {
            perm |= perm::vfile::WRITE;
        }
        if ((oflags & flags::O_EXECUTE) != 0) {
            perm |= perm::vfile::EXEC;
        }
        return perm;
    }

    [[nodiscard]]
    b64 dir_perm_from_oflags(flags::oflg_t oflags) {
        b64 perm = 0;
        if ((oflags & flags::O_READ) != 0) {
            perm |= perm::vdir::READ;
        }
        if ((oflags & flags::O_WRITE) != 0) {
            perm |= perm::vdir::WRITE;
        }
        if ((oflags & flags::O_EXECUTE) != 0) {
            perm |= perm::vdir::EXEC;
        }
        return perm;
    }
}  // namespace

Result<util::refc_ptr<VINode>> VFS::_resolve_from(util::refc_ptr<VINode> base,
                                                  const util::Path &path,
                                                  VSuperblock &vsb) const {
    VINode *current = base.get();
    if (current == nullptr) {
        unexpect_return(ErrCode::NULLPTR);
    }
    const util::Path relpath = path.normalize();
    if (relpath == ".") {
        return base;
    }
    for (const auto &entry : relpath) {
        auto lookup_res = current->inode()->as_directory().and_then(
            [entry](IDirectory *dir) { return dir->lookup(entry); });
        propagate(lookup_res);

        auto next_vind = vsb.get_vnode(lookup_res.value());
        propagate(next_vind);
        current = next_vind.value().get();
    }
    return util::refc_ptr(current);
}

Result<VFile *> VFS::_open_file_at(VINode &parent, const util::Path &mount_path,
                                   const char *relpath) {
    auto valid_res = validate_relpath(relpath);
    propagate(valid_res);

    auto parent_dir_res = parent.inode()->as_directory();
    propagate(parent_dir_res);

    auto target_res = _resolve_from(util::refc_ptr(&parent),
                                    util::Path::from(relpath).normalize(),
                                    parent.superblock());
    propagate(target_res);
    auto file_res = target_res.value()->inode()->as_file();
    propagate(file_res);

    auto *file = new VFile(*target_res.value().get(), mount_path, *this);
    if (file == nullptr) {
        unexpect_return(ErrCode::ALLOCATION_FAILED);
    }

    auto mount_res = _lookup_mount_record(mount_path);
    if (!mount_res.has_value()) {
        delete file;
        unexpect_return(ErrCode::FS_ERROR);
    }
    mount_res.value()->active_files++;
    return file;
}

Result<VDirectory *> VFS::_open_dir_at(VINode &parent,
                                       const util::Path &mount_path,
                                       const char *relpath) {
    auto valid_res = validate_relpath(relpath);
    propagate(valid_res);

    auto parent_dir_res = parent.inode()->as_directory();
    propagate(parent_dir_res);

    auto target_res = _resolve_from(util::refc_ptr(&parent),
                                    util::Path::from(relpath).normalize(),
                                    parent.superblock());
    propagate(target_res);
    auto dir_res = target_res.value()->inode()->as_directory();
    propagate(dir_res);

    auto *dir = new VDirectory(*target_res.value().get(), mount_path, *this);
    if (dir == nullptr) {
        unexpect_return(ErrCode::ALLOCATION_FAILED);
    }

    auto mount_res = _lookup_mount_record(mount_path);
    if (!mount_res.has_value()) {
        delete dir;
        unexpect_return(ErrCode::FS_ERROR);
    }
    mount_res.value()->active_files++;
    return dir;
}

Result<VFile *> VFS::_open_file(const char *filepath) {
    if (filepath == nullptr) {
        unexpect_return(ErrCode::NULLPTR);
    }
    if (*filepath == '\0') {
        unexpect_return(ErrCode::INVALID_PARAM);
    }

    util::Path mount_path;
    auto vind_res =
        _resolve_inode(util::Path::from(filepath).normalize(), mount_path);
    propagate(vind_res);

    auto *file = new VFile(*vind_res.value().get(), mount_path, *this);
    if (file == nullptr) {
        unexpect_return(ErrCode::ALLOCATION_FAILED);
    }

    auto mount_res = _lookup_mount_record(mount_path);
    if (!mount_res.has_value()) {
        delete file;
        unexpect_return(ErrCode::FS_ERROR);
    }
    mount_res.value()->active_files++;
    return file;
}

Result<VDirectory *> VFS::_open_dir(const char *filepath) {
    if (filepath == nullptr) {
        unexpect_return(ErrCode::NULLPTR);
    }
    if (*filepath == '\0') {
        unexpect_return(ErrCode::INVALID_PARAM);
    }

    util::Path mount_path;
    auto vind_res =
        _resolve_inode(util::Path::from(filepath).normalize(), mount_path);
    propagate(vind_res);
    auto dir_res = vind_res.value()->inode()->as_directory();
    propagate(dir_res);

    auto *dir = new VDirectory(*vind_res.value().get(), mount_path, *this);
    if (dir == nullptr) {
        unexpect_return(ErrCode::ALLOCATION_FAILED);
    }

    auto mount_res = _lookup_mount_record(mount_path);
    if (!mount_res.has_value()) {
        delete dir;
        unexpect_return(ErrCode::FS_ERROR);
    }
    mount_res.value()->active_files++;
    return dir;
}

Result<CapIdx> VFS::open(const char *filepath, cap::CHolder &holder) {
    auto file_res = _open_file(filepath);
    propagate(file_res);

    auto insert_res = holder.insert_to_free(file_res.value());
    if (!insert_res.has_value()) {
        file_res.value()->destruct();
        propagate_return(insert_res);
    }
    return insert_res.value();
}

Result<CapIdx> VFS::open(cap::Capability &parent_dir_cap, const char *relpath,
                         flags::oflg_t oflags, cap::CHolder &holder) {
    auto oflag_res = validate_file_oflags(oflags);
    propagate(oflag_res);

    auto *parent = parent_dir_cap.payload_as<VDirectory>();
    if (parent == nullptr) {
        unexpect_return(ErrCode::TYPE_NOT_MATCHED);
    }

    auto file_res = _open_file_at(*parent->vinode(), parent->mount_path(),
                                  relpath);
    propagate(file_res);

    auto insert_res =
        holder.insert_to_free(file_res.value(), file_perm_from_oflags(oflags));
    if (!insert_res.has_value()) {
        file_res.value()->destruct();
        propagate_return(insert_res);
    }
    return insert_res.value();
}

Result<CapIdx> VFS::opendir(cap::Capability &parent_dir_cap, const char *relpath,
                            flags::oflg_t oflags, cap::CHolder &holder) {
    auto oflag_res = validate_dir_oflags(oflags);
    propagate(oflag_res);

    auto *parent = parent_dir_cap.payload_as<VDirectory>();
    if (parent == nullptr) {
        unexpect_return(ErrCode::TYPE_NOT_MATCHED);
    }

    auto dir_res = _open_dir_at(*parent->vinode(), parent->mount_path(),
                                relpath);
    propagate(dir_res);

    auto insert_res =
        holder.insert_to_free(dir_res.value(), dir_perm_from_oflags(oflags));
    if (!insert_res.has_value()) {
        dir_res.value()->destruct();
        propagate_return(insert_res);
    }
    return insert_res.value();
}

Result<CapIdx> VFS::open_initrd(cap::CHolder &holder) {
    return open_dir("/initrd/", holder,
                    perm::vdir::READ | perm::vdir::WRITE | perm::vdir::EXEC);
}

Result<CapIdx> VFS::open_dir(const char *filepath, cap::CHolder &holder,
                             b64 perm) {
    auto dir_res = _open_dir(filepath);
    propagate(dir_res);
    auto insert_res = holder.insert_to_free(dir_res.value(), perm);
    if (!insert_res.has_value()) {
        dir_res.value()->destruct();
        propagate_return(insert_res);
    }
    return insert_res.value();
}

Result<VFile *> VFS::__debug_open(const char *filepath) {
    return _open_file(filepath);
}

Result<std::pair<util::Path, VSuperblock *>> VFS::_resolve_mount(
    const util::Path &path) {
    util::Path cur_path = path;
    while (true) {
        auto mount_res = mount_table.at_nt(cur_path);
        if (mount_res.has_value()) {
            return std::make_pair(cur_path,
                                  mount_res.value().get().superblock.get());
        }
        if (cur_path == "/") {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        cur_path = cur_path.parent_path();
    }
}

Result<VFS::MountRecord *> VFS::_lookup_mount_record(
    const util::Path &mount_path) {
    auto record_res = mount_table.at_nt(mount_path);
    if (!record_res.has_value()) {
        unexpect_return(ErrCode::ENTRY_NOT_FOUND);
    }
    return &record_res.value().get();
}

Result<util::refc_ptr<VINode>> VFS::_resolve_inode(const util::Path &path,
                                                   util::Path &mount_path) {
    auto mount_res = _resolve_mount(path);
    propagate(mount_res);
    mount_path       = mount_res.value().first;
    VSuperblock *vsb = mount_res.value().second;

    auto root_res = vsb->sb()->root();
    propagate(root_res);

    auto current_vind = vsb->get_vnode(root_res.value());
    propagate(current_vind);
    VINode *current = current_vind.value().get();

    const util::Path relpath = path.relative_to(mount_path);
    if (relpath == ".") {
        return util::refc_ptr(current);
    }

    for (const auto &entry : relpath) {
        auto lookup_res = current->inode()->as_directory().and_then(
            [entry](IDirectory *dir) { return dir->lookup(entry); });
        propagate(lookup_res);

        auto next_vind = vsb->get_vnode(lookup_res.value());
        propagate(next_vind);
        current = next_vind.value().get();
    }

    return util::refc_ptr(current);
}

void VFS::_on_vfile_destroy(const util::Path &mount_path) noexcept {
    auto active_res = mount_table.at_nt(mount_path);
    if (!active_res.has_value()) {
        loggers::VFS::WARN("VFile 销毁时找不到挂载点: %s", mount_path.c_str());
        return;
    }
    MountRecord &record = active_res.value().get();
    if (record.active_files == 0) {
        loggers::VFS::WARN("VFile 活跃计数已经为 0: %s", mount_path.c_str());
        return;
    }
    record.active_files--;
}

Result<size_t> VFS::read(VFile &vfile, off_t offset, void *buf,
                         size_t len) const {
    auto file_res = vfile.vinode()->inode()->as_file();
    propagate(file_res);
    IFile *file = file_res.value();

    auto read_res = file->read(offset, buf, len);
    propagate(read_res);

    if (file->file_cache() == FileCachePolicy::NONE) {
        auto sync_res = file->sync();
        if (!sync_res.has_value() && sync_res.error() != ErrCode::NOT_SUPPORTED)
        {
            propagate_return(sync_res);
        }
    }
    return read_res.value();
}

Result<size_t> VFS::write(VFile &vfile, off_t offset, const void *buf,
                          size_t len) const {
    auto file_res = vfile.vinode()->inode()->as_file();
    propagate(file_res);
    IFile *file = file_res.value();

    auto write_res = file->write(offset, buf, len);
    propagate(write_res);

    if (file->file_cache() == FileCachePolicy::NONE) {
        auto sync_res = file->sync();
        if (!sync_res.has_value() && sync_res.error() != ErrCode::NOT_SUPPORTED)
        {
            propagate_return(sync_res);
        }
    }
    return write_res.value();
}

Result<size_t> VFS::size(VFile &vfile) const {
    auto file_res = vfile.vinode()->inode()->as_file();
    propagate(file_res);
    return file_res.value()->size();
}

Result<void> VFS::sync(VDirectory &vdir) const {
    auto dir_res = vdir.vinode()->inode()->as_directory();
    propagate(dir_res);
    return dir_res.value()->sync();
}

Result<void> VFS::sync(VFile &vfile) const {
    auto file_res = vfile.vinode()->inode()->as_file();
    propagate(file_res);
    return file_res.value()->sync();
}
