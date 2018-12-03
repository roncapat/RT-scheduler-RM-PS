#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#include <sys/time.h>
#include <sys/types.h>

#include <signal.h>
#include <ucontext.h>
#include <pthread.h>

#include "context_swapping.h"

// We create two execution contexes: 
// the loop_context will host the long (or infinite) task to manage
// the manage_context will kick off after a timeout
ucontext_t loop_context, manage_context;

void timeout_handler(int signum, siginfo_t *info, void *sig_context);
void loop();
void manage();

int main(){

  // Configure the timer
  setup_timer(4 , 0, /* seconds */
              0 , 0  /* NO REPETITION */ );


  // Specify how to handle the timeout
  setup_signal(SIGALRM, timeout_handler);
  

  // Create contexes (stack, instruction pointer, signal mask, parent context...)
  craft_context(&loop_context, loop);
  craft_context(&manage_context, manage);

  //Start the timer that will swap from loop to manage context
  start_timer();
  //Start the long function
  setcontext(&loop_context);
  
  return 0;
}

// Prints alphabet. It doesn't know that it will be interrupted from time to time...
void loop(){
  int idx = 0;
  while (true) {
    printf("%c", char(97+idx++));
    fflush(stdout);
    idx %= 26;
    usleep(500000);
  }
}

// Sleeps, than restart the timer and resumes the loop_context
void manage(){
  printf("\nPAUSE\n");
  fflush(stdout);
  sleep(3);
  printf("RESUME\n");
  start_timer();
  setcontext(&loop_context); // We don't swap, so we do not save the status of this context.
                             // There's no reason to do so, next time we will load manage_context
                             // We will simply start from the beginning of the routine
}

// Save loop_context and loads manage_context
void timeout_handler(int signum, siginfo_t *info, /* ucontext_t* */ void *sig_context){
  (void)signum, (void)info, (void)sig_context; //(We discard parameters and suppress -Wunused_variable warning locally) 
  swapcontext(&loop_context, &manage_context);
}
