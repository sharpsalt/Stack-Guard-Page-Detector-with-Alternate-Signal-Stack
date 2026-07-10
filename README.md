# Stack Guard Page Detector with Alternate Signal Stack

This project demonstrates a critical low-level systems programming concept: handling fatal stack overflows gracefully using an **Alternate Signal Stack** in C.

## 🧠 Concepts Explained

To truly understand this project, let's break down the core components involved:

### 1. The Call Stack
Every thread in a program has a memory region assigned to it known as the "call stack" (or simply the stack). It is used to store local variables, function arguments, and return addresses. On most modern architectures (like x86_64), the stack grows *downwards* in memory (from higher addresses to lower addresses). 
By default on Linux, a thread's stack size is typically around 8 MB (you can check yours using `ulimit -s`).

### 2. The Stack Guard Page
What happens if you allocate too much local data or recurse too deeply? The stack pointer moves past the designated stack limit. To prevent the stack from silently corrupting other memory regions (like the heap or memory-mapped segments), the Operating System (OS) maps a special memory page right at the end of the stack, known as the **Guard Page**.
This page has its permissions set to `PROT_NONE` (no read, no write, no execute). If the stack pointer reaches this page and tries to write to it, the Memory Management Unit (MMU) triggers a hardware fault. The kernel catches this and sends a `SIGSEGV` (Segmentation Fault) signal to the process.

### 3. The Problem with Standard Signal Handlers on Stack Overflow
Usually, when your program receives a signal (like `SIGINT` from Ctrl+C or `SIGSEGV` from a bad pointer), the kernel pauses your program and pushes the **signal handler's stack frame** onto your current stack, then jumps to your signal handler function.

**The Fatal Catch-22:** 
When a `SIGSEGV` is triggered *because* of a stack overflow, the stack is completely full! The kernel has no space left on the stack to push the signal handler's frame. Because the OS cannot execute the handler, it forcefully kills the process immediately, dumping core. You never get a chance to log the error or fail gracefully.

### 4. The Solution: Alternate Signal Stack (`sigaltstack`)
POSIX operating systems provide a mechanism to circumvent this issue: the **Alternate Signal Stack**.
You can allocate a separate, distinct chunk of memory (e.g., on the heap using `malloc`) and tell the kernel, *"Hey, if you need to run a signal handler, don't use my normal stack; use this alternate memory region instead."*

This is accomplished using two system calls:
1. `sigaltstack()`: Registers the alternate stack memory region.
2. `sigaction()`: Using the `SA_ONSTACK` flag, instructs the OS to route specific signals (like `SIGSEGV`) to be handled using the alternate stack.

In our project, we use this exact mechanism to safely catch the stack overflow and print a graceful error message before exiting.

---

## 🛠️ Code Architecture

* **`detector.c`**: The primary C source file. Contains:
  * `setup_alternate_stack_and_signal()`: Allocates the alternate stack and configures `sigaltstack` and `sigaction`.
  * `sigsegv_handler()`: The async-signal-safe custom signal handler that executes on the alternate stack.
  * `cause_stack_overflow()`: A recursive function that allocates large local buffers to rapidly deplete the stack.
* **`Makefile`**: Compilation instructions with specific flags (`-O0`) to prevent the compiler from optimizing away our intentional infinite recursion (e.g., tail-call optimization).

---

## 🚀 How to Build and Run

### Prerequisites
- A Linux/UNIX environment
- `gcc` (GNU Compiler Collection)
- `make`

### Build Instructions
Open your terminal and run the `make` command:
```bash
make
```
This will compile the `detector.c` file and generate an executable named `detector`.

### Run Instructions
Execute the compiled binary:
```bash
./detector
```

### Expected Output
When you run the program, you should see output similar to this:

```text
=== Stack Guard Page Detector ===
[+] Initializing system...
[+] Alternate signal stack registered at 0x55bc3a4e92a0 with size 131072 bytes.
[+] SIGSEGV handler successfully registered with SA_ONSTACK flag.

[+] Triggering intentional stack overflow in 2 seconds...
[*] Recursion depth:    200 | Approx stack usage:    800 KB
[*] Recursion depth:    400 | Approx stack usage:   1600 KB
[*] Recursion depth:    600 | Approx stack usage:   2400 KB
... (continues until 8MB stack limit is hit) ...
[*] Recursion depth:   2000 | Approx stack usage:   8000 KB

=======================================================
[!] SIGSEGV (Segmentation Fault) CAUGHT!
[!] Stack Guard Page hit: Stack Overflow Detected.
[!] Successfully executing on the Alternate Signal Stack.
[!] Faulting address: 0x7ffe10204f28
=======================================================
```

---

## ⚠️ Important Considerations for Signal Handlers

1. **Async-Signal-Safety:** Inside a signal handler (especially for asynchronous signals or hardware faults), you are heavily restricted in what C standard library functions you can call. You cannot use functions that use global locks (like `malloc`, `free`, or `printf`). Doing so can cause deadlocks. This is why in `detector.c`, we use the POSIX `write()` system call directly to print our error messages instead of `printf`. We also use `_exit()` instead of `exit()` to terminate without flushing stdio buffers that might be corrupted or locked.
2. **Recovery from Stack Overflow:** While we successfully *detect* the stack overflow, actually recovering from it and continuing execution of the main program is extraordinarily complex and usually unsafe. The program's state is corrupted, and the stack pointer is invalid. The most robust action upon catching a guard page violation is to log the error and terminate.

---
*Created for educational purposes on Advanced Systems Programming.*
