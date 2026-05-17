#include <asm/sbi.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

#define SBI_EXT_CPULOAD 0x08000451

SYSCALL_DEFINE1(get_cpu_load, unsigned long __user *, val) {
  struct sbiret ret;

  ret = sbi_ecall(SBI_EXT_CPULOAD, 0, 0, 0, 0, 0, 0, 0);

  if (ret.error)
    return -EIO;

  if (copy_to_user(val, &ret.value, sizeof(ret.value)))
    return -EFAULT;

  return 0;
}