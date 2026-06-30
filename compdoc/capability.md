# Capability

Capability(能力) 是内核的核心概念. 每个进程都有一个 Capability-Space (CSpace), 用于存放自己所持有的 Capability, 进程通过指定 Capability-Index (CapIdx) 来指定 CSpace 中的一个指定 Capability.

## Payload & Permission

每个进程都持有对于其 Capability 的所有权, 但是 Capability 本身并不直接是内核资源.
实际上, 每个 Capability 都含有两个部分: Payload 和 Permission. Payload 是一个指针, 指向真正内核资源, 而 Permission 则是一个位掩码, 用于指定进程对于该内核资源的访问权限.

Capability 会主动维护其对于 Payload 的引用计数. 当 Capability 被创建时, Payload 的引用计数会增加; 当 Capability 被销毁时, Payload 的引用计数会减少. 当 Payload 的引用计数为 0 时, Payload 会被销毁.

Capability 除了直接存放在 CSpace 中, 部分特殊的内核资源也会持有 Capability, 以保证资源不会被提前销毁.
例如, MemoryPayload 对象如果是 file-backed 的, 则会持有这个文件的 Capability, 保证在 MemoryPayload 对象被销毁之前, 文件不会被销毁, 类似地, VMA 对象也会持有 MemoryPayload 的 Capability, 保证在 VMA 被销毁之前, MemoryPayload 不会被销毁.

## Capability-Space (CSpace)

由于不同的程序持有的 Capability 数量不同, 因此使用一个固定大小的单层数组来存放 Capability 是不现实的.我们采用一种特殊的双层数组. 我们将 CSpace 分为两层, 其中 CSpace 数组只存放 Capability-Group (CGroup) 的指针, 而 CGroup 中再存放 Capability 的指针,这样, 使用 Capability 较少的程序只需要使用一到两个 CGroup, 而使用 Capability 较多的程序则可以使用更多的 CGroup, 以动态地扩展 CSpace 的大小.

在目前的内核中, 一个 CSpace 中至多含有 4096 个 CGroup (CSPACE_SIZE 常量),
而每个 CGroup 中至多含有 256 个 Capability (CGROUP_SLOTS 常量),
因此一个 CSpace 中至多含有 4096 * 256 = 1048576 (大致一百万) 个 Capability. (CSPACE_CAPACITY 常量)

每个 CGroup 占用 8 * 256 = 2 KiB,
而一个 CSpace 占用 8 * 4096 = 32 KiB.
因此, 一般进程的 Capability 信息占用的内存空间大致为 36 KiB. (512 个 Capability 已经可以干很多事了)

## Syscalls

内核中除了极少数特殊的临时性系统调用(它们大多以 0xFFC0 开头)与部分能力创建类系统调用之外, 几乎所有的系统调用的第一个参数都是一个 CapIdx. 这种设计是有着特殊考量的: 如果让我们考虑为什么进程需要调用系统调用, 就会发现系统调用的本意是为了控制进程, 使其以受控的方式操作特权资源. 这与 OOP 中使用成员函数来封装对象操作, 保护对象的内部状态并维护一致性与完整性的思想是一致的. 因此, 除去部分能力创建类系统调用与极少数特殊的临时性系统调用之外, 几乎所有的系统调用都可以看作是对一个对象的操作, 那么第一个参数自然是对象的 this 指针 —— 在内核中, 也就是 CapIdx.

## CapIdx

内核不可能直接将 Capability 对象的指针直接传递给用户态程序, 让用户态程序通过 Capability 指针作为系统调用的第一个参数来操作内核资源, 这是不安全的, 因为内核无法确认其是否是真正的 Capability 对象, 也无法确认其是否是该用户态程序所持有的 Capability, 因此, 内核使用 CapIdx 来代替 Capability 指针. CapIdx 是一个 32 位无符号整数, 其[0:7]位是 CGroup 的索引, [8:19]位是 CGroup 中的 Capability 的索引, 剩余位除 MASK_VALID 外均是保留位, 应当设置为 0.

## Memory 与 VMA

传统的 mmap 系统调用将文件映射到进程的虚拟地址空间中, 抑或是直接分配一段匿名的虚拟地址空间. 本内核将 "创建一块内存区域" 与 "将内存区域映射到进程的虚拟地址空间中" 分为两部, 以增强程序对具体细节的掌控能力. 具体而言, Memory 对象代表着一段内存区域, 但这段内存区域如何映射到进程的虚拟地址空间中, 由 VMA 对象来决定. Memory 对象可以是匿名的, 也可以是 file-backed 的.

