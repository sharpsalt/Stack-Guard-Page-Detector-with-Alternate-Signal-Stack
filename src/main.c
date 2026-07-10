/**
 * @file main.c
 * @brief Entry point for the Stack Guard Page Detector program.
 */

#include <stdio.h>
#include "guard_page.h"

int main() {
    printf("=== Stack Guard Page Detector ===\n");
    printf("[+] Initializing system via spawned thread...\n");
    
    // Delegate execution to the thread implementation.
    // We are running the stack overflow on a spawned thread to demonstrate
    // that thread stacks also have guard pages, and that alternate signal 
    // stacks must be initialized on a per-thread basis!
    run_in_thread();

    // Step 3: This code will never be reached because the program exits
    // forcefully from the signal handler.
    printf("[-] ERROR: This message should never be printed.\n");
    
    return 0;
}
