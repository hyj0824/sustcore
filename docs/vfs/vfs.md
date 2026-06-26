# Virtual File System

本文总结 `kernel/vfs/vfs.h` 与 `kernel/vfs/vfs.cpp` 中的 VFS 运行时层。VFS 负责把文件系统驱动、挂载点、路径解析、inode 缓存和 capability payload 连接起来，对外提供统一的 `open/read/write/size/sync` 接口。

## 总体职责

VFS 当前承担四类职责:

1. **文件系统注册表**: 通过文件系统名称管理 `IFsDriver` 实例。
2. **挂载表**: 维护目录树中的挂载点位置与 `VSuperblock` 的映射。
3. **路径解析**: 从全局路径找到对应挂载点，再沿目录逐级 lookup 到目标 inode。
4. **能力对象入口**: `open()` / `opendir()` 创建 `VFile` / `VDirectory` payload 并插入 `cap::CHolder`，后续文件操作经 capability 对象转回 VFS。

## 初始化

`VFS` 是显式初始化的全局单例:

- `VFS::init()` 通过 placement new 构造静态实例。
- `VFS::initialized()` 返回初始化状态。
- `VFS::inst()` 在未初始化时 `panic("VFS 未初始化!")`。

`kernel/main.cpp` 中的 `init_vfs()` 当前执行顺序是:

1. `VFS::init()`
2. 注册 `tarfs::TarFSDriver`
3. 注册 `devfs::DevFSDriver`
4. 将 initrd 作为 `tarfs` 挂载到 `/initrd/`
5. 将 `devfs` 挂载到 `/sys/`

## 核心包装对象

### `VFsDriver`

`VFsDriver` 是 VFS 内部对 `IFsDriver` 的引用计数包装。它持有:

- `util::owner<IFsDriver *> _fsd`

`closable()` 只有在引用计数不再存活时才返回 `true`。`unregister_fs()` 会用该状态阻止仍被挂载 superblock 引用的文件系统驱动被卸载。

### `VSuperblock`

`VSuperblock` 包装具体文件系统返回的 `ISuperblock`，并保存其所属 `VFsDriver`。

它额外维护:

- `util::owner<ISuperblock *> _sb`
- `util::refc_ptr<VFsDriver> _fsd`
- `std::unordered_map<inode_t, VINode *> _inode_cache`

`get_vnode(inode_id)` 是 VFS 获取 vnode 的唯一入口:

1. 优先查询 `_inode_cache`。
2. 缓存未命中时调用底层 `ISuperblock::get_inode()`。
3. 用返回的 `IINode` 构造 `VINode`。
4. 若 `inode_cache() != INodeCachePolicy::NONE`，则把 `VINode` 放入缓存。

当 inode 仍然存在但其内容或目录项视图已变化时，VFS 会优先调用
`invalidate_inode(inode_id)` 原地刷新已缓存的 `VINode`，而不是移除该
`VINode` 对象本身。这样可以保持引用该 vnode 的挂载键和已打开 payload
地址稳定。

`VSuperblock::on_death()` 会释放 inode 缓存引用并删除底层 `ISuperblock`。

### `VINode`

`VINode` 是 VFS 对具体 `IINode` 的包装。它持有:

- `util::owner<IINode *> _inode`
- `util::refc_ptr<VFsDriver> _fsd`
- `util::refc_ptr<VSuperblock> _vsb`

这保证只要 VFS 层仍持有某个 vnode，其所属文件系统驱动和 superblock 都不会提前销毁。

`VINode::invalidate()` 会重新向底层 superblock 请求同一 `inode_id` 的
新 `IINode`，并在保留 `VINode *` 身份不变的前提下替换内部 `_inode`。

### `VFile`

`VFile` 是 capability 系统中的 `PayloadType::VFILE` payload。它持有:

- `util::refc_ptr<VINode> _vind`
- `util::Path _mount_path`
- `VFS *_vfs`

