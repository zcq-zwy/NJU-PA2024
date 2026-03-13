# Lock Lab 笔记

适用范围：本仓库里的 `xv6-rv32 + NEMU` 版本，覆盖官方 `2024` 与 `2025` 两套 lock lab 测试。

## 1. 总体做法

我的做法不是维护两套内核，而是：

- 做一套内核实现
- 保留两套官方测试入口
- 用年份开关在编译时切换 `2024` / `2025` 测试文件

相关位置：

- 年份选择与评分入口：`grade-lab-lock`
- 年份切换包装：`grade-lab-lock-2024`、`grade-lab-lock-2025`
- 年份切换用户态测试包装：`user/kalloctest.c`
- 官方原始测试存档：`official-lock-tests/original/2024`、`official-lock-tests/original/2025`
- RV32/NEMU 适配测试：`official-lock-tests/rv32/2024`、`official-lock-tests/rv32/2025`

## 2. 2024 lock lab：核心思路

`2024` 的重点是两件事：

- 降低 `kalloc` 的锁竞争
- 降低 `bcache` 的锁竞争

### 2.1 `kalloc`：从全局空闲链表改成 per-CPU

原始 xv6 的问题是所有 CPU 都争同一个空闲页链表锁。

我做的改法：

- 每个 CPU 一条 `freelist`
- 本 CPU 优先从自己的 `freelist` 分配
- 本地没有空闲页时，再去别的 CPU 偷页
- 页引用计数 `refcnt[]` 单独用分桶锁保护

数据结构在：

- `kernel/kalloc.c`

核心对象：

- `freelist[NCPU]`
- `lock[NCPU]`
- `refcnt[]`
- `reflock[NREFLOCK]`

这样做的效果是：

- 常见路径只打本地锁
- 真正共享的只有偷页和引用计数
- `kalloctest` 看到的 `kmem` 竞争显著下降

### 2.2 `bcache`：从单链表 + 全局锁改成哈希分桶

原始 xv6 的 buffer cache 用一把全局 `bcache.lock`。

我做的改法：

- 把缓存拆成 `NBUCKET=13` 个 bucket
- 通过 `(dev + blockno) % NBUCKET` 选 home bucket
- 命中时只锁 home bucket
- home bucket 没有空闲 buf 时，再通过 `steal_lock` 协调跨桶偷 buf

数据结构在：

- `kernel/bio.c`
- `kernel/buf.h`

核心对象：

- `bucket[NBUCKET]`
- `bucket[i].lock`
- `steal_lock`
- `buf.bucketno`

这样做的效果是：

- 不同 block 的访问大概率分散到不同桶
- `bcachetest` 的热点不再压在一把全局锁上

### 2.3 锁统计链路

官方 `kalloctest` / `bcachetest` 需要通过 `statistics` 设备读取锁统计。

我做了三件事：

- 在 `spinlock` 层维护锁注册表和统计信息
- 增加 `statistics` 设备
- 在 `init` 启动时创建设备节点

相关文件：

- `kernel/spinlock.c`
- `kernel/stats.c`
- `kernel/file.h`
- `kernel/main.c`
- `user/init.c`

统计输出包括：

- `#test-and-set`
- `#acquire()`
- `kmem*` / `bcache*` 锁
- top 5 contended locks

## 3. 2025 lock lab：核心思路

`2025` 建立在 `2024` 的 `kalloc` 基础上，重点变成：

- `rwlock`
- `cpupin`
- 在更强压力下验证 `kalloc`

### 3.1 `rwspinlock`

我实现了一个内核态读写自旋锁，核心状态包括：

- `readers`
- `writer`
- `waiting_writers`
- `cpu`
- 一个内部保护状态的普通 `spinlock`

设计策略：

- 有 writer 或等待中的 writer 时，新的 reader 不再进入
- 这样偏向 writer，避免 writer 饥饿

相关文件：

- `kernel/spinlock.h`
- `kernel/spinlock.c`

测试入口：

- `sys_rwlktest()`

### 3.2 `cpupin`

`2025` 测试会把不同子进程固定到不同 CPU 上。

我做的实现：

- 在 `struct proc` 中增加 `pincpu`
- `sys_cpupin()` 设置 `pincpu`
- `scheduler()` 只在目标 CPU 上调度该进程

相关文件：

- `kernel/proc.h`
- `kernel/proc.c`
- `kernel/sysproc.c`
- `kernel/syscall.c`
- `kernel/syscall.h`
- `user/user.h`
- `user/usys.pl`

