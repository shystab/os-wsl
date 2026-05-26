#include "types.h"
#include "user.h"

struct lock_t {
  int locked;
};

int xchg(volatile int *addr, int newval) {
  int result;
  asm volatile("xchgl %0, %1" : "+m"(*addr), "=a"(result) : "1"(newval));
  return result;
}

void lock_init(struct lock_t *lk) {
  lk->locked = 0;
}

void lock_acquire(struct lock_t *lk) {
  while (xchg(&lk->locked, 1) != 0)
    ;
}

void lock_release(struct lock_t *lk) {
  xchg(&lk->locked, 0);
}

struct lock_t lock;
int shared_counter = 0;

void worker(void *arg1, void *arg2) {
  int i;
  for (i = 0; i < 1000000; i++) {
    lock_acquire(&lock);
    shared_counter++;
    lock_release(&lock);
  }
  exit();
}

int main(void) {
  void *stack1, *stack2;
  int pid1, pid2;

  lock_init(&lock);

  stack1 = malloc(4096);
  stack2 = malloc(4096);
  if (stack1 == 0 || stack2 == 0) {
    printf(1, "malloc failed\n");
    exit();
  }

  printf(1, "parent calling clone...\n");
  pid1 = clone(worker, 0, 0, (void*)((uint)stack1 + 4096));
  pid2 = clone(worker, 0, 0, (void*)((uint)stack2 + 4096));

  if (pid1 < 0 || pid2 < 0) {
    printf(1, "clone failed\n");
    exit();
  }
  printf(1, "clone returned pid=%d\n", pid1);

  pid1 = join(&stack1);
  printf(1, "join returned pid=%d\n", pid1);

  pid2 = join(&stack2);
  printf(1, "join returned pid=%d\n", pid2);

  printf(1, "counter = %d (expected 2000000)\n", shared_counter);

  free(stack1);
  free(stack2);
  exit();
}
