# Interrupt System

本文总结 `kernel/device/int.*`、`kernel/driver/int/*` 和相关 FDT 装配逻辑。它描述当前内核的中断框架，以及 `riscv,cpu-intc`、`clint`、`plic` 三类中断控制器如何接入。

## 总体模型

当前中断系统是三层结构:

1. **IrqChip**: 具体中断控制器驱动，面向硬件
2. **IrqDomain**: 域内 `hwirq <-> virq` 映射层
3. **IrqManager**: 全局 virq 分发与 handler 注册中心

其中:

- `hwirq` 是某个控制器域内的本地中断号
- `virq` 是全局稳定编号
- `domain` 是某个控制器实例对应的命名空间

## 基本类型

关键类型定义在 `kernel/device/device.h` 和 `kernel/driver/int/base.h`:

- `domain_t`
- `hwirq_t`
- `virq_t`
- `intc_t`
- `irq_prio_t`
- `cpu_mask_t`

特殊常量:

- `INVALID_DOMAIN_ID`
- `INVALID_ICTRL_ID`
- `INVALID_VIRQ`

## `IrqChip`

`IrqChip` 是所有中断控制器驱动的抽象基类，继承自 `DriverBase`。

它要求子类实现:

- `enable_irq(hwirq)`
- `disable_irq(hwirq)`
- `set_priority(hwirq, prio)`
- `set_affinity(hwirq, mask)`
- `ack(event)`
- `set_trigger(hwirq, trigger)`

另有一个可覆写钩子:

```cpp
attach_to_parent_domain(IrqManager &, IrqDomain &)
```

默认实现为空，表示该控制器不需要父域挂接。级联控制器如 PLIC 会覆写它。

## `IrqDomain`

`IrqDomain` 抽象某个控制器实例的本地中断域。

它负责:

- 绑定 `hwirq -> virq`
- 反查 `virq -> hwirq`
- 判断某个 `virq` 是否属于本域
- 把 enable/disable/ack/priority/trigger 等操作转发给底层 `IrqChip`

### `LinearIrqDomain`

当前唯一实现是 `LinearIrqDomain<MAX_HW_IRQ>`。

它假设:

- `hwirq` 是从 `0` 或 `1` 开始的线性编号
- 域大小固定

内部保存:

- `std::optional<virq_t> _virqs[MAX_HW_IRQ]`
- `std::unordered_map<virq_t, hwirq_t> _virq_to_hwirq`

因此绑定后支持双向查询。

## `IrqManager`

`IrqManager` 是全局中断中心，保存在 `DeviceModel` 中。

它维护:

- `domain_id -> IrqDomain`
- `virq -> {domain_id, hwirq}`
- `virq -> handler`
- `_next_virq`

### 核心能力

- `register_domain(domain)`
- `allocate_virq(domain_id, hwirq)`
- `get_domain(domain_id)`
- `resolve(virq)`
- `enable_irq(virq)`
- `disable_irq(virq)`
- `ack(event)`
- `set_priority(virq, ...)`
- `set_affinity(virq, ...)`
- `set_trigger(virq, ...)`
- `register_handler(virq, handler)`
- `unregister_handler(virq)`
- `dispatch(event)`

### `allocate_virq`

它是 virq 稳定分配的唯一入口。

行为是:

1. 找到目标 domain
2. 如果该 `hwirq` 已绑定过 virq，则直接复用
3. 否则分配新的全局 virq
4. 更新 `IrqDomain` 和 `_virq_map`

因此同一个 `(domain, hwirq)` 永远对应同一个 virq。

## 中断事件对象

`IrqEvent` 是一次分发的上下文:

- `virq`
- `hw_irq`
- `domain`
- `chip_specific[2]`

`chip_specific` 是为具体芯片保留的上下文槽。当前 PLIC 使用它记录 `ctx_id`。

## 本地中断控制器: `RiscVIntC`

`RiscVIntC` 表示单个 hart 的本地中断入口，`compatible = "riscv,local-intc"`。

### 支持的 hwirq

当前主要处理三类 Supervisor 级本地中断:

- `SOFTWARE_LOCAL_IRQ = 3`
- `CLOCK_LOCAL_IRQ = 7`
- `EXTERNAL_LOCAL_IRQ = 9`

