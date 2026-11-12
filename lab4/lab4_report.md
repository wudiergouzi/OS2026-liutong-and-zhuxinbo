# Lab 4：虚拟内存与内核线程管理实验报告

小组成员：刘通（2411526）、朱信博（2411339）。实验日期：2026-11-12。

本实验依据课程提供的 `lab4_new` 代码和本地 Lab 4 指导书完成，使用 AI 辅助分析与实现，并在 WSL 的 RISC-V 工具链和 QEMU/OpenSBI 环境编译运行。

## 小组分工安排

| 成员 | 分工任务 | 工作量占比 |
| --- | --- | --- |
| 刘通（2411526） | 页映射与引用计数、页表失败回滚、调度器及 switch_to 的 spec/prompt 整理、实现和验证；整理地址空间与上下文切换说明。 | 约 50% |
| 朱信博（2411339） | PCB 初始化、内核栈与线程创建、首次运行现场的 spec/prompt 整理、实现和验证；整理生命周期、调试问题与扩展思考题。 | 约 50% |

两人共同承担 Lab 2/3 依赖复核、完整测试结果核对和报告统稿。

## 1. 实验整体逻辑与实现顺序

本实验先让地址空间可靠可用，再建立线程对象，最后把 CPU 交给线程。执行链为 `kern_init -> pmm_init -> pic_init/idt_init -> vmm_init -> proc_init -> clock_init/intr_enable -> cpu_idle -> schedule -> proc_run -> switch_to`。

第一步继承 Lab 2 页描述符和连续物理页分配，以及 Lab 3 Trap 入口、恢复、中断和异常分类。在这个基础上，`page_insert/page_remove` 管理映射、物理页引用和 TLB；VMA 框架用 `mm_struct/vma_struct` 描述虚拟区域。第二步分配 PCB、内核栈和首次运行现场，把线程加入 PID 哈希表及进程链表并置为可运行。第三步从链表选择线程，更新 current、切换 satp、保存和恢复 context，最终进入线程函数。

内核线程共享内核地址空间，`mm == NULL`，不等于拥有独立用户地址空间的用户进程。在三个基础模块之外，本实验进一步实现内核线程退出、父线程等待、孤儿收养和僵尸资源回收；未实现按需缺页分配、页面置换或用户进程资源释放。

## 2. 功能模块一：虚拟内存页操作

代码：`kern/mm/pmm.c`。

### Spec 的确定

从指导书的接口、已有 `get_pte` 以及页表测试确定三个不变量：有效叶子项对应正确物理页；物理页引用计数等于该接口建立的有效映射数；页表变更后旧 TLB 翻译不能继续生效。

`page_insert(pgdir, page, la, perm)` 成功返回 0，建立或更新指定映射；页表页不足时返回 `-E_NO_MEM`。重复映射同一页只能调整权限，不能意外释放该页；替换为其他页时释放旧引用；只有旧引用降到 0 时才释放物理页。`page_remove` 对不存在的映射静默返回，重复调用具有幂等性。

### Prompt 的组织

以下四段是本次根据指导书整理的实现约束，不是历史 AI 回复的逐字转录。

> 背景：阅读 pmm.c/pmm.h、mmu.h 和 Page 引用计数接口，确认 Sv39 三级页表结构与当前启动映射。
>
> 任务：补齐 page_insert 和 page_remove，在现有页表框架中完成插入、同页更新、替换及撤销映射。
>
> 约束：保留 LAB4 标记并标注学号；不得仅覆盖旧 PTE；保持页引用、权限和 TLB 一致；无映射删除不分配页表；中间页表分配失败时撤销本次新建结构。
>
> 验收：运行课程 PMM 检查，并额外用零个/一个空闲页验证两级中间页表分配失败，检查根项、目标页引用和空闲页数。

### 核心函数理解

