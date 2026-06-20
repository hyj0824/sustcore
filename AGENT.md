# Ext4 Development Notes

## Known Pitfalls

### VFS::open auto-creates files only with O_CREAT
`VFS::open()` (kernel/vfs/vfs.cpp:654-655) only auto-calls `mkfile()` when both conditions are met:
1. `_open_file()` returns `ENTRY_NOT_FOUND`
2. `oflags & flags::O_CREAT` is set

Without `O_CREAT`, `fopen(path, "r")` on a non-existent file returns an error instead of silently creating it.

### BufferHandler only covers _dev_block_size
`BufferCache` handlers cover `_dev_block_size` (128 bytes for virtio-blk), NOT `_block_size` (1024 for ext4). Always use `read_fs_block`/`write_fs_block` for directory block access — they handle multi-device-block IO. Never use `get_buffer_async()` directly for ext4 block operations.

### Ext4Directory cache invalidation
`_entries_cached` is set to false in `mkfile`, `mkdir`, `unlink`, `rmdir`, `rename`. However `VSuperblock::get_vnode()` may return a different `Ext4Directory` instance for the same inode. This is a separate issue from VINode-level cache eviction (below) — it concerns directory entry cache coherence across VINode instances, not inode type mismatch.

### Buffer cache write visibility
Writes via `write_fs_block` go to `BufferCache` and mark buffers dirty. `read_fs_block` reads from the same cache. If a dirty buffer is evicted (unlikely with 8192 slots), data is lost. For critical operations (rename, unlink), call `sync()` after writes to flush dirty buffers to disk.

## VINode Cache Rules

### Rule: Always evict VINode cache when an inode is freed

`VSuperblock::_inode_cache` (kernel/vfs/vfs.cpp:43) caches `VINode*` by inode number. When an inode is freed and later reallocated (possibly as a different type — file→dir or vice versa), a stale cache hit returns the old VINode whose `IINode` type no longer matches the on-disk inode. This causes `as_directory()` / `as_file()` to return `INVALID_PARAM`.

**Must do**: After `Ext4Superblock::release_file_inode()` is called (via unlink/rmdir/delete), call `VSuperblock::evict_inode(inode_id)` on the parent's VSuperblock.

**Where**: `VFS::unlink()` and `VFS::rmdir()` (kernel/vfs/vfs.cpp). Look up the target inode_id via `lookup()` before the delete, then `evict_inode()` after.

**Why not in ext4 layer**: `Ext4Superblock` has no reference to `VSuperblock`. The VFS layer bridges them.

### Safety net: fallback eviction in `_open_dir()`

Even if unlink/rmdir eviction is missed, `_open_dir()` (line ~580) retries with cache eviction when `as_directory()` fails on a resolved VINode. This catches cases where the cached VINode's IINode type is stale.

### Pattern

```cpp
// In VFS::unlink / VFS::rmdir:
auto lookup_res = parent_dir->lookup(name);   // get inode_id before delete
propagate(lookup_res);
auto del_res = parent_dir->unlink(name);       // or rmdir
propagate(del_res);
parent_vinode->superblock().evict_inode(lookup_res.value());  // CRITICAL
```

## Adding a New Test Module

Test modules are user-space programs compiled into `.mod` files, loaded from initrd by the init module.

### Step 1: Create module directory
```
module/test_MYNAME/
├── main.cpp       # test code
├── include.mk     # sources += main.cpp
├── options.mk     # component-name, module-output, flags
└── Makefile       # copy from another test module
```

### Step 2: Register in Makefile
In the top-level `Makefile`:
1. Add to `module-components` list (line 36)
2. Add `module-component-makefile.test_MYNAME` entry (line ~62)
3. Add explicit build line in `build-mods` target (line ~98):
```
$(q)$(MAKE) -f $(module-component-makefile.test_MYNAME) $(arg-basic) build
```

### Step 3: Set component name in options.mk
```
component-name := test_MYNAME
module-output := test_MYNAME.mod
```

### Step 4: Add to init module
In `module/init/main.cpp`, add spawn code after existing tests:
```cpp
fd = kmod_fopen("/initrd/test_MYNAME.mod", "x");
if (fd >= 0) {
    if (spawn_with_root_dir(fd, SCHED_CLASS_RR, root_dir_cap) == cap::error)
        printf("init: create test_MYNAME failed\n");
    else
        printf("init: MYNAME test spawned\n");
    kmod_fclose(fd);
}
```

Tests run **sequentially** — each `spawn_with_root_dir` blocks until the child process exits.