`VFile::destruct()` 会通知 VFS 当前挂载点少了一个活跃文件，然后 `delete this`。这也是 `VFS::umount()` 判断 busy 状态的基础。

### `VDirectory`

`VDirectory` 是 capability 系统中的 `PayloadType::VDIR` payload。它与 `VFile` 一样持有:

- `util::refc_ptr<VINode> _vind`
- `util::Path _mount_path`
- `util::Path _global_path`
- `VFS *_vfs`

其主要用途是:

- 表示“已打开目录”
- 作为 `open(parent_dir_cap, relpath, ...)` / `opendir(parent_dir_cap, relpath, ...)` 的父目录 capability
- 通过 `cap::VDirectoryObject` 暴露有限的目录能力接口

## 注册与卸载文件系统

### `register_fs(driver)`

注册流程:

1. 读取 `driver->name()` 作为注册表 key。
2. 如果同名文件系统已存在，返回 `ErrCode::INVALID_PARAM`。
3. 用 `VFsDriver` 包装并接管 `IFsDriver` 所有权。

### `unregister_fs(fs_name)`

注销流程:

1. 文件系统名不存在时返回 `ErrCode::INVALID_PARAM`。
2. 查到 `VFsDriver` 后检查 `closable()`。
3. 若仍被 superblock 或 vnode 引用，返回 `ErrCode::BUSY`。
4. 从注册表移除并删除驱动包装对象。

## 挂载接口

VFS 当前同时保留传统路径挂载接口和新的 mount capability 路径。

### 传统挂载

接口为:

```cpp
Result<void> mount(const char *fs_name, size_t devno,
                   const char *mountpoint, MountFlags flags,
                   const char *options);
Result<void> mount(const char *fs_name, const char *mountpoint,
                   const char *options);
```

其中块设备文件系统现在通过 `devno` 挂载，而不是直接传 `IBlockDeviceOps *`。

### Mount capability

VFS 还提供:

- `create_mount(...)`
- `mount_attach(...)`
- `mount_detach(...)`
- `mount_root(...)`

用于先创建 `VMount` payload，再把它附着到某个已打开目录下。

## 挂载记录

当前内部挂载记录至少包含:

```cpp
struct MountRecord {
    VINode *parent_vinode;
    std::string entry_name;
    util::Path mount_path;
    util::owner<VSuperblock *> superblock;
    size_t devno;
    bool is_block_mount;
    size_t active_files = 0;
    VMount *owner_mount;
};
```

## 卸载

传统 `umount(mountpoint)` 的流程仍保留；使用 mount capability 时，更常通过 `mount_detach()` 完成分离。

1. 规范化挂载点路径。
2. 找不到挂载点时返回 `ErrCode::INVALID_PARAM`。
3. `active_files != 0` 时返回 `ErrCode::BUSY`。
4. 调用底层 superblock 的 `sync()`。
5. `sync()` 返回 `ErrCode::NOT_SUPPORTED` 时视为可接受。
6. 若存在 `BufferCache`，调用 `cache->sync_all()`。
7. 从挂载表移除记录。
8. 调用文件系统驱动 `unmount(sb)`。
9. 删除 `VSuperblock` 和 `BufferCache`。

## 路径解析

### 挂载点选择

`_resolve_mount(path)` 从完整路径开始，逐级向父目录回退，查找第一个已挂载路径。

例如路径 `/initrd/bin/app` 会尝试:

1. `/initrd/bin/app`
2. `/initrd/bin`
3. `/initrd`
4. `/`

找到挂载点后返回 `(mount_path, VSuperblock *)`。如果回退到 `/` 仍没有命中，则返回 `ErrCode::INVALID_PARAM`。

### inode 解析

`_resolve_inode(path, mount_path)` 的流程是:

1. 先用 `_resolve_mount()` 找到挂载点和 superblock。
2. 调用 `ISuperblock::root()` 取得根 inode。
3. 通过 `VSuperblock::get_vnode()` 取得根 vnode。
4. 计算 `path.relative_to(mount_path)`。
5. 若相对路径为 `"."`，直接返回根 vnode。
6. 否则逐项调用当前 inode 的 `as_directory()` 和 `IDirectory::lookup(entry)`。
7. 每次 lookup 得到下一个 inode id 后再通过 `get_vnode()` 取得 vnode。

