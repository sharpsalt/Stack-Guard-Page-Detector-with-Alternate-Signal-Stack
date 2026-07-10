````markdown
# Stack Guard Page Detector with Alternate Signal Stack

A Linux systems programming project demonstrating stack overflow detection using a manually configured **guard page** and reliable signal handling through an **alternate signal stack**.

The project allocates a custom thread stack with `mmap(2)`, protects the lowest memory page with `mprotect(PROT_NONE)`, and executes a recursive workload until the stack overflows into the guard page. The resulting `SIGSEGV` is handled on an alternate signal stack configured via `sigaltstack(2)`, allowing the process to safely identify and report the overflow even after the primary stack has been exhausted.

---

## Overview

Modern operating systems place an inaccessible memory page adjacent to each thread stack to detect stack overflows before they corrupt neighboring memory. This project reproduces that mechanism manually, illustrating how Linux combines virtual memory protection and signal delivery to detect and terminate stack overflows safely.

Unlike a conventional segmentation fault handler, this implementation executes on an alternate signal stack, ensuring that the signal handler remains functional even when the thread's primary stack is no longer usable.

The project serves as a practical demonstration of Linux virtual memory management, POSIX thread stack configuration, and low-level signal handling.

---

## Features

- Manual thread stack allocation using `mmap()`
- Guard page creation using `mprotect(PROT_NONE)`
- Custom thread stack configuration via `pthread_attr_setstack()`
- Alternate signal stack using `sigaltstack()`
- `SIGSEGV` handling with `sigaction()`
- Differentiation between stack overflow and unrelated segmentation faults
- Async-signal-safe error reporting
- Minimal implementation with no external dependencies

---

## Project Structure

```text
stack_guard_detector/
├── Makefile
├── README.md
└── src/
    └── main.c
```

---

# Architecture

```
                     +----------------------+
                     |      main()          |
                     +----------+-----------+
                                |
                                v
                Configure Alternate Signal Stack
                                |
                                v
                  Register SIGSEGV Signal Handler
                                |
                                v
                 Allocate Custom Thread Stack
                     using mmap()
                                |
                                v
               Protect First Page (Guard Page)
                    using mprotect()
                                |
                                v
                  Create Worker Thread
                                |
                                v
                  Recursive Stack Growth
                                |
                                v
                  Stack Reaches Guard Page
                                |
                                v
                         Page Fault
                                |
                                v
                           SIGSEGV
                                |
                                v
              Handler Executes on Alternate Stack
                                |
                                v
                     Report Stack Overflow
```

---

# Memory Layout

The thread stack is manually allocated instead of relying on the default pthread stack.

```
Low Address
────────────────────────────────────────────

+--------------------------------------+
| Guard Page                           |
| PROT_NONE                            |
+--------------------------------------+

+--------------------------------------+
|                                      |
|                                      |
|        Usable Thread Stack           |
|                                      |
|                                      |
+--------------------------------------+

High Address
```

Since the stack grows toward lower addresses, recursive function calls eventually attempt to access the protected page, causing the processor to generate a page fault.

---

# Signal Handling Flow

```
Recursive Function

        │
        ▼

Thread Stack Exhausted

        │
        ▼

Access Guard Page

        │
        ▼

CPU Page Fault

        │
        ▼

Kernel Delivers SIGSEGV

        │
        ▼

Signal Handler Executes
(on Alternate Stack)

        │
        ▼

Stack Overflow Reported

        │
        ▼

Process Terminated
```

---

# Implementation Details

## Custom Thread Stack

Instead of using the default stack provided by `pthread_create()`, the application explicitly allocates a contiguous virtual memory region with `mmap()`.

This approach provides complete control over the stack layout and allows a dedicated guard page to be inserted manually.

---

## Guard Page

The first memory page is marked inaccessible using

```c
mprotect(..., PROT_NONE);
```

Any attempt to read from or write to this page immediately generates a page fault, preventing the overflow from silently corrupting adjacent memory.

---

## Alternate Signal Stack

A stack overflow leaves the thread's primary stack in an unusable state.

Executing a signal handler on the same corrupted stack is unreliable and frequently results in immediate process termination before diagnostic information can be collected.