### kmod API differences vs POSIX
- No `open(path, flags)` → use `kmod_mkfile(path, "w+")` or `kmod_fopen(path, "r"/"w")`
- `kmod_mkfile` creates or fails if file exists (returns error, no overwrite)
- No `lseek` → sequential I/O only via `kmod_fread`/`kmod_fwrite`
- No `stat` → use `sys_vfs_size(kmod_getcap(fd))` for file size
- Directory listing: `sys_vfs_opendir` + `sys_vfs_getdents`
- `exit(0)` for PASS, `exit(-1)` for FAIL

# prompt.md

本项目为 Sustcore, 一个面向 RISC‑V 64 位架构、基于能力 (capability) 的操作系统内核.
本文档包含参与本项目开发的智能体 (agent) 必须严格遵守的指令.

面向 AI 智能体:  如果你被要求编辑此文件, 请立即停止.

## 代码风格

- 以仓库现有代码为准, 优先遵守 `.clang-format` 与 `.editorconfig`, 不要手工对抗格式化结果.
- 类型名使用 `UpperCamelCase`, 函数/变量/命名空间使用 `snake_case`, 私有成员使用前缀下划线 `_member`, 宏与配置开关使用全大写.
- 文件头统一使用现有 Doxygen 风格块注释. 公开接口、复杂数据结构、关键辅助函数应补充 Doxygen 注释.
- 注释优先解释意图、前置条件和不变量, 少写机械式"把 A 赋给 B”. 新增注释优先中文, 但应跟随周围文件风格.
- 使用 `[[nodiscard]]`、`noexcept`、`constexpr`、`static_assert` 时以语义收益为先, 贴合相邻代码现有写法.
- **禁止使用 C++ 异常.** 错误处理统一使用项目内置的 `Result` 类型与相关辅助宏.
- **避免使用函数内部静态状态.** 全局状态通常采用 `_INSTANCE + _initialized + init()/inst()` 的显式初始化方案.
- 将复杂函数拆分为文件内辅助函数并放入匿名命名空间; 协程函数沿用 `util::cotask<Result<T>>` 与 `co_propagate(...)`.
- 使用 `util::owner`、`util::nonnull`、`util::Guard`、`delete_guard`、`remove_guard` 等项目设施表达所有权与回滚语义.
- 遵守代码库中已有的命名、日志、权限检查和资源管理习惯, 不要引入新的局部风格.

## 通用指令

- 任何修改后必须构建并运行测试, 除非该修改仅限于文档类的 .md 文件修改.
- 简洁是美德:  如果一段代码能用 50 行表达却写了 200 行, 请重写. 但是, 请务必确认重写不会使得代码过于晦涩而难懂.
- 错误处理时, 通过 `.transform`、`.transform_error`、`.and_then` 等链式处理 `Result`, 并善用 `always()`、`mem_fn`、`unwrap_ref`、`unwrap_owner` 等辅助工具, 减少冗余代码.
- 使用 `propagate()` 传播错误, 避免重复编写 `if (!result) return unexpected(...)`.
- 使用项目的**日志系统** (`sus/logger.h`) 记录重要事件:
  - `DEBUG` —— 详细的诊断信息 (应覆盖所有执行路径).
  - `INFO`  —— 常规信息与主要执行节点.
  - `ERROR` —— 可恢复的错误.
  - `FATAL` —— 致命错误, 触发程序终止.
  - 在执行流日志中, 主干部分使用 `INFO` 级别, 细节步骤使用 `DEBUG` 级别.
- 运行或调试系统时, 务必使用 `timeout` 命令限制时间, 避免终端死锁.
  示例:  `timeout 10s make run` 会在 10 秒后强制终止.
- 内核调试请使用 gdb 远程调试. `make dbg` 会自动为 QEMU 添加 `-s -S` 参数, 随后在宿主机用 `gdb` 连接 1234 端口. 配置参考 `config-ref/gdb-setup.sh` 与 `config-ref/qemu-setup.sh`.
- 日志中 `test_call_service: done` 表示 rpc 测试成功完成, 请通过观察日志确认系统正常运行.
- 不要畏惧于大规模重构代码. 当示意你进行代码重构时, 不要担心修改了大量行数, 也不要过于保守, 保留太多旧的代码的结构与实现细节, 而是确保重构后的代码更加简洁, 风格统一, 易于理解.

## 分领域参考索引

在操作特定子系统前, 请先阅读相应文档:

- **能力系统**:  `docs/cap/framework.md`、`docs/cap/memory.md`、`docs/cap/endpoint.md`、`docs/cap/task.md`、`docs/cap/vfile.md`、`docs/cap/notif.md`
- **设备与驱动**:  `docs/device/framework.md`、`docs/device/fdt.md`、`docs/device/drivers.md`、`docs/device/intterupt.md`
- **构建系统**:  `docs/build_system.md`
- **虚拟文件系统**:  `docs/cap/vfile.md` 以及 `kernel/vfs/` 源码
- **调度器**:  `kernel/schd/` 目录及相关测试文件
- **测试**:  测试代码位于 `kernel/test/`, 测试框架参考 `kernel/test/framework.h`

处理具体组件时, 务必查阅相关头文件和源文件.

## 常用命令

### 构建与运行

项目使用基于 Make 的自定义构建系统.

- `make build` —— 构建内核及所有模块
- `make run`   —— 构建并在 QEMU 中运行 (**务必**配合 `timeout` 使用)
- `make dbg`   —— 构建并以调试模式启动 (启用 gdb 远程调试桩)
- `make clean` —— 清除所有构建产物

> **注意:  ** 执行 `make run` 或 `make dbg` 时, 请一律使用 `timeout` 包装, 如 `timeout 15s make run`.

### 脚本与工具

- `gen_compile_commands.sh` —— 生成 `compile_commands.json` 供语言服务器使用
- `quick_locate.sh` —— 可用于快速从地址定位到源代码行, 例如 `quick_locate.sh 0xffffffff80001234`.
- `stat.sh` —— 代码统计

### 测试

- 内核单元测试在启动过程中运行, 可通过串口输出查看结果.
- 新增测试请放置在 `kernel/test/` 目录, 并遵循既有模式.

### 版本控制

- 你不需要负责版本控制.

## 调试

- 连接调试桩:  `target remote :1234`.
- 常用 gdb 命令:  `c` (继续) 、`si` (单步指令) 、`b 函数名`、`info registers`、`x /10i $pc`.

# 代码风格

根据当前代码库中 `kernel/`、`include/`、`libs/` 的实现, Sustcore 的实际代码风格可总结如下. 编写新代码时, 应首先贴合所在目录与相邻文件的既有写法, 以下规则用于统一细节与补足约定.

---

### 一、命名约定

| 类别                    | 风格                              | 示例                                                      |
| ----------------------- | --------------------------------- | --------------------------------------------------------- |
| 类/结构体               | 大驼峰 (PascalCase)               | `TaskManager`、`DeviceModel`、`SchedMeta`                 |
| 函数/方法               | 小写下划线 (snake_case)           | `init_tcb`、`populate_task`、`find_devices_by_compatible` |
| 变量 (局部、全局)       | 小写下划线                        | `tcb_guard`、`startup_blob_size`、`_initialized`          |
| 私有成员变量            | 下划线前缀 `_`                    | `_pid_map`、`_main_kernel_pgd`、`_regions`                |
| 静态/全局单例实例       | `_INSTANCE` / `_initialized` 模式 | `_INSTANCE`、`_initialized`                               |
| 命名空间                | 全小写                            | `task`、`cap`、`schd`、`device`                           |
| 宏                      | 尽量避免；必须时全大写            | `MAX_HARTS`、`__CONF_KERNEL_TESTS`                        |
| 常量 / `constexpr` 变量 | 以全大写常量为主                  | `MAX_INITIAL_STACK_SIZE`、`USER_STACK_BOTTOM`             |
| 枚举值                  | `enum class`, 值可用大写/小写混合 | `MemoryStatus::FREE`、`ThreadState::READY`                |
| 类型别名                | `using` 小写 + 下划线             | `tid_t`、`pid_t`、`WaitReasonId`                          |
| 模板参数                | 大驼峰或单字母                    | `typename SU`、`typename SUType`                          |

**补充说明**
- Getter 方法命名与对应成员变量去掉下划线前缀, 例如 `_kernel_pcb` 的访问器为 `kernel_pcb()`.
- 名词性布尔函数不加 `is` 前缀, 直接用形容词, 如 `initialized()`、`executing()`.
- 文件内匿名辅助函数多使用动词或短语命名, 如 `check_msg_valid`、`find_best_factory`、`page_align_area`.
- 对象权限常量按命名空间分组组织, 如 `perm::memory::READ`、`perm::endpoint::GRANT`; 局部实现中常见 `using namespace perm::xxx;`.

---

### 二、代码组织与格式

1. **头文件保护**
   统一使用 `#pragma once`.

2. **包含顺序**
   - 跟随现有文件习惯: 先本模块头文件, 再项目内头文件, 最后标准库头文件.
   - 可以按逻辑分组, 不必强行按字母序重排.

