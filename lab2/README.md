# Lab 2：物理内存管理

本目录以课程提供的 `lab2.zip` 为基础，完成物理页初始化和 Best-Fit 连续页分配。实验报告见 [lab2_report.md](lab2_report.md)。

在安装 RISC-V 交叉编译器、QEMU、`make`、Python 3 和 `bc` 的 Linux/WSL 环境中运行：

```bash
make
make qemu
```

内核打印 `memory management: best_fit_pmm_manager` 和 `check_alloc_page() succeeded!` 后会进入无限循环，可用 `Ctrl+A`、`X` 退出 QEMU。

课程包自带评分脚本，运行 `make grade` 可执行 Page Init 和 Best-Fit 两项测试。它会把源码复制到 `/tmp/lab2_grade`，注入课程包自带的测试代码，在临时副本中构建和运行。本组只把评分脚本的 QEMU 启动参数从 `-device loader` 改为 `-kernel`，以适配当前 OpenSBI；没有改动测试条件或分值。实测结果为 `Total Score: 20/20`。