To avoid this, an alternate stack is registered using

```c
sigaltstack()
```

and the signal handler is installed with

```c
SA_ONSTACK
```

ensuring that signal delivery occurs on a clean execution stack.

---

## Signal Handler

The signal handler receives extended fault information through

```c
siginfo_t
```

Specifically,

```c
info->si_addr
```

contains the virtual address responsible for the fault.

The handler compares this address against the configured guard page.

If the address lies inside the protected region, the fault is classified as a stack overflow.

Otherwise, it is treated as a conventional segmentation fault.

Only async-signal-safe functions are used inside the handler.

---

## Stack Exhaustion

The worker thread intentionally performs unbounded recursion.

Each recursive call allocates a local buffer, steadily consuming stack space until the guard page is reached.

This deterministic workload provides a reproducible stack overflow without invoking undefined behavior through arbitrary memory writes.

---

# Building

## Requirements

- Linux
- GCC
- POSIX Threads
- GNU Make

Compile the project using

```bash
make
```

Remove generated files

```bash
make clean
```

---

# Running

```bash
./stack_guard_detector
```

---

# Example Output

```text
[FATAL] Stack Overflow Detected: Guard page violation at address.
```

---

# Design Decisions

### Why `mmap()` instead of `malloc()`?

`mprotect()` operates on page-aligned virtual memory.

`mmap()` guarantees page alignment and provides direct control over page permissions, making it the appropriate allocation mechanism for implementing guard pages.

---

### Why use `pthread_attr_setstack()`?

The default thread stack is managed internally by the operating system.

A manually allocated stack is required to insert a custom guard page into the memory layout.

---

### Why use an Alternate Signal Stack?

After stack exhaustion, the original thread stack can no longer safely execute additional function calls.

Using `sigaltstack()` guarantees that the signal handler executes on independent memory that remains valid even after the overflow.

---

### Why use `write()` instead of `printf()`?

Signal handlers are restricted to async-signal-safe functions.

`printf()` may internally acquire locks or allocate memory, making it unsafe during signal delivery.

`write()` is specified by POSIX as async-signal-safe and is therefore suitable for reporting fatal errors.

---

### Why call `_exit()`?

`exit()` performs runtime cleanup, invokes registered handlers, and flushes buffered streams.

Following a stack overflow, the process state may already be inconsistent.

`_exit()` immediately terminates the process without executing additional runtime logic.

---

# Limitations

- Linux-specific implementation
- Demonstration project rather than a production crash-reporting library
- Detects only overflows into the configured guard page
- Does not recover execution after stack exhaustion
- No stack trace generation
- Single worker thread demonstration

---

# Future Improvements

- Support multiple guarded thread stacks
- Configurable guard page size
- Stack trace collection using `libunwind`
- Structured crash reporting
- Integration with sanitizers
- Optional CMake build system
- Automated unit and integration tests

---

# Key Linux APIs

| API | Purpose |
|------|----------|
| `mmap()` | Allocate page-aligned virtual memory |
| `mprotect()` | Configure page protection |
| `sigaction()` | Register signal handler |
| `sigaltstack()` | Install alternate signal stack |
| `pthread_attr_setstack()` | Configure custom thread stack |
| `pthread_create()` | Create worker thread |
| `write()` | Async-signal-safe output |
| `_exit()` | Immediate process termination |

---

# References

- Michael Kerrisk, *The Linux Programming Interface*
- W. Richard Stevens, *Advanced Programming in the UNIX Environment*
- POSIX.1-2017 Specification
- Linux Manual Pages:
  - `mmap(2)`
  - `mprotect(2)`
  - `sigaction(2)`
  - `sigaltstack(2)`
  - `pthread_create(3)`
  - `signal-safety(7)`

---

# License

This project is released for educational purposes and is intended to demonstrate low-level Linux memory management, custom thread stack configuration, and robust signal handling techniques.
````

This style is much closer to what you'd see in a serious systems programming repository: concise, design-focused, and emphasizing engineering decisions rather than teaching the concepts step by step. It would fit well in a GitHub portfolio for backend, systems, or infrastructure engineering roles.