3. **注释规范**
   - 文件头使用 Doxygen 风格块注释 (`@file`、`@brief`、`@author`、`@version`、`@date`).
   - 公开接口、复杂 payload、状态机式函数、文件内关键辅助函数通常都写 Doxygen 注释.
   - 新增注释优先中文, 但应尊重周围文件语言与术语习惯.
   - 注释重点写 **为什么需要这段逻辑、有哪些前置条件、成功后保证什么**, 少写机械翻译代码的废话.
   - 复杂流程通常通过分段注释组织, 如 "先校验...” / "然后构造...” / "最后回滚...”.

4. **格式与排版**
   - 缩进 4 空格, 行尾 `LF`, 去掉行尾空白, 与 `.editorconfig` 保持一致.r
   - 指针绑定到类型右侧, 如 `Type *ptr`, `const Node *node`.
   - `namespace` 内容整体缩进, 与 `.clang-format` 的 `NamespaceIndentation: All` 一致.
   - 花括号、`case` 缩进、短函数布局遵循 `.clang-format`, 不手工制造与格式化器冲突的版式.
   - 允许在连续赋值/宏中使用对齐增强可读性, 但不要为了对齐大面积扰动已有代码.

5. **函数设计**
   - 功能单一, 粒度细 (如 `insert_pcb_cap`、`verify_reserved_caps`、`remove_unreserved_caps`).
   - 复杂逻辑提取为文件内静态函数 (放在匿名命名空间).
   - 使用 `[[nodiscard]]` 标记返回值不应忽略的函数.
   - 使用 `noexcept` 标记不抛出异常的函数.
   - 永不返回的函数标注 `[[noreturn]]`.
   - 返回 `Result<T>` 的函数应让成功路径顺滑、错误路径前置, 避免多层嵌套.
   - 协程函数统一沿用 `util::cotask<Result<T>>` 和 `co_propagate(...)` 风格.
   - 虚析构函数与平凡析构函数优先 `= default`; 需要释放资源时再显式实现.

6. **类设计**
   - 单例模式:  静态 `_INSTANCE` + `_initialized` 标记, `inst()` 返回引用并检查初始化状态, 构造通过 `init()` 显式调用 (placement new).
   - 禁止拷贝/移动 (`= delete`) 或仅移动.
   - 资源持有者实现 RAII, 提供 `cleanup()` 或依赖析构.
   - capability/object/driver 等封装类通常保持轻量, 重点是权限检查、状态协调和对子系统接口的薄包装.
   - 访问控制可使用 **passkey 惯用法** (如 `key::tmm`) 实现精细读写权限(仅对于 env 中的起效)

7. **模板与概念**
   - 使用 `concept` 约束模板参数 (如 `SchdClassBasicTrait`).
   - 模板类型别名常用 `SUType` 等.
   - 静态成员模板用于类型擦除, 如 `SchedMeta::ENTITY_OFFSET<SUType>`.

8. **内联与 constexpr**
   - 短小且频繁调用的函数在头文件中实现并标记 `inline`.
   - 编译期可求值的常量/函数尽量使用 `constexpr`.
   - `constexpr` 构造函数允许在编译期初始化对象 (如 `HartContext`).

9. **编译期断言**
   使用 `static_assert` 检查内存布局 (如 `PaddedHartContext` 的大小和偏移).

---

### 三、错误处理

- **禁止使用 C++ 异常**, 统一使用项目内置的 `Result<T>` 类型.
- 错误传播通过以下宏/函数完成:
  - `propagate(result)`:  若为错误则直接返回该错误.
  - `unexpect_return(error)`:  构造一个 `unexpected` 并返回.
  - `propagate_return(result)`:  返回 `result` 的错误值, 相当于 `unexpect_return(result.error())`.
  - `void_return()`:  返回 `Result<void>` 的成功值.
- 链式处理使用 `.transform`、`.and_then`、`.transform_error`, 配合 `always()`、`mem_fn` 等辅助函数.
- 不检查 `Result` 的 `has_value()` 而直接取 `value()` 时, 必须确保前一步已传播错误 (相当于断言成功).
- 常见风格是: 入口先校验参数/权限并尽早失败, 中间步骤使用 `propagate(...)`, 如果错误并不会继续影响接下来的逻辑, 抑或是需要继续清理, 则显式保存第一处错误码.
- 对"正常但无结果”的情况, 项目会按模块约定返回 `false`、`nullptr` 或 `ENTRY_NOT_FOUND`; 不要随意更换同模块既有语义.

