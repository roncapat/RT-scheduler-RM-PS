#define _GNU_SOURCE
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
#include <sys/types.h>
#include <sys/prctl.h>
#include <signal.h>
#include <errno.h>
#include "context_swapping.h"

#define MILLION 1000000
#define BILLION 1000000000

bool go = true;

clockid_t clock_id = CLOCK_MONOTONIC;
int sig_timeout = SIGRTMIN+1;

ucontext_t aperiodic_loader_context, polling_server_context;

void stop_handler(int signum, siginfo_t *info, void *sig_context);
void timeout_handler(int signum, siginfo_t *info, void *sig_context);
void input_handler(int signum, siginfo_t *info, void *sig_context);
void aperiodic_loader();
void polling_server();

int sub(struct timespec time1, struct timespec time2, struct timespec *result);
void increment(struct timespec *time, long int nano);
void print_timespec(struct timespec t);

bool aperiodic_request = false;

void long_task_code( ); //RUN APERIODICAL
void short_task_code_1( ); //RUN PERIODICAL
void short_task_code_2( ); //RUN PERIODICAL

void *polling_server_runner( void *);
void *periodic_task_runner( void *);

struct payload {
  int idx=0;
  void (*inner_task)();
};

const int NPERIODICTASKS = 2;
const int NTASKS = NPERIODICTASKS + 1;

#define POLL_SRV_EXEC_TIME_NS 20000000 //20 millis
long int periods[NTASKS];
struct timespec next_arrival_time[NTASKS];
long int WCET[NPERIODICTASKS];
pthread_attr_t attributes[NTASKS];
pthread_t thread_id[NTASKS];
struct sched_param parameters[NTASKS];
int missed_deadlines[NTASKS];

pthread_barrier_t bar;

struct payload payloads[NPERIODICTASKS];
void (*inner_tasks[NPERIODICTASKS])() = {short_task_code_1, short_task_code_2};
cpu_set_t cset;


int main(){
  if (getuid() != 0){
    fprintf(stderr, "You need to be root in order to run tasks with RT policy. Aborting...");
    abort();
  }
  
  int ret;

  ret = pthread_barrier_init(&bar, NULL, NTASKS);
  if (ret != 0) abort();
  ret = prctl(PR_SET_TIMERSLACK, 1);
  if (ret != 0) abort();

  // set task periods in nanoseconds
  periods[0]= 100000000; //in nanosecondi // 100 millis
  periods[1]= 200000000; //in nanosecondi // 200 millis
  periods[2]= 300000000; //in nanosecondi // 300 millis

  struct sched_param priomax, priomin;
  priomax.sched_priority=sched_get_priority_max(SCHED_FIFO);
  priomin.sched_priority=sched_get_priority_min(SCHED_FIFO);

  ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &priomax);
  if (ret != 0) abort();

  struct timespec ora, res;
  clock_getres(clock_id, &res);
  clock_gettime(clock_id, &ora);
  printf("Clock resolution: %ld ns\n", res.tv_nsec);
  
  
