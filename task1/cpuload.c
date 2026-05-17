#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

#define __NR_get_cpu_load 471

int main(void) {
  unsigned long val = 0;
  long ret;

  ret = syscall(__NR_get_cpu_load, &val);

  if (ret != 0) {
    printf("syscall failed, errno=%d\n", errno);
    perror("get_cpu_load");
    return 1;
  }

  printf("CSR_MCYCLE = %lu (0x%lx)\n", val, val);

  return 0;
}