`page_insert` 先调用 `get_pte(..., 1)`，失败时尚未增加目标页引用。取得 PTE 后先增加新页引用；若旧项有效且对应同一页，减去刚增加的一次，最终引用不变；若旧项对应其他页，则经 `page_remove_pte` 撤销旧映射。最后用物理页号和权限创建新项，调用 `tlb_invalidate`。

先增加新引用的好处是同页重插入时不会把唯一引用减到 0 并错误释放。`page_remove_pte` 只处理有效项，减少引用、必要时释放页、清 PTE 并刷新对应地址。`page_remove` 使用 `create=0` 查找，因此空地址删除不会产生新页表。

`get_pte` 原本将查找和创建合在一起。本次增加本次新分配的中间页记录：第一个页表页分配成功、第二个失败时，清除根项并归还新页；不撤销调用前已存在的结构。非叶项使用仅 V 的目录项，拒绝把已有大页叶子当成下级页表继续遍历。

启动时仍用临时 1 GiB 映射完成高地址跳转；随后单独分配清零的根页表，预先建立 RAM 的 4 KiB 内核映射，写入 Sv39 satp 并刷新 TLB。这使后续静态映射检查实际走三级页表，不会破坏正在使用的启动大页。

## 3. 功能模块二：进程创建与初始化

代码：`kern/process/proc.c`、`kern/process/entry.S`。

### Spec 的确定

从 PCB 字段、forkret/forkrets/__trapret 的调用关系和进程创建测试确定要求。`alloc_proc` 返回安全空白对象：UNINIT 状态、pid=-1、零运行次数、空 parent/mm/tf、清零 context/name/flags，pgdir 使用内核根页表。`proc_init` 创建 pid=0 的 idleproc，并把当前启动执行流与 bootstack 关联，再创建 pid=1 的 initproc。

`kernel_thread` 返回新 PID 或负错误码；`do_fork` 只有在 PCB、内核栈、地址空间占位和首次现场准备完成后，才分配 PID、挂链并唤醒。栈分配失败需释放 PCB，后续失败需连同内核栈一起回收。`copy_mm` 当前只允许内核线程共享内核页表，不实现用户地址空间复制。

### Prompt 的组织

> 背景：阅读 proc_struct、context、Trap frame、kernel_thread_entry、forkret 和 forkrets，明确栈顶位置与首次恢复链。
>
> 任务：实现 alloc_proc、proc_init、kernel_thread、do_fork、copy_thread、copy_mm，创建 idleproc 和 initproc。
>
> 约束：PCB 清零后设置明确默认值；idle 复用当前 bootstack；用 s0/s1 传递线程函数和参数；新现场返回 S 态并通过 SPIE 恢复中断；先准备资源再发布，失败不得留下半初始化的链表节点。
>
> 验收：验证 PID、父线程、状态、栈和首次 context，运行 init_main 输出；用耗尽空闲页的测试验证 kernel_thread 返回 -E_NO_MEM 且未改变 current。

### 核心函数理解

`alloc_proc` 用 kmalloc 分配并 memset 清零，随后设置状态、PID 和内核页表物理地址，初始化两个链表节点。`proc_init` 初始化 proc_list/hash_list，把 idleproc 置为 RUNNABLE，PID 为 0，current 指向它，need_resched 置 1，使 cpu_idle 能触发首次调度。idle 不加入普通候选链表，而作为没有候选线程时的后备。

`kernel_thread` 构造临时 Trap frame：s0=函数指针，s1=参数，epc=kernel_thread_entry，SPP=1、SPIE=1、SIE=0。线程创建时不直接调用函数，而是将这份现场交给 do_fork。

`copy_thread` 把 Trap frame 放到新线程内核栈顶下方，令返回值 a0=0，sp 为指定栈或帧首；context.ra 指向 forkret，context.sp 指向帧首。首次 switch_to 的 ret 于是进入 forkret，再经 `forkrets(current->tf) -> __trapret -> sret -> kernel_thread_entry`。后者把 s1 移入 a0，调用 s0 中的函数地址。

