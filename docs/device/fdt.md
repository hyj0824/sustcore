# FDT Backend

本文总结 `kernel/device/fdt.*` 的实现。它是当前项目的主设备后端，负责把 DTB 解析成统一设备模型，并完成中断控制器、CPU、内存区和普通设备节点的注册。

## 总体职责

`fdt::FDTProvider` 是 `DeviceProvider` 的实现。它负责:

1. 解析 DTB，构建内部 `Configuration`
2. 注册默认设备工厂和 IRQ 工厂
3. 将 FDT 节点包装成 `FDTDeviceNode`
4. 向 `DeviceModel` 导入内存区、CPU、普通节点和中断域映射

对设备框架来说，FDTProvider 是“平台事实来源”。

## 内部数据结构

### `Property`

`fdt::Property` 是对 FDT 原始属性的一层薄包装，直接引用 DTB 内部缓冲区，不做复制。

它提供几种解析能力:

- `as_string_list()`
- `as_string()`
- `as_integral()`
- `as_phandle()`
- `as_byte_array()`
- `as_regions(RegionCells)`

`as_regions()` 专门用来解析 `reg` 风格属性，按 `#address-cells + #size-cells` 解释成 `std::vector<PhyArea>`。

### `Node`

`fdt::Node` 是 FDT 树的内存表示，包含:

- `name`
- `properties`
- `children`
- `parent`
- `phandle`

### `Configuration`

`Configuration` 是整棵树的根对象，包含:

- `root`
- `phandle_map`

它提供:

- `get_node_by_path(path)`
- `get_node_by_phandle(phandle)`

因此 FDTProvider 后续的 CPU、中断、节点注册都建立在 `Configuration` 上。

## 解析阶段

`make_config(dtb, config)` 是 DTB 解析入口。

### 过程

1. 初始化 `FDTHelper`
2. 创建根节点 `/`
3. 递归遍历每个 FDT 节点
4. 为每个属性建立 `Property`
5. 自动识别 `phandle` / `linux,phandle`
6. 将非零 phandle 加入 `phandle_map`

最终得到一份独立于 libfdt 迭代接口的内存结构树。

## `FDTDeviceNode`

`FDTDeviceNode` 是 `DeviceNode` 的 FDT 实现，负责把 FDT 原始属性映射到设备框架定义的统一语义。

### 基本信息

- `name()` 返回 FDT 节点名
- `platform()` 固定返回 `"fdt"`

### 属性映射

`property(name)` 的规则如下:

- `compatible` -> 直接映射 `compatible`
- `mmio` -> 解析 `reg`
- `irqs` -> 解析 `interrupts-extended` 或 `interrupts`
- `interrupt-parent` -> 沿祖先链解析生效的 `interrupt-parent`
- 其它属性 -> 直接按原名读取

### 原始属性推断类型

`raw_property()` 并不完全依赖属性名，而是结合长度推断类型:

- `compatible`、`status`、`device_type` -> `STRING` / `STRING_LIST`
- 4 字节属性 -> `INTEGER`
- 4 字节对齐的多元素属性 -> `INTEGER_LIST`
- 其它 -> `BYTE_ARRAY`

### MMIO 解析

`mmio_property()`:

1. 查找 `reg`
2. 依据父节点的 `#address-cells` / `#size-cells`
3. 解析成 `std::vector<PhyArea>`
4. 封装成 `DevicePropView::REGION_LIST`

### IRQ 解析

`irq_property()` 是延迟解析的。它只创建 `DevicePropView::from_virq_list(loader)`，真正解析发生在驱动访问 `node.irqs()` 时。

这样做的好处是:

- 设备节点注册不依赖中断域必须先就绪
- 中断控制器节点创建顺序更灵活

## 中断引用解析

FDT 后端统一把设备中断描述解析成:

```cpp
using InterruptRef = std::pair<phandle_t, hwirq_t>;
```

也就是“某个控制器 phandle 下的某个 hwirq”。

