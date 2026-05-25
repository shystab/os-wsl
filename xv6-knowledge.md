# xv6 实验知识手册

## 目录

1. [Unix_Shell 细节](#unix_shell-细节)
2. [Xv6-Syscall 细节](#xv6-syscall-细节)
3. [Xv6-Lottery 细节](#xv6-lottery-细节)
4. [Xv6-Threads 细节](#xv6-threads-细节)
5. [通用概念](#通用概念)

---

## Unix_Shell 细节

### 整体架构：三层

```
main() → 循环读一行
           ↓
parse_and_execute(line) → 去掉 \n，跳过空行
           ↓
split_parallel_commands(line) → 按 & 拆成多个子命令
           ↓
execute(cmd, should_wait) → 解析 token、> 重定向、builtin、fork-exec
```

### main() 入口

```c
int main(int argc, char *argv[])
```

- `argc == 1`（`./seush`）→ 交互模式，从 stdin 读
- `argc == 2`（`./seush tests/1.in`）→ 批处理模式，从文件读
- `argc > 2` → 错误，exit(1)

#### `getline()`

```c
char *line = NULL;
size_t len = 0;
ssize_t nread = getline(&line, &len, input);
```

- `getline` 内部自动 malloc/realloc，不需要预分配缓冲区
- 返回读取的字节数（包括换行符），EOF 返回 -1
- 用完需要 `free(line)`
- `line[strcspn(line, "\n")] = '\0'` 去掉末尾换行

#### `fflush(stdout)`

`printf("seush> ")` 没有 `\n`，默认 stdout 是行缓冲，不 fflush 可能不会立即显示。`fflush(stdout)` 强制刷新缓冲区。

### parse_and_execute()

```c
void parse_and_execute(char *line) {
    line[strcspn(line, "\n")] = '\0';
    for (char *p = line; *p; p++)
        if (*p != ' ' && *p != '\t') { split_parallel_commands(line); return; }
}
```

`strcspn(line, "\n")` 返回 `\n` 在字符串中的位置，没有找到则返回字符串长度。改成 `\0` 去掉换行。

### split_parallel_commands() — 按 & 拆分

```c
void split_parallel_commands(char *line) {
    char *cmd[64];
    int cmd_count = 0;
    char *p = line;
    while (*p && cmd_count < 64) {
        char *start = p;
        while (*p && *p != '&') p++;
        char saved = *p;
        *p = '\0';           // 把 & 替换成字符串结束符
        if (p > start)       // 非空段才加入
            cmd[cmd_count++] = start;
        if (saved == '&')
            p++;             // 跳过 &，继续下一段
    }
```

- 用 `*p = '\0'` 切割字符串，不使用额外内存
- 空段（`&&` 中间）被跳过

```c
    if (cmd_count == 1) {
        execute(cmd[0], 1);  // 单命令：等待
        return;
    }
    for (int s = 0; s < cmd_count; s++)
        execute(cmd[s], 0);   // 多命令：不等待
    while (waitpid(-1, NULL, 0) > 0);  // 统一收尸
}
```

- `waitpid(-1, NULL, 0)`：`-1` 表示等任意子进程，等价于 `wait(NULL)`
- `> 0` 表示还有子进程就继续等，等不到返回 -1 时循环结束

### execute() — 预处理重定向

```c
for(i=0,j=0; cmd_str[i]; i++){
    if(cmd_str[i]=='>'){
        if(j>0 && processed[j-1]!=' ' && processed[j-1]!='\t')
            processed[j++]=' ';     // > 前面没空格 → 补一个
        processed[j++]='>';
        if(cmd_str[i+1] && cmd_str[i+1]!=' ' && cmd_str[i+1]!='\t')
            processed[j++]=' ';     // > 后面没空格 → 补一个
    } else {
        processed[j++]=cmd_str[i];
    }
}
processed[j]='\0';
```

输入 `cat>file` → 输出 `cat > file`。这样 `strtok` 就能把 `>` 作为独立 token 拆分。

### execute() — strtok 切分

```c
char *tokens[128];
int ntokens = 0;
char *p = strtok(processed, " \t");
while (p && ntokens < 128) {
    tokens[ntokens++] = p;
    p = strtok(NULL, " \t");
}
```

- `strtok` 在字符串中找空格/tab，替换成 `\0`，返回 token 指针
- 第一次调用传字符串，后续传 NULL（内部静态指针记位置）
- **修改原字符串**（破坏性的）
- 线程不安全（内部静态指针），但单线程没问题

### execute() — 识别 > 和普通参数

```c
for (int i = 0; i < ntokens && !error; i++) {
    if (strcmp(tokens[i], ">") == 0) {
        if (redirect_file) { print_error(); error = 1; break; }
        i++;
        if (i >= ntokens) { print_error(); error = 1; break; }
        redirect_file = tokens[i];
    } else {
        if (redirect_file) { print_error(); error = 1; break; }
        argv[argc++] = tokens[i];
    }
}
```

三种错误检测：
1. **多个 `>`**：`ls > out1 > out2` — 遇到第二个 `>` 时 `redirect_file` 已非 NULL
2. **`>` 后面没文件名**：`ls >` — `i++` 后 `i >= ntokens`
3. **重定向后有多余参数**：`ls > out extra` — 设置完 `redirect_file` 后遇到额外 token

### execute() — builtins

```c
if (strcmp(argv[0], "exit") == 0) {
    if (argc != 1 || redirect_file) print_error();
    else { free_path(); exit(0); }
}
if (strcmp(argv[0], "cd") == 0) {
    if (argc != 2 || redirect_file) print_error();
    else if (chdir(argv[1]) != 0) print_error();
}
if (strcmp(argv[0], "path") == 0) {
    if (redirect_file) { print_error(); return; }
    set_path(argc > 1 ? &argv[1] : NULL, argc - 1);
}
```

三个 builtin 在当前进程中执行，不 fork。重定向对 builtin 无效，全部检查 `redirect_file` 并报错。

### execute() — fork + exec

```c
pid_t pid = fork();
if (pid == 0) {                     // 子进程
    if (redirect_file) {
        int fd = open(redirect_file, O_WRONLY|O_CREAT|O_TRUNC, 0666);
        dup2(fd, STDOUT_FILENO);     // stdout → 文件
        dup2(fd, STDERR_FILENO);     // stderr → 文件
        close(fd);
    }
    execv(fullpath, argv);           // 替换成目标程序
    print_error(); exit(1);          // exec 失败才到这
} else if (pid < 0) {
    print_error();                   // fork 失败
}
if (should_wait) {
    waitpid(pid, NULL, 0);
    return;
}
```

- `fork()` 一次调用两次返回：父进程得到子进程 PID，子进程得到 0
- `dup2(oldfd, newfd)`：关闭 newfd，让 newfd 指向 oldfd 同一个文件
- `execv(path, argv)`：**成功没有返回**，失败返回 -1
- `waitpid(pid, NULL, 0)`：阻塞等待指定子进程结束

### find_executable() — 搜索 PATH

```c
int find_executable(const char *cmd, char *fullpath, int size) {
    if (strchr(cmd, '/')) {
        if (access(cmd, X_OK) == 0) { strncpy(fullpath, cmd, size); return 1; }
        return 0;
    }
    for (int i = 0; i < path_count; i++) {
        snprintf(fullpath, size, "%s/%s", path_dirs[i], cmd);
        if (access(fullpath, X_OK) == 0) return 1;
    }
    return 0;
}
```

- `strchr(cmd, '/')`：检查命令是否包含 `/`（绝对/相对路径）
- `access(path, X_OK)`：检查文件是否存在且可执行
- `snprintf`：安全拼接字符串（不会溢出）

### set_path() — path 内置命令

```c
static void set_path(char **dirs, int ndirs) {
    for (int i = 0; i < path_count; i++) free(path_dirs[i]);
    path_count = ndirs;
    path_dirs = realloc(path_dirs, sizeof(char*) * (ndirs > 0 ? ndirs : 1));
    for (int i = 0; i < ndirs; i++) path_dirs[i] = strdup(dirs[i]);
}
```

- `path /bin` → 替换路径列表
- `path` → 清空路径（`ndirs == 0`，`realloc` 分配 1 个指针防止 NULL）
- `strdup`：malloc + strcpy 的组合

---

## Xv6-Syscall 细节

### 系统调用完整路径

```
用户程序调 getreadcount()
  ↓ C 编译器生成 call getreadcount
usys.S:
  .globl getreadcount
  getreadcount:
    movl $SYS_getreadcount, %eax    # eax = 22
    int $T_SYSCALL                    # int 64，陷入内核
    ret                               # 返回后回到用户程序
  ↓
CPU 硬件自动做：
    1. 查 IDT[64] → 找到 vector64 的地址
    2. 从 TSS 读内核栈地址，%esp 切换到内核栈
    3. 把用户 ss, esp, eflags, cs, eip 压入内核栈
    4. 跳转到 vector64
  ↓
vector64:
    pushl $64       # 把中断号 64 压入内核栈
    jmp alltraps    # 跳到统一保存逻辑
  ↓
alltraps (trapasm.S):
    pushl %ds, %es, %fs, %gs    # 保存段寄存器
    pushal                       # 保存 8 个通用寄存器（包括 eax=22）
    pushl %esp                   # 把栈指针（trapframe 地址）作为参数
    call trap(tf)                # 调 C 函数
  ↓
trap.c:
    if (tf->trapno == T_SYSCALL)  # 是 64 → 系统调用
        syscall();
  ↓
syscall.c:
    num = cp->tf->eax;            # 从 trapframe 取出 eax = 22
    if (num > 0 && num < NELEM(syscalls) && syscalls[num])
        cp->tf->eax = syscalls[num]();  # syscalls[22] = sys_getreadcount
  ↓
sysproc.c:
    int sys_getreadcount(void) {
        return readcount;
    }
  ↓
返回路径：
    syscall() 返回值写回 cp->tf->eax
    → trap() 返回
    → trapret:
        popal                # 恢复 8 个通用寄存器（eax = readcount 的值）
        popl %gs, %fs, %es, %ds
        addl $8, %esp        # 跳过 trapno 和 errcode
        iret                 # 恢复 eip, cs, eflags, esp, ss，回到用户态
    → usys.S 的 ret 指令
    → 用户程序拿到返回值
```

### usys.S — SYSCALL 宏

```asm
#define SYSCALL(name) \
  .globl name; \          # 把 name 声明为全局标签（链接器看到）
  name: \                  # 定义标签
    movl $SYS_ ## name, %eax; \  # put 调用号进 eax
    int $T_SYSCALL; \     # 触发中断 64
    ret                   # 内核返回后回到调用者
```

C 代码 `fork()` → 链接器找到 `fork:` 标签 → 执行这三条指令。编译器不关心它是不是真正的函数，只关心有没有标签地址。

### syscall.c — syscalls[] 表

```c
static int (*syscalls[])(void) = {
    [SYS_fork]    sys_fork,    // syscalls[1] = sys_fork
    [SYS_read]    sys_read,    // syscalls[5] = sys_read
    [SYS_getreadcount] sys_getreadcount,  // syscalls[22]
};
```

**函数指针数组**。`[SYS_fork]` 是 C99 指定初始化语法，等价于 `syscalls[SYS_fork] = sys_fork`。

`syscall()` 从 `cp->tf->eax` 读出调用号 22，调 `syscalls[22]()` → 调 `sys_getreadcount()`，返回值写回 `cp->tf->eax`。用户态通过 `popal` 恢复 eax 拿到返回值。

### IDT（中断描述符表）

IDT 是物理内存里的一个数组，256 个条目，每个条目 8 字节。CPU 的 `IDTR` 寄存器存 IDT 的基地址和大小。

`tvinit()` 设置 IDT[64]：

```c
SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);
```

- `off = vectors[T_SYSCALL]`：处理函数地址
- `dpl = DPL_USER`（=3）：允许用户态触发此中断（否则 int 64 会崩）
- `istrap = 1`：陷阱门（不清 IF，系统调用期间还能响应时钟中断）

区别：**中断门**（istrap=0，用于设备中断）会清 IF，处理期间不接受其他中断。**陷阱门**（istrap=1，用于系统调用）不清 IF。

### trapframe 结构

```c
struct trapframe {
    uint edi, esi, ebp, oesp, ebx, edx, ecx, eax;  // pushal 顺序
    ushort gs, fs, es, ds;                            // 段寄存器
    uint trapno;                                      // 中断号（64）
    uint err;                                         // 错误码
    uint eip;                                         // 用户态返回地址
    ushort cs;                                        // 用户代码段
    uint eflags;                                      // 标志位
    uint esp;                                         // 用户栈指针
    ushort ss;                                        // 用户栈段
};
```

入栈顺序（从高到低）：ss → esp → eflags → cs → eip → err → trapno → gs → fs → es → ds → edi → esi → ebp → oesp → ebx → edx → ecx → eax

- `eax`：进内核时 = 调用号，出内核时 = 返回值
- `eip`：`int $64` 的下一条指令地址
- `esp`：用户栈指针

### int $64 硬件做了什么

1. 查 `IDT[64]`，取出 cs 和 offset
2. 检查 `CPL <= DPL`（用户 CPL=3，DPL=3，通过）
3. 从 TSS 读出内核栈地址，%esp 切换到内核栈
4. 把用户 ss, esp, eflags, cs, eip 压入内核栈（保存）
5. 跳转到 offset（= vector64）

### argint / argptr / argstr

系统调用从用户栈取参数的工具：

| 函数 | 用途 | 检查 |
|------|------|------|
| `argint(n, &ip)` | 取第 n 个整数参数 | 无 |
| `argptr(n, &pp, size)` | 取第 n 个指针参数 | 指针在用户空间内不越界 |
| `argstr(n, &pp)` | 取第 n 个字符串参数 | 指针有效且以 \0 结尾 |

原理：从 trapframe 中保存的用户 %esp 开始计算偏移：

```c
*ip = *(int*)(cp->tf->esp + 4 + 4*n);
```

### 为什么要加锁（test_2）

```c
int readcount;
struct spinlock readcount_lock;
```

`readcount++` 不是原子操作（读 → 加 → 写三步）。两个核同时执行可能丢失一次增量。

```
CPU0: 读取 readcount=10
CPU1: 读取 readcount=10
CPU0: 写入 11
CPU1: 写入 11  ← 丢了！应该 12
```

`acquire(&readcount_lock)` 保证同一时刻只有一个核执行 `readcount++`。

### 为什么 readcount++ 放在参数检查之前

`test_1` 调 `read(4, buf, 1)`，fd 4 是无效描述符。如果 `readcount++` 放在 `argfd` 之后，参数检查失败返回 -1，计数器不加。移到前面确保每次 read 都计数。

---

## Xv6-Lottery 细节

### 实验内容

把 xv6 调度器从轮询（round-robin）改成彩票调度。进程拥有票数越多，被选中运行的概率越高。

### 调度器核心算法

```c
// scheduler() 中的彩票逻辑
int total = 0;
for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == RUNNABLE)
        total += p->tickets;

if(total == 0) {
    release(&ptable.lock);
    sti();
    continue;
}

int winner = krand() % total;
int cumulative = 0;
for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state != RUNNABLE) continue;
    cumulative += p->tickets;
    if(cumulative <= winner) continue;

    p->ticks++;
    c->proc = p;
    switchuvm(p);
    p->state = RUNNING;
    swtch(&(c->scheduler), p->context);
    switchkvm();
    c->proc = 0;
    break;
}
```

抽签范围：winner ∈ [0, total)。假设 total=60，票数 30:20:10：

| winner 范围 | 选中进程 |
|:-----------:|:--------:|
| 0-29 | PID 3（30 票） |
| 30-49 | PID 4（20 票） |
| 50-59 | PID 5（10 票） |

概率 = 30/60, 20/60, 10/60 = 3:2:1

### 随机数生成器

```c
static unsigned int rng = 1;
static unsigned int krand(void) {
    rng = rng * 1664525 + 1013904223;
    return rng;
}
```

线性同余发生器（LCG），32 位整型自动溢出回绕，周期 2^32。

### 进程状态流转

```
UNUSED → allocproc() → EMBRYO → 初始化完成 → RUNNABLE
                                               ↓
                                      scheduler 选中 → RUNNING
                                                         ↓
                                              yield() → RUNNABLE（时间片到）
                                              sleep() → SLEEPING（等事件）
                                              exit()  → ZOMBIE
                                                          ↓
                                                wait/join → UNUSED
```

### struct proc 所有字段

```c
struct proc {
    uint sz;                     // 进程虚拟空间大小（字节），sbrk 用
    pde_t* pgdir;                // 页表指针（%cr3 的值）
    char *kstack;                // 内核栈底部（allocproc 时 kalloc 分配）
    enum procstate state;        // 进程状态
    int pid;                     // 进程 ID
    struct proc *parent;         // 父进程
    struct trapframe *tf;        // 中断帧（在内核栈上）
    struct context *context;     // swtch 切换的寄存器（在内核栈上）
    void *chan;                  // 睡眠通道（sleep/wakeup 用）
    int killed;                  // 设为 1 时进程会被杀死
    struct file *ofile[NOFILE];  // 打开文件表
    struct inode *cwd;           // 当前目录
    char name[16];               // 进程名
    int tickets;                 // 彩票票数
    int ticks;                   // 累计运行时间片
    int isthread;                // 是否是线程（thread 实验加的）
};
```

### 改动清单

| 文件 | 改动 |
|------|------|
| `proc.h` | `struct proc` 加 `int tickets; int ticks;` |
| `proc.c` allocproc | `p->tickets = 1; p->ticks = 0;` |
| `proc.c` fork | `np->tickets = curproc->tickets;` |
| `proc.c` scheduler | 轮询 → 彩票抽签 |
| `proc.c` | 加 `krand()` 随机数函数 |
| `proc.c` | 加 `fill_pstat()` 遍历填数据 |
| `pstat.h` | 新建，定义 `struct pstat` |
| `syscall.h` | `SYS_settickets 23`, `SYS_getpinfo 24` |
| `usys.S` | `SYSCALL(settickets)`, `SYSCALL(getpinfo)` |
| `user.h` | 函数原型 |
| `syscall.c` | extern + 调度表 |
| `sysproc.c` | `sys_settickets`, `sys_getpinfo` 实现 |
| `defs.h` | `void fill_pstat(struct pstat*);` |

### pstat.h

```c
struct pstat {
    int inuse[NPROC];     // 进程槽是否在使用
    int tickets[NPROC];   // 每个进程的票数
    int pid[NPROC];       // 每个进程的 PID
    int ticks[NPROC];     // 每个进程累计运行时间片
};
```

### 测试验证

三个进程票数 30:20:10，单核运行后 `getpinfo` 打印 ticks。输出比例应接近 3:2:1。偏差来源于父进程先启动导致的几 tick 领先。

---

## Xv6-Threads 细节

### 线程 vs 进程的核心区别

```c
// fork() 复制页表（创建独立地址空间）
if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0)

// clone() 共享页表（线程共享地址空间）
np->pgdir = curproc->pgdir;
```

这是 fork 和 clone 的**唯一本质区别**。其他所有操作（allocproc、复制 ofile、复制 tf、设为 RUNNABLE）都一模一样。

### clone() 完整实现

```c
int clone(void(*fcn)(void*,void*), void *arg1, void *arg2, void *stack)
{
    int i, pid;
    struct proc *np;
    struct proc *curproc = myproc();

    if((np = allocproc()) == 0)
        return -1;

    // 共享页表（线程的核心）
    np->pgdir = curproc->pgdir;
    np->sz = curproc->sz;

    // 复制父进程 trapframe
    np->parent = curproc;
    *np->tf = *curproc->tf;
    np->tf->eax = 0;  // clone() 在子线程返回 0

    // 设置独立用户栈和入口点
    uint *sp = (uint *)stack;
    *--sp = (uint)arg2;         // 压入第二个参数
    *--sp = (uint)arg1;         // 压入第一个参数
    *--sp = 0xffffffff;          // 压入假返回地址
    np->tf->esp = (uint)sp;     // 用户栈指针
    np->tf->eip = (uint)fcn;    // 入口函数

    // 共享文件描述符
    for(i = 0; i < NOFILE; i++)
        if(curproc->ofile[i])
            np->ofile[i] = filedup(curproc->ofile[i]);
    np->cwd = idup(curproc->cwd);

    safestrcpy(np->name, curproc->name, sizeof(curproc->name));
    np->isthread = 1;

    pid = np->pid;

    acquire(&ptable.lock);
    np->state = RUNNABLE;
    release(&ptable.lock);

    return pid;
}
```

### 用户栈布局

`stack` 是传入的 malloc(4096) 的栈顶（`stack1 + 4096`）。clone 在栈上放数据：

```
高地址
┌──────────────────────┐ ← stack = malloc + 4096
│                      │
│    可用栈空间（往下长）│
│                      │
│    arg2              │ ← sp = stack - 4
│    arg1              │ ← sp = stack - 8
│    0xffffffff        │ ← sp = stack - 12（%esp 指向这）
└──────────────────────┘
低地址
```

子线程通过 `iret` 启动时，%esp = sp，%eip = fcn。fcn 看到的栈就像是被 `call` 调进来的一样：

```
[%esp+0] = 0xffffffff（看起来是返回地址）
[%esp+4] = arg1（第一个参数）
[%esp+8] = arg2（第二个参数）
```

### 0xffffffff 的作用

线程应该调 `exit()` 结束，不应该 `return`。如果程序员在 fcn 里写了 `return`：

```
CPU 执行 ret → 从栈顶弹出 0xffffffff → 试图跳转 → 缺页异常 → 内核杀进程
```

这是故意的：保护机制，防止线程`return`后往栈上乱跳。

### join() 完整实现

```c
int join(void **stack)
{
    struct proc *p;
    int havekids, pid;
    struct proc *curproc = myproc();

    acquire(&ptable.lock);
    for(;;){
        havekids = 0;
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
            if(p->parent != curproc || !p->isthread)
                continue;       // 跳过非线程子进程
            havekids = 1;
            if(p->state == ZOMBIE){
                pid = p->pid;
                kfree(p->kstack);
                p->kstack = 0;
                p->pid = 0;
                p->parent = 0;
                p->name[0] = 0;
                p->killed = 0;
                p->state = UNUSED;
                if(stack) *stack = 0;
                release(&ptable.lock);
                return pid;
            }
        }
        if(!havekids || curproc->killed){
            release(&ptable.lock);
            return -1;
        }
        sleep(curproc, &ptable.lock);
    }
}
```

- `wait()` 跳过 `isthread == 1` 的子进程（留给 join 回收）
- `join()` 只匹配 `isthread == 1` 的子进程
- join 不调 `freevm()`——页表由最后一个普通进程退出时回收

### thread_test.c 关键点

```c
stack1 = malloc(4096);
pid1 = clone(worker, 0, 0, (void*)((uint)stack1 + 4096));
```

- `malloc(4096)`：在堆上分配 4096 字节作为线程的用户栈
- `stack1 + 4096`：传入栈顶地址（栈向下增长）
- `clone(worker, 0, 0, …)`：传入 4 个参数，worker 是执行函数，0 和 0 是传给 worker 的参数

### ticket lock 实现

```c
int xchg(volatile int *addr, int newval) {
    int result;
    asm volatile("xchgl %0, %1" : "+m"(*addr), "=a"(result) : "1"(newval));
    return result;
}
```

`xchg` 是 x86 原子交换指令：把 `newval` 写入 `*addr`，同时返回 `*addr` 的旧值。`"=a"(result)` 把结果放 eax 寄存器。`lock;` 前缀保证多核间原子性。

```c
void lock_acquire(struct lock_t *lk) {
    while (xchg(&lk->locked, 1) != 0)  // 原子尝试获取锁
        ;                               // 没拿到，自旋等待
}

void lock_release(struct lock_t *lk) {
    xchg(&lk->locked, 0);               // 原子释放锁
}
```

### sys_clone() 参数提取

```c
int sys_clone(void)
{
    int fcn, arg1, arg2, stack;
    if(argint(0, &fcn) < 0) return -1;
    if(argint(1, &arg1) < 0) return -1;
    if(argint(2, &arg2) < 0) return -1;
    if(argint(3, &stack) < 0) return -1;
    return clone((void(*)(void*,void*))fcn, (void*)arg1, (void*)arg2, (void*)stack);
}
```

系统调用 handler 没有 C 参数，通过 `argint` 从用户栈取。`fcn` 是函数指针（本质就是地址），用 `argint` 取整数再转型。

### wait() 改动

```c
if(p->parent != curproc || p->isthread)
    continue;
```

跳过 `isthread == 1` 的子进程。线程由 `join()` 回收，普通子进程由 `wait()` 回收。

---

## 通用概念

### 两个栈

每个进程有两个栈：

**用户栈**：在用户地址空间（0x00000000 ~ 0x7FFFFFFF），存局部变量、函数调用帧。`%esp` 指向这里。

**内核栈**：在物理内存的高地址区（映射在 KERNBASE 以上），`allocproc` 时 `kalloc()` 分配一页（4096 字节）。`int $64` 时 CPU 自动切换到内核栈。

`int $64` 时 CPU 硬件自动：
1. 从 TSS 读内核栈地址 → %esp = 内核栈顶
2. 压入用户 ss, esp, eflags, cs, eip 到内核栈（保存用户上下文）
3. 跳转到 IDT[64] 指定的处理函数

### 页表（虚拟内存）

虚拟地址到物理地址的翻译：

```
虚拟地址 0x12345678：
    PDX (10位)     PTX (10位)    offset (12位)
    0x48           0x345         0x678

1. pgdir[PDX] → 找到页目录项（指向一个页表页）
2. pgtab[PTX] → 在页表页里找到 PTE
3. PTE 存着物理页基地址 + 权限位
4. 物理地址 = 物理页基地址 + offset
```

`pgdir`（页目录指针）存在 CPU 的 `%cr3` 寄存器。

`fork()` 调 `copyuvm`：遍历整个页表，为每一页分配新物理页、复制内容、建立新页表。结果父子进程各自有独立物理内存。

`clone()` 直接 `np->pgdir = curproc->pgdir`：共享同一个页表，所有虚拟地址翻译到同样的物理地址。

### 函数指针

```c
void(*fcn)(void*, void*)
```

从右往左读：`fcn` 是一个指针，指向一个"接受两个 `void*` 参数、返回 `void`"的函数。

`clone` 的参数 `fcn` 就是这样一个函数指针。线程创建后从 `fcn(arg1, arg2)` 开始执行。

### x86 调用约定（cdecl）

```c
worker(arg1, arg2);
```

编译成汇编：

```asm
push arg2      # 第二个参数先入栈（栈向下生长，esp -= 4）
push arg1      # 第一个参数后入栈（esp -= 4）
call worker    # push 返回地址（esp -= 4），然后跳转到 worker
```

worker 内部通过 %esp 访问参数：
```
[%esp+0] = 返回地址
[%esp+4] = arg1
[%esp+8] = arg2
```

线程启动不是通过 `call`，而是通过 `iret` 从 trapframe 恢复寄存器。所以需要在栈上手动摆好 `返回地址 → arg1 → arg2` 的排列，让 fcn 看起来就像是被 `call` 调用进来的。

### proc.h 完整结构

```c
struct proc {
    uint sz;                     // 进程大小（字节）
    pde_t* pgdir;                // 页表指针
    char *kstack;                // 内核栈底部
    enum procstate state;        // 进程状态
    int pid;                     // 进程 ID
    struct proc *parent;         // 父进程
    struct trapframe *tf;        // 中断帧
    struct context *context;     // swtch 上下文
    void *chan;                  // 睡眠通道
    int killed;                  // 是否被 kill
    struct file *ofile[NOFILE];  // 打开文件
    struct inode *cwd;           // 当前目录
    char name[16];               // 进程名
    int tickets;                 // 彩票票数
    int ticks;                   // 运行时间片
    int isthread;                // 线程标记
};
```

每个字段对应操作系统在管理进程时需要追踪的一条信息。

### Makefile 基本规则

```makefile
目标: 依赖
	命令
```

- `make` = 执行第一个目标
- `make clean` = 执行 clean 目标
- `make test` = 执行 test 目标
- 命令前面必须是 **Tab**，不能是空格
- `.PHONY: test clean`：声明这些目标不对应实际文件

### xv6 文件分工

| 文件 | 职责 |
|------|------|
| `usys.S` | 用户态汇编桩，触发 int $64 |
| `trapasm.S` | alltraps/trapret，保存/恢复 trapframe |
| `trap.c` | 中断分发（系统调用/时钟/设备） |
| `syscall.c` | syscalls[] 路由表 + syscall() 函数 |
| `syscall.h` | 系统调用编号定义 |
| `sysproc.c` | sys_clone, sys_settickets 等系统调用的 handler |
| `sysfile.c` | 文件相关的系统调用 handler |
| `proc.h` | struct proc 定义 |
| `proc.c` | allocproc, fork, clone, join, scheduler, wait, exit |
| `vm.c` | 页表操作（walkpgdir, copyuvm, mappages） |
| `swtch.S` | 上下文切换汇编 |
| `defs.h` | 函数声明 |
| `user.h` | 用户态函数原型 |
| `ulib.c` | 用户态库函数 |
| `main.c` | 内核入口初始化 |

### 常用命令速查

```bash
# 编译内核
make clean && make

# 编译用户程序
make fs.img

# 启动 xv6（无图形）
make qemu-nox CPUS=1

# 退出 xv6
Ctrl + A 然后按 X

# 杀 QEMU（卡死时）
pkill qemu
```