`do_fork` 依次申请 PCB、内核栈，调用 copy_mm/copy_thread。完成后在关中断临界区分配唯一 PID、加入哈希表及进程链表、增加 nr_process、唤醒为 RUNNABLE。copy_mm 保留接口形态，明确当前只支持 `current->mm == NULL`。

## 4. 功能模块三：调度与上下文切换

代码：`kern/schedule/sched.c`、`kern/process/proc.c`、`kern/process/switch.S`。

### Spec 与 Prompt

spec 来自 schedule 的候选状态、proc_run 的原子性要求，以及 context 的字段偏移：cpu_idle 在 need_resched 为真时调用 schedule；调度器从当前位置向后循环扫描，选择 RUNNABLE 线程，无候选时回到 idle；proc_run 更新 current、写入目标页表并切换现场；switch_to 对齐 context 保存 ra、sp 和 s0-s11。

> 背景：阅读进程链表、need_resched、runs、context、sync.h 和 pmm.c 的 enable_paging。
>
> 任务：实现 cpu_idle、schedule、proc_run、switch_to，让 idle 能切到 init，并支持多个内核线程重复让出 CPU。
>
> 约束：选择及切换关键步骤保存并关闭中断，返回后按原标志恢复；跳过链表头和非 RUNNABLE 节点；Sv39 模式字段位于 satp 高位，不使用 Sv32 的 lsatp；汇编偏移必须对应 context，不额外承担调度策略。
>
> 验收：运行课程两个线程反复 schedule 的测试，确认两个线程均执行且原线程能恢复，检查 initproc 输出。

### 核心函数理解

`schedule` 清除当前线程的 need_resched，从当前节点的下一个位置开始循环扫描，跳过 proc_list 哨兵。链表由创建顺序登记，调度按固定环顺序选择下一个可运行实体，形成课程所用的简单队列式轮转；没有时间片动态优先级。选中实体 runs 加一，必要时调用 proc_run。

`proc_run` 在关中断期间保存 prev、更新 current、写 satp。写入值是 `((uintptr_t)SATP_MODE_SV39 << 60) | (proc->pgdir >> RISCV_PGSHIFT)`，然后 flush_tlb 并调用 switch_to。switch_to 返回时，执行的已经是该线程上次暂停的调用点，因此它会恢复属于自己这次调用的中断标志。

`switch_to` 按 context 的 14 个槽保存旧线程 ra/sp/s0-s11，再从新 context 恢复它们，最后 ret。普通调用约定允许调用者保存寄存器在函数调用期间变化，因此 context 不需要像 Trap frame 一样保存全部通用寄存器。首次切换用预置 ra 进入 forkret，后续切换用以前保存的 ra 恢复原调用链。

本实验没有增加定时器抢占调度；测试线程通过调用 schedule 主动让出 CPU。线程函数返回时，kernel_thread_entry 将函数返回值交给 do_exit；本次扩展让它进入正常退出和回收流程，不再以框架 panic 作为正常结束。

### 扩展：线程退出、等待与回收

新增接口 `do_wait(int pid, int *code_store)`：pid=0 表示等待任意子线程，正数表示指定子线程；成功回收返回 0，并可写回退出码；没有对应子线程返回 `-E_BAD_PROC`，非法 PID 返回 `-E_INVAL`。该接口仅供当前内核线程调用，code_store 必须为 NULL 或可写的内核地址，不是尚未实现的用户态 wait 系统调用。

PCB 新增 exit_code、waiting_child、wait_pid。do_exit 在关中断临界区记录退出码并设置 ZOMBIE，将尚存子线程转交给仍存活的 initproc；若 init 本身正在退出或已不存活，则转交给 idleproc。已经是僵尸的被收养线程也触发相应等待通知。随后唤醒正在等待该子线程的父线程并 schedule，僵尸不再参与 RUNNABLE 选择。