### 支持的两种来源

1. `interrupts-extended`
2. `interrupt-parent + interrupts`

当前实现只支持:

- `#interrupt-cells == 1`

也就是说每个中断条目最终只会解析出一个 `hwirq_t`。

### 解析步骤

1. 先把属性解析成 `InterruptRef` 列表
2. 根据 `phandle -> domain_id` 映射解析到 `IrqDomain`
3. 交给 `IrqManager::allocate_virq(domain_id, hwirq)` 分配稳定 virq
4. 返回 `std::vector<virq_t>`

因此 FDT 后端并不自己管理 virq 编号，只负责把 FDT 中断引用翻译成“域 + hwirq”。

## Provider 初始化行为

`FDTProvider::FDTProvider(void *dtb)` 会立即做三件事:

1. `make_config(dtb, _config)`
2. `init_device_factories()`
3. `init_irq_factories()`

### 默认普通设备工厂

当前默认注册:

- `driver::SerialDeviceFactory`
- `driver::GoldfishRTCFactory`

### 默认 IRQ 工厂

当前默认注册:

- `RiscVIntCIrqFactory`
- `ClintIrqFactory`
- `PlicIrqFactory`

其中后两者依赖 provider 内部的辅助状态:

- `_cpu_intc_candidates`
- `_local_intc_map`
- `_irq_domains`

## 导入 `DeviceModel` 的顺序

`FDTProvider::register_device(model)` 的执行顺序是固定的:

1. `register_memory_regions(model)`
2. `register_cpus(model)`
3. `register_nodes(model)`
4. `register_intcs(model)`
5. `register_clock_virq(model)`

这很关键，因为后面的步骤依赖前面建立的上下文。

## 内存区导入

### 普通内存

FDTProvider 会扫描根节点下所有 `device_type = "memory"` 的节点，把其 `reg` 解析成 `FREE` 区域。

### 保留内存

同时会扫描 `/reserved-memory` 下的子节点，把它们的 `reg` 解析成 `RESERVED` 区域。

导入后的区域再交给 `DeviceModel::collect_memory_regions()` 统一规范化。

## CPU 导入

CPU 导入逻辑集中在 `register_cpus(model)`。

### 做了什么

1. 清空旧 `_irq_domains`、`_cpu_intc_candidates`、`_local_intc_map`
2. 清空 `CpuGroupInfo`
3. 解析 `/cpus`
4. 读取 `timebase-frequency`
5. 创建 `CSRTimeClockSource`
6. 解析每个 CPU 节点
7. 构建 `RiscV64Cpu`
8. 构建 CPU 拓扑
9. 收集每个 CPU 的本地中断节点候选

### CPU 节点要求

一个可用 CPU 节点至少需要:

- `device_type = "cpu"`
- `reg`
- `riscv,isa`
- 子节点中存在 `interrupt-controller`

并会额外尝试读取:

- `model`
- `mmu-type`

### 本地中断映射

解析 CPU 时还会建立:

- `cpu_phandle -> cpuid`
- `local_intc_phandle -> cpuid`

后者对 CLINT 和 PLIC 的目标 hart 解析很关键。

## 普通节点注册

`register_nodes(model)` 负责把“可见设备节点”包装成 `FDTDeviceNode` 并交给 `DeviceModel`。

### 可见节点判定

`scan_visible_nodes()` 会:

- 跳过 `status != ok/okay` 的节点
- 跳过没有 `compatible` 的节点
- 遇到 `simple-bus` 时递归进入子节点，但不把 bus 本身作为设备注册

所以普通设备扫描表现为“穿透 simple-bus，只注册可绑定设备节点”。

### CPU local intc 的特殊处理

`/cpus` 下的本地中断端点通常不会被通用可见节点扫描覆盖，所以 `register_nodes()` 会在末尾补登记 `_cpu_intc_candidates` 对应的节点。

## IRQ 控制器注册

