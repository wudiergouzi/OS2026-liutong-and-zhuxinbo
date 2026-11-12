#include <assert.h>
#include <error.h>
#include <pmm.h>
#include <proc.h>
#include <sched.h>
#include <stdio.h>
#include <riscv.h>
#include <intr.h>

static volatile int orphan_pid;

static int return_code(void *arg) { return (int)(intptr_t)arg; }
static int explicit_exit(void *arg) { do_exit(99); }

static int check_sleeping_parent(void *arg)
{
    assert(current->parent->state == PROC_SLEEPING);
    assert(current->parent->waiting_child);
    assert(do_wait(current->parent->pid, NULL) == -E_BAD_PROC);
    return -7;
}

static int orphan_parent(void *arg)
{
    orphan_pid = kernel_thread(return_code, (void *)33, 0);
    assert(orphan_pid > 0);
    if (arg != NULL) {
        while (find_proc(orphan_pid)->state != PROC_ZOMBIE) schedule();
    }
    return 19;
}

static int idle_adopted_child(void *arg)
{
    assert(current->parent == idleproc);
    cprintf("[Lifecycle idle adoption] PASS\n");
    return 0;
}

void test_lab4_lifecycle(void)
{
    assert(current == initproc);
    // The finite variants of course scheduling workers are children of init.
    while (do_wait(0, NULL) == 0) {}
    size_t before = nr_free_pages();
    int code, pid;
    assert(do_wait(0, NULL) == -E_BAD_PROC);
    assert(do_wait(-1, NULL) == -E_INVAL);
    assert(do_wait(MAX_PID, NULL) == -E_INVAL);

    pid = kernel_thread(return_code, (void *)37, 0);
    assert(pid > 0);
    while (find_proc(pid)->state != PROC_ZOMBIE) schedule();
    assert(do_wait(pid, &code) == 0 && code == 37);
    assert(find_proc(pid) == NULL && do_wait(pid, NULL) == -E_BAD_PROC);
    assert(nr_free_pages() == before);
    cprintf("[Lifecycle zombie reap/status/double wait] PASS\n");

    pid = kernel_thread(check_sleeping_parent, NULL, 0);
    assert(pid > 0 && do_wait(pid, &code) == 0 && code == -7);
    assert(current->state == PROC_RUNNABLE && !current->waiting_child);
    assert((read_csr(sstatus) & SSTATUS_SIE) != 0);
    cprintf("[Lifecycle blocking wait/wakeup/ownership] PASS\n");

    intr_disable();
    pid = kernel_thread(explicit_exit, NULL, 0);
    assert(pid > 0 && do_wait(pid, &code) == 0 && code == 99);
    assert((read_csr(sstatus) & SSTATUS_SIE) == 0);
    intr_enable();
    cprintf("[Lifecycle explicit exit/interrupt state preserved] PASS\n");

    // An unrelated child's exit must not lose a targeted wait notification.
    int first = kernel_thread(return_code, (void *)11, 0);
    int second = kernel_thread(return_code, (void *)22, 0);
    assert(first > 0 && second > 0);
    assert(do_wait(first, &code) == 0 && code == 11);
    assert(do_wait(0, &code) == 0 && code == 22);
    assert(find_proc(first) == NULL && find_proc(second) == NULL);
    cprintf("[Lifecycle targeted and any-child wait] PASS\n");

    for (int zombie_first = 0; zombie_first < 2; zombie_first++) {
        orphan_pid = 0;
        pid = kernel_thread(orphan_parent, (void *)(intptr_t)zombie_first, 0);
        assert(pid > 0 && do_wait(pid, &code) == 0 && code == 19);
        int adopted = orphan_pid;
        assert(adopted > 0 && find_proc(adopted)->parent == current);
        assert(do_wait(adopted, &code) == 0 && code == 33);
        assert(find_proc(adopted) == NULL);
    }
    cprintf("[Lifecycle live and zombie orphan adoption] PASS\n");

    // Repeated create/wait catches unreleased stacks, stale hashes and counts.
    for (int i = 0; i < MAX_PID + 8; i++) {
        pid = kernel_thread(return_code, (void *)(intptr_t)i, 0);
        assert(pid > 0 && do_wait(pid, &code) == 0 && code == i);
        assert(find_proc(pid) == NULL);
    }
    assert(nr_free_pages() == before);
    cprintf("[Lifecycle repeated create/exit/reap, free pages restored] PASS\n");
    cprintf("[Lifecycle PID wraparound] PASS\n");

    // Leave a live child when init returns: idle must adopt and reap it.
    assert(kernel_thread(idle_adopted_child, NULL, 0) > 0);
}