#define N_TEST 20

  for (int i = 0; i < NPERIODICTASKS; i++){
    long total = 0, before = 0;
    for (int j = 0; j < N_TEST; j++){
        static struct timespec timespec_1, timespec_2, measure;

        ret = clock_gettime(clock_id, &timespec_1);
        if (ret != 0) abort();
        
        inner_tasks[i]();
        
        ret = clock_gettime(clock_id, &timespec_2);
        if (ret != 0) abort();

        sub(timespec_2, timespec_1, &measure);
        printf("M "); print_timespec(measure);fflush(stdout);

        before = total;
        total += measure.tv_sec * BILLION + measure.tv_nsec + 1.5;
        //1.5: pthread context switch cost on my laptop
        if (total < before) abort(); //Overflow
    }
    WCET[i] = total/N_TEST;
    printf("\nWorst Case Execution Time %d = %ld ns \n", i+1, WCET[i]);
  }

  double U = double(POLL_SRV_EXEC_TIME_NS)/periods[0] 
           + double(WCET[0])/periods[1]
           + double(WCET[1])/periods[2];

  double Ulub = NTASKS*(pow(2.0,(1.0/NTASKS)) -1);
  
  if (U > Ulub){
    fprintf(stderr, "\n U=%lf Ulub=%lf Non schedulable Task Set\n", U, Ulub);
    return(-1);
  }
  printf("\n U=%lf Ulub=%lf Scheduable Task Set\n", U, Ulub);
  fflush(stdout);
  sleep(1);

  ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &priomin);
  if (ret != 0) abort();
  
  // set the attributes of each task, including scheduling policy and priority
  for (int i =0; i < NTASKS; i++){
    ret = pthread_attr_init(&(attributes[i]));    
    if (ret != 0) abort();
    ret = pthread_attr_setinheritsched(&(attributes[i]), PTHREAD_EXPLICIT_SCHED);
    if (ret != 0) abort();
    ret = pthread_attr_setschedpolicy(&(attributes[i]), SCHED_FIFO);
    if (ret != 0) abort();
    parameters[i].sched_priority = priomax.sched_priority - i;
    ret = pthread_attr_setschedparam(&(attributes[i]), &(parameters[i]));
    if (ret != 0) abort();
    
    missed_deadlines[i] = 0;
  }
  
  CPU_ZERO( &cset);
  CPU_SET( 0, &cset);
  
  ret = block_signal(SIGALRM);
  if (ret != 0) abort();
  ret = block_signal(SIGUSR1);
  if (ret != 0) abort();
  ret = block_signal(sig_timeout);
  if (ret != 0) abort();
  ret = setup_signal(SIGTERM, stop_handler);
  if (ret != 0) abort();
  
  ret = pthread_create(&(thread_id[0]), &(attributes[0]), polling_server_runner, NULL);
  if (ret != 0) abort();
  
  printf("\nSignal SIGUSR1 to PID %d to trigger aperiodic task\n", getpid());
  printf("Signal SIGTERM to PID %d to trigger scheduler exit (with stats)\n", getpid());
  printf("Press ENTER to start the scheduler\n\n");
  getchar();
  
  for (int i = 1; i <= NPERIODICTASKS; i++){
    payloads[i-1].idx = i;
    payloads[i-1].inner_task = inner_tasks[i-1];
    ret = pthread_create( &(thread_id[i]), &(attributes[i]), periodic_task_runner, (void*)(payloads+i-1));
    if (ret != 0) abort();
  }
  
  printf("Thread creation completed\n");
  
  // join all threads
  for (int i = 0; i < NTASKS; i++){
    pthread_join(thread_id[i], NULL);
    printf("Joined thread #%d\n", i+1);
  }
  
  for (int i = 0; i < NTASKS; i++){
    printf("Thread %d missed deadline %d times.\n", i+1, missed_deadlines[i]);
  }
  fflush(stdout);
  exit(0);
}

void *polling_server_runner( void *ptr){
  (void) ptr;
  int ret;
  
  ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cset);
  if (ret != 0) abort();
  
  ret = block_signal(SIGTERM);
  if (ret != 0) abort();

  ret = setup_signal(sig_timeout, timeout_handler);
  if (ret != 0) abort();
  ret = setup_signal(SIGUSR1, input_handler);
  if (ret != 0) abort();
  
  setup_timer(0, POLL_SRV_EXEC_TIME_NS, 0, 0, sig_timeout);
  
  craft_context(&polling_server_context, polling_server);
  craft_context(&aperiodic_loader_context, aperiodic_loader);

  printf("Polling server ready\n");

  fflush(stdout); 
  pthread_barrier_wait(&bar);
  printf("Polling server started\n");
  fflush(stdout);
  
  struct timespec ora;
  clock_gettime(clock_id, &ora);
  increment(&ora, periods[0]);
  next_arrival_time[0] = ora;
  
  setcontext(&polling_server_context);
  return 0;
}

void polling_server(){
  static struct timespec ora, waittime, start, end, elapsedtime;
  clock_gettime(clock_id, &end);
  sub(end, start, &elapsedtime);
  
  clock_gettime(clock_id, &ora);
  if (sub(next_arrival_time[0], ora, &waittime) == -1){
    ++missed_deadlines[0];
    //printf("\nE  "); print_timespec(elapsedtime);fflush(stdout);
  } else {
    //printf("E  "); print_timespec(elapsedtime);fflush(stdout);
    //printf("W "); print_timespec(waittime);fflush(stdout);
    while (clock_nanosleep(clock_id, TIMER_ABSTIME, &next_arrival_time[0], NULL) == EINTR);
  }  
  increment(&next_arrival_time[0], periods[0]);

  start_timer();
  clock_gettime(clock_id, &start);
  setcontext(&aperiodic_loader_context);
}

void aperiodic_loader(){
  while (go){
    if (aperiodic_request){
      long_task_code();
      aperiodic_request = false;
    }
    stop_timer();
    swapcontext(&aperiodic_loader_context, &polling_server_context);
  }
  printf("\nPolling server exiting...\n");
  fflush(stdout);
  pthread_exit(0);
}

