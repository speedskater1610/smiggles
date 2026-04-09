#include "kernel.h"

// ── Linux i386 ABI compatibility layer ───────────────────────────────────────
// Provides the minimum syscall surface needed to run statically-linked
// 32-bit Linux ELF binaries once the ELF loader is in place.
//
// Register mapping on int 0x80 (matches Linux and our isr_syscall_handler):
//   eax = syscall number
//   ebx = arg0,  ecx = arg1,  edx = arg2
//
// Errors are returned as small negative integers (-errno), successes ≥ 0.
// ─────────────────────────────────────────────────────────────────────────────

// Simple memcpy used internally
static void sc_memcpy(void* dst, const void* src, int n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}
static void sc_memset(void* dst, unsigned char val, int n) {
    unsigned char* d = (unsigned char*)dst;
    for (int i = 0; i < n; i++) d[i] = val;
}

// Shared VGA cursor for Linux-process stdout/stderr output.
// Auto-positioned to the row after the last non-blank line on first use.
static int linux_stdout_cursor = -1;

static void linux_ensure_cursor(void) {
    if (linux_stdout_cursor >= 0) return;
    char* video = (char*)0xB8000;
    linux_stdout_cursor = 0;
    for (int i = 80 * 25 - 1; i >= 0; i--) {
        if (video[i * 2] != ' ' && video[i * 2] != 0) {
            linux_stdout_cursor = ((i / 80) + 1) * 80;
            break;
        }
    }
}

// Write n bytes of buf to the VGA terminal (stdout/stderr).
// linux_ensure_cursor() already positions linux_stdout_cursor at the *start*
// of a fresh line, so we use print_string_sameline (no leading newline advance)
// to avoid skipping an extra blank line on every write.
extern void print_string_sameline(const char* str, int len, char* video, int* cursor, unsigned char color);
static void linux_term_write(const char* buf, int n) {
    char* video = (char*)0xB8000;
    linux_ensure_cursor();
    print_string_sameline(buf, n, video, &linux_stdout_cursor, 0x07);
}

// Simple per-process heap break (one shared region for now).
// Starts just above 4 MiB and grows upward on brk() calls.
#define LINUX_HEAP_BASE  0x00400000u
static unsigned int linux_brk_ptr = LINUX_HEAP_BASE;

unsigned int linux_syscall_dispatch(unsigned int number,
                                    unsigned int arg0,
                                    unsigned int arg1,
                                    unsigned int arg2) {
    switch (number) {

    // ── exit / exit_group ────────────────────────────────────────────────────
    case LINUX_SYS_EXIT:
    case LINUX_SYS_EXIT_GROUP:
        process_exit();
        return 0; // not reached

    // ── write ────────────────────────────────────────────────────────────────
    // fd=1 (stdout) and fd=2 (stderr) → VGA terminal
    // other fds → kernel fs_fd_write
    case LINUX_SYS_WRITE: {
        int   fd  = (int)arg0;
        const char* buf = (const char*)arg1;
        int   len = (int)arg2;
        if (!buf || len <= 0) return 0;
        if (fd == 1 || fd == 2) {
            linux_term_write(buf, len);
            return (unsigned int)len;
        }
        int r = fs_fd_write(fd, buf, len);
        return (r < 0) ? (unsigned int)LINUX_EBADF : (unsigned int)r;
    }

    // ── read ─────────────────────────────────────────────────────────────────
    // fd=0 (stdin) → blocking keyboard read into buf
    // other fds → fs_fd_read
    case LINUX_SYS_READ: {
        int   fd  = (int)arg0;
        char* buf = (char*)arg1;
        int   len = (int)arg2;
        if (!buf || len <= 0) return 0;
        if (fd == 0) {
            // Read one line from keyboard (blocking spin on keyboard buffer)
            int n = 0;
            while (n < len - 1) {
                unsigned char sc;
                while (!keyboard_pop_scancode(&sc)) { /* spin */ }
                if (sc & 0x80) continue;                    // key release
                char c = scancode_to_char(sc, 0);
                if (!c) continue;
                if (c == '\n') { buf[n++] = '\n'; break; }
                if (c == 8 && n > 0) { n--; continue; }    // backspace
                if (c >= 32 && c <= 126) buf[n++] = c;
            }
            buf[n] = 0;
            return (unsigned int)n;
        }
        int r = fs_fd_read(fd, buf, len);
        return (r < 0) ? (unsigned int)LINUX_EBADF : (unsigned int)r;
    }

    // ── open ─────────────────────────────────────────────────────────────────
    case LINUX_SYS_OPEN: {
        const char* path  = (const char*)arg0;
        int         flags = (int)arg1;
        // Map Linux open flags to our FS flags
        int our_flags = 0;
        if ((flags & 3) == 0)         our_flags |= FS_O_READ;
        if ((flags & 3) == 1)         our_flags |= FS_O_WRITE;
        if ((flags & 3) == 2)         our_flags |= FS_O_READ | FS_O_WRITE;
        if (flags & 0x40 /*O_CREAT*/) our_flags |= FS_O_CREATE;
        if (flags & 0x200/*O_TRUNC*/) our_flags |= FS_O_TRUNC;
        if (flags & 0x400/*O_APPEND*/) our_flags |= FS_O_APPEND;
        int fd = fs_fd_open(path, our_flags);
        return (fd < 0) ? (unsigned int)LINUX_ENOENT : (unsigned int)fd;
    }

    // ── close ────────────────────────────────────────────────────────────────
    case LINUX_SYS_CLOSE: {
        int r = fs_fd_close((int)arg0);
        return (r < 0) ? (unsigned int)LINUX_EBADF : 0;
    }

    // ── getpid ───────────────────────────────────────────────────────────────
    case LINUX_SYS_GETPID:
        if (current_process < 0) return 1;
        return (unsigned int)current_process;

    // ── getuid / getgid / geteuid / getegid ─ always "root" for now ─────────
    case LINUX_SYS_GETUID:
    case LINUX_SYS_GETGID:
    case LINUX_SYS_GETEUID:
    case LINUX_SYS_GETEGID:
        return 0;

    // ── brk ──────────────────────────────────────────────────────────────────
    // Return current break if arg0==0; otherwise try to set new break.
    // We don't actually map memory here yet — that needs the ELF loader and
    // per-process address spaces.  Return the same pointer so static binaries
    // that only call brk() to query the break still work.
    case LINUX_SYS_BRK:
        if (arg0 == 0 || arg0 < linux_brk_ptr)
            return linux_brk_ptr;
        linux_brk_ptr = arg0;
        return linux_brk_ptr;

    // ── uname ────────────────────────────────────────────────────────────────
    case LINUX_SYS_UNAME: {
        LinuxUtsname* u = (LinuxUtsname*)arg0;
        if (!u) return (unsigned int)LINUX_EFAULT;
        sc_memset(u, 0, sizeof(LinuxUtsname));
        sc_memcpy(u->sysname,  "Smiggles",  9);
        sc_memcpy(u->nodename, "smiggles",  9);
        sc_memcpy(u->release,  "1.0.0",     6);
        sc_memcpy(u->version,  "#1",        3);
        sc_memcpy(u->machine,  "i686",      5);
        return 0;
    }

    // ── mmap / munmap ─ stub (no per-process address spaces yet) ────────────
    case LINUX_SYS_MMAP:
    case LINUX_SYS_MUNMAP:
        return (unsigned int)LINUX_ENOMEM;

    // ── ioctl ─ stub ─────────────────────────────────────────────────────────
    case LINUX_SYS_IOCTL:
        return (unsigned int)LINUX_EINVAL;

    default:
        return (unsigned int)LINUX_ENOSYS;
    }
}

