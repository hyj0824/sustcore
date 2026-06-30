# Bootstrap 信息

由于进程启动时已经由父进程传递了部分 Capability, 但是这部分 Capability 本身不具有任何含义,
因此在进程启动时需要额外加载一段 Bootstrap 信息以供进程正确地解析现有 Capability 并正确地初始化自身的运行环境.

为了与 linux ABI 兼容, Bootstrap 信息的布局如下

```
栈顶(sp)
+--------------------+
|  argc              |
+--------------------+
|  argv[0]           |
|  argv[1]           |
|  ...               |
|  argv[argc - 1]    |
|  nullptr           |
+--------------------+
|  envp[0]           |
|  envp[1]           |
|  ...               |
|  envp[envc - 1]    |
|  nullptr           |
+--------------------+
|  auxv[0]           |
|  auxv[1]           |
|  ...               |
|  auxv[auxc - 1]    |
|  nullptr           |
+--------------------+
|  bsargc            |
+--------------------+
|  bsargv[0]         |
|  bsargv[1]         |
|  ...               |
|  bsargv[bsargc - 1]|
|  nullptr           |
+--------------------+
```

其中 bsargv 是一个指针数组, 其每个元素指向一个 Bootstrap 信息, 每个 Bootstrap 信息都有个信息头
```cpp
struct bsheader {
    uint32_t size;
    uint32_t type;
};
```
其中 size 表示该 Bootstrap 信息的大小, type 表示该 Bootstrap 信息的类型, 目前支持的类型有
```cpp
namespace boot {
    constexpr uint32_t TYPE_CAPEXP   = 0x1;
    constexpr uint32_t TYPE_VADDREXP = 0x2;
    constexpr uint32_t TYPE_PATHEXP  = 0x3;
    constexpr uint32_t TYPE_SHELLIO  = 0x4;
}  // namespace boot
```

这几个类型的含义与布局如下

## Capability解释(TYPE_CAPEXP)

这是一条 Capability 解释信息, 用于解释某个 CapIdx 指向的 Capability 的含义, 其布局如下
```
struct BootstrapCapExplainView {
    CapIdx cap_idx;
    PayloadType cap_type;
    b64 cap_perm;
    char cap_desc[];
};
```

cap_idx 表示要解释的 Capability 的索引, cap_type 表示该 Capability 的类型, cap_perm 表示该 Capability 的权限, cap_desc 是一个以 '\0' 结尾的字符串, 用于描述该 Capability 的含义.
这个字符串内可以填入任意的内容, 但是以 "#" 开头的字符串则具有特殊含义, 其含义根据 cap_type 的不同而不同, 具体如下:

1. 如果 cap_type 是 PCB, 那么
    a. "#self:<pid>" 代表该 Capability 指向的 PCB 是当前进程的 PCB, 其中 <pid> 是当前进程的 PID, 但是目前 <pid> 的值是 0 (待后续完善)
    b. "#parent" 代表该 Capability 指向的 PCB 是当前进程的父进程的 PCB
2. 如果 cap_type 是 TCB, 那么
    a. "#main:<tid>" 代表该 Capability 指向的 TCB 是当前进程的主线程的 TCB, 其中 <tid> 是当前进程的主线程的 TID, 但是目前 <tid> 的值是 0 (待后续完善)
3. 如果 cap_type 是 MEMORY, 那么
    a. "#heap" 代表该 Capability 指向的 MEMORY 是当前进程的堆内存
    b. 对 linux subsystem, "#ss-heap" 代表该 Capability 指向的 MEMORY 是当前进程的 linux 子系统的堆内存
4. 如果 cap_type 是 VDIR, 那么
    a. "#cwd" 代表该 Capability 指向的 VDIR 是当前进程的工作目录
    b. "#/" 代表该 Capability 指向的 VDIR 是当前进程的根目录
    c. "#<path>" 代表该 Capability 指向的 VDIR 是当前进程的 <path> 目录, 其中 <path> 是一个绝对路径
