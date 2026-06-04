# Drivers

本文总结 `kernel/driver/` 下的驱动框架和当前已经接入的设备驱动。

## 总体分层

驱动子系统可以分成三层:

1. **驱动基类层**: `DriverBase`
2. **工厂与生命周期层**: `IDeviceFactory` / `IIrqChipFactory` / `DriverModel`
3. **具体驱动层**: 串口、RTC、中断控制器、时钟设备

设备框架提供 `DeviceNode` 和资源，驱动框架负责把这些输入实例化成真正可运行的对象。

## `DriverBase`

`driver::DriverBase` 是所有驱动对象的共同基类。

### 它持有什么

驱动并不直接拥有平台节点，而是持有:

- `const DeviceNode *_node`
- `std::vector<owner<VIrqResource *>> _virqs`
- `std::vector<owner<MMIOResource *>> _mmios`

这些资源通过 `DriverBase::DevRes` 一次性打包传入。

### 它解决什么问题

1. 统一保存驱动节点与资源
2. 在析构时回收资源副作用
3. 提供少量公共辅助函数

### 自动清理语义

`DriverBase::~DriverBase()` 会自动:

- 注销所有已注册的 virq handler
- 解除所有已映射的 MMIO

这很重要，因为项目里大多数具体驱动本身没有手写复杂析构逻辑，资源回收主要依赖这里。

### 公共辅助函数

`DriverBase::__load_integral(...)` 用于从 `DeviceNode` 读取整型属性，目前串口驱动用它读取 `clock-frequency`。

## 工厂模型

### 两类工厂

驱动框架区分两类工厂:

- `IDeviceFactory`: 普通设备
- `IIrqChipFactory`: 中断控制器

二者接口基本一致，都以 `compatible()` 为索引，以 `create(node, model)` 为创建入口。

### 注册表

有两个注册表:

- `DeviceFactoryRegistry`
- `IrqChipFactoryRegistry`

查找策略都一样:

- 遍历节点 `compatible` 列表
- 按顺序查找第一个匹配的工厂

这意味着 FDT `compatible` 的先后顺序直接影响驱动匹配结果。

## `DriverModel`

`DriverModel` 是驱动系统的单例管理器。

它维护:

- 普通工厂注册表
- IRQ 工厂注册表
- 两类工厂对象的所有权
- 已创建驱动对象的所有权

### 主要职责

- 注册工厂
- 按节点选择匹配工厂
- 创建驱动
- 接管驱动与工厂生命周期

### 创建策略

`create_driver(node)` 的优先级是:

1. 先找 IRQ 工厂
2. 找不到再找普通设备工厂

所以如果一个节点同时能匹配两类工厂，IRQ 工厂优先。

## MMIO 访问辅助

`driver/base.h` 中定义了两个辅助模板:

- `mmio_reference<T, offset>`
- `mmio_offset_reference<T>`

它们本质上是对 `volatile` MMIO 指针的轻量包装，提供:

- `read()`
- `write()`
- 赋值语法
- 隐式读取

当前具体驱动更多直接使用寄存器结构体映射，但这些模板为后续更细粒度寄存器封装留好了接口。

## 已接入的普通驱动

### `SerialDevice`

`SerialDevice` 是当前的 NS16550A 风格串口驱动。

#### 绑定条件

主 `compatible`:

```cpp
"ns16550a"
```

#### 创建过程

`SerialDeviceFactory::create()` 会:

1. 从节点读取 `clock-frequency`
2. 提取 virq 与 mmio 资源
3. 校验第一个 MMIO 区域至少覆盖 UART 寄存器布局
4. 将该 MMIO 区域映射到内核地址空间
5. 构造 `SerialDevice`

#### 提供的功能

- `clock_frequency()`
- `writec(char)`
- `write(const char *, size_t)`

当前实现是非常薄的发送型驱动，没有接收路径、FIFO 控制或中断收发逻辑。

### `GoldfishRTC`

`GoldfishRTC` 是 `google,goldfish-rtc` 驱动。

#### 绑定条件

