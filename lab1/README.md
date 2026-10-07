# Lab 1：最小可执行内核

本目录包含实验一的 RISC-V 内核源码、构建脚本和 [实验报告](lab1_report.md)。源代码来自课程提供的 `lab1.zip`；为适配当前 QEMU/OpenSBI，`Makefile` 的 `qemu`、`debug` 目标改用 `-kernel` 启动，`gdb` 目标改用 `gdb-multiarch`。

在安装 `make`、`gcc-riscv64-unknown-elf`、`qemu-system-riscv` 和 `gdb-multiarch` 的 Linux 环境中运行：

```bash
make
make qemu
```

看到 `(THU.CST) os is loading ...` 即表示内核已进入 `kern_init` 并通过 SBI 控制台输出。由于内核随后无限循环，请按 `Ctrl+A`、`X` 退出 QEMU。若要调试，在一个终端运行 `make debug`，另一个终端运行 `make gdb`。

课程包未包含 `tools/grade.sh`。本组补充了一个明确标注为“本地自检”的脚本，因此现在可以运行 `make grade`。它依次核对编译产物、ELF 入口地址以及 QEMU/OpenSBI 的实际启动输出，显示 `3/3 PASS` 时只代表这些项目自检通过，不代表助教的官方评分。GDB 调试记录见实验报告第五节。