### 创建过程

`RiscVIntC::create(...)` 会:

1. 构造 `RiscVIntC`
2. 为其创建 `LinearIrqDomain<16>`
3. 将该 domain 注册进 `IrqManager`
4. 调用 `initialize(domain)`

这里的 domain ID 直接取 `identifier`，而在 FDT 装配中该标识通常就是 local-intc 的 phandle。

### 硬件操作

`enable_irq()` / `disable_irq()` 直接读写 RISC-V CSR `sie`:

- `ssie`
- `stie`
- `seie`

### ack 语义

`ack(event)` 的实现是重新 `enable_irq(event.hw_irq)`。

这说明本地中断的处理模型是:

1. 分发前或处理中先屏蔽该本地源
2. ack 时重新打开

### 主动投递

`post(hwirq)` 会:

1. 通过 domain 把 `hwirq` 转成 virq
2. 先 `disable_irq(hw_irq)`
3. 手工构造 `IrqEvent`
4. 调用 `IrqManager::dispatch(event)`

这为本地软件中断或定时中断提供了统一分发入口。

## CLINT

`Clint` 表示 RISC-V Core Local Interruptor。

### 它在当前设计中的角色

CLINT 目前更像一个“系统时钟和软件中断描述设备”，而不是一个完整的硬件控制器实现。

从接口看它是 `IrqChip`，但其大部分操作都返回 `NOT_SUPPORTED`:

- `enable_irq`
- `disable_irq`
- `set_priority`
- `set_affinity`
- `ack`
- `set_trigger`

### 它实际提供什么

它主要提供两个 virq 语义:

- `software_virq()`
- `clock_virq()`

并记录:

- 自身 `identifier`
- 默认 `hart_id`
- 覆盖的 `target_harts`

### 域模型

FDT 的 `ClintIrqFactory` 会额外为其创建 `LinearIrqDomain<16>`，然后把 CLINT 节点 phandle 映射到该 domain。

不过当前 `Clint` 自己并没有覆写 `attach_to_parent_domain()`，所以它并不执行级联挂接逻辑。

### 时钟 virq

系统时钟中断 virq 是在 `ClintIrqFactory::create()` 中写入 `DeviceModel` 的:

```cpp
model.set_clock_virq(clint.clock_virq());
```

后续 `ClintAlarm` 会使用这个 virq。

## PLIC

`Plic` 是当前中断系统里实现最完整的级联控制器。

### 基本模型

它持有:

- 控制器 `identifier`
- 中断源数量 `_srccnt`
- context 数量 `_ctxcnt`
- `std::vector<Context> _contexts`
- `virq -> ctx_id` 映射
- 自身 `IrqDomain *`
- MMIO 寄存器基址和各寄存器块视图

### `Context`

每个 PLIC context 记录:

- `hart_id`
- `external_virq`
- `ctx_id`
- `enabled`
- `completed`

`external_virq` 不是 PLIC 自己域内的 virq，而是父域中“外部中断入口”的 virq，也就是 local-intc 的 `EXTERNAL_LOCAL_IRQ` 所映射出来的 virq。

### 创建过程

`Plic::create(...)` 会:

1. 校验有 MMIO、virq 和 context
2. 映射第一个 MMIO 区域
3. 构造 `Plic`
4. 创建 `LinearIrqDomain<PLIC_MAX_SOURCES>`
5. 把 domain 注册到 `IrqManager`
6. 调用 `initialize(domain)`

### `initialize()` 做的事

1. 找到第一个 enabled context，保存为 `_first_enabled_ctx`
2. 对所有 context 的所有 source 执行 `disable_irq_for`
3. 把所有 source 优先级设置为 1
4. 把所有 context threshold 置 0
5. 为 `1.._srccnt` 的每个 source 分配/绑定 virq

所以 PLIC 初始化完成后，它的每个硬件中断源都有稳定 virq。

### 父域挂接

`attach_to_parent_domain()` 是 PLIC 最关键的部分。

它会对每个 enabled context:

1. 找到对应的 `VIrqResource`，即父域 `external_virq`
2. 为该 virq 注册 `handle_parent_irq`
3. 记录 `_virq_to_context[parent_virq] = ctx_id`
4. 使能该父域 virq