// ── Our own kernel-internal syscall table ────────────────────────────────────
// saved_cs is the CS selector that was active when int 0x80 fired.
// If its RPL bits (bits 1:0) are 3, the caller is a ring-3 (user-mode)
// process and we dispatch through the Linux ABI compatibility layer.
// Ring-0 kernel callers go through our own compact syscall table.
unsigned int syscall_dispatch(unsigned int number,
                              unsigned int arg0,
                              unsigned int arg1,
                              unsigned int arg2,
                              unsigned int saved_cs) {
    // Ring-3 caller → Linux ABI
    if ((saved_cs & 3) == 3)
        return linux_syscall_dispatch(number, arg0, arg1, arg2);

    switch (number) {
        case SYS_YIELD:
            process_yield();
            return 0;
        case SYS_GET_TICKS:
            return (unsigned int)ticks;
        case SYS_GET_PID:
            if (current_process < 0) return 0xFFFFFFFFu;
            return (unsigned int)current_process;
        case SYS_WAIT_TICKS: {
            unsigned int start = (unsigned int)ticks;
            while (((unsigned int)ticks - start) < arg0) {
                process_yield();
            }
            return (unsigned int)ticks;
        }
        case SYS_SPAWN_DEMO:
            return (unsigned int)process_spawn_demo_with_work(arg0);
        case SYS_KILL_PID:
            return (unsigned int)process_kill((int)arg0);
        case SYS_GET_CPL:
            return protection_get_cpl();
        case SYS_OPEN:
            return (unsigned int)fs_fd_open((const char*)arg0, (int)arg1);
        case SYS_CLOSE:
            return (unsigned int)fs_fd_close((int)arg0);
        case SYS_READ:
            return (unsigned int)fs_fd_read((int)arg0, (char*)arg1, (int)arg2);
        case SYS_WRITE:
            return (unsigned int)fs_fd_write((int)arg0, (const char*)arg1, (int)arg2);
        default:
            return 0xFFFFFFFFu;
    }
}

unsigned int syscall_invoke(unsigned int number) {
    return syscall_invoke1(number, 0);
}

unsigned int syscall_invoke1(unsigned int number, unsigned int arg0) {
    return syscall_invoke2(number, arg0, 0);
}

unsigned int syscall_invoke2(unsigned int number, unsigned int arg0, unsigned int arg1) {
    return syscall_invoke3(number, arg0, arg1, 0);
}

unsigned int syscall_invoke3(unsigned int number, unsigned int arg0, unsigned int arg1, unsigned int arg2) {
    unsigned int ret;
    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(number), "b"(arg0), "c"(arg1), "d"(arg2)
        : "memory"
    );
    return ret;
}