`register_intcs(model)` 分两轮执行。

### 第一轮: CPU local intc

先创建 `riscv,cpu-intc`，目的是尽快建立根中断域映射。

### 第二轮: 其它 IRQ 控制器

再创建其它控制器，如:

- CLINT
- PLIC

这使得级联控制器在创建时能够解析其父域。

## IRQ 域映射表

`FDTProvider` 内部维护:

```cpp
std::unordered_map<phandle_t, domain_t> _irq_domains;
```

它不是硬件事实，而是“FDT 控制器 phandle -> 运行时 IrqDomain ID”的解析缓存。

该映射由各 IRQ 工厂在成功创建控制器后登记。

例如:

- `RiscVIntCIrqFactory` 把 CPU local intc phandle 映射到对应 domain
- `ClintIrqFactory` 把 CLINT 节点 phandle 映射到新建 domain
- `PlicIrqFactory` 把 PLIC 节点 phandle 映射到新建 domain

这样普通设备节点后续在解析 `interrupts` 时，就能按 phandle 找到正确的中断域。

## 三个 IRQ 工厂的装配逻辑

### `RiscVIntCIrqFactory`

它从 `_cpu_intc_candidates` 中找到与当前节点匹配的 CPU 本地中断描述，然后:

1. 提取节点资源
2. 调用 `RiscVIntC::create(...)`
3. 取回创建后的 domain
4. 将 `phandle -> domain_id` 登记到 provider

### `ClintIrqFactory`

它会:

1. 读取当前 CLINT 节点的 `interrupts-extended`
2. 利用 `_local_intc_map` 解析目标 hart 列表
3. 提取当前节点的 MMIO 与 virq 资源
4. 创建 `Clint`
5. 手动新建 `LinearIrqDomain<16>`
6. 把 CLINT 自身 phandle 映射到该 domain
7. 设置 `model.set_clock_virq(clint.clock_virq())`

注意: 当前 `Clint::attach_to_parent_domain()` 使用的是 `IrqChip` 默认实现，即它自己不做二级级联注册。

### `PlicIrqFactory`

它会:

1. 读取 PLIC 节点的 `interrupts-extended`
2. 将每个条目转换成 `Plic::Context`
3. 从 `riscv,ndev` 读取中断源数量
4. 提取节点资源
5. 创建 `Plic`
6. 将 PLIC phandle 映射到对应 domain
7. 调用 `plic.attach_to_parent_domain(...)`

这里真正完成了 PLIC 到父域的二级挂接。

## PLIC context 的构造规则

`build_plic_contexts()` 会遍历 `interrupts-extended` 中的每个条目，并把它解释成一个 PLIC context。

单个 context 的构造过程:

1. 通过条目的 phandle 找到 CPU local intc 节点
2. 反向找到其父 CPU 节点
3. 解析出 `hart_id`
4. 按 `ctx_id = hart_id * 2 + index % 2` 生成 context 编号
5. 若父域已就绪，则为 `(parent_domain, hwirq)` 分配 `external_virq`
6. 置 `enabled = true`

如果父域还未解析成功，context 会保留为 disabled。

因此 PLIC context 是一种“FDT 条目 + 父域 virq + hart 归属”的运行时对象。

## `register_clock_virq`

当前 `register_clock_virq()` 只做日志输出。真正把时钟 virq 写入模型的是 `ClintIrqFactory::create()` 中的:

```cpp
model.set_clock_virq(clint.clock_virq());
```

也就是说，系统时钟中断的来源目前由 CLINT 决定。

## 总结

FDT 后端的角色可以概括为:

- 把 DTB 解析成内存中的设备树
- 把设备树节点翻译成统一 `DeviceNode`
- 把 phandle 中断引用翻译成运行时 `IrqDomain + virq`
- 按正确顺序把 CPU、本地中断、级联中断和普通设备接入 `DeviceModel`

它既是平台解析器，也是当前设备系统的装配器。