主 `compatible`:

```cpp
"google,goldfish-rtc"
```

#### 创建过程

`GoldfishRTCFactory::create()` 会:

1. 提取 virq 与 mmio 资源
2. 使用第一个 virq 作为 RTC alarm virq
3. 使用第一个 MMIO 作为 RTC 寄存器基址
4. 映射 MMIO
5. 构造 `GoldfishRTC`

#### 运行时行为

构造函数里会立即:

- 刷一次 RTC 时间寄存器
- 为第一个 virq 注册 `handle_alarm_irq`

#### 提供的功能

- `read_time()`
- `set_alarm(units::time when, AlarmHandler handler)`

`set_alarm()` 会:

1. 关闭 IRQ
2. 清中断状态
3. 写入闹钟时间
4. 打开 IRQ
5. 使能对应 virq

闹钟中断到达时:

1. 清除 RTC 中断
2. 调用用户提供的 `_alarm_handler`
3. 调用 `DeviceModel::interrupt().ack(event)` 对中断链路应答

## 时钟与定时器抽象

虽然 `clock.*` 不直接对应某个设备节点，但它属于驱动侧运行时设施。

### `ClockSource`

提供只读时间基准:

- `now()`
- `frequency()`
- `to_ns(ticks)`

当前实现是 `CSRTimeClockSource`，它直接读取 RISC-V CSR `time`。

### `Alarm`

`Alarm` 表示“能编程下一次中断”的设备。它绑定一个 `ClockSource`，提供:

- `set_next_event(delta)`
- `max_delta()`
- `set_handler(...)`

### `ClintAlarm`

`ClintAlarm` 是当前具体 Alarm 实现。

它的特点是:

- 依赖 CLINT 时钟 virq
- 用 `sbi_legacy_set_timer` 编程下一次时钟事件
- 构造时直接向 `IrqManager` 注册 clock virq handler

### `TimeKeeper`

`TimeKeeper` 维护一个当前 hart 的到期动作小根堆。

职责:

- `enqueue(action)`
- `on_timer_irq(event)`
- `rearm_timer()`

运行逻辑是:

1. 新动作入队
2. 若堆顶变化则重编程硬件定时器
3. 时钟 IRQ 到来后弹出所有到期动作
4. 执行后再重编程下一次最早到期事件

这部分构成了内核延时/定时任务调度的基础设施。

## 中断控制器驱动在驱动层的位置

`RiscVIntC`、`Clint`、`Plic` 从分类上也属于驱动，只是它们同时参与中断框架本身的搭建。详细的域绑定、级联和 ack 流程放在 `intterupt.md` 中单独说明。

## 已接入驱动清单

当前在 FDTProvider 中自动注册的驱动工厂如下:

### 普通驱动

- `SerialDeviceFactory`
- `GoldfishRTCFactory`

### IRQ 驱动

- `RiscVIntCIrqFactory`
- `ClintIrqFactory`
- `PlicIrqFactory`

## 典型创建流程

一个普通设备驱动的创建流程是:

1. `DriverModel::create_driver(node)`
2. 注册表按 `compatible` 找到工厂
3. 工厂从 `DeviceNode` 中提取资源
4. 如有需要，工厂先做 MMIO 映射
5. 构造具体驱动
6. `DriverModel` 接管驱动生命周期

一个 IRQ 驱动的创建流程额外还会:

1. 创建或注册 `IrqDomain`
2. 建立 `phandle -> domain` 的 FDT 映射
3. 视情况挂接到父域

## 当前设计特点

这个驱动框架的设计特点很明确:

- 资源先于驱动: 先把节点翻译成资源，再构造驱动
- 工厂负责装配: 驱动本身尽量不碰平台解析逻辑
- 生命周期集中管理: `DriverModel` 接管驱动对象，`DriverBase` 负责收尾
- MMIO 使用显式映射: 驱动只能操作已经由 `MMIOManager` 映射好的区域

对当前仓库而言，这套框架已经足够支撑串口、RTC 和中断控制器三类驱动。
