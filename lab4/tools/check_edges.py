#!/usr/bin/env python3
"""Run extra failure-path checks in an isolated temporary source copy."""
from pathlib import Path
import shutil
import subprocess
import tempfile

source = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="lab4-edges-") as temp:
    root = Path(temp) / "src"
    shutil.copytree(source, root, ignore=shutil.ignore_patterns("obj", "bin", ".git"))
    subprocess.run(["python3", str(root / "tools/inject.py"), str(root)], check=True)
    shutil.copyfile(root / "tools/test_lab4_edges.c", root / "kern/test/test_lab4_edges.c")
    header = root / "kern/test/test_lab4.h"
    header.write_text(header.read_text() + "\nvoid test_lab4_edges(void);\nvoid test_lab4_fork_oom(void);\n")
    for relative, anchor, call in [
        ("kern/mm/pmm.c", "test_lab4_pmm_basic();", "test_lab4_edges();"),
        ("kern/process/proc.c", "test_lab4_process_create();", "test_lab4_fork_oom();"),
    ]:
        path = root / relative
        text = path.read_text()
        assert text.count(anchor) == 1
        path.write_text(text.replace(anchor, anchor + "\n    " + call))
    build = subprocess.run(["make", "-s"], cwd=root, capture_output=True, text=True)
    if build.returncode:
        raise RuntimeError(build.stdout + build.stderr)
    # Framework do_exit panics; its monitor immediately requests SBI shutdown.
    result = subprocess.run(["timeout", "8s", "qemu-system-riscv64", "-machine", "virt",
        "-nographic", "-bios", "default", "-kernel", "bin/ucore.img", "-monitor", "none"],
        cwd=root, capture_output=True, text=True)
    print(result.stdout)
    assert result.returncode == 0, result.stderr
    for message in ["[Edge PMM OOM rollback] PASS", "[Edge fork OOM cleanup] PASS",
                    "[Test Schedule] PASS", "process exit!!"]:
        assert message in result.stdout, message
    print("All additional Lab 4 failure-path checks passed.")
