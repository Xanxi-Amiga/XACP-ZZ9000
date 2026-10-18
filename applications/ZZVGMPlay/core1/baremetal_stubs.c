/*
 * Minimal Newlib syscall stubs for the ZZ9000 Core1 bare-metal payload.
 *
 * The payload has no process/OS environment.  These symbols are only here
 * so that libc/libgcc helper code (notably setjmp/longjmp used by puff)
 * cannot drag in unresolved host syscalls at link time.
 */

void _exit(int status)
{
    (void)status;
    for (;;) { }
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    return -1;
}

int _getpid(void)
{
    return 1;
}