因此 VFS 只理解“目录 lookup 得到 inode id”，不理解具体文件系统的目录格式。

## 打开文件

`_open_file(filepath)` 是内部打开入口:

1. `filepath == nullptr` 返回 `ErrCode::NULLPTR`。
2. 空字符串返回 `ErrCode::INVALID_PARAM`。
3. 规范化路径并解析到 `VINode`。
4. 构造 `VFile`。
5. 找到对应挂载记录并递增 `active_files`。

公开接口 `open(filepath, holder)` 会把 `VFile` 插入到 `cap::CHolder` 的空闲 capability 槽中，并返回 `CapIdx`。如果插入失败，会立即 `destruct()` 刚创建的 `VFile`，避免活跃计数泄漏。

当前还提供两组基于目录 capability 的打开入口:

- `open(parent_dir_cap, relpath, oflg_t, holder)`
- `opendir(parent_dir_cap, relpath, oflg_t, holder)`

规则如下:

- `parent_dir_cap` 必须是 `VDIR`
- `relpath` 必须是相对路径
- 文件目标只允许 `R`、`W`、`RW`、`X`
- 文件目标带 `X` 时必须是纯 `X`
- 目录目标允许 `R/W/X` 的任意非零组合

VFS 还提供临时辅助入口 `open_initrd(holder)`，它直接把 `"/initrd/"` 目录作为 `VDIR` capability 插入到指定 holder。当前 `libkmod` 依赖它实现 `kmod_fopen("/initrd/...", options)`。

`__debug_open()` 只供测试使用，直接返回裸 `VFile *`。

## 文件操作

VFS 提供四个运行时文件接口:

- `read(VFile &, offset, buf, len)`
- `write(VFile &, offset, buf, len)`
- `size(VFile &)`
- `sync(VFile &)`

`read()` / `write()` 会先把 vnode 里的 `IINode` 转换成 `IFile`。如果 inode 实际是目录，则 `as_file()` 返回 `ErrCode::INVALID_PARAM`。

当底层文件的 `file_cache() == FileCachePolicy::NONE` 时，VFS 会在 `read()` 或 `write()` 后调用 `sync()`。如果该 `sync()` 返回 `ErrCode::NOT_SUPPORTED`，VFS 不把它当作失败。

## Capability 接入

`kernel/object/vfile.cpp` 中的 `cap::VFileObject` 是用户通过 capability 操作文件的入口:

- `read()` 需要 `perm::vfile::READ`。
- `write()` 需要 `perm::vfile::WRITE`。
- `size()` 需要 `perm::vfile::READ`。
- `sync()` 需要 `perm::vfile::WRITE`。

权限检查通过后，所有操作都会转发到 `VFS::inst()`。

`kernel/object/vdir.cpp` 中的 `cap::VDirectoryObject` 是目录 capability 的薄包装:

- `sync()` 需要 `perm::vdir::WRITE`

当前目录 capability 的主要用途仍是“作为父目录继续打开子项”和“作为 mount attach 的父目录”，而不是提供完整目录 I/O。

## 当前限制

当前 VFS 运行时层仍有一些限制:

- 挂载表和文件系统注册表没有内部锁。
- `MountFlags` 目前只有 `NONE`，挂载路径不会解释额外标志。
- 目录遍历只依赖 `lookup()`，没有 readdir/listdir 接口。
- `INodeCachePolicy::SHARED` 当前不会在引用计数归零时主动从 `_inode_cache` 移除。
- VFS 不负责创建、删除、重命名文件或目录。
- `open_initrd()` 只是当前系统挂载布局下的临时入口，它返回的是 `"/initrd/"`，不是真正意义上的 `/`。
- `__debug_open()` 返回裸指针，仅应在测试代码中使用。
