# VFS 与文件系统

VFS 为上层应用提供统一的文件操作接口, 同时将具体的文件系统实现作为驱动接入。VFS 文件、目录和挂载点均以 Capability Payload 的形式暴露给用户态。

## 总体架构

VFS 子系统在纵向分为四个层次:

```
用户态进程
    │ CapIdx (VFile / VDir / VMount)
    ▼
┌─────────────────────────────────────────┐
│  Capability 层                           │
│  VFileObject / VDirectoryObject          │
│  (系统调用入口, 权限检查, 结果转换)        │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│  VFS 运行时层 (kernel/vfs/vfs.cpp)       │
│  VFS / VFsDriver / VSuperblock / VINode  │
│  VFile / VDirectory / VMount             │
│  (挂载表, inode 缓存, 路径解析)           │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│  文件系统接口层 (kernel/vfs/ops.h)        │
│  IFsDriver / ISuperblock / IINode        │
│  IFile / IDirectory / ISymlink           │
│  (统一契约, 具体 FS 实现此接口)           │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│  具体文件系统实现                          │
│  tarfs / devfs / ext4 / tmpfs / procfs   │
└─────────────────────────────────────────┘
```

## 文件系统接口

VFS 接口分为五类:

| 接口          | 职责                                                       |
| ------------- | ---------------------------------------------------------- |
| `IMetadata`   | 元数据空基类                                               |
| `IINode`      | inode 通用接口, 支持 `FILE` / `DIRECTORY` / `SYMLINK` RTTI |
| `IFile`       | 普通文件读写                                               |
| `IDirectory`  | 目录操作                                                   |
| `ISuperblock` | 超级块                                                     |
| `IFsDriver`   | 文件系统驱动                                               |

所有可失败操作返回 `Result<T>`, 错误通过 `ErrCode` 表达。

### 缓存策略

| 策略                          | 含义                                        |
| ----------------------------- | ------------------------------------------- |
| `INodeCachePolicy::NONE`      | 不缓存, 每次 open 创建新实例, 关闭即销毁    |
| `INodeCachePolicy::SHARED`    | 可共享缓存, 引用计数归零后可回收            |
| `INodeCachePolicy::PERMANENT` | 持久缓存, 即使引用归零也不回收 (如设备目录) |
| `FileCachePolicy::NONE`       | 不缓存, 每次 write 后立即 sync (字符设备)   |
| `FileCachePolicy::SHARED`     | 允许普通缓存                                |

### VFsDriver

`VFsDriver` 是 `IFsDriver` 的引用计数包装。它持有 `util::owner<IFsDriver *>`, 通过 `closable()` 阻止仍被挂载 superblock 引用的文件系统驱动被卸载。

### VSuperblock

`VSuperblock` 包装 `ISuperblock`, 并保存其所属 `VFsDriver`。额外维护:

```
_inode_cache: unordered_map<inode_t, VINode *>
```

`get_vnode(inode_id)` 是 VFS 获取 vnode 的入口:

1. 优先查询 `_inode_cache`
2. 缓存未命中时调用底层 `ISuperblock::get_inode()`
3. 若缓存策略不是 `NONE`, 放入缓存

当 inode 内容变化时, VFS 使用 `invalidate_inode()` 原地刷新已缓存的 `VINode`, 。当 inode 被删除时, 使用 `evict_inode()` 从缓存中移除。

`on_death()` 在 superblock 销毁时释放 inode 缓存并删除底层 `ISuperblock`。

### VINode

`VINode` 是 `IINode` 的引用计数包装, 持有:

- `util::owner<IINode *> _inode`
- `util::refc_ptr<VFsDriver> _fsd`
- `util::refc_ptr<VSuperblock> _vsb`

保证只要 VFS 层仍持有某个 vnode, 其所属文件系统驱动和 superblock 都不会提前销毁。

`VINode` 还实现了文件页缓存, 用于 file-backed memory 映射时的页面管理。每个缓存页包含:

- `paddr`: 物理地址
- `valid`: 有效字节数
- `dirty`: 脏标记
- `active`: 活跃标记 (LRU 驱逐使用)
- `evicting` / `invalidating`: 并发控制标记