线程不能释放仍在使用的自身内核栈。因此 do_exit 只标记退出，不在当前栈上执行 put_kstack/kfree。父线程 do_wait 确认子线程为僵尸后，解除 proc_list 和 PID 哈希链接，减少 nr_process，从父线程自己的执行栈上归还子线程栈页并释放 PCB。若回收的是 initproc，同时清空全局指针，避免悬空引用。所有线程共享的 boot_pgdir 不随某个线程退出而释放。

do_wait 将“检查子线程、设置等待条件、进入 SLEEPING”放在同一个关中断临界区，避免检查之后、睡眠之前丢失退出唤醒。schedule 返回后重新扫描，正确处理指定 PID、任意子线程和重新检查条件。调用前的中断状态在最终返回时恢复；嵌套调用不会无条件开启中断。

idle 复用启动栈，不能像普通线程一样阻塞等待或退出。cpu_idle 增加非阻塞扫描，只回收归它所有的僵尸线程；init 退出后剩余子线程由 idle 接管。直到 nr_process=1、proc_list 为空且 initproc 已清空，才输出 `[Process cleanup] all kernel threads reaped` 并正常请求 SBI 关机。程序仍可以继续调度尚存的可运行线程，不会在任意一个线程退出时直接关机。

这部分的 spec 是“退出不返回、保留退出码、延迟释放当前栈、父线程只能回收自己的子线程、无丢失唤醒、孤儿最终可回收”。实现提示约束据此组织为：阅读 PCB/调度/调用返回链；替换 panic 型 do_exit 并增加 do_wait 和 idle 回收；保留共享内核页表、在临界区发布状态与唤醒；验证多种退出次序、重复回收、资源恢复和 PID 绕回。提示内容是实现要求整理，不冒充历史逐字对话。

## 5. AI 辅助实现、编译与调试中的问题

1. 继承分配器时最初取用了 First-Fit 选择逻辑，但本课程包保留的是 Best-Fit 的检查函数。复核最小适配块断言后，改为遍历所有可用块选择最小满足项；检查函数的断言保持原样。
2. 当前 GCC 将 kmalloc.c 的整数地址传给 `kva2page(void *)` 诊断为类型错误。显式转换为 `(void *)kva`，不改变地址和分配算法。
3. 课程的调度测试把线程函数声明为 void 返回，而 kernel_thread 接口需要 int 返回，导致函数指针类型编译错误。仅将 test_lab4_thread 返回类型改为 int；该函数本身无限调度，不需要新增返回语句，原有测试断言及三项各 10 分均保留。
4. 初次运行在 `boot_pgdir_va[0] & PTE_U` 的断言停止。Sv39 非叶项的 U/A/D 应清零，因此目录项使用 V，不沿用旧权限写法；将这一个架构不兼容断言改为检查 U/A/D 均为 0，同时保留叶子项 U/W 的检查。这是明确的测试适配，不声称所有课程测试源码逐字未改。
5. 启动大页不能直接交给仅面向 4 KiB 三级页表的 get_pte 遍历。因此保留临时启动映射，另建预映射内核页表后切换；静态映射检查中的物理地址循环使用 uintptr_t，并从 PADDR(KERNBASE) 开始，避免原 int 变量截断及错误的虚拟/物理边界比较。
6. 沿用前两次实验的 QEMU 兼容修正：用 -kernel 加载映像，启动页表加入低地址恒等映射。评分环境使用可用的 gdb-multiarch，增加构建失败即退出及 GDB 超时，避免失败后挂起。
7. 初版框架的 do_exit 会 panic，再由 kmonitor 关机，不能证明线程回收完成。本次扩展删除该正常路径，改由父线程/idle 回收后关机；测试同时检查 QEMU 返回 0、全部生命周期标记以及没有 kernel panic，避免把错误处理中的关机当成成功。

