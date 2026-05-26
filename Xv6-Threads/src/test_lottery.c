#include "types.h"
#include "user.h"
#include "pstat.h"

int
main(void)
{
  int pid1, pid2, i;
  struct pstat ps;
  volatile int x = 0;

  settickets(30);

  pid1 = fork();
  if (pid1 == 0) {
    settickets(20);
    pid2 = fork();
    if (pid2 == 0) {
      settickets(10);
    }
  }

  for (i = 0; i < 2000000000; i++)
    x += i;

  if (pid1 > 0) {
    getpinfo(&ps);
    wait();
    wait();
    printf(1, "PID\tTickets\tTicks\n");
    for (i = 0; i < NPROC; i++) {
      if (ps.inuse[i])
        printf(1, "%d\t%d\t%d\n", ps.pid[i], ps.tickets[i], ps.ticks[i]);
    }
  }

  exit();
}