### VFile 与 VDirectory

- `VFile` 是 `PayloadType::VFILE` 的 Capability Payload, 持有 `VINode` / 挂载路径 / VFS 引用。

- `VDirectory` 是 `PayloadType::VDIR` 的 Capability Payload, 额外持有全局路径。作为 `open(parent_dir_cap, relpath)` / `opendir(parent_dir_cap, relpath)` 的父目录 Capability, 实现基于父目录的访问控制。

### init VDir

内核采用 Capability-based 访问控制

```
kinit 内核线程
    │ (特殊接口获取根目录 Capability)
    ├─→ init 进程
    │       │ (opendir 获取子目录 Capability)
    │       ├─→ 子进程 A (继承 /usr/ Capability)
    │       └─→ 子进程 B (继承 /etc/ Capability)
```

kinit 通过特殊接口获得根目录的 Capability, 传递给 init 进程。init 通过该 Capability 获取根目录下的文件和目录的 Capability, 再传递给子进程。

### VMount

`VMount` 是 `PayloadType::VMOUNT` 的 Capability Payload。用户程序通过创建 VMount 对象 (指定文件系统类型和选项), 再将 VMount 绑定到某个 VDir 的子目录上来实现挂载文件系统。同时, 用户也可以通过 VMount 获取挂载点根目录的 Capability。

## 挂载系统

VFS 的挂载表 (Mount Table) 维护全局路径到 `VSuperblock` 的映射。挂载流程:

```
用户态: create_vmount(fs_type, options) → vmount_cap
用户态: mount(dir_cap, vmount_cap, name)
    │
内核: VFS::mount(parent_vdir, name, vmount)
    │
    1. 查找文件系统驱动 (按 fs_type 名称)
    2. 调用 IFsDriver::probe() 获取 ISuperblock
    3. 包装为 VSuperblock
    4. 在父目录 inode 下创建目录项
    5. 写入挂载表: path → VSuperblock
    │
    └─→ 用户可以 opendir(dir_cap, name) 获取挂载点根目录的 Capability
```

## 文件系统实现

当前内核实现了以下文件系统:

### TarFS

只读 tar 文件系统, 用于解析 initrd 中的 ustar 格式 archive。

- **inode 编号**: `tar header offset / 512`
- **目录 lookup**: 在 tar 数据中线性扫描, 匹配 header 中的 `name` 字段
- **文件读取**: 直接从 tar 数据中读取 header 后的文件内容
- **数据来源**: RamDisk 设备 (直接引用内存, 不复制)

### DevFS

伪文件系统 (`IPesudoFsDriver`), 不依赖块设备。挂载到 `/sys/`, 将内核设备暴露为文件节点。

- **目录**: `DevFSDirectory` 维护 `name → inode_t` 映射
- **字符设备**: 通过 `CharFactory` 延迟创建。superblock 只保存 inode → factory 映射, 在 VFS 缓存未命中时通过 factory 创建 `CharDevFile`
- **CharDevFile**: 字符设备基类, 要求 `offset == 0` (设备无偏移概念), `file_cache()` 返回 `NONE`

### Ext4

Ext4 文件系统实现 (`kernel/vfs/ext4.cpp`), 支持完整的 ext4 磁盘布局: superblock、block group descriptors、inode table、extent tree 等。提供完整的读写能力, 使用 BufferCache 进行块级缓存。

### TmpFS

基于内存的临时文件系统 (`kernel/vfs/tmpfs.cpp`), 继承 `IPesudoFsDriver`。文件和目录内容全部保存在内存中, 适合 `/tmp` 等临时目录。

### ProcFS

进程信息伪文件系统 (`kernel/vfs/procfs.cpp`), 继承 `IPesudoFsDriver`。将内核中的进程、线程等信息暴露为文件节点, 挂载于 `/proc/`。

## 块设备层

块设备层 (`kernel/bio/`) 是 VFS 与物理存储之间的桥梁, 分为三层:

