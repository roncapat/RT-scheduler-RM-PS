#ifndef CONTEXT_SWAPPING_H
#define CONTEXT_SWAPPING_H

#include <signal.h>

int block_signal(int sig);
int unblock_signal(int sig);
int setup_signal(int sig, void (*handler)(int, siginfo_t *, void *));

void setup_timer(int sec, long int nsec, int per_sec, long int per_nsec, int sig = SIGALRM);
void craft_context(ucontext_t *uc, void (*function)(void));
int start_timer();
int stop_timer();


#endif /*CONTEXT_SWAPPING_H*/
