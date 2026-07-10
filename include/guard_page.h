/**
 * @file guard_page.h
 * @brief Header file for the stack guard page detector using alternate signal stacks.
 */

#ifndef GUARD_PAGE_H
#define GUARD_PAGE_H

/**
 * @brief Allocates and registers an alternate signal stack for the current thread,
 * and sets up a SIGSEGV handler to use it.
 * 
 * NOTE: sigaltstack is thread-specific. If you want a specific thread to catch
 * its own stack overflow, this must be called from within that thread.
 */
void setup_alternate_stack_and_signal(void);

/**
 * @brief A highly recursive function designed to exhaust the call stack very quickly.
 * 
 * @param depth The current depth of recursion. Used to print our progress.
 */
void cause_stack_overflow(int depth);

/**
 * @brief Spawns a new POSIX thread (pthread). Inside the thread, it sets up the
 * alternate signal stack and intentionally causes a stack overflow to trigger the
 * guard page detector.
 */
void run_in_thread(void);

#endif // GUARD_PAGE_H
