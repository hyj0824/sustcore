# 驱动与设备模型

## 设计目标

1. **屏蔽平台差异**: 驱动不直接读取 DTB/ACPI 原始结构, 统一通过 `DeviceNode` 查询属性。
2. **集中维护全局设备状态**: 由 `DeviceModel` 保存设备节点、内存区域、CPU 信息和中断管理器。
3. **把节点属性转成运行时资源**: 将设备节点中的 MMIO 和中断声明转换成 `MMIOResource` / `VIrqResource`。
4. **为驱动层提供稳定入口**: 驱动只依赖 `DeviceNode`、资源对象和 `DriverModel`。

## DeviceModel —— 设备信息聚合中心

`device::DeviceModel` 是设备系统的核心单例, 统一维护:

| 成员         | 说明                                    |
| ------------ | --------------------------------------- |
| `_devices`   | 所有已注册的统一设备节点                |
| `_regions`   | 物理内存区域 (含状态: FREE/RESERVED 等) |
| `_cpus`      | CPU 列表、频率和拓扑信息                |
| `_interrupt` | 全局中断管理器 (IrqManager)             |
| `_providers` | 设备信息提供者列表                      |

### Provider 机制

`DeviceProvider` 是设备信息来源接口:

```cpp
class DeviceProvider {
    virtual Result<void> register_device(DeviceModel &model) const = 0;
    virtual const char *name() const = 0;
};
```

项目中的 Provider:

| Provider         | 职责                                                   |
| ---------------- | ------------------------------------------------------ |
| `FDTProvider`    | 解析 DTB (Flattened Device Tree), 导入所有硬件设备节点 |
| `KernelProvider` | 将内核镜像 `[skernel, ekernel)` 注册为 RESERVED 内存区 |
| PCI 枚举         | (规划中) 通过 PCI bus 枚举生成 PCI DeviceNode          |

## DeviceNode —— 统一设备节点

`device::DeviceNode` 是平台无关的统一设备节点接口。驱动层只应依赖这个接口, 而不是具体的 FDT 节点结构。

### 最小接口

```cpp
class DeviceNode : public RTTIBase<DeviceNode, DevicePlatform> {
    virtual const char *name() const noexcept = 0;
    virtual DevicePlatform platform() const noexcept = 0;
    virtual Optional<DevicePropView> property(string_view name) const = 0;
};
```

### 平台类型

```cpp
enum class DevicePlatform { FDT, PCI };
```

当前主要实现是 `fdt::FDTDeviceNode`, 它将原始 FDT 节点的属性按语义包装为统一的属性视图。PCI 设备同样有对应的 `pci::PCIDeviceNode`。

### 统一语义属性

| 方法                   | 统一语义      | 对应 FDT 属性      |
| ---------------------- | ------------- | ------------------ |
| `name()`               | 设备名称      | —                  |
| `compatibles()`        | 兼容性列表    | `compatible`       |
| `is_compatible_with()` | 兼容性匹配    | `compatible`       |
| `mmio_regions()`       | MMIO 区域列表 | `reg` 中的 MMIO    |
| `irqs()`               | 虚拟中断列表  | `interrupts` 等    |
| `interrupt_parent()`   | 中断父控制器  | `interrupt-parent` |

### DevicePropView —— 统一属性视图

| 类型           | 说明                                  |
| -------------- | ------------------------------------- |
| `STRING`       | 字符串                                |
| `STRING_LIST`  | 字符串列表                            |
| `INTEGER`      | 整数 (u32/u64)                        |
| `INTEGER_LIST` | 整数列表                              |
| `BYTE_ARRAY`   | 字节数组                              |
| `REGION_LIST`  | MMIO 区域列表 (框架构造)              |
| `VIRQ_LIST`    | 虚拟中断列表 (框架构造, 支持延迟求值) |

## 内存区域模型

`MemRegion` 由 `PhyArea + MemoryStatus` 组成:

| 状态               | 说明                       |
| ------------------ | -------------------------- |
| `FREE`             | 可用内存                   |
| `RESERVED`         | 保留内存 (内核/DTB/initrd) |
| `ACPI_RECLAIMABLE` | ACPI 可回收                |
| `ACPI_NVS`         | ACPI 非易失存储            |
| `BOOT_DATA`        | 启动数据                   |

## 驱动模型

### DriverBase —— 驱动基类

`driver::DriverBase` 是所有驱动的基类。它通过 `DevRes` 持有设备的运行时资源:

