#!/bin/sh
# Lab 1 local self-check, added by the group; not an official course grader.
set -eu

echo "[Lab 1 self-check] This is a local check, not an official course score."

for tool in make riscv64-unknown-elf-gcc riscv64-unknown-elf-readelf qemu-system-riscv64 timeout; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "FAIL: required tool is missing: $tool" >&2
        exit 1
    fi
done

make --no-print-directory
test -s bin/kernel
test -s bin/ucore.img
echo "PASS 1/3: kernel ELF and binary image built"

if ! riscv64-unknown-elf-readelf -h bin/kernel |
    grep -Eq 'Entry point address:[[:space:]]+0x80200000'; then
    echo "FAIL: ELF entry is not 0x80200000" >&2
    exit 1
fi
echo "PASS 2/3: ELF entry is 0x80200000"

set +e
qemu_output=$(timeout 8s qemu-system-riscv64 \
    -machine virt \
    -nographic \
    -bios default \
    -kernel bin/ucore.img 2>&1)
qemu_status=$?
set -e

if [ "$qemu_status" -ne 124 ]; then
    echo "$qemu_output" >&2
    echo "FAIL: QEMU exited unexpectedly (status $qemu_status)" >&2
    exit 1
fi

if ! printf '%s\n' "$qemu_output" |
    grep -Eq 'Domain0 Next Address[[:space:]]*:[[:space:]]*0x0*80200000'; then
    echo "$qemu_output" >&2
    echo "FAIL: OpenSBI did not hand off to 0x80200000" >&2
    exit 1
fi

if ! printf '%s\n' "$qemu_output" |
    grep -Fq '(THU.CST) os is loading ...'; then
    echo "$qemu_output" >&2
    echo "FAIL: kernel startup message was not printed" >&2
    exit 1
fi

echo "PASS 3/3: OpenSBI reached the kernel and the kernel printed its message"
echo "LAB 1 LOCAL SELF-CHECK: 3/3 PASS (not an official course grade)"