因为 Memory 对象可以被映射到进程的虚拟空间中的任意位置, 甚至可以在不同进程中被映射到不同的位置, 因此其中任何一个数据的虚拟地址都是不确定的. 然而, 该虚拟地址到起始地址的偏移量是确定的. 因此, 当针对 Memory 对象进行操作时, 往往需要指定一个 offset 以确定操作的具体位置.

Memory 对象被创建时, 内核不会立即为其分配物理内存, 而是采用按需分配的方式, 也就是在 Memory 对象被访问时, 内核才会为其分配物理内存(ensure_page). 如此可以极大程度上缓解内存压力, 同时允许提前创建一个足够大的 Memory 对象, 而不必担心内存不足的问题.

Memory 对象也可以是 file-backed 的, 也就是 Memory 对象的内容是由一个文件提供的. 这时, Memory 对象会持有这个文件的 Capability, 以保证在 Memory 对象被销毁之前, 文件不会被销毁. 由于目前的文件系统支持文件的页级别缓存, 因此, file-backed 的 Memory 对象最大的特点是, ensure_page 不再是创建一个新的物理页, 而是从文件的页缓存中获取相应的物理页, 并将其映射到 Memory 对象中. 比较特殊的情况是 Memory 对象的首尾页, Memory 相对文件的缓存不一定是页对齐的, 因此, Memory 对象的首尾页需要创建一个新的物理页, 并将文件中对应的内容拷贝到这个物理页中, 并将剩余部分清零 (考虑特殊的 PT_LOAD 段, 其 offset 不是 4K-aligned 的)

## VFile, VDir, VMount

VFile 与 VDir 是 VFS 提供的能力, 用于描述一个文件对象和一个目录对象. 特殊的是, 要获取一个 VFile 或 VDir 对象, 你 **必须** 指定其父目录的 Capability, 以及该文件或目录在父目录中的相对路径. 这一方面符合内核 Capability-based 的设计理念, 另一方面也使得内核可以在用户态程序访问文件系统时, 通过检查父目录的 Capability 来判断用户态程序是否有权限访问该文件或目录. 然而, 这极易造成一个鸡蛋问题: 第一个 VDir 对象从何而来? kinit 内核线程通过特殊的接口获得根目录的Capability, 并将其传递给 init 进程, init 进程再通过该 Capability 来获取根目录下的文件和目录的 Capability, 并将 Capability 继续传递给其他进程, 以此类推. 这就形成了一个 Capability 的传递链, 也保证了资源的分配是受限的: 如果某个目录下的文件或目录没有被传递给某个进程, 那么该进程就无法访问该文件或目录. (不过, 目前其具有一个缺陷: symlink 系统调用允许用户态程序创建一个符号链接, 该符号链接可以指向任意路径, 这就可能造成用户态程序访问到其没有权限访问的文件或目录. 由于初赛时间紧迫, 暂时没有修复这个设计问题, 但是我们将在后续的开发中尝试修复这个问题, 以保证内核的安全性.)

VMount 则是一个特殊的能力. 其用于说明要挂载的文件系统的规格等信息, 用户程序通过创建 VMount 对象, 再将 VMount 对象绑定到某个 VDir 对象的子目录上来实现挂载文件系统的操作. 同时, 用户也可以通过 VMount 对象来获取其根目录的 Capability, 以便于访问挂载的文件系统.

## CLONE, MIGRATE, MIGRATE-ONCE

CLONE, MIGRATE, MIGRATE-ONCE 是所有 Capability 共通的三个权限, 也对应着 cap_clone 这个系统调用.
cap_clone 将根据 Capability 的权限来决定如何操作.
如果是 CAP_CLONE, 那么将会创建一个和原 Capability 指向同一个 Payload, 有着相同权限的 Capability, 并返回这个新的 Capability 的 CapIdx.
如果是 CAP_MIGRATE, 那么将会创建一个和原 Capability 指向同一个 Payload, 有着相同权限的 Capability, 并返回这个新的 Capability 的 CapIdx, 同时将原 Capability 从 CSpace 中移除.
如果是 CAP_MIGRATE_ONCE, 那么将会创建一个和原 Capability 指向同一个 Payload, 有着相同权限的 Capability, 并返回这个新的 Capability 的 CapIdx, 同时将原 Capability 从 CSpace 中移除, 并将新 Capability 的 CAP_MIGRATE_ONCE 权限移除, 也就是新 Capability 只能被迁移一次.
优先级中 CAP_CLONE > CAP_MIGRATE > CAP_MIGRATE_ONCE, 也就是说, 如果一个 Capability 同时拥有 CAP_CLONE 和 CAP_MIGRATE_ONCE 权限, 那么 cap_clone 将会创建一个新的有 CAP_CLONE 和 CAP_MIGRATE_ONCE 的 Capability, 而不会移除原 Capability, 也不会取消 CAP_MIGRATE_ONCE 权限.