```cpp
struct DevRes {
    const DeviceNode *node;                         // 设备节点 (非拥有)
    vector<owner<VIrqResource *>> virqs;            // 虚拟中断资源
    vector<owner<MMIOResource *>> mmios;            // MMIO 资源
};
```

### 中断资源

`VIrqResource` 封装一个虚拟中断号 (virq), 提供:

- `register_handler(handler)`: 注册中断处理函数
- `enable()` / `disable()`: 使能/禁用中断
- `set_priority(prio)`: 设置中断优先级

### MMIO 资源

`MMIOResource` 封装一个物理 MMIO 区域, 提供:

- `region()`: 获取 `PhyArea`
- `kva()`: 获取映射后的内核虚拟地址 (KVA)
- `size()`: 区域大小

### DriverModel —— 驱动生命周期管理

`driver::DriverModel` 统一掌管驱动工厂注册、工厂对象生命周期和运行时驱动对象生命周期。

核心流程:

```
DriverModel::activate_runtime(device_nodes)
    │
    ├─ 遍历所有 device_nodes
    │   └─ 对每个节点, 遍历所有注册的 IDeviceFactory
    │       └─ factory.probe(node, model, driver_flag)
    │           │
    │           └─ 匹配成功
    │               └─ factory.create(node, model, driver_flag)
    │                   └─ 返回 DriverBase *
    │
    └─ 在 DevFS 中为每个设备创建 /sys/ 目录项
```

### 驱动工厂与设备匹配

`IDeviceFactory` 定义驱动如何匹配和创建:

```cpp
class IDeviceFactory {
    virtual const DeviceId &device_id() const noexcept = 0;
    virtual bool probe(const DeviceNode &node, DeviceModel &model, b64 driver_flag) const;
    virtual Result<DriverBase *> create(const DeviceNode &node, DeviceModel &model, b64 driver_flag) const = 0;
};
```

`DeviceId` 同时支持 FDT 和 PCI 两种匹配方式:

```cpp
struct FDTDeviceId {
    const char *compatible;    // compatible 字符串
    b64 driver_flag;           // 驱动私有标记
};

struct PCIDeviceId {
    b16 vendor_id, subvendor_id;
    b16 device_id, subdevice_id;
    b32 class_code, class_mask;
    b64 driver_flag;
};

struct DeviceId {
    const FDTDeviceId *fdt_ids;  // FDT 匹配表 (可为 null)
    const PCIDeviceId *pci_ids;  // PCI 匹配表 (可为 null)
};
```

匹配逻辑:

1. 若节点是 FDT 类型, 遍历 `fdt_ids`, 用 `node.is_compatible_with(id.compatible)` 匹配
2. 若节点是 PCI 类型, 遍历 `pci_ids`, 匹配 vendor/device/class 等信息

## 中断子系统

### IrqManager

`driver::IrqManager` 是全局中断管理器, 维护:

- **中断域 (IrqDomain)**: 每个中断控制器创建一个域。域内管理 `hwirq → virq` 的映射。
- **virq 分配**: 将各个中断控制器的硬件中断号映射到全局虚拟中断号。
- **级联关系**: 支持中断控制器之间的父子域级联 (如 PLIC 挂接到 RISC-V INTC)。

### 中断控制器类型

| 控制器              | 类型     | 说明                         |
| ------------------- | -------- | ---------------------------- |
| RISC-V INTC (CLINT) | 核内     | 软件中断 + 定时器中断        |
| RISC-V PLIC         | 平台级   | 外部中断, 支持优先级和亲和性 |
| LoongArch LS7A      | 桥片     | LoongArch 平台的中断控制器   |
| Goldfish RTC        | 设备中断 | 虚拟 RTC 时钟中断            |

### 中断分发流程

```
硬件中断触发
    │
    ├─ RISC-V: mtvec → trap handler
    │
    ▼
顶层中断控制器 (如 PLIC)
    │ claim hwirq
    ▼
IrqManager::dispatch(virq)
    │ 查表找到注册的 handler
    ▼
驱动中断处理函数 (通过 VIrqResource 注册)
    │
    ├─ 设备 ISR
    │
    ▼
中断控制器 ack (EOI)
```

## 块设备子系统

块设备层 (`kernel/bio/`) 是设备驱动与 VFS 之间的存储 I/O 层。

### 请求模型

块设备当前采用请求队列模型:

- BufferCache 将 I/O 请求提交到 `BlockRequestQueue`
- 请求状态: `SUBMITTED → PROCESSING → COMPLETED/FAILED`
- 设备 worker 从队列取请求, 处理后调用 `complete()` 推进状态机

