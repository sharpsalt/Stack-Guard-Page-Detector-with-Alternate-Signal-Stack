/**
 * @file signal_handler.c
 * @brief Handles alternate signal stack initialization and the SIGSEGV handler logic.
 */

#include "guard_page.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

// Define a safe size for our alternate stack (128 KB)
// This provides plenty of space for our signal handler to execute.
#define ALT_STACK_SIZE (128 * 1024)

/**
 * @brief The signal handler for SIGSEGV.
 * 
 * Executed when the thread hits its stack guard page. Must only use async-signal-safe 
 * functions like `write` and `_exit`.
 */
static void sigsegv_handler(int signum, siginfo_t *info, void *context) {
    (void)signum; // Unused
    (void)context; // Unused

    const char *msg1 = "\n=======================================================\n";
    const char *msg2 = "[!] SIGSEGV (Segmentation Fault) CAUGHT!\n";
    const char *msg3 = "[!] Stack Guard Page hit: Stack Overflow Detected.\n";
    const char *msg4 = "[!] Successfully executing on the Alternate Signal Stack.\n";
    
    // Attempt to format and print the faulting memory address
    char address_msg[128];
    int len = snprintf(address_msg, sizeof(address_msg), "[!] Faulting address: %p\n", info->si_addr);

    const char *msg5 = "=======================================================\n";

    // Write all messages to standard output using async-signal-safe write()
    write(STDOUT_FILENO, msg1, strlen(msg1));
    write(STDOUT_FILENO, msg2, strlen(msg2));
    write(STDOUT_FILENO, msg3, strlen(msg3));
    write(STDOUT_FILENO, msg4, strlen(msg4));
    if (len > 0) {
        write(STDOUT_FILENO, address_msg, len);
    }
    write(STDOUT_FILENO, msg5, strlen(msg5));

    // Terminate cleanly.
    _exit(EXIT_FAILURE); 
}

/**
 * @brief Sets up the alternate signal stack and registers the SIGSEGV handler.
 * 
 * IMPORTANT: This affects the calling thread's alternate stack configuration.
 */
void setup_alternate_stack_and_signal(void) {
    stack_t sigstk;
    struct sigaction sa;

    // 1. Allocate memory for the alternate signal stack
    sigstk.ss_sp = malloc(ALT_STACK_SIZE);
    if (sigstk.ss_sp == NULL) {
        perror("Failed to allocate memory for alternate signal stack");
        exit(EXIT_FAILURE);
    }
    sigstk.ss_size = ALT_STACK_SIZE;
    sigstk.ss_flags = 0;

    // 2. Register the alternate signal stack with the kernel for the current thread
    if (sigaltstack(&sigstk, NULL) == -1) {
        perror("sigaltstack failed");
        free(sigstk.ss_sp);
        exit(EXIT_FAILURE);
    }
    printf("[+] Alternate signal stack registered at %p with size %d bytes.\n", sigstk.ss_sp, ALT_STACK_SIZE);

    // 3. Configure the sigaction structure for our signal handler
    // SA_ONSTACK is the CRITICAL flag that routes this signal to the alternate stack.
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sa.sa_sigaction = sigsegv_handler;
    sigemptyset(&sa.sa_mask);

    // 4. Register the SIGSEGV handler globally
    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        perror("sigaction failed");
        exit(EXIT_FAILURE);
    }
    printf("[+] SIGSEGV handler successfully registered with SA_ONSTACK flag.\n");
}
