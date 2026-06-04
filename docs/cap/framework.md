# Capability Framework

本文总结 `kernel/cap/` 与 `kernel/object/perm.h` 中定义的能力系统框架。它描述当前内核如何表示 capability、如何管理 capability 空间，以及权限传播与传递的基本语义。

## 总体模型

当前能力系统由四层组成:

1. **Payload**: capability 指向的真实对象或对象载荷
2. **Capability**: `Payload + permission bits`
3. **CSpace**: 一个持有者的 capability 槽位空间
4. **CHolder**: 拥有一个 CSpace 的能力持有者

可以把它理解为:

- Payload 决定“这是个什么对象”
- Capability 决定“你能对它做什么”
- CSpace 决定“它在本持有者里放在哪个槽位”
- CHolder 决定“是谁持有这一组能力”

## 能力编号与空间布局

能力索引类型是 `CapIdx`，定义在 [include/sustcore/capability.h](/home/flysong/Sustcore/include/sustcore/capability.h)。

### 编码格式

`CapIdx` 是 64 位值，其中:

- 最高位 `MASK_VALID` 表示该索引是否有效
- `MASK_GROUP` 表示所属 `CGroup`
- `MASK_SLOT` 表示组内槽位

辅助函数:

- `cap::valid(idx)`
- `cap::make(group, slot)`
- `cap::group(idx)`
- `cap::slot(idx)`

### 两级空间

能力空间不是一个平铺的大数组，而是两级结构:

- `CSpace` 持有若干 `CGroup *`
- `CGroup` 内部持有固定数量的 `Capability *`

这样做的好处是:

- 逻辑容量很大
- 但未使用的 group 不需要分配

### 结构关系

```text
CHolder
  -> CSpace
       -> CGroup[group]
            -> Capability[slot]
                 -> Payload
```

## `Payload`

`cap::Payload` 是所有可被 capability 指向对象的基类。

### 核心职责

- 提供运行时类型标识 `PayloadType`
- 提供引用计数
- 定义 clone 行为
- 定义最终销毁行为

### 引用计数

每个 `Payload` 内部维护 `_refcount`，接口为:

- `keep()`
- `release()`
- `ref_count()`

`Capability` 构造时会 `keep()`，析构时会 `release()`。

因此 payload 的生命周期由引用它的 capability 数量决定。

### clone 语义

默认 `clone_payload()` 返回 `this`，表示共享同一个 payload。

具体 payload 可以覆写它，定义更复杂的 clone 行为。当前最重要的例子是:

- `MemoryPayload` 对非 shared memory 执行带 COW 语义的克隆

### 销毁语义

默认 `destruct()` 直接 `delete this`。

某些 payload 会覆写它，把对象释放和外部系统状态清理绑在一起，例如:

- `PCBPayload` 析构时把 PCB 送入 `TaskManager` 回收队列
- `MemoryPayload` 析构时释放持有的物理页

## `Capability`

`Capability` 是对 payload 的一层包装:

```cpp
Capability(Payload *payload, b64 perm)
```

它包含:

- `Payload *_payload`
- `b64 _perm`

### 主要能力

- `payload()`
- `payload_as<T>()`
- `perm()`
- `clear_perm(bits)`
- `clone()`
- `downgrade(new_perm)`
- `imply(required)`

### 权限单调性

`downgrade(new_perm)` 不是任意改权限，而是要求:

```cpp
perm::imply(old_perm, new_perm)
```

也就是新权限必须是旧权限的子集，能力只能降权，不能升权。

### clone 语义

`Capability::clone()` 会复制 capability 自身，但 payload 的克隆行为由 `Payload::clone_payload()` 决定。

所以 clone 可能有两种结果:

- 多个 capability 指向同一个 payload
- 多个 capability 指向经过定制复制的新 payload

## `CapObj<T>`

`CapObj<T>` 是面向对象操作层的通用封装。

它把:

- `Capability *`
- `PayloadType *`

绑定在一起，并提供:

- `cap()`
- `obj()`
- `imply(...)`

所有具体 capability 对象类，如 `EndpointObject`、`MemoryObject`、`PCBObject`，都是通过继承 `CapObj<T>` 来集中做权限检查与对象方法封装。

这意味着 syscall 层一般只负责:

1. lookup capability
2. 校验 payload type
3. 构造对应 `CapObj`
4. 调用对象方法

## `CGroup`

`CGroup` 是一组固定大小的 capability 槽位。

它提供:

- `get(idx)`
- `set(idx, cap)`
- `take(idx)`
- `remove(idx)`

### 语义

- `set()` 会替换旧槽位中的 capability，并负责删除旧 capability
- `take()` 只取出指针，不删除
- `remove()` 本质上是 `set(idx, nullptr)`

## `CSpace`

`CSpace` 是 `CHolder` 的实际能力空间。

它提供:

- `get(idx)`
- `set(idx, cap)`
- `remove(idx)`
- `take(idx)`
- `move(target, src)`
- `lookup_freeslot()`
- `foreach(...)`
- `clear()`

### 空闲槽位查找

`lookup_freeslot()` 会:

1. 先找未分配的 group
2. 再找已分配 group 里的空槽位

找不到时返回 `NO_FREE_SLOT`。

## `CHolder`

`CHolder` 是 capability 持有者。它内部拥有一个 `CSpace` 和一个持有者 ID。

典型的 CHolder:

- 进程
- 其他需要维护 capability 集合的内核对象