```
┌──────────────────────────────────┐
│  VFS (文件系统)                    │
│    │ 按块读写 (BufferCache)        │
├────▼─────────────────────────────┤
│  BufferCache / BufferHandler     │  ← 块缓存层
│    │ 脏块写回, LRU 淘汰            │
├────▼─────────────────────────────┤
│  BlockRequestQueue               │  ← 请求队列层
│    │ SUBMITTED → PROCESSING → DONE│
├────▼─────────────────────────────┤
│  IBlockDeviceOps (设备驱动)       │  ← 设备接口层
│    │ read_blocks / write_blocks   │
├────▼─────────────────────────────┤
│  RamDisk / VirtIO-blk / ...      │  ← 具体设备
└──────────────────────────────────┘
```

### IBlockDeviceOps

块设备统一抽象接口:

- `block_sz()`: 块大小 (字节)
- `block_cnt()`: 块数量
- `readonly()`: 是否只读
- `read_blocks(lba, buf, cnt)`: 从 LBA 读取若干块
- `write_blocks(lba, buf, cnt)`: 写入若干块
- `process_request(req)`: 处理请求队列中的请求
- `run_request_loop()`: 设备 worker 主循环

### BlkManager

块设备注册管理器, 维护 `设备对象 → 设备号` 的双向映射。设备号用于持久化引用, 在重启后通过设备号重新定位设备。

### BufferCache

块缓存层为上层文件系统提供:

- 按块号访问缓存块, 未命中时从设备读取
- 脏块标记与写回
- LRU 淘汰策略回收空闲缓存
- `BufferHandler` RAII 句柄, 自动管理块引用计数

### RamDiskDevice

内存后端块设备, 直接从内存地址读写。initrd 使用时, tarfs 直接引用其底层内存, 避免额外复制。

### BlockDevice (驱动基类)

`driver::BlockDevice` 同时继承 `DriverBase` 和 `IBlockDeviceOps`, 使得真实硬件块设备驱动 (如 virtio-blk) 既能复用设备框架的 MMIO/virq 管理, 又能作为 VFS 可挂载的块设备暴露。

## 系统调用映射

VFS 通过 Capability 对象暴露给用户态的主要系统调用:

| 系统调用             | 操作对象    | 说明                        |
| -------------------- | ----------- | --------------------------- |
| `open(dir, path)`    | VDir        | 打开文件, 返回 VFile CapIdx |
| `opendir(dir, path)` | VDir        | 打开目录, 返回 VDir CapIdx  |
| `read(file, buf)`    | VFile       | 读取文件                    |
| `write(file, buf)`   | VFile       | 写入文件                    |
| `size(file)`         | VFile       | 获取文件大小                |
| `create_vmount(...)` | —           | 创建挂载规格                |
| `mount(dir, vmnt)`   | VDir+VMount | 挂载文件系统                |
| `umount(dir)`        | VDir        | 卸载文件系统                |
| `sync(file)`         | VFile       | 同步文件                    |
| `getdents(dir)`      | VDir        | 获取目录项列表              |

每个系统调用的第一个参数都是操作对象的 CapIdx, 内核通过检查 Capability 的类型和权限来决定是否允许操作。

## 初始化序列

`kernel/main.cpp` 中的 `init_vfs()` 执行 VFS 初始化:

```
1. VFS::init()                          — 构造 VFS 单例
2. register_fs("tarfs", TarFSDriver)    — 注册文件系统驱动
3. register_fs("devfs", DevFSDriver)    — 注册设备文件系统
4. register_fs("ext4", Ext4FSDriver)    — 注册 Ext4 驱动
5. register_fs("procfs", ProcFSDriver)  — 注册进程文件系统
6. register_fs("tmpfs", TmpFSDriver)    — 注册临时文件系统
7. mount("/initrd/", "tarfs", ramdisk)  — 将 initrd 挂载到 /initrd/
8. mount("/sys/", "devfs")              — 将设备文件系统挂载到 /sys/
```

之后, init 进程可以通过根目录 Capability 访问 `/initrd/` 下的可执行文件, 并挂载更多文件系统。