### driver::BlockDevice

```cpp
class BlockDevice : public DriverBase, public IBlockDeviceOps {
    explicit BlockDevice(DevRes res) noexcept;
};
```

### BlkManager

`blk::BlkManager` 维护块设备对象到设备号的映射, 用于设备持久化引用。

## 具体驱动实现

### VirtIO 块设备

`virtio::VirtIOBlkDriver` 继承 `driver::BlockDevice`, 通过 VirtIO 传输层与 QEMU 提供的 virtio-blk 设备交互。

- **Factory 匹配**: `compatible = "virtio,mmio"` + FDT device node 中的 `virtio,29` (virtio-blk 设备 ID)
- **资源配置**: 通过 MMIO 访问 VirtIO MMIO 寄存器, 无专用中断 (使用 VirtIO 传输层提供的事件通知)
- **初始化**: 读取 VirtIO config space 获取块大小和容量, 注册到 `BlkManager`, 创建 worker 线程

### NS16550 串口

`SerialDriver` 继承 `DriverBase`, 通过 MMIO 访问 16550 UART 寄存器。

- **Factory 匹配**: `compatible = "ns16550a"` 或 `compatible = "ns16550"`
- **中断处理**: 注册 UART 接收中断处理函数
- **用途**: 内核日志输出 (`debugcon`), 用户态终端

### RTC 时钟

支持 `GoldfishRTC` (goldfish-rtc, 虚拟平台) 和 `LS7ARTC` (LoongArch 桥片)。

- **GoldfishRTC**: MMIO 寄存器 `TIME_LOW` / `TIME_HIGH` 读取 64 位纳秒时间戳
- **时钟源注册**: 将 RTC 注册为 `driver::ClockSource`, 供内核时钟子系统使用

### PCI 主机桥

`PCIHostBridgeDriver` 继承 `DriverBase`, 通过 ECAM 空间枚举 PCI 总线。

核心数据结构:

```cpp
struct PCIHostControllerConfig {
    b16 segment;
    b8 bus_start, bus_end;
    PhyArea ecam_region;        // ECAM MMIO 区域
    KvaAddr ecam_base;          // ECAM 内核虚拟地址
    vector<PCIHostRange> ranges; // PCI 地址空间到 CPU 地址空间的映射
};
```

- **ECAM 访问**: `PCIConfigOps` 接口提供 `read8/16/32` 和 `write32` 配置空间访问
- **BAR 解析**: 枚举每个 PCI function 的 BAR (Base Address Register), 解析 MMIO 和 I/O 空间
- **设备生成**: 将发现的 PCI 设备创建为 `pci::PCIDeviceNode` 并注册到 `DeviceModel`

## 初始化序列

```
1. DeviceModel::init()             — 构造设备模型单例
2. DriverModel::init()             — 构造驱动模型单例
3. IrqManager::init()              — 构造中断管理器
4. MMIOManager::init()             — 构造 MMIO 映射管理器
5. register_provider(FDTProvider)  — 解析 DTB, 导入设备节点和内存区域
6. register_provider(KernelProvider) — 标记内核镜像为保留内存
7. IrqManager::build_ic_graph()       — 构建中断控制器拓扑图
8. IrqManager::finalize_virq()        — 为所有中断生成全局 virq
9. DriverModel::activate_runtime(nodes) — 匹配驱动工厂并创建驱动实例
10. DriverModel::activate_block_devices() — 注册块设备到 BlkManager / DevFS
```

## DevFS 集成

设备文件系统 (DevFS) 挂载在 `/sys/`, 将内核设备暴露为文件节点:

```
/sys/
├── block/
│   ├── vda/          (virtio-blk 块设备)
│   ├── ram0/         (RamDisk)
│   └── ...
├── char/
│   ├── ttyS0/        (串口)
│   ├── rtc0/         (RTC 时钟)
│   └── ...
└── bus/
    ├── platform/     (平台设备)
    └── pci/          (PCI 设备)
```

`DriverModel::activate_runtime()` 在每个设备驱动创建成功后, 在 DevFS 对应目录下:

1. 创建 `DevFSDirectory` 目录节点
2. 通过 `register_char()` 注册字符设备文件 (使用 `CharFactory` 延迟创建)
3. 块设备通过 `BlkManager` 另行注册

用户态通过 `opendir("/sys/...")` 获取设备目录的 Capability, 进而与设备交互。
