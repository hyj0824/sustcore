# File System Interface

本文总结 `kernel/vfs/ops.h` 中的文件系统接口契约。该文件定义了 VFS 运行时可以理解的最小抽象: inode、文件、目录、superblock 和文件系统驱动。

## 接口层次

VFS 接口分成五类:

1. **元数据接口**: `IMetadata`
2. **inode 接口**: `IINode`
3. **文件和目录接口**: `IFile` / `IDirectory`
4. **superblock 接口**: `ISuperblock`
5. **文件系统驱动接口**: `IFsDriver` / `IPesudoFsDriver`

所有可失败操作都返回项目内置 `Result<T>`，错误通过 `ErrCode` 表达。

## 基本类型

### `inode_t`

当前 inode 编号类型为:

```cpp
using inode_t = size_t;
```

inode 编号只要求在同一个 superblock 内稳定唯一。具体含义由文件系统实现决定。例如 tarfs 使用 `tar header offset / 512` 作为 inode id。

### `SeekWhence`

`SeekWhence` 定义了 `SET`、`CUR`、`END`，但当前 VFS 对外接口直接使用显式 offset，尚未暴露 seek 风格状态接口。

## 缓存策略

### `INodeCachePolicy`

`IINode::inode_cache()` 返回 inode 对象在 VFS 层的缓存策略:

- `NONE`: 不缓存，每次解析创建新 `VINode`。
- `SHARED`: 可共享缓存，语义上适合常规文件。
- `PERMANENT`: 持久缓存，适合设备目录等长期存在对象。

当前 VFS 实现中，只要策略不是 `NONE`，`VSuperblock` 就会缓存对应 `VINode`，并在 superblock 销毁时统一释放。
对于已缓存的 vnode，VFS 现在区分两类失效处理：

- inode 仍然存在但内容视图已变化：原地 `invalidate`
- inode 已删除或不再可复用：从缓存中 `evict`

### `FileCachePolicy`

`IFile::file_cache()` 返回文件内容缓存策略:

- `NONE`: VFS 在 `read()` / `write()` 后立即调用 `sync()`。
- `SHARED`: 允许普通缓存。
- `PERMANENT`: 语义上表示长期缓存。

当前 VFS 只对 `NONE` 有特殊处理。

## 元数据接口

`IMetadata` 是一个空基类:

```cpp
class IMetadata {
public:
    virtual ~IMetadata() = default;
};
```

元数据生命周期由提供者管理。常见做法是把具体 metadata 作为 inode 或 superblock 的成员变量，然后通过 `metadata()` 返回引用。

## inode 接口

`IINode` 继承 `RTTIBase<IINode, INodeType>`，当前支持
`FILE` / `DIRECTORY` / `SYMLINK` 运行时类型判定。

必须实现:

- `metadata()`
- `inode_id()`

可覆写:

- `inode_cache()`

辅助转换:

- `as_directory()`
- `as_file()`
- `as_symlink()`

这两个转换本质上调用 `as<IDirectory>()` / `as<IFile>()`，失败时返回 `ErrCode::INVALID_PARAM`。

## 文件接口

`IFile` 表示普通可读写文件，继承 `IINode`。

必须实现:

- `read(offset, buf, len)`
- `write(offset, buf, len)`
- `size()`
- `sync()`

默认行为:

- `type_id()` 返回 `INodeType::FILE`。
- `file_cache()` 默认返回 `FileCachePolicy::SHARED`。

读写接口返回实际完成的字节数。调用方需要自行判断短读、短写是否符合预期。`cap::VFileObject::read_exact()` 会在实际读取长度不等于请求长度时返回 `ErrCode::IO_ERROR`。

此外 `IFile` 现在还提供默认的 `truncate(size_t)`，默认返回
`ErrCode::NOT_SUPPORTED`。

## 符号链接接口

`ISymlink` 表示符号链接 inode，继承 `IINode`。

必须实现:

- `target()`

## 目录接口

`IDirectory` 表示目录，继承 `IINode`。

必须实现:

- `lookup(std::string_view name)`
- `mkfile(std::string_view name, const char *options)`
- `mkdir(std::string_view name, const char *options)`
- `entry_count()`
- `entry_at(size_t index)`
- `sync()`

默认行为:

- `type_id()` 返回 `INodeType::DIRECTORY`。

`lookup()` 输入一个目录项名称，返回该目录项对应的 inode id。VFS 的路径解析完全依赖该接口逐级推进。

当前目录层已经补充了若干带默认实现的修改接口，默认返回
`ErrCode::NOT_SUPPORTED`:

- `unlink`
- `rmdir`
- `link`
- `rename`
- `symlink`

## Superblock 接口

`ISuperblock` 是单次挂载实例的根对象。

必须实现:

- `fs()`: 返回所属文件系统驱动。
- `sync()`: 同步 superblock 状态。
- `root()`: 返回根目录 inode id。
- `alloc_inode(type)`: 为可写文件系统分配 inode。
- `free_inode(inode_id)`: 释放 inode。
- `get_inode(inode_id)`: 根据 inode id 创建或取得具体 `IINode`。
- `is_symlink(inode_id)`: 判断 inode 是否为符号链接。
- `readlink(inode_id)`: 读取符号链接目标。
- `metadata()`: 返回 superblock metadata。
- `sb_id()`: 返回 superblock id。

所有权语义:

- `get_inode()` 返回 `util::owner<IINode *>`。
- 调用方获得 inode 对象所有权。
- VFS 会用该 owner 构造 `VINode`，再按 inode 缓存策略决定是否缓存。

`sync()` 返回 `ErrCode::NOT_SUPPORTED` 时，VFS 在卸载路径中会将其视为可接受结果。

## 文件系统驱动接口

### `IFsDriver`

`IFsDriver` 是所有文件系统驱动的基类。

必须实现:

- `name()`
- `probe(size_t devno, const char *options)`
- `mount(size_t devno, const char *options)`
- `unmount(ISuperblock *sb)`

默认分类:

- `is_pseudo()` 返回 `false`。
- `is_block_fs()` 返回 `false`。

普通块设备文件系统和伪文件系统可以分别继承适配类，避免手写不适用的接口。

### `IPesudoFsDriver`

`IPesudoFsDriver` 是伪文件系统适配类。名称中当前拼写为 `IPesudoFsDriver`。

它将块设备入口 final 化为不支持:

- `probe(size_t, options)` 返回 `ErrCode::NOT_SUPPORTED`。
- `mount(size_t, options)` 返回 `ErrCode::NOT_SUPPORTED`。
- `is_pseudo()` 固定返回 `true`。

子类需要实现:

- `probe(const char *name, const char *options)`
- `mount(const char *name, const char *options)`

伪文件系统挂载时不需要块设备，也不会在 VFS 挂载记录中持有 `BufferCache`。

## 实现新文件系统的最小步骤

实现一个普通文件系统通常需要:

1. 定义具体 metadata 类型，可以为空。
2. 实现 `IFile` 和 `IDirectory`，至少提供读写、大小、lookup、sync。
3. 实现 `ISuperblock`，负责 root inode、inode id 到 inode 对象的映射和 superblock 同步。
4. 实现 `IFsDriver`，负责 probe、mount、unmount。
5. 在 VFS 初始化阶段调用 `VFS::register_fs()` 注册驱动。

实现伪文件系统通常需要:

1. 继承 `IPesudoFsDriver`。
2. 为根目录和动态节点实现 inode。
3. 由 `mount(name, options)` 创建一个不依赖块设备的 superblock。
4. 按需暴露注册设备、目录创建等伪文件系统专用接口。

## 静态约束

`ops.h` 末尾通过 `static_assert` 检查关键接口契约:

- `IFile`、`IDirectory`、`ISuperblock` 都必须是 `ISyncable`。
- `IINode`、`ISuperblock` 都必须是 `IMetadataProvider`。

这能在接口变动时尽早发现遗漏。

## 当前缺口

当前接口层仍未抽象统一的:

- 权限/属主/时间戳 metadata 结构
- seek 状态对象
- 更细的 dentry 生命周期接口
