# Block Device

本文总结 `kernel/bio/` 下的块设备抽象、块设备管理器和块缓存实现。它是设备驱动与 VFS 之间的存储 I/O 层: 驱动向上暴露统一的块读写接口，文件系统通过 `BufferCache` 获得按块缓存与写回能力。

## 总体分层

块设备子系统当前可以分成三层:

1. **块设备接口层**: `IBlockDeviceOps` 定义统一的块大小、容量、读写与同步接口。
2. **设备注册层**: `blk::BlkManager` 维护块设备对象到稳定设备号的映射。
3. **块缓存层**: `blk::BufferCache` / `blk::BufferHandler` 为上层文件系统提供缓存块访问、脏块写回和空闲缓存回收。

其中 `driver::BlockDevice` 是真实设备驱动接入块设备层的基类；`RamDiskDevice` 是当前已实现的内存块设备，主要用于 initrd、tarfs 和文件系统测试。

## 基本接口

### `IBlockDeviceOps`

`IBlockDeviceOps` 定义在 `kernel/bio/block.h` 中，继承 `RTTIBase<IBlockDeviceOps, BlockDeviceType>`，用于支持运行时类型识别。

核心接口包括:

- `block_sz()`: 返回单个块大小，单位为字节。
- `block_cnt()`: 返回设备包含的块数量。
- `readonly()`: 返回设备是否只读，默认实现为 `false`。
- `total_bytes()`: 默认通过 `block_sz() * block_cnt()` 计算总字节数。
- `read_blocks(lba, buf, cnt)`: 从起始 LBA 读取若干块，返回实际读取块数。
- `write_blocks(lba, buf, cnt)`: 从起始 LBA 写入若干块，返回实际写入块数。
- `sync()`: 将设备内部状态同步到底层介质。

`read_blocks()` 和 `write_blocks()` 返回的是实际完成的块数，因此调用方必须检查返回值是否等于请求值。`BufferCache` 在读写单块时会显式要求返回值为 `1`，否则视为 `IO_ERROR`。

### LBA 与容量语义

当前 LBA 类型为:

```cpp
using lba_t = size_t;
```

合法块号范围是 `[0, block_cnt())`。是否允许跨越设备末尾由具体设备实现决定，但上层不应假设一定能完整完成跨界请求。

### `driver::BlockDevice`

`driver::BlockDevice` 同时继承 `DriverBase` 和 `IBlockDeviceOps`。真实硬件块设备驱动应优先继承该类:

```cpp
class BlockDevice : public DriverBase, public IBlockDeviceOps {
public:
    explicit BlockDevice(DevRes res) noexcept
        : DriverBase(std::move(res)) {}
    ~BlockDevice() override = default;
};
```

这样驱动既能复用设备框架提供的 `DevRes`、MMIO、virq 生命周期管理，又能作为 VFS 可挂载的块设备暴露。

## `RamDiskDevice`

`RamDiskDevice` 是内存后端块设备。它持有:

- `D_base`: 起始内存地址。
- `D_block_size`: 块大小。
- `D_block_count`: 块数量。

### 读写行为

`read_blocks()` / `write_blocks()` 的行为如下:

- `buf == nullptr && cnt != 0` 返回 `ErrCode::NULLPTR`。
- `lba == block_count && cnt == 0` 合法，返回 `0`。
- `lba >= block_count` 返回 `ErrCode::OUT_OF_BOUNDARY`。
- `cnt == 0` 返回 `0`。
- 若 `lba + cnt` 超出设备末尾，只完成能容纳的部分，并返回实际块数。

`sync()` 对 RamDisk 是空操作，因为数据已经在内存中。

### initrd 使用

`kernel/main.cpp` 中的 `make_initrd()` 会把链接脚本提供的 `[s_initrd, e_initrd)` 包装成一个单块 `RamDiskDevice`:

```cpp
auto device = util::owner(new RamDiskDevice(&s_initrd, sz, 1));
```

也就是说当前 initrd 的块大小等于整个 initrd 大小，块数量为 `1`。`tarfs` 挂载 RamDisk 时会直接使用其底层内存，避免额外复制。

## `BlkManager`

`blk::BlkManager` 定义在 `kernel/bio/blk.h` 中，是块设备注册管理器。

它采用项目常见的单例初始化模式:

- 静态 `_INSTANCE`
- 静态 `_initialized`
- `init()`
- `inst()`
- `initialized()`

内部维护两张表:

- `_devices`: `id -> owner<IBlockDeviceOps *>`
- `_device_ids`: `device pointer -> id`

### 主要接口

- `contains(id)`: 检查设备号是否存在。
- `lookup(id)`: 根据设备号取得设备指针。
- `find_device_id(device)`: 根据设备指针反查设备号。
- `register_device(device)`: 接管设备所有权并分配新设备号。
- `unregister_device(id)`: 删除注册表记录并释放设备对象。

### 所有权语义

`register_device()` 接收 `util::owner<IBlockDeviceOps *>`，注册成功后由 `BlkManager` 接管对象生命周期。`unregister_device()` 和 `BlkManager` 析构都会 `delete` 对应设备对象。

当前 VFS 挂载路径仍直接接收 `IBlockDeviceOps *`，并不强制通过 `BlkManager` 查找设备；因此 `BlkManager` 更像是预留的全局块设备目录，而不是 VFS 当前挂载的唯一入口。

## 块缓存

### `Buffer`

`blk::Buffer` 是一个缓存槽的数据结构，每个实例代表某个设备块的缓存副本。

字段包括:

