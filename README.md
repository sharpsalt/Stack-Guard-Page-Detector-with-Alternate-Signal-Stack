# Stack Guard Page Detector & Alternate Signal Stack Implementation

## 1. Executive Summary

This repository provides a comprehensive reference implementation and deep-dive technical guide into handling fatal stack overflows in C/C++ on POSIX-compliant systems. It demonstrates how to utilize an **Alternate Signal Stack** (`sigaltstack`) to safely intercept, log, and recover (or gracefully terminate) from what would otherwise be an uncatchable segmentation fault caused by hitting a stack guard page.

This document is specifically designed for professional systems engineers, backend developers, C/C++ programmers, and individuals preparing for senior-level systems and infrastructure engineering interviews. It explores the low-level intricacies of virtual memory, kernel signal delivery mechanisms, the Memory Management Unit (MMU), and the critical concept of async-signal-safe execution.

---

## 2. Linux Process Memory Layout and The Stack

To understand stack guard pages and stack overflows, we must first examine the virtual memory layout of a standard Linux process. When an Executable and Linkable Format (ELF) binary is loaded into memory, the Linux kernel assigns it an isolated, virtual address space. Address Space Layout Randomization (ASLR) ensures that the exact addresses change on every execution, but the relative layout remains consistent.

From the lowest virtual address to the highest address, the layout is typically structured as follows:
1. **Text Segment (`.text`)**: Contains the executable machine instructions. This segment is mapped as Read-Only and Executable (`PROT_READ | PROT_EXEC`).
2. **Data Segment (`.data`)**: Contains globally initialized variables and static variables. Mapped as Read-Write.
3. **BSS Segment (`.bss`)**: Contains uninitialized global and static variables. The kernel zeroes this memory out upon process startup.
4. **Heap**: Dynamically allocated memory managed via system calls like `brk()` or `sbrk()`, and wrapped by standard library functions like `malloc()`. The heap grows **upwards** towards higher memory addresses.
5. **Memory Mapping Segment (`mmap`)**: An area used for mapping files into memory, loading shared libraries (`.so`), performing large anonymous allocations, and allocating memory for thread stacks.
6. **The Stack**: Used for tracking execution state, storing local variables, function arguments, and return addresses.

On x86_64 architectures, the Stack grows **downwards**—from higher memory addresses toward lower memory addresses. The main thread's stack has a default soft limit, commonly 8 MB on Linux, which can be inspected or modified using `ulimit -s` or programmatically via the `setrlimit(RLIMIT_STACK, ...)` system call.

---

## 3. The Call Stack Mechanics in Detail

As your program executes and functions call other functions, the operating system allocates chunks of memory called "Stack Frames" for each function invocation.

### The Registers Involved
*   **The Stack Pointer (`%rsp` / `%esp`)**: Points to the top of the stack (which is the lowest memory address currently in use by the stack segment). 
*   **The Base Pointer / Frame Pointer (`%rbp` / `%ebp`)**: Points to the start (base) of the current function's stack frame. It provides a stable, unchanging reference point for the duration of the function call to access local variables and passed arguments.

### The Function Prologue and Epilogue
When a function is called, the hardware and calling conventions (like the System V AMD64 ABI) dictate a specific sequence:
1.  **Call setup**: The caller pushes arguments onto the stack (or places them in registers like `%rdi`, `%rsi`, etc.).
2.  **The `call` instruction**: Pushes the Instruction Pointer's Return Address onto the stack and jumps to the function.
3.  **The Prologue**: The callee saves the old Base Pointer (`push %rbp`), sets the new Base Pointer to the current Stack Pointer (`mov %rsp, %rbp`), and then subtracts a specific amount from the Stack Pointer to allocate space for local variables (`sub $0x20, %rsp`).
4.  **The Epilogue**: When the function finishes, it restores the stack pointer, pops the old base pointer, and executes the `ret` instruction to jump back to the caller.

---

## 4. The Guard Page Mechanism

Consider what happens if a program enters infinite recursion, or allocates an excessively large local array (e.g., `double matrix[10000][10000];`).

Without any internal protection, the stack pointer (`%rsp`) would simply continue moving downward, eventually bleeding into the heap or memory-mapped regions. This would cause silent, unpredictable, and catastrophic memory corruption.

