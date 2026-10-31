#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <sched.h>
int main(int argc, char **argv){
  int ms = atoi(argv[1]), n = atoi(argv[2]);
  if (argc > 3) { struct sched_param sp={50}; printf("fifo=%d\n", sched_setscheduler(0, SCHED_FIFO,&sp)); }
  struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
  long long mx=0,sum=0;
  for(int i=0;i<n;i++){
    t.tv_nsec += ms*1000000L; while(t.tv_nsec>=1000000000L){t.tv_nsec-=1000000000L;t.tv_sec++;}
    clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&t,NULL);
    struct timespec a; clock_gettime(CLOCK_MONOTONIC,&a);
    long long j=((long long)a.tv_sec-t.tv_sec)*1000000000LL+(a.tv_nsec-t.tv_nsec); j/=1000;
    sum+=j; if(j>mx)mx=j;
  }
  printf("avg=%lld us max=%lld us\n", sum/n, mx); return 0; }
