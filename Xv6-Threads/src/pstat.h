#include "param.h"
struct pstat {
  int inuse[NPROC];   // 进程槽是否在使用
  int tickets[NPROC]; // 每个进程的票数
  int pid[NPROC];     // 每个进程的 PID
  int ticks[NPROC];   // 每个进程累计运行的时间片数
};