上述过程区分实现失误、课程兼容问题和测试预期错误，没有把静态推断当作运行结果。

## 6. 验证方法与实际结果

在 WSL 中进入本目录运行：

```sh
make grade
python3 tools/check_edges.py
make
make qemu
```

课程评分脚本在 `/tmp/lab4_grade` 副本注入测试，正式源码不含测试调用。三项结果：

```text
check PMM:             OK
check Process Create:  OK
check Schedule:        OK
Total Score: 30/30
```

PMM 测试覆盖首次映射、同页权限更新、映射替换、引用变化和重复删除；创建测试检查 idle/init 的 PID、状态、父线程和首次 context；调度测试创建两个线程并多次主动让出 CPU，检查两者均实际运行。

附加测试源码独立存于 `tools/test_lab4_edges.c` 和 `tools/test_lab4_lifecycle.c`，由 check_edges.py 在临时目录中注入和编译，未增加课程分值。课程调度测试源文件仍保留；仅在附加测试的临时副本中，将两个测试线程的末尾无限循环改为 return 0，使其在原有三次主动让出后可退出，以便继续验证 wait/reap。

```text
[Edge PMM OOM rollback] PASS
[Edge fork OOM cleanup] PASS
[Test Schedule] PASS
[Lifecycle zombie reap/status/double wait] PASS
[Lifecycle blocking wait/wakeup/ownership] PASS
[Lifecycle explicit exit/interrupt state preserved] PASS
[Lifecycle targeted and any-child wait] PASS
[Lifecycle live and zombie orphan adoption] PASS
[Lifecycle repeated create/exit/reap, free pages restored] PASS
[Lifecycle PID wraparound] PASS
[Lifecycle idle adoption] PASS
[Process cleanup] all kernel threads reaped
All additional Lab 4 failure-path and lifecycle checks passed.
```

页表 OOM 测试耗尽空闲页，分别保留 0 页、1 页，验证 get_pte 无法完成中间页表时 root 不留下项、目标页引用不变、刚申请的页被归还，释放测试资源后空闲总数恢复。线程 OOM 测试在无空闲页时创建线程，验证返回 -E_NO_MEM、current 不变及清理后页数恢复。

生命周期测试覆盖先退出后等待、先等待再退出、正常返回与显式 do_exit、正负退出码、等待非子线程、重复等待、指定和任意 PID、活孤儿及僵尸孤儿收养、init 退出后的 idle 收养。还执行 MAX_PID+8（8200）次创建—退出—等待，跨过 PID 分配边界，每次验证旧 PID 从哈希表消失，最后断言空闲物理页数恢复到循环前数值。共享分配器仍可保留自身缓存，PCB 内存通过 kfree 归还给分配器；测试不把“每次都把分配器缓存退给物理页管理器”作为线程回收语义。

普通启动通过 `check_alloc_page`、`check_pgdir`、`check_boot_pgdir`、`check_vma_struct`、`check_vmm`，随后实际输出：

```text
this initproc, pid = 1, name = "init"
To U: "Hello world!!".
To U: "en.., Bye, Bye. :)"
```

随后输出 `[Process cleanup] all kernel threads reaped`，QEMU 正常返回 0，日志没有 kernel panic。课程评分仍为 30/30；评分脚本在 do_exit 入口结束调度演示，正常关机和完整回收由普通启动及独立生命周期测试验证。内置分数仅表示所覆盖测试通过，不代替课程最终评定。最新运行日志保存在 `evidence/`。

## 7. 实验知识点与 OS 原理的对应

