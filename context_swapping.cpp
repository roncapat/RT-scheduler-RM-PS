/* CCFLAGS: -lrt -Wall -Wextra
 * */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include <math.h>
#include <sys/types.h>
#include <signal.h>
#include <ucontext.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "context_swapping.h"

timer_t timerid;
struct sigevent sev;
struct itimerspec its;

int block_signal(int sig){
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, sig);
  return pthread_sigmask(SIG_BLOCK, &set, NULL);
}

int unblock_signal(int sig){
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, sig);
  return pthread_sigmask(SIG_UNBLOCK, &set, NULL);
}

int setup_signal(int sig, void (*handler)(int, siginfo_t *, void *)){
    unblock_signal(sig);
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO | SA_RESTART;
    act.sa_sigaction = handler;

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, sig);
    return sigaction(sig, &act, NULL);
}

void setup_timer(int sec, long int nsec, int per_sec, long int per_nsec, int sig){
  sev.sigev_notify = SIGEV_SIGNAL;
  sev.sigev_signo = sig;
  sev.sigev_value.sival_ptr = &timerid;
  timer_create(CLOCK_REALTIME, &sev, &timerid);
  
  its.it_value.tv_sec = sec;
  its.it_value.tv_nsec = nsec;
  its.it_interval.tv_sec = per_sec;
  its.it_interval.tv_nsec = per_nsec;
}

void craft_context(ucontext_t *uc, void (*function)(void)){
  getcontext(uc);
  uc->uc_stack.ss_sp = (char*) malloc(SIGSTKSZ);
  uc->uc_stack.ss_size = SIGSTKSZ;
  uc->uc_stack.ss_flags = 0;
  sigemptyset(&(uc->uc_sigmask));
  makecontext(uc, function, 0);
}

int start_timer(){
  int res = timer_settime(timerid, 0, &its, NULL);
  if (res != 0){
    printf("Failed to start timer. Aborting.\n");
    abort();
  }
  return res;
}

int stop_timer(){
  struct itimerspec zero;
  zero.it_value.tv_sec = 0;
  zero.it_value.tv_nsec = 0;
  zero.it_interval.tv_sec = 0;
  zero.it_interval.tv_nsec = 0;
  int res = timer_settime(timerid, 0, &zero, NULL);
    if (res != 0){
    printf("Failed to stop timer. Aborting.\n");
    switch (errno){
        case EFAULT: printf("EFAULT\n"); break;
        case EINVAL: printf("EINVAL\n"); break;
    }
    printf("%s\n", strerror(errno));
    abort();
  }
  return res;
}