### 核心接口

- `lookup(idx)`
- `insert(idx, payload, perm)`
- `insert_to_free(payload, perm)`
- `create<PayloadType>(...)`
- `remove(idx)`
- `clear()`
- `clone(src_idx)`
- `derive(src_idx, new_perm)`
- `downgrade(idx, new_perm)`
- `transfer_to(dst, src_idx)`
- `copy_all_to(dst)`

## 插入与删除语义

### 插入

`insert()` 会:

1. 检查 `CapIdx` 合法性
2. 检查 payload 非空
3. 检查目标槽位空闲
4. 创建一个新的 `Capability(payload, perm)`
5. 放入目标槽位

### 删除

`remove(idx)` 最终调用 `set_slot(idx, nullptr)`。删除 capability 之后，如果这是最后一个引用该 payload 的 capability，就会触发 payload 的 `destruct()`。

## clone / derive / downgrade

### `clone(src_idx)`

`CHolder::clone()` 要求源 capability 持有 `perm::basic::CLONE`。

流程:

1. 找到源 capability
2. 检查 `CLONE`
3. 克隆 capability
4. 放到当前 holder 的空闲槽位
5. 如果 payload 是 `MemoryPayload` 且非 shared，则额外对内存做 COW 保护

所以 clone 不只是权限复制，还可能伴随对象语义层面的 clone。

### `derive(src_idx, new_perm)`

`derive()` 是:

1. 先 `clone()`
2. 再对新能力 `downgrade(new_perm)`

因此 derive 的语义是“在不影响原能力的前提下生成一个降权副本”。

### `downgrade(idx, new_perm)`

直接对已有槽位中的 capability 降权。

## 传递语义

### 基础权限位

定义在 [kernel/object/perm.h](/home/flysong/Sustcore/kernel/object/perm.h) 的基础能力权限有:

- `perm::basic::CLONE`
- `perm::basic::MIGRATE`
- `perm::basic::MIGRATE_ONCE`

### `transfer_to(dst, src_idx)`

`CHolder::transfer_to()` 的行为分两类:

1. 若源 capability 有 `CLONE`
2. 否则要求有 `MIGRATE` 或 `MIGRATE_ONCE`

#### 有 `CLONE`

它不会移动源槽位，而是:

- 克隆 capability
- 把克隆件插入目标 holder 的空闲槽位

#### 有 `MIGRATE` / `MIGRATE_ONCE`

它会:

1. 用相同 payload 在目标 holder 中创建新 capability
2. 目标权限位会清除 `MIGRATE_ONCE`
3. 成功后删除源槽位

因此:

- `MIGRATE` 是可移动传递
- `MIGRATE_ONCE` 是一次性转交

这套机制被 `endpoint` 的 capability 传递直接复用。

### `copy_all_to(dst)`

遍历当前 holder 的所有 capability，并尝试在相同槽位把 payload 和权限复制到目标 holder。

它不会重新编号，也不做复杂权限变换。

## `CHolderManager`

`CHolderManager` 是 CHolder 的全局管理器。

它维护:

- 全局 holder ID 分配
- `id -> CHolder *` 映射
- 一个递增时间戳

### 主要能力

- `init()`
- `inst()`
- `create_holder(...)`
- `get_holder(id)`
- `remove_holder(id)`
- `timestamp()`

当前 `timestamp()` 主要被能力传递相关路径预留使用。

## 权限系统

权限位分两层:

1. 基础能力权限位
2. 对象类型权限位

### 基础规则

`perm::imply(owned, required)` 本质上做位包含判断。

`perm::allperm()` 返回全 1，通常用于在创建新对象时授予完整权限。

### 对象级权限空间

当前定义了这些对象级权限集合:

- `perm::endpoint`
- `perm::reply`
- `perm::intobj`
- `perm::memory`
- `perm::mutex`
- `perm::notif`
- `perm::pcb`
- `perm::sintobj`
- `perm::vfile`

其中 `notif` 比较特殊: 每个 signal 占 2 个权限位，分别控制:

- `SIGNAL`
- `QUERY`

## 载荷类型

当前已定义的 `PayloadType` 包括:

- `INTOBJ`
- `SINTOBJ`
- `VFILE`
- `NOTIF`
- `MUTEX`
- `PCB`
- `TCB`
- `ENDPOINT`
- `MEMORY`
- `REPLY`

其中有些类型在 `kernel/object/` 中有完整对象封装，有些仍主要作为测试或兼容类型存在。

## 生命周期总结

能力系统的生命周期大致如下:

1. 创建 payload
2. 用 payload 构造 capability
3. capability 插入某个 CHolder 的 CSpace
4. 通过 clone / derive / transfer 传播 capability
5. 当最后一个 capability 被删除时，payload 执行自己的 `destruct()`

因此系统真正的所有权中心不是 capability 本身，而是 payload 的引用计数。

## 当前设计特点

这套能力系统有几个明显特点:

- **能力和对象分离**: capability 只保存权限和 payload 指针，对象操作逻辑在 `CapObj<T>` 子类中。
- **权限单调下降**: derive/downgrade 只能变弱，不能变强。
- **传递语义显式化**: clone 和 migrate 是两条不同路径。
- **对象语义可定制**: payload 可以覆写 clone/destruct，自定义复制和销毁行为。
- **与 syscall 解耦**: syscall 层只做 lookup 和类型分派，真正的权限检查在对象层完成。
