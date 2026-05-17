#include <sbi/riscv_asm.h>
#include <sbi/sbi_ecall.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_trap.h>

static int cpuload_handler(unsigned long extid, unsigned long funcid,
                           struct sbi_trap_regs *regs,
                           struct sbi_ecall_return *out) {
  out->value = csr_read(CSR_MCYCLE);
  return 0;
}

struct sbi_ecall_extension ecall_cpuload;

static int sbi_ecall_cpuload_register(void) {
  extern struct sbi_ecall_extension ecall_cpuload;
  return sbi_ecall_register_extension(&ecall_cpuload);
}

struct sbi_ecall_extension ecall_cpuload = {
    .name = "cpuload",
    .extid_start = SBI_EXT_CPULOAD,
    .extid_end = SBI_EXT_CPULOAD,
    .register_extensions = sbi_ecall_cpuload_register,
    .handle = cpuload_handler,
};