- `blkno`: 块号。
- `data`: 指向块数据的缓冲区，大小等于设备块大小。
- `dirty`: 是否需要写回设备。
- `valid`: `data` 是否已经从设备读入有效内容。
- `refcnt`: 当前引用计数。

`keep()` / `release()` 只维护引用计数。当前实现中这两个操作不是原子的，且 `Buffer` 自身没有锁。

### `BufferHandler`

`BufferHandler` 是访问 `Buffer` 的 RAII 句柄。它在构造、拷贝时增加引用计数，在析构、赋值替换时释放引用计数。

它提供的主要访问能力:

- `blkno()`
- `devno()`
- `blksz()`
- `refcnt()`
- `is_dirty()`
- `is_valid()`
- `read(offset, buf, len)`
- `write(offset, data, len)`
- `readblk(buf, buflen)`
- `writeblk(buf, buflen)`

`read()` / `write()` 面向块内偏移，超出块大小时只完成块内可容纳的部分。`write()` 会把缓存标记为 dirty。`readblk()` / `writeblk()` 要求传入缓冲区大小等于块大小，并用 `assert` 表达此前置条件。

### `BufferCache`

`BufferCache` 绑定一个 `IBlockDeviceOps` 和一个设备号:

```cpp
BufferCache(IBlockDeviceOps *dev_ops, size_t devno);
```

构造时会读取设备块大小并断言块大小非 0。它不可拷贝、不可移动。

内部状态包括:

- `_dev_ops`: 底层块设备接口。
- `_devno`: 设备号，仅用于标识。
- `_blksz`: 缓存块大小。
- `_buffers[MAX_CACHE_SIZE]`: 固定大小缓存槽数组。
- `_mapping`: `lba -> cache slot index` 映射。

当前 `MAX_CACHE_SIZE` 为 `8192`。

## 缓存生命周期

### 获取缓存块

`get_buffer(blkno)` 是上层访问缓存的入口:

1. 调用 `ensure_buffer(blkno)` 确保缓存槽存在。
2. 若缓存不存在，调用 `find_free()` 分配空槽或回收空闲槽。
3. 新建 `Buffer`，分配大小为 `_blksz` 的数据缓冲区，状态为 `dirty = false`、`valid = false`、`refcnt = 0`。
4. 若缓存内容尚未有效，从底层设备读取一个块。
5. 读取成功且返回块数为 `1` 后标记 `valid = true`。
6. 返回 `BufferHandler`，由句柄维护引用计数。

### 查找和回收缓存槽

`find_free()` 的策略是:

1. 优先返回第一个空槽。
2. 如果没有空槽，查找 `refcnt == 0` 的缓存块。
3. 若该块是 dirty，先 `writeback_buffer()` 写回设备。
4. 调用 `clear_slot()` 删除缓存块并复用槽位。
5. 如果所有缓存块都仍在被引用，返回 `ErrCode::BUSY`。

当前实现没有 LRU、时钟算法或访问热度统计，因此满缓存时回收的是数组顺序上的第一个空闲块。

### 同步与清理

同步接口包括:

- `sync(handler)`: 写回指定句柄指向的缓存块，然后调用设备 `sync()`。
- `sync_all()`: 写回所有 dirty 缓存块，然后调用设备 `sync()`。
- `tidy()`: 先 `sync_all()`，再删除所有 `refcnt == 0` 的缓存块。

析构 `BufferCache` 时会调用 `sync_all()`，随后释放所有缓存块和映射表。析构函数当前会忽略 `sync_all()` 的错误返回值，因此上层需要在卸载、关闭等关键路径主动调用同步接口并检查错误。

## VFS 接入

VFS 与块设备的连接点主要在 `kernel/vfs/ops.h` 和 `kernel/vfs/vfs.cpp`。

### VFS 挂载记录

`VFS::mount(fs_name, devno, mountpoint, flags, options)` 对块文件系统会:

1. 校验设备号和挂载点。
2. 查找文件系统驱动。
3. 调用文件系统驱动的 `mount(devno, options)`。
4. 在 `MountRecord` 中记录 `VSuperblock`、`devno`、挂载位置和活跃文件数。

卸载时，VFS 会:

1. 拒绝仍有打开文件的挂载点。
2. 调用 superblock 的 `sync()`。
3. 调用文件系统驱动的 `unmount()`。
4. 删除 superblock。

## 当前测试覆盖

`kernel/test/fs.cpp` 中已有块缓存相关测试:

- `BufferCache 读写与同步行为`: 覆盖首次读入、块内写入、dirty 标记、单块同步、缓存命中和设备内容回读。
- `BufferCache tidy 回收空闲块`: 覆盖句柄释放后的 dirty 块写回、缓存槽回收和重新加载。

同一文件还覆盖了使用 `RamDiskDevice` 挂载 initrd tarfs、打开文件、忙状态卸载失败、重复挂载检查等 VFS 行为。

## 当前限制

当前块设备层仍有一些明确限制:

- `Buffer` 引用计数不是原子操作。
- `BufferCache` 没有内部锁，多线程并发访问需要外层同步。
- 缓存淘汰策略是线性查找空闲槽，没有 LRU 或优先级。
- `BufferCache` 不会主动校验 `blkno < block_cnt()`，越界错误由底层设备读写接口返回。
- `readonly()` 目前没有被 `BufferCache::write()` 或 `sync()` 强制检查。
- `BlkManager` 与 `devno` 分配仍然是块文件系统挂载的前置条件。

新增真实块设备驱动时，应至少实现 `IBlockDeviceOps` 的完整容量、读写和同步语义，并确保短读短写路径能被调用方明确识别。
