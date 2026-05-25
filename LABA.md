# LABA 实验手册

---

## 目录

1. [环境准备](#环境准备)
2. [Unix_Shell（必做）](#unix_shell必做)
3. [Xv6-Syscall（必做）](#xv6-syscall必做)
4. [Xv6-Lottery（选做）](#xv6-lottery选做)
5. [Xv6-Threads（选做）](#xv6-threads选做)
6. [QEMU 操作说明](#qemu-操作说明)
7. [函数解析](#函数解析)

---

## 环境准备

### 安装依赖

```bash
sudo apt-get install qemu-system-i386 gcc-multilib
sudo apt-get install tcl tk expect
```

### xv6 编译

```bash
cd <实验目录>/src
make clean && make && make fs.img
```

---

## Unix_Shell（必做）

### 目录

```
/home/shysta/LABA/Unix_Shell/
```

### 编译

```bash
cd /home/shysta/LABA/Unix_Shell
make
```

### 运行

```bash
# 交互模式
./seush

# 批处理模式（从文件读命令）
./seush tests/1.in
```

### 测试

```bash
make test
```

或手动：

```bash
rm -rf tests-out && mkdir tests-out && ./test-seush.sh
```

全部 22 个测试通过显示绿色 `passed`。

---

## Xv6-Syscall（必做）

### 实验内容

添加一个系统调用 `getreadcount()`，返回自开机以来 `read()` 被调用的次数。

### 改动的文件

| 文件 | 改动 |
|------|------|
| `src/syscall.h` | `#define SYS_getreadcount 22` |
| `src/usys.S` | `SYSCALL(getreadcount)` |
| `src/user.h` | `int getreadcount(void);` |
| `src/syscall.c` | `extern` 声明 + 调度表 `[SYS_getreadcount] sys_getreadcount,` |
| `src/sysproc.c` | 实现 `sys_getreadcount()` 返回 `readcount` |
| `src/sysfile.c` | 定义 `readcount` + 在 `sys_read()` 里 `acquire`/`readcount++`/`release` |

### 编译 & 测试

```bash
cd /home/shysta/LABA/Xv6-Syscall/src
make clean && make && make fs.img
cd ..
./test-getreadcount.sh
```

### 测试输出

```
test1: XV6_TEST_OUTPUT 0 1 1000    ← 简单的计数器验证
test2: XV6_TEST_OUTPUT 200000      ← 并发测试（需加锁）
```

---

## Xv6-Lottery（选做）

### 实验内容

把 xv6 调度器从轮询（round-robin）改成彩票调度（lottery scheduling）。每个进程按票数比例获得 CPU 时间。

### 改动的文件

| 文件 | 改动 |
|------|------|
| `src/proc.h` | `struct proc` 加 `int tickets; int ticks;` |
| `src/proc.c` | `allocproc`: 初始化 `tickets=1, ticks=0` |
| | `fork()`: 子进程继承父进程票数 |
| | `scheduler()`: 改成彩票抽签逻辑 |
| | 加 `fill_pstat()`: 遍历进程表填数据 |
| `src/pstat.h` | 新建，定义 `struct pstat` |
| `src/syscall.h` | 加 `SYS_settickets 23`, `SYS_getpinfo 24` |
| `src/usys.S` | `SYSCALL(settickets)`, `SYSCALL(getpinfo)` |
| `src/user.h` | `int settickets(int);`, `int getpinfo(struct pstat*);` |
| `src/syscall.c` | extern + 调度表 |
| `src/sysproc.c` | 实现 `sys_settickets()`, `sys_getpinfo()` |
| `src/defs.h` | `void fill_pstat(struct pstat*);` |

### 调度器核心算法

```c
// scheduler() 中的彩票逻辑
int total = 0;
for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == RUNNABLE)
        total += p->tickets;

if(total == 0) continue;

int winner = krand() % total;
int cumulative = 0;
for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state != RUNNABLE) continue;
    cumulative += p->tickets;
    if(cumulative <= winner) continue;
    p->ticks++;
    // 运行该进程一个时间片
    break;
}
```

### 编译 & 测试

```bash
cd /home/shysta/LABA/Xv6-Lottery/src
make clean && make && make fs.img
make qemu-nox CPUS=1
```

在 xv6 的 `$` 下运行：

```
test_lottery
```

### 预期输出（单核）

```
PID	Tickets	Ticks
3	30	202
4	20	105
5	10	47
```

比例大致为 3:2:1，接近票数比例。偏差来源于父进程先启动的几 tick 优势。

---

## Xv6-Threads（选做）

### 实验内容

给 xv6 添加内核线程：`clone()` 创建共享地址空间的线程，`join()` 回收线程。

### 改动的文件

| 文件 | 改动 |
|------|------|
| `src/proc.h` | `struct proc` 加 `int isthread;` |
| `src/proc.c` | `allocproc`: 初始化 `isthread=0` |
| | `clone()`: 共享页表、设置独立用户栈、`isthread=1` |
| | `join()`: 等待共享地址空间的子线程 |
| | `wait()`: 跳过 `isthread==1` 的子进程 |
| `src/syscall.h` | `SYS_clone 25`, `SYS_join 26` |
| `src/usys.S` | `SYSCALL(clone)`, `SYSCALL(join)` |
| `src/user.h` | 函数原型 |
| `src/syscall.c` | extern + 调度表 |
| `src/sysproc.c` | `sys_clone()`, `sys_join()` 实现 |
| `src/defs.h` | `clone()`, `join()` 声明 |

### clone() 关键代码

```c
int clone(void(*fcn)(void*,void*), void *arg1, void *arg2, void *stack) {
    if((np = allocproc()) == 0) return -1;

    np->pgdir = curproc->pgdir;      // 关键：共享页表，不复制
    np->sz = curproc->sz;

    *np->tf = *curproc->tf;          // 复制 trapframe
    np->tf->eax = 0;                 // 子线程 clone() 返回 0
    np->tf->eip = (uint)fcn;         // 从 fcn 开始执行
    np->tf->esp = (uint)stack等设置;  // 用独立栈

    np->isthread = 1;
    np->state = RUNNABLE;
    return np->pid;
}
```

### 用户栈布局

```
高地址
┌──────────────────────┐ ← stack + PGSIZE
│                      │
│    可用栈空间         │
│                      │
│    arg2              │
│    arg1              │ ← fcn 的参数
│    0xffffffff        │ ← 假返回地址（防 return）
└──────────────────────┘ ← %esp 指向这里（子线程启动时）
```

### 编译 & 运行

```bash
cd /home/shysta/LABA/Xv6-Threads/src
make clean && make && make fs.img
make qemu-nox CPUS=1
```

在 `$` 下运行：

```
thread_test
```

### 预期输出

```
parent calling clone...
clone returned pid=4
child thread running, arg1=42 arg2=99
join returned pid=4
```

锁测试（完整版）：

```
counter = 2000000 (expected 2000000)
```

---

## QEMU 操作说明

### 启动 xv6

```bash
make qemu-nox          # 无图形界面（当前终端内运行）
make qemu              # 有图形界面
make qemu-nox CPUS=1   # 单核（调试用，彩票/线程测试建议单核）
```

### 退出 QEMU

```
Ctrl + A  然后按  X
```

### 在 xv6 内运行测试程序

xv6 启动后会显示 `$` 提示符，直接输入测试程序名：

```
$ test_lottery
$ thread_test
```

### 查看 xv6 内的文件

```
$ ls
```

---

## 函数解析

### 系统调用全链路

```
用户程序调 clone()
  ↓ C 编译器生成 call clone
usys.S clone: 标签
  ↓ movl $25, %eax   （放入系统调用号）
  ↓ int $64           （触发中断，陷入内核）
CPU 硬件
  ↓ 查 IDT[64] → 跳转到 vector64
  ↓ 切换到内核栈，保存用户寄存器到 trapframe
  ↓ pushl $64 → jmp alltraps
alltraps（trapasm.S）
  ↓ pushl %ds, %es, %fs, %gs
  ↓ pushal 保存所有通用寄存器
  ↓ pushl %esp → call trap(tf)
trap.c trap()
  ↓ tf->trapno == T_SYSCALL → syscall()
syscall.c syscall()
  ↓ num = cp->tf->eax（取出系统调用号 25）
  ↓ cp->tf->eax = syscalls[25]() → 调 sys_clone()
  ↓ 返回值写回 tf->eax
返回
  ↓ trapret → popal → iret
  ↓ 回到 usys.S → ret → 回到用户程序
```

### struct proc 字段含义

| 字段 | 类型 | 作用 | 分配时机 |
|------|------|------|---------|
| `sz` | `uint` | 进程用户空间大小（字节） | fork/clone/sbrk |
| `pgdir` | `pde_t*` | 页表物理地址 | fork（复制）/ clone（共享） |
| `kstack` | `char*` | 内核栈底部 | allocproc |
| `state` | `enum procstate` | 进程状态 | 各函数修改 |
| `pid` | `int` | 进程 ID | allocproc |
| `parent` | `struct proc*` | 父进程 | fork/clone |
| `tf` | `struct trapframe*` | trapframe 指针 | allocproc（在内核栈上） |
| `context` | `struct context*` | swtch 上下文 | allocproc（在内核栈上） |
| `ofile[NOFILE]` | `struct file*[]` | 打开文件表 | 各函数修改 |
| `cwd` | `struct inode*` | 当前目录 | fork/clone/exec |
| `tickets` | `int` | 彩票票数 | 互斥实验加的 |
| `ticks` | `int` | 累计运行时间片 | 互斥实验加的 |
| `isthread` | `int` | 是否线程 | 线程实验加的 |

### trapframe 结构

```c
struct trapframe {
  uint edi, esi, ebp, oesp, ebx, edx, ecx, eax;  // pushal 顺序
  ushort gs, fs, es, ds;                           // 段寄存器
  uint trapno;                                      // 中断号（64）
  uint err;                                         // 错误码
  uint eip;                                         // 用户态返回地址
  ushort cs;                                        // 用户代码段
  uint eflags;                                      // 标志位
  uint esp;                                         // 用户栈指针
  ushort ss;                                        // 用户栈段
};
```

### fork() vs clone() 对比

| 操作 | fork() | clone() |
|------|--------|---------|
| 页表 | `copyuvm()` 复制整个地址空间 | 直接 `np->pgdir = curproc->pgdir` |
| 用户栈 | 继承父进程 `%esp` | 使用传入的 `stack` |
| eip | 从 fork() 返回处继续 | 从 `fcn` 开始执行 |
| 回收 | `wait()` | `join()` |
| 地址空间 | 独立 | 共享 |
| 文件表 | 复制（filedup） | 复制（filedup） |

### 进程 6 种状态流转

```
UNUSED → allocproc() → EMBRYO → fork/clone 完成 → RUNNABLE
                                                        ↓
                                               scheduler 选中 → RUNNING
                                                                   ↓
                                                        yield() → RUNNABLE（时间片用完）
                                                        sleep() → SLEEPING（等事件）
                                                        exit()  → ZOMBIE
                                                                            ↓
                                                                  wait/join → UNUSED
```