当通过 fork 系统调用创建一个新进程, 或是通过 create_process 的 rsvd_caps 参数保留部分能力给新进程的时候, 实际上执行的都是 cap_clone 系统调用. 因此, 通过将某个 Capability 的 CAP_CLONE 权限移除并添加 CAP_MIGRATE_ONCE 权限, 就可以保证该 Capability 只能被迁移一次, 也就是只能被 fork 或 create_process 一次, 以此来保证某个资源不会被二次分发.

不论是 fork 系统调用还是 create_process 系统调用, 都会保证如果一个能力被保留到了新进程中, 那么 CapIdx 将会在新进程中保持不变, 也就是说, 如果一个 Capability 的 CapIdx 是 0x00000001, 那么在新进程中该 Capability 的 CapIdx 仍然是 0x00000001. 这使得通过 fork 系统调用创建新进程时, 新进程不必重新维护相关信息(例如, fd 与 CapIdx 对照表).
当然, 唯一的例外的这个进程的 PCB 能力的 CapIdx. 创建新进程时, PCB 能力的 CapIdx 也会保持不变, 但是由于进程本身发生了变化, 对于新进程, 原先的 CapIdx 对应的 PCB 能力不是新进程的 PCB 能力, 而是父进程的 PCB 能力, 因此, 新进程应当通过 fork 系统调用中的 new_pcb_cap 参数来获取新进程的 PCB 能力索引并更新自己的 PCB 能力索引, 以保证新进程进行系统调用时使用的是正确的能力索引(见 kmod 与 linux subsystem 中关于 fork 的实现).

## PCB 与 TCB 能力

由于时间紧迫, 目前暂时未完成完整的 PCB 与 TCB 能力的设计, 也未完成多线程的能力设计. 这也使得当前不得不将信号机制放在内核态中进行实现. 但是如果在后续的开发中支持了多线程, Fault能力, TCB能力的 Suspend/Resume/Context 等功能, 就可以将信号机制放在用户态中进行实现. 目前的大致设想是(以异步信号为例, 其实际上通过一个 Notification 能力来实现, 同时在内核中支持多路等待)

1. linux subsystem(或是新的libc库) 为每个线程创建一个专门的 Notification 能力来模拟各个信号位, 并创建一个 sighandler 线程, sighandler 等待不同线程的 Notification 能力的多个信号位, 当有信号位被触发时, sighandler 线程将会被唤醒, 并进行以下操作:
---
1. TCB_Suspend(), 将当前线程挂起, 并保存上下文. 在多核的情况下, TCB_Suspend 将会发送一个 IPI 中断到当前线程所在的 CPU, 以便于在该 CPU 上挂起当前线程. TCB_Suspend 是一个阻塞操作.
2. 通过 TCB_ReadContext() 获取当前线程的上下文, 并将当前线程的上下文转储到 linux subsystem(或是新的libc库) 自己维护的一个数据结构中
3. 通过 ReadContext 获得的 sp 指针操作用户态线程的栈, 以实现 sigframe 的创建
4. 将新的 sp 指针与 pc 指针(指向sigaction中的handler)通过 TCB_WriteContext() 写入当前线程的上下文中
5. TCB_Resume(), 将当前线程恢复运行. 需要注意的是, TCB_Resume() 只是将当前线程的状态设置为可运行, 并不会立即恢复运行, 需要等待调度器调度到该线程时才会真正恢复运行. TCB_Resume() 是一个非阻塞操作.
6. 当用户态线程的 sigaction 中的 handler 执行完毕后, 调用 sigreturn 系统调用, 其实际操作是查询 linux subsystem(或是新的libc库) 自己维护的线程上下文数据结构, 通过 TCB_WriteContext() 将原先的上下文写入当前线程的上下文中. 此处不需要 TCB_Resume, 因为执行这个操作的是这个线程本身, TCB_WriteContext 会立即将其上下文恢复到原先的状态, 并继续执行原先的代码.
---

通过以上的设计, 可以将信号机制完全放在用户态中进行实现. 对于 Fault 能力(同步信号), 其实现方式与异步信号类似, 只不过这种情况下, sighandler 将通过 Fault 能力来之间获取上下文, 且无需通过 TCB_Suspend() 来挂起当前线程, 因为 Fault 能力可用时, 当前线程必然已经因为异常而被挂起.

## 未来规划: 对用户态接口的 OOP 化改造

接下来一个可选且有益的工作是在 linuxss-libc 与 kmodlibc 中对系统调用再封装一层 OOP 接口, 将 CapIdx 作为对象中存储的唯一成员, 并总是将 CapIdx 作为第一个参数传递给系统调用, 以此来实现对内核资源的封装, 并在用户态中实现对内核资源的访问控制. 这样可以使得用户态程序的代码更加清晰, 更加语义化.