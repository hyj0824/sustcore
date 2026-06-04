# VFile Object

本文总结 `kernel/object/vfile.*` 以及它和 `VFS` 的关系。VFile capability 是当前文件访问能力的封装层。

## 对象来源

`VFileObject` 操作的底层 payload 不是在 `kernel/object/` 里定义的，而是在 [kernel/vfs/vfs.h](/home/flysong/Sustcore/kernel/vfs/vfs.h) 中定义:

```cpp
class VFile : public cap::_PayloadHelper<PayloadType::VFILE>
```

因此:

- `VFile` 既是 VFS 对象，也是 capability payload
- `VFileObject` 只是它的权限封装与能力接口

## `VFile` payload

`VFile` 内部持有:

- `nonnull<VINode *> _vind`
- `_discarded` 标志

构造时会 `keep()` 对应的 `VINode`，析构时会 `release()`。

### 销毁语义

`VFile::destruct()` 并不直接 `delete this`，而是只把 `_discarded` 置为 true。

这说明:

- payload 生命周期和常规 `delete this` 不同
- VFS 侧可能还有额外的管理语义

同时，析构函数若发现对象未被正确 discard，会打印警告。

## `VFileObject`

`VFileObject` 继承自 `CapObj<::VFile>`。

构造时会额外缓存:

- `VINode *_vind = _obj->vind()`

并断言其非空。

### 提供的接口

- `read(offset, buf, len)`
- `write(offset, buf, len)`
- `size()`
- `sync()`
- `read_exact(offset, buf, len)`

## 权限模型

定义在 `perm::vfile`:

- `READ`
- `WRITE`
- `EXEC`

当前代码里实际用到的是:

- `READ`
- `WRITE`

`EXEC` 目前尚未在 `VFileObject` 方法中直接使用。

## `read()`

要求 `READ` 权限。

成功后直接调用:

```cpp
VFS::inst().read(_vind, offset, buf, len)
```

因此 `VFileObject` 本身不实现文件系统逻辑，只做 capability 权限门控。

## `write()`

要求 `WRITE` 权限。

成功后调用:

```cpp
VFS::inst().write(_vind, offset, buf, len)
```

## `size()`

要求 `READ` 权限。

返回:

```cpp
VFS::inst().size(_vind)
```

这里把“查询文件大小”视为读权限的一部分。

## `sync()`

要求 `WRITE` 权限。

成功后调用:

```cpp
VFS::inst().sync(_vind)
```

说明同步刷新被视为一种修改型操作，而不是纯查询操作。

## `read_exact()`

`read_exact()` 是一个便捷包装:

1. 调用 `read()`
2. 若失败则传播错误
3. 若返回长度不等于请求长度，则返回 `IO_ERROR`

因此它适合用于需要完整读取固定长度头部或结构体的路径。

## 与 VFS 的关系

`VFileObject` 自身不保存文件偏移，也不保存打开模式状态。它只是:

- 通过 payload 指向 `VINode`
- 把请求转发给 VFS 全局单例
- 在转发前做 capability 权限检查

因此它更像“受权限保护的 vnode 访问句柄”，而不是 POSIX 风格带独立 file offset 的 `struct file`。

## 生命周期关系

链路大致是:

1. `VFile` payload keep 一个 `VINode`
2. capability 引用 `VFile`
3. `VFileObject` 只是 capability 的栈上包装
4. 当最后一个 `VFILE` capability 被移除时，payload 进入 discard/destroy 路径
5. 对应 `VINode` 的引用被 release

因此文件对象的最终生命周期受 capability 持有数量控制。

## 当前设计特点

VFile capability 的特点是:

- **直接复用 VFS payload**
- **对象层极薄，只做权限检查与转发**
- **权限模型简单清晰**
- **适合和 capability 传递结合，作为进程间可转交文件句柄**

它本质上是“VFS 节点的 capability 封装”。