---

### 四、资源管理与所有权

- **所有权**
  - 用 `util::owner<T>` 封装裸指针, 明确所有权. 但 `util::owner` 仅表达所有权语义, 不自动调用 `delete`; 资源释放通常通过 `cleanup()` 方法或 RAII 守卫完成.
  - 非空指针使用 `util::nonnull<T>`, 构造时通过 `util::nnullforce(ptr)` 包装.
- **RAII 守卫**
  - `util::Guard`:  在作用域结束时执行自定义清理动作.
  - `delete_guard(owner_ptr)`:  自动 `delete`.
  - `remove_guard(cholder, idx)`:  自动从能力空间中移除能力.
- **常见回滚模式**
  - 先创建 `owner`
  - 用 `delete_guard(...)` / `remove_guard(...)` 保护中间状态
  - 在成功提交后 `release()`
- **自定义分配器**
  - PCB、TCB 等高频对象通过 `KOP<T>` 实现专用内存池, 并重载 `operator new` / `operator delete`.
- **标准容器**
  使用 `std::vector`、`std::unordered_map` 等, 元素为 `util::owner` 时注意所有权转移.
  使用 `IntrusiveList` (依赖成员指针) 管理侵入式链表.
- **生命周期语义**
  - payload 的 `destruct()` 可能不等于 `delete this`; 新代码不要擅自把现有对象改成普通 delete 语义.
  - 析构与 `cleanup()` 二者若并存, 应保持单一释放来源, 通常由析构调用 `cleanup()`.

---

### 五、日志系统

- 头文件 `sus/logger.h`, 日志记录器在命名空间 `loggers` 下定义, 如 `loggers::SUSTCORE`、`loggers::TASK`、`loggers::DEVICE`.
- 日志级别:
  - `DEBUG`:  覆盖完整执行路径的细节信息.
  - `INFO`:  关键流程节点和一般信息.
  - `ERROR`:  可恢复的错误.
  - `FATAL`:  不可恢复错误, 记录后终止.
- 日志消息可使用 printf 风格格式化.
- 代码库常见模式是:
  - 主流程节点记录 `INFO`
  - 资源解析、映射、绑定、分支路径记录 `DEBUG`
  - 失败前尽量记录上下文参数与错误码
  - 同一层已经记录并立即传播的错误, 下层无需重复刷屏

---

### 六、类型系统与地址

- 物理地址 `PhyAddr`、虚拟地址 `VirAddr`、内核物理地址 `KpaAddr` 均为强类型封装, 通过 `convert<>()`、`convert_pointer()` 等进行安全转换.
- 地址区间 `PhyArea`、`VirArea` 提供 `begin`、`end`, 支持 `nullable()` 判断空区间.
- `units::time` 等带单位的值类型, 提供 `from_milliseconds()` 等工厂方法.
- 对页、位掩码、寄存器、时间单位等强语义值, 优先复用现有包装类型与辅助函数, 不退回裸整数.
- 权限判断优先使用 `perm::imply(...)`、对象局部 `imply(...)` 或专门辅助函数, 不要把裸位运算散落到业务逻辑深处.

---

### 七、其他约定

- `auto` 推导广泛用于减少冗余, 尤其是迭代器和工厂返回值.
- 使用 `std::ranges::sort`、`std::ranges::for_each` 等现代 C++ 设施. 当设施不存在时, 停下来并向我询问是否应该引入新设施, 抑或是使用其它替代方案.
- 匿名命名空间用于文件局部函数和类型, 避免污染外部符号.
- 临界区通常通过 `InterruptGuard guard; guard.enter();` 显式包裹; 条件检查与入队/出队等状态变化应尽量放在同一临界区内以避免竞态.
- 权限检查通常靠近对象方法入口完成, syscall 层主要负责 lookup、类型判定和用户态缓冲区搬运.
- `assert` 用于表达"按模块不变量这里不可能失败”; 对可恢复失败路径应返回 `Result`.
- 测试代码位于 `kernel/test/` 目录, 通过 `TestFramework` 收集并运行, 成功标志通常为特定日志输出.
- 全局状态需延迟初始化 (`init()` 模式) , 并在首次访问前断言 `initialized()`.
- 断言 `assert` 用于"不可能发生”的情况, 如内存布局假设.

---

以上规范共同构成了 Sustcore 内核项目严谨、可维护、安全的 C++ 编码风格, 所有新代码必须严格遵循. 
如果理解了以上内容, 请回复 "已理解" 并准备好按照这些规范进行开发.