判断逻辑是：

- 若 `p->pincpu == 0`，任意 CPU 可运行
- 若 `p->pincpu == c`，只允许 CPU `c` 运行

### 3.3 `rwlktest`

我把 `rwlktest` 做成系统调用驱动：

- 懒初始化一个全局 `rwspinlock`
- 做固定次数的读锁/写锁加解锁
- 用户态测试检查 4 个 CPU 是否都成功完成

## 4. 两个 lab 的知识图

### 4.1 2024

```text
kalloctest
  -> sbrk()/fork()/exit()
    -> kalloc()/kfree()
      -> freelist[NCPU]
      -> kmem.lock[cpu]
      -> reflock[hash(pa)]
  -> statistics()
    -> statslock()
      -> locks[] / nts / n

bcachetest
  -> bread()/brelse()/log_write()
    -> bget()
      -> bucket[hash(dev, blockno)]
      -> bucket[i].lock
      -> steal_lock
```

### 4.2 2025

```text
kalloctest(2025)
  -> cpupin(cpu)
    -> proc.pincpu
    -> scheduler() 只在目标 CPU 运行
  -> sbrk()/fork()/exit()
    -> per-CPU kalloc
      -> freelist[NCPU]
      -> steal from other CPU
      -> refcnt[]

rwlktest
  -> sys_rwlktest()
    -> rwspinlock
      -> readers
      -> writer
      -> waiting_writers
      -> internal spinlock
```

## 5. 测试点与实现对应

### 5.1 `kalloctest`

- `test1`
  - 并发 `sbrk(+4096/-4096)`
  - 看 `kmem` 锁竞争是否下降
- `test2`
  - 反复统计 free pages
  - 看 allocator 是否丢页
- `test3`
  - 并发 fork + 分配/释放
  - 看 steal 路径与正确性
- `test4`
  - 更高强度压力测试
  - 看性能和调度是否稳定

### 5.2 `bcachetest`

- `test0~test3`
  - 小文件/大文件/并发 create/unlink
  - 看 `bcache` 锁竞争是否下降

### 5.3 `rwlktest`

- 看读写锁在多 CPU 下是否都能完成

## 6. 这次实现里真正踩到的坑

### 6.1 年份切换时对象文件复用

问题：

- 切换 `2024` / `2025` 后，`user/kalloctest.o` 可能还是旧年份编出来的
- 表面上像是“代码没生效”，本质是旧对象复用

修法：

- 在 `gradelib.py` 的 `build_env()` 里先 `make clean`

相关位置：

- `gradelib.py`

### 6.2 2025 `kalloctest test4` 卡死

现象：

- `kalloctest` 打出三个 `child done 100` 后不结束
- 父进程卡在 `wait()`

我用 `Ctrl-P` 抓到的状态是：

- 父进程 `sleep`
- 一个子进程一直 `RUNNABLE`

根因：

- 2025 适配版测试把最后那个“无限分配/释放内存”的压力子进程也 `cpupin(i)` 了
- 在这个 `RV32 + NEMU` 环境里，它被固定到不会被调度到的 hart 上，永远跑不到

修法：

- 只给前 3 个工作子进程做 `cpupin()`
- 最后那个压力子进程不绑核

相关位置：

- `official-lock-tests/rv32/2025/user/kalloctest.c`

## 7. 我最后的结论

一句话概括：

- `2024`：核心是把全局锁拆成局部锁
- `2025`：核心是在局部锁基础上，再引入更适合读多写少场景的锁语义和 CPU 绑定测试

从实现角度看，最关键的是四件事：

- per-CPU `kalloc`
- bucketized `bcache`
- `statistics` 锁统计链路
- `rwspinlock + cpupin + scheduler` 配合

## 8. 当前验证结果

- `python3 ./grade-lab-lock-2024`：`110/110`
- `python3 ./grade-lab-lock-2025`：`100/100`

## 9. 建议的阅读顺序

建议按下面顺序看代码：

1. `kernel/kalloc.c`
2. `kernel/bio.c`
3. `kernel/spinlock.c`
4. `kernel/stats.c`
5. `kernel/proc.c`
6. `kernel/sysproc.c`
7. `official-lock-tests/rv32/2024/user/kalloctest.c`
8. `official-lock-tests/rv32/2025/user/kalloctest.c`