### The Memory Management Unit (MMU) and Virtual Memory Areas (VMA)
To prevent this, modern operating systems implement a **Guard Page**.
At the very bottom of the allocated stack space (the lowest virtual address of the stack segment), the OS maps a special memory page (typically 4KB in size, corresponding to the architecture's page size). 

The permissions of this Guard Page are explicitly set to `PROT_NONE` using internal memory protection mechanisms akin to the `mprotect()` system call. This means the page cannot be read from, written to, or executed. 

### The Hardware Page Fault Lifecycle
When the stack grows too large, the stack pointer (`%rsp`) moves into this `PROT_NONE` Guard Page. When the CPU subsequently attempts to push a value onto the stack:
1.  The MMU detects a memory protection violation.
2.  The hardware generates an interrupt (a Page Fault, Exception 14 on x86 architectures).
3.  The CPU pauses user-space execution, transitions into kernel mode, and executes the kernel's page fault handler.
4.  The kernel examines the faulting memory address, traverses the VMA tree for the process, and identifies that the process attempted an invalid memory access.
5.  The kernel translates this hardware fault into a POSIX Signal: `SIGSEGV` (Segmentation Fault, Signal 11), and queues it for delivery to the offending thread.

---

## 5. The Signal Delivery "Catch-22"

Here lies the fundamental problem of catching stack overflows using standard user-space signal handlers.

### How Signals are Delivered from Kernel to User Space
When the kernel delivers a signal to a user-space process, it must temporarily interrupt the process's normal execution flow and redirect the Instruction Pointer to the registered signal handler function.

To allow the process to resume execution normally after the handler finishes, the kernel must save the thread's current execution state (registers, CPU flags, the interrupted instruction pointer). The kernel achieves this by pushing a **Signal Frame** (represented by a `sigcontext` or `ucontext_t` structure) onto the user-space thread's **current stack**.

Once the signal handler finishes, it calls the `sigreturn()` system call, which tells the kernel to pop this Signal Frame and restore the original execution state.

### The Double Fault Scenario
In the event of a stack overflow, this mechanism breaks down entirely:
1.  The stack is completely exhausted; `%rsp` is currently pointing inside the unmapped Guard Page.
2.  The kernel prepares to deliver the `SIGSEGV` signal.
3.  The kernel attempts to push the heavily sized Signal Frame onto the user's stack.
4.  The kernel's write operation hits the `PROT_NONE` Guard Page again.

Because the kernel cannot safely write the Signal Frame, it cannot invoke the user-space signal handler. The kernel recognizes this as an unrecoverable state (a double fault in the signal delivery path), strips the registered signal handler, forcefully terminates the process, and generates a core dump. 

This is exactly why, traditionally, a stack overflow results in a silent, immediate crash, bypassing any `try/catch` blocks or standard signal handlers.

---

## 6. The Alternate Signal Stack (`sigaltstack`)

To circumvent this Catch-22, POSIX-compliant operating systems provide the Alternate Signal Stack mechanism. 

An Alternate Signal Stack is a dedicated, pre-allocated region of memory reserved exclusively for executing signal handlers. By utilizing this feature, we guarantee that the kernel always has valid, writable memory to store the Signal Frame, even if the primary stack is corrupted, exhausted, or invalid.

### Implementation Steps

**1. Allocate the Memory Segment**
First, we allocate a distinct block of memory. This is typically done via `malloc()` or `mmap()`. A standard macro `SIGSTKSZ` is provided by the system, but developers often allocate manually (e.g., 64KB to 128KB) to ensure enough room for complex signal handling logic.
```c
stack_t ss;
ss.ss_size = 128 * 1024; // 128 KB
ss.ss_sp = malloc(ss.ss_size);
if (ss.ss_sp == NULL) { /* handle error */ }
ss.ss_flags = 0;
```

**2. Register the Alternate Stack with the Kernel**
We use the `sigaltstack()` system call to inform the kernel about this new memory region. This updates the internal kernel structures for the calling thread.
```c
if (sigaltstack(&ss, NULL) == -1) { /* handle error */ }
```

**3. Configure the Signal Handler with `SA_ONSTACK`**
We register our `SIGSEGV` handler using the robust `sigaction()` API. Crucially, we must include the `SA_ONSTACK` flag in the `sa_flags` bitmask.
```c
struct sigaction sa;
sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
sa.sa_sigaction = custom_segv_handler;
sigemptyset(&sa.sa_mask);
sigaction(SIGSEGV, &sa, NULL);
```

### The Kernel's Modified Execution Path
When the stack overflow occurs after configuration:
1.  The MMU triggers the page fault on the Guard Page.
2.  The kernel prepares to deliver `SIGSEGV`.
3.  The kernel checks the registered `sigaction` structure and observes the `SA_ONSTACK` flag.
4.  The kernel switches the thread's stack pointer (`%rsp`) to the alternate stack (specifically, to `ss.ss_sp + ss.ss_size`, because stacks grow downward).
5.  The kernel safely writes the Signal Frame to this newly assigned alternate stack.
6.  User-space execution resumes inside the signal handler, functioning perfectly.

---

## 7. Thread-Local Context and POSIX Threads (pthreads)

It is absolutely critical to understand that **stacks are thread-specific entities**.

When an application uses multithreading (e.g., POSIX threads / pthreads) under Linux, it utilizes the `clone()` system call.
- The main thread gets its primary stack from the kernel during the `execve` phase.
- Spawned threads receive their stacks via `mmap()` calls made by the thread library implementation (e.g., glibc). The library maps memory with `MAP_ANONYMOUS | MAP_PRIVATE | MAP_STACK`.
- Every single thread gets its own separate stack region and its own separate Guard Page.

Because the execution context is thread-local, **the Alternate Signal Stack configuration is also strictly thread-local**. 

If you have a multi-threaded application, calling `sigaltstack()` in the main thread will *not* protect the worker threads. If you want to safely catch stack overflows in worker threads, you must allocate memory and call `sigaltstack()` from *within the execution context of each individual thread*. 

*(Note: The signal handler function itself, registered via `sigaction()`, is global to the process. However, the `sigaltstack` mapping updates the `task_struct` specific to the thread making the syscall.)*

---

## 8. Async-Signal-Safety (A Critical Systems Restriction)

Writing code inside a signal handler is notoriously dangerous. Because signals are asynchronous, they can interrupt a thread at any arbitrary machine instruction.

### The Reentrancy and Deadlock Problem
Consider standard C library functions like `malloc()`, `free()`, or `printf()`. These functions manage complex internal state (like memory arenas or I/O buffers) and rely heavily on **global mutex locks** to ensure thread safety.

Imagine the following scenario:
1.  Your main application thread is executing `printf("Processing data...\n");`
2.  Internally, the `printf` implementation acquires the `stdout` global mutex.
3.  A `SIGSEGV` occurs immediately after the lock is acquired, interrupting the thread.
4.  Execution jumps to your registered signal handler.
5.  Your signal handler attempts to print an error: `printf("Stack overflow detected!");`
6.  The inner `printf` attempts to acquire the `stdout` global mutex.
7.  **Deadlock.** The lock is currently held by the interrupted execution frame of the exact same thread. Your program hangs forever.

### POSIX Async-Signal-Safe Functions
To guarantee safety and prevent deadlocks or state corruption, POSIX defines a strict list of functions that are "async-signal-safe." These functions are guaranteed to be reentrant or non-interruptible. 

Inside a signal handler, you **must never** use `printf()`, `malloc()`, or `syslog()`. Instead, you must rely on raw system calls:
- `write(STDOUT_FILENO, msg, len)`
- `_exit(EXIT_FAILURE)` (Crucial: `_exit()` terminates immediately without attempting to flush stdio buffers, unlike standard `exit()`).
- `read()`
- `kill()`
- `abort()`

In this project's implementation, we strictly adhere to these rules. We pre-format any required strings or use safe buffer techniques, and execute the raw `write()` system call to ensure complete safety.

---

## 9. Codebase Architecture Overview

This repository is structured to demonstrate these concepts cleanly and effectively:

- **`include/guard_page.h`**: Exposes the public API for setting up the alternate stack and triggering the overflow.
- **`src/main.c`**: The application entry point. It purposefully delegates execution to a secondary thread to prove the thread-local requirement of `sigaltstack`.
- **`src/thread.c`**: Contains the pthread creation logic and the intentionally flawed recursive function that forces stack exhaustion.
- **`src/signal_handler.c`**: The core of the project. It implements the `setup_alternate_stack_and_signal()` initialization logic and the async-safe `sigsegv_handler`.
- **`Makefile`**: Configures the build system. Crucially, it compiles the code with the `-O0` flag (No Optimization). Without this flag, modern compilers (GCC/Clang) would detect the infinite recursion and apply **Tail-Call Optimization (TCO)**, effectively converting the recursive function into an infinite `while` loop, thereby preventing the stack overflow entirely!

---

## 10. Compilation and Execution Guide

### Build Requirements
- A POSIX-compliant operating system (Tested extensively on Linux; macOS is supported but Linux provides the most accurate standard behavior).
- GCC or Clang compiler.
- GNU Make.

### Build Instructions
Open your terminal and execute:
```bash
make clean
make
```

### Execution Instructions
Run the compiled binary:
```bash
./detector
```

### Expected Output Behavior
The program will initialize the thread, allocate the alternate stack, and begin rapid recursion. You will observe the stack usage growing until it hits the Guard Page. 

```text
=== Stack Guard Page Detector ===
[+] Initializing system via spawned thread...
[+] Spawned thread starting execution.
[+] Alternate signal stack registered successfully.
[+] SIGSEGV handler successfully registered with SA_ONSTACK flag.
... (intentional recursion exhausting the stack) ...
=======================================================
[!] SIGSEGV (Segmentation Fault) CAUGHT!
[!] Stack Guard Page hit: Stack Overflow Detected.
[!] Successfully executing on the Alternate Signal Stack.
[!] Faulting address: 0x7ffe10204f28
=======================================================
```

---

## 11. Systems Engineering Interview Q&A Cheatsheet

If you are preparing for a senior backend, infrastructure, or embedded systems role, study these questions and answers:

**Q1: How exactly does a stack overflow cause a process to crash?**
**A1:** The OS maps a Guard Page with `PROT_NONE` permissions at the end of the stack space. When the stack pointer hits this unmapped page, the MMU triggers a hardware page fault. The kernel intercepts this and translates it to a `SIGSEGV` signal sent to the process.

**Q2: Why can't a normal `SIGSEGV` handler catch a stack overflow?**
**A2:** To execute a signal handler, the kernel must push a signal frame (execution context) onto the thread's stack. Since the stack is full and pointing to the `PROT_NONE` Guard Page, this push causes a double-fault in kernel space. Recognizing an unrecoverable state, the kernel strips the handler and terminates the process immediately.

**Q3: What is the purpose of `sigaltstack`?**
**A3:** It provides the kernel with an alternative, guaranteed-writable memory region to store the signal frame, bypassing the exhausted primary stack and allowing the user-space handler to execute gracefully.

**Q4: Is `sigaltstack` applied globally to the process?**
**A4:** No, it is strictly thread-specific. The `stack_t` configuration alters the kernel's internal `task_struct` for the calling thread only. Each thread must manually allocate and register its own alternate stack if global application protection is desired.

**Q5: What happens if you call `printf` inside a signal handler?**
**A5:** You risk a deadlock. If the signal interrupted a thread that was already holding the internal `stdout` mutex lock within `printf`, calling it again in the handler will cause the thread to wait indefinitely for the lock to be released.

**Q6: How do you safely log an error from a signal handler?**
**A6:** By using only POSIX async-signal-safe functions, primarily the `write()` system call directed to `STDERR_FILENO` or `STDOUT_FILENO`. You cannot use `malloc`, `sprintf`, or any function relying on non-reentrant state.

**Q7: Why must we use `-O0` when compiling the test for this project?**
**A7:** Modern compilers perform Tail-Call Optimization (TCO). If a function's last instruction is a recursive call to itself, the compiler reuses the current stack frame rather than pushing a new one. This turns the recursion into an infinite loop and prevents the stack overflow from occurring.

**Q8: Can you safely recover execution after hitting a guard page?**
**A8:** Technically yes, using `siglongjmp` (if a `sigsetjmp` was prepared earlier before the overflow), but practically, it is highly discouraged. The application state is heavily corrupted, local variables are lost, and continuing execution is unsafe. The primary and recommended use of `sigaltstack` is graceful degradation: securely logging the faulting address, flushing safe states, and exiting cleanly via `_exit()`.

**Q9: How do you differentiate a Stack Overflow from a standard null-pointer dereference inside the signal handler?**
**A9:** By inspecting the `siginfo_t` structure passed to the handler (via `SA_SIGINFO`). The `si_addr` field contains the faulting memory address. If `si_addr` is near the thread's stack pointer (`%rsp` captured in the `ucontext_t` context) or matches the known stack boundaries mapped in `/proc/self/maps`, it is a stack overflow. If `si_addr` is `0x0` or very low memory, it is a null pointer dereference.

---
*Developed for advanced systems programming education, architectural reference, and technical interview preparation.*
