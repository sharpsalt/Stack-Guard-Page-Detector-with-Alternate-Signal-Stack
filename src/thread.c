/**
 * @file thread.c
 * @brief Handles thread creation and the intentional stack overflow execution.
 */

#include "guard_page.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

/**
 * @brief Intentionally causes a stack overflow using unbounded recursion.
 */
void cause_stack_overflow(int depth) {
    // Allocate a large buffer (4 KB) on the stack to speed up the overflow
    volatile char buffer[4096];
    
    // Prevent the compiler from optimizing away the buffer allocation
    buffer[0] = 'A';
    buffer[4095] = 'Z';

    // Print progress sporadically to provide visual feedback
    if (depth % 200 == 0) {
        printf("[*] Recursion depth: %6d | Approx stack usage: %6d KB\n", depth, (depth * 4096) / 1024);
    }

    // Recurse deeper!
    cause_stack_overflow(depth + 1);
}

/**
 * @brief Thread entry point.
 */
static void* thread_start_routine(void* arg) {
    (void)arg; // Unused

    printf("[+] Worker thread successfully spawned.\n");
    
    // sigaltstack is per-thread! We must set it up inside the thread
    // that expects to catch the stack overflow.
    printf("[+] Setting up alternate signal stack for this worker thread...\n");
    setup_alternate_stack_and_signal();

    printf("\n[+] Triggering intentional stack overflow in 2 seconds...\n");
    sleep(2);
    
    cause_stack_overflow(1);

    return NULL;
}

/**
 * @brief Spawns the worker thread.
 */
void run_in_thread(void) {
    pthread_t thread;
    pthread_attr_t attr;
    
    pthread_attr_init(&attr);
    
    // Explicitly configure a guard page size for the thread stack.
    // By default pthreads provide a 1-page guard, but it's good practice
    // to explicitly set it to ensure deterministic behavior.
    size_t guard_size = sysconf(_SC_PAGESIZE);
    pthread_attr_setguardsize(&attr, guard_size);

    // Create the worker thread
    if (pthread_create(&thread, &attr, thread_start_routine, NULL) != 0) {
        perror("Failed to create thread");
        exit(EXIT_FAILURE);
    }
    
    pthread_attr_destroy(&attr);
    
    // Wait for the thread (it won't return cleanly due to EXIT_FAILURE in handler)
    pthread_join(thread, NULL);
}
