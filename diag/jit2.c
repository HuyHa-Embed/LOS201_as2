#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <sched.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "../../driver/sms_sensor.h"
static long long us(struct timespec a, struct timespec b){return ((long long)a.tv_sec-b.tv_sec)*1000000LL+(a.tv_nsec-b.tv_nsec)/1000;}
int main(int argc,char**argv){
  int ms=atoi(argv[1]), n=atoi(argv[2]), mode=atoi(argv[3]); unsigned iv=ms;
  struct sched_param sp={50}; sched_setscheduler(0,SCHED_FIFO,&sp);
  int fd=open("/dev/sms_sensor",O_RDONLY); ioctl(fd,SMS_SET_INTERVAL,&iv);
  struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
  long long sj=0,sr=0,mj=0,mr=0; char b[64];
  for(int i=0;i<n;i++){
    t.tv_nsec+=ms*1000000L; while(t.tv_nsec>=1000000000L){t.tv_nsec-=1000000000L;t.tv_sec++;}
    clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&t,NULL);
    struct timespec a,c; clock_gettime(CLOCK_MONOTONIC,&a);
    if(mode) read(fd,b,sizeof b);
    clock_gettime(CLOCK_MONOTONIC,&c);
    long long j=us(a,t), r=us(c,a); sj+=j; sr+=r; if(j>mj)mj=j; if(r>mr)mr=r;
  }
  printf("mode=%d jitter avg=%lld max=%lld | read avg=%lld max=%lld us\n",mode,sj/n,mj,sr/n,mr); return 0;}
