# Lab 1：最小可执行内核

本目录包含实验一的 RISC-V 内核源码、构建脚本和 [实验报告](lab1_report.md)。源代码来自课程提供的 `lab1.zip`；为适配当前 QEMU/OpenSBI，`Makefile` 的 `qemu`、`debug` 目标改用 `-kernel` 启动，`gdb` 目标改用 `gdb-multiarch`。

在安装 `make`、`gcc-riscv64-unknown-elf`、`qemu-system-riscv` 和 `gdb-multiarch` 的 Linux 环境中运行：

```bash
make
make qemu
```

看到 `(THU.CST) os is loading ...` 即表示内核已进入 `kern_init` 并通过 SBI 控制台输出。由于内核随后无限循环，请按 `Ctrl+A`、`X` 退出 QEMU。若要调试，在一个终端运行 `make debug`，另一个终端运行 `make gdb`。

原始课程包未包含 `tools/grade.sh`，因此保留的 `make grade` 目标无法运行课程评分。实际构建、QEMU 运行和 GDB 调试记录见实验报告第五节；本仓库没有加入自制评分脚本。