void timeout_handler(int signum, siginfo_t *info, /* ucontext_t* */ void *sig_context){
  (void) signum, (void) info, (void) sig_context;
  swapcontext(&aperiodic_loader_context, &polling_server_context);
}

void input_handler(int signum, siginfo_t *info, /* ucontext_t* */ void *sig_context){
  (void) signum, (void) info, (void) sig_context;
  aperiodic_request = true;
}

void stop_handler(int signum, siginfo_t *info, /* ucontext_t* */ void *sig_context){
  (void) signum, (void) info, (void) sig_context;
  go = false;
}

void *periodic_task_runner( void *ptr){
  int idx = ((payload*)ptr)->idx;
  void (*inner_task)() = ((payload*)ptr)->inner_task;
  
  pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cset);
  
  block_signal(SIGALRM);
  block_signal(SIGTERM);
  block_signal(SIGUSR1);
  block_signal(sig_timeout);
  
  printf("Periodic task %d ready\n", idx);
  fflush(stdout);
  pthread_barrier_wait(&bar);
  printf("Periodic task %d started\n", idx);
  fflush(stdout);
  
  struct timespec ora, waittime, start, end, elapsedtime;
  clock_gettime(clock_id, &ora);
  increment(&ora, periods[idx]);
  next_arrival_time[idx] = ora;
  
  while (go){
    clock_gettime(clock_id, &start);
    inner_task();
    clock_gettime(clock_id, &end);
    sub(end, start, &elapsedtime);
    printf("\nS%d ", idx); print_timespec(start);fflush(stdout);
    printf("E%d ", idx); print_timespec(end);fflush(stdout);
    printf("T%d ", idx); print_timespec(elapsedtime);fflush(stdout);
    
    if (sub(next_arrival_time[idx], ora, &waittime) == -1){
      ++missed_deadlines[idx];
      //printf("E%d ", idx); print_timespec(elapsedtime);fflush(stdout);
    } else {
      //printf("E%d ", idx); print_timespec(elapsedtime);fflush(stdout);
      //printf("W%d ", idx); print_timespec(waittime); printf("\n");fflush(stdout);
      while (clock_nanosleep(clock_id, TIMER_ABSTIME, &next_arrival_time[idx], NULL) == EINTR);
    }
    increment(&next_arrival_time[idx], periods[idx]);
  }
  printf("\nPeriodic task %d exiting...\n", idx);
  fflush(stdout);
  pthread_exit(0);
}



/*** UTILITIES ***/

int sub(struct timespec time1, struct timespec time2, struct timespec *result){
  /* Subtract the second time from the first. */
  if ((time1.tv_sec < time2.tv_sec) ||
      ((time1.tv_sec == time2.tv_sec) &&
       (time1.tv_nsec <= time2.tv_nsec))) {		/* TIME1 <= TIME2? */
      return -1;
  } else {						/* TIME1 > TIME2 */
      result->tv_sec = time1.tv_sec - time2.tv_sec;
      if (time1.tv_nsec < time2.tv_nsec) {
          result->tv_nsec = time1.tv_nsec + 1000000000L - time2.tv_nsec;
          result->tv_sec-- ;				/* Borrow a second. */
      } else {
          result->tv_nsec = time1.tv_nsec - time2.tv_nsec;
      }
    return 0;
  }
}

void increment(struct timespec *time, long int nano){
    time->tv_sec += nano / BILLION;
    time->tv_nsec += nano % BILLION;
    if (time->tv_nsec > BILLION){
      time->tv_sec += 1;
      time->tv_nsec -= BILLION;
  }
}

void print_timespec(struct timespec t){
  printf("%ld.%09ld\n", t.tv_sec, t.tv_nsec);
}



/*** DUMMY CODE FOR PERIODIC AND APERIODIC TASKS ***/
#define EXT 150
unsigned int seed = 0;

void short_task_code_1(){
  for (int i = 0; i < EXT; i++){
    for (int j = 0; j < 10000; j++){
	  double uno = rand_r(&seed)*rand_r(&seed);
	  (void) uno;
	}
  }
  printf("1");
  fflush(stdout);
}


void short_task_code_2(){
  for (int i = 0; i < EXT; i++){
    for (int j = 0; j < 10000; j++){
	  double uno = rand_r(&seed)*rand_r(&seed);
	  (void) uno;
	}
  }
  printf("2");
  fflush(stdout);
}

void long_task_code(){
  printf("*");
  fflush(stdout);
  for (int i = 0; i < 40; i++){
    for (int j = 0; j < 1000000; j++){
      double uno = rand_r(&seed)*rand_r(&seed);
      (void) uno;
    }
    printf("_");
    fflush(stdout);
  }
  printf("*");
  fflush(stdout);
}