| 实验知识点 | 对应原理及含义 | 联系与区别 |
| --- | --- | --- |
| page_insert/remove、Page.ref | 页映射生命周期、共享页计数 | 多个虚拟地址可共享一物理页；页表页引用与映射页引用用途不同。 |
| 三级 PTE 与 satp、TLB | 虚拟地址转换及翻译缓存一致性 | 修改内存中 PTE 后需使旧翻译失效；本实验只处理单核本地 TLB。 |
| mm_struct、vma_struct | 地址空间的逻辑描述 | VMA 描述范围和属性，页表落实映射；二者不是同一个结构。 |
| proc_struct 与状态 | PCB、线程生命周期 | 当前 RUNNABLE 同时覆盖正在运行和可运行，实际运行者由 current 指示。 |
| context 与 Trap frame | 调度现场、异常现场 | context 遵守函数调用边界，Trap frame 面对异步打断，保存集合不同。 |
| schedule 与 proc_run | 调度策略和调度执行 | 前者决定选谁，后者组织 current/地址空间/现场切换。 |
| local_intr_save/restore | 单核临界区与原子性 | 避免切换到一半被同级中断打断；不能替代多核锁，也不屏蔽同步异常。 |
| do_fork 失败回滚 | 资源所有权和事务式创建 | 未挂链前资源归创建者，失败必须逆序释放，不能留下可运行的半成品。 |
| do_exit/do_wait/idle 回收 | 僵尸状态、父子关系与延迟销毁 | 退出保留状态供父线程读取；换到其他栈后才能销毁原内核栈，孤儿需有收养者。 |

## 8. 原理中重要但本实验未覆盖的内容

未实现用户程序装载、独立用户地址空间复制、写时复制、用户态 exit/wait 系统调用与用户地址校验、文件及用户页表的退出释放、按需缺页、换入换出、多核 TLB shootdown、优先级与时间片抢占、公平性和实时性保证、通用设备等待队列。当前已实现内核线程的退出、等待与僵尸回收，不能将其扩大解释为完整用户进程资源管理。这些机制需要在目前的 PCB、VMA、页表和调度接口基础上继续扩展。

## 9. 扩展练习

### 9.1 local_intr_save/restore 如何开关中断

宏 local_intr_save(intr_flag) 调用 __intr_save，读取 sstatus.SIE。若原来开启，则调用 intr_disable 清 SIE，并返回 1；若原来关闭则返回 0。local_intr_restore 按该标志决定是否重新开启，避免把调用前就关闭的中断无条件打开。

例如 schedule 先保存为 1 并关闭，内部 proc_run 再保存时得到 0；内部恢复不会提前打开中断，外层恢复才按原状态打开。首次运行的新线程则通过预置 Trap frame 的 SPIE 和 sret 恢复中断。该机制针对本核同级中断，不自动提供多核互斥，也不能阻止非法指令等同步异常。

### 9.2 get_pte 的两段代码为何相像，是否应拆分

Sv32 用两级页表，页表项 4 字节，每级索引 10 位，每表 1024 项；Sv39 用三级，页表项 8 字节，每级索引 9 位，每表 512 项；Sv48 用四级，同样每级 9 位、每表 512 项。三者常规页内偏移均为 12 位，每个页表本身占一页。

get_pte 中的两段代码分别处理 Sv39 根表到中间表、再到末级表。每层都先检查当前项，不存在且 create 为真时分配、清零并登记下一级页表，再根据物理地址获得其内核虚拟地址。因此控制逻辑相似，区别主要是取哪一级索引。Sv39 需要两次向下走才取得末级 PTE；Sv48 可用类似循环再增加一层。遇到大页叶子必须识别层级，不能当目录项继续访问。

合并查找与分配让 page_insert 的调用简短，适合教学接口；代价是 create 参数带来隐藏副作用，失败回滚和资源所有权更难表达。只查找可抽为 lookup_pte，创建可抽为 walk_or_alloc 或统一遍历器的显式模式，记录本次分配用于回滚。不是必须拆成两个完全重复的实现，较好的方式是共享逐级遍历逻辑，让调用端明确是否允许分配，并保持失败后状态可解释。本次保留原接口、补充局部回滚和大页识别。