这意味着 PLIC 是通过“监听 local-intc 的 external interrupt virq”接入整套系统的。

### 二级分发流程

当某个外部中断到来时，链路如下:

1. hart 本地 `external` 中断到来
2. local-intc 对应 virq 被分发
3. 该 virq 的 handler 是 `Plic::handle_parent_irq`
4. PLIC 根据父 virq 找到 context
5. PLIC 对该 context 执行 `claim()`
6. 得到真正的 PLIC `hw_irq`
7. 通过本域把 `hw_irq -> virq`
8. 构造 `IrqEvent{virq, hw_irq, domain, ctx_id}`
9. 调用 `IrqManager::dispatch(event)`

### ack 流程

`Plic::handle_parent_irq()` 里先注册了一个 RAII `acker`，它会在函数退出时对父域 event 调用:

```cpp
irqman().ack(parent_event)
```

也就是先保证父域入口最终被应答。

而对 PLIC 自身的子中断，`Plic::ack(event)` 会:

1. 校验 `event.hw_irq` 与 `event.chip_specific[0]`
2. 向 `complete` 寄存器写回 `hw_irq`
3. 标记该 context `completed = true`
4. 清理 `_claimlist`

因此 PLIC 的 ack 语义是“complete claimed source”，与 local-intc 的重新开中断不同。

### 当前限制

当前 PLIC 还有几个明确限制:

- `set_affinity()` 未实现
- `enable_irq()` / `disable_irq()` 只作用于 `_first_enabled_ctx`
- 没有锁保护 claim/enable bitmap
- `set_trigger()` 只接受 `EDGE_RISING`

这些都在代码里已有 `TODO` 或显式返回 `NOT_SUPPORTED`。

## FDT 装配下的中断初始化顺序

当前实际顺序是:

1. 先注册所有 CPU local-intc
2. 建立 local-intc 的 `phandle -> domain`
3. 再注册 CLINT / PLIC
4. CLINT / PLIC 在创建时解析自己的父域引用
5. 普通设备节点在读取 `irqs()` 时再解析 virq

这套顺序保证了:

- 叶子设备看到的 `interrupt-parent` 一定能解析到 domain
- 级联控制器可以向父域挂接

## `ClintAlarm` 和时钟中断

`device/int.cpp` 中的 `ClintAlarm` 把时钟中断接到了定时器框架。

### 构造行为

构造 `ClintAlarm(clock_source, clock_virq)` 时会:

1. 记录当前时间为 `_last_recorded_time`
2. 向 `IrqManager` 注册 `clock_virq -> handle_irq`

### 定时器编程

`set_next_event(delta)` 调用:

```cpp
sbi_legacy_set_timer(now + delta * frequency)
```

这说明当前硬件时钟事件的真正编程接口是 SBI 定时器，而不是直接操作 CLINT MMIO。

### 时钟 IRQ 处理

`handle_irq(event)` 会:

1. 验证 virq 匹配
2. 读取当前时间
3. 调用上层 `_handler(ClockEvent{last, now})`
4. 更新 `_last_recorded_time`

这里不直接调用 `ack`，因为时钟 IRQ 的语义更多由上层和 local-intc/SBI 链路保证。

## 一个完整的中断路径示例

以外设挂在 PLIC 上为例，完整路径如下:

1. FDT 设备节点声明 `interrupts = <N>` 或 `interrupts-extended`
2. `FDTDeviceNode::irqs()` 延迟解析
3. FDTProvider 把 `(plic_phandle, hwirq=N)` 解析成 `virq`
4. 设备驱动通过 `VIrqResource::register_handler(...)` 注册 handler
5. 外部中断到来，hart 收到 local-intc 的 external 中断
6. local-intc 的 virq 分发到 `Plic::handle_parent_irq`
7. PLIC claim 出真实 source
8. PLIC 把 source 映射到该设备的 virq
9. `IrqManager::dispatch(event)` 调到设备 handler
10. 设备 handler 处理完成后调用 `IrqManager::ack(event)`
11. PLIC complete source
12. 父域 local-intc 在 `handle_parent_irq` 退出时被 ack

这是当前系统里最完整的一条中断分发链。