5. 如果 cap_type 是 VFILE, 那么
    a. "#stdin" 代表该 Capability 指向的 VFILE 是当前进程的标准输入
    b. "#stdout" 代表该 Capability 指向的 VFILE 是当前进程的标准输出
    c. "#stderr" 代表该 Capability 指向的 VFILE 是当前进程的标准错误输出
    d. "#exe"    代表该 Capability 指向的 VFILE 是当前进程的可执行文件
    e. "#<path>" 代表该 Capability 指向的 VFILE 是当前进程的 <path> 文件, 其中 <path> 是一个绝对路径

如果 cap_desc 不是以 "#" 开头的字符串, 那么内核对用户应该如何解读该 Capability 不做任何限制, 但是用户可以自行定义解读方式

其中, PCB 的 "#self:<pid>" 与 TCB 的 "#main:<tid>", 以及 MEMORY 的 "#heap" 与 "#ss-heap" 都是内核在创建进程时自动生成的.

## 虚拟地址解释(TYPE_VADDREXP)

这是一条虚拟地址解释信息, 用于解释某个虚拟地址的含义, 其布局如下
```
struct BootstrapVaddrExplainView {
    VirAddr vaddr;
    char vaddr_desc[];
};
```
其中 vaddr 表示要解释的虚拟地址, vaddr_desc 是一个以 '\0' 结尾的字符串, 用于描述该虚拟地址的含义.
这个字符串内可以填入任意的内容, 但是以 "#" 开头的字符串则具有特殊含义, 其含义根据 cap_type 的不同而不同, 具体如下:
1. '#heap' 代表该虚拟地址是当前进程的堆内存的起始地址
2. '#ss-heap' 代表该虚拟地址是当前进程的 linux 子系统的堆内存的起始地址

## 路径解释(TYPE_PATHEXP)
这是一条路径解释信息, 用于解释某个路径的含义, 其布局如下
```
struct BootstrapPathExplainView {
    char path_desc[];
};
```
其中 path_desc 是一个以 '\0' 结尾的字符串, 用于描述该路径的含义.
这个字符串内可以填入任意的内容, 但是以 "#" 开头的字符串则具有特殊含义, 其含义根据 cap_type 的不同而不同, 具体如下:
1. '#cwd:<path>' 代表 <path> 是当前进程的工作目录的绝对路径
2. '#exe:<path>' 代表 <path> 是当前进程的根目录的绝对路径
3. '#stdint:<path>' 代表 <path> 是当前进程的标准输入的绝对路径
4. '#stdout:<path>' 代表 <path> 是当前进程的标准输出的绝对路径
5. '#stderr:<path>' 代表 <path> 是当前进程的标准错误输出的绝对路径

## Shell I/O解释(TYPE_SHELLIO)

这是一条 Shell I/O 解释信息, 用于解释当前进程的 Shell I/O 的模式, 其布局如下
```
struct BootstrapShellIOExplainView {
    uint32_t flags;
    uint32_t target;
};
```
其中 flags 表示 target 的 Shell I/O 的模式, 其值为以下枚举的按位或
```
namespace boot {
    constexpr uint32_t SHELLIO_FLAG_OVERWRITE = 0;
    constexpr uint32_t SHELLIO_FLAG_APPEND    = 1;
}
```
其中 `SHELLIO_FLAG_OVERWRITE` 表示 target 的 Shell I/O 的模式为覆盖模式, `SHELLIO_FLAG_APPEND` 表示 target 的 Shell I/O 的模式为追加模式.

target 表示要解释的 Shell I/O 的目标, 其值为以下枚举
```
namespace boot {
    constexpr uint32_t SHELLIO_TARGET_STDIN  = 0;
    constexpr uint32_t SHELLIO_TARGET_STDOUT = 1;
    constexpr uint32_t SHELLIO_TARGET_STDERR = 2;
}
```
其中 `SHELLIO_TARGET_STDIN` 表示要解释的 Shell I/O 的目标是标准输入, `SHELLIO_TARGET_STDOUT` 表示要解释的 Shell I/O 的目标是标准输出, `SHELLIO_TARGET_STDERR` 表示要解释的 Shell I/O 的目标是标准错误输出.

用户也可以自定义 Bootstrap 信息的类型, 但是内核不会对用户自定义的 Bootstrap 信息进行任何解释, 用户需要自行解析这些信息. 此外, 用户自定义的 Bootstrap 信息的类型值必须以 "0xFFFF0000" 开头, 以避免与内核定义的类型值冲突.s