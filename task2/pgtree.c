#include <asm/pgtable.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pgtable.h>
#include <linux/pid.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Student");
MODULE_DESCRIPTION("User page table tree visualizer");

static inline p4d_t *pgtree_p4d_offset(pgd_t *pgd, unsigned long addr) {
  (void)addr;
  return (p4d_t *)pgd;
}

static inline pud_t *pgtree_pud_offset(p4d_t *p4d, unsigned long addr) {
  (void)addr;
  return (pud_t *)p4d;
}

static int target_pid = 1;
static struct task_struct *pgtree_get_task_by_pid(pid_t nr) {
  struct pid *pid;
  struct task_struct *task;

  pid = find_get_pid(nr);
  if (!pid)
    return NULL;

  task = get_pid_task(pid, PIDTYPE_PID);
  put_pid(pid);

  return task;
}

static void pte_flags_str(pte_t pte, char *buf, size_t len) {
  snprintf(buf, len, "%c%c%c%c%c%c", pte_present(pte) ? 'P' : '-',
           pte_write(pte) ? 'W' : '-', pte_exec(pte) ? 'X' : '-',
           pte_young(pte) ? 'A' : '-', pte_dirty(pte) ? 'D' : '-',
           pte_user(pte) ? 'U' : '-');
}

static void walk_pte(struct seq_file *m, pmd_t *pmd, unsigned long addr,
                     unsigned long end) {
  char flags[8];

  for (; addr < end; addr += PAGE_SIZE) {
    pte_t *ptep;
    pte_t pte;
    unsigned long pa;

    ptep = pte_offset_kernel(pmd, addr);
    pte = READ_ONCE(*ptep);

    if (pte_none(pte))
      continue;

    pte_flags_str(pte, flags, sizeof(flags));

    if (!pte_present(pte)) {
      seq_printf(m, "│   │   │   └── [PTE] VA: 0x%016lx -> NONPRESENT [%s]\n",
                 addr, flags);
      continue;
    }

    pa = ((unsigned long)pte_pfn(pte)) << PAGE_SHIFT;

    seq_printf(m, "│   │   │   └── [PTE] VA: 0x%016lx -> PA: 0x%016lx [%s]\n",
               addr, pa, flags);
  }
}

static void walk_pmd(struct seq_file *m, pud_t *pud, unsigned long addr,
                     unsigned long end) {
  pmd_t *pmd;
  unsigned long next;
  int idx;

  pmd = pmd_offset(pud, addr);
  idx = pmd_index(addr);

  for (; addr < end; addr = next, pmd++, idx++) {
    next = pmd_addr_end(addr, end);

    if (pmd_none(*pmd))
      continue;

    if (pmd_leaf(*pmd)) {
      seq_printf(m,
                 "│   │   ├── [PMD %3d] VA: 0x%016lx  LEAF/HUGE PAGE (2MB)\n",
                 idx, addr);
      continue;
    }

    seq_printf(m, "│   │   ├── [PMD %3d] VA: 0x%016lx\n", idx, addr);

    walk_pte(m, pmd, addr, next);
  }
}

static void walk_pud(struct seq_file *m, p4d_t *p4d, unsigned long addr,
                     unsigned long end) {
  pud_t *pud;
  unsigned long next;
  int idx;

  pud = pgtree_pud_offset(p4d, addr);
  idx = pud_index(addr);

  for (; addr < end; addr = next, pud++, idx++) {
    next = pud_addr_end(addr, end);

    if (pud_none(*pud))
      continue;

    if (pud_leaf(*pud)) {
      seq_printf(m, "│   ├── [PUD %3d] VA: 0x%016lx  LEAF/HUGE PAGE (1GB)\n",
                 idx, addr);
      continue;
    }

    seq_printf(m, "│   ├── [PUD %3d] VA: 0x%016lx\n", idx, addr);

    walk_pmd(m, pud, addr, next);
  }
}

static void walk_p4d(struct seq_file *m, pgd_t *pgd, unsigned long addr,
                     unsigned long end) {
  p4d_t *p4d;
  unsigned long next;
  int idx;

  p4d = pgtree_p4d_offset(pgd, addr);
  idx = p4d_index(addr);
  for (; addr < end; addr = next, p4d++, idx++) {
    next = p4d_addr_end(addr, end);

    if (p4d_none(*p4d))
      continue;

    seq_printf(m, "├── [P4D %3d] VA: 0x%016lx\n", idx, addr);

    walk_pud(m, p4d, addr, next);
  }
}

static void walk_pgd(struct seq_file *m, struct mm_struct *mm) {
  pgd_t *pgd;
  unsigned long addr, next;
  int idx;
  struct vm_area_struct *vma;
  VMA_ITERATOR(vmi, mm, 0);

  for_each_vma(vmi, vma) {
    addr = vma->vm_start;
    seq_printf(m, "\nVMA [0x%016lx - 0x%016lx] %c%c%c\n", vma->vm_start,
               vma->vm_end, (vma->vm_flags & VM_READ) ? 'r' : '-',
               (vma->vm_flags & VM_WRITE) ? 'w' : '-',
               (vma->vm_flags & VM_EXEC) ? 'x' : '-');

    for (; addr < vma->vm_end; addr = next) {
      pgd = pgd_offset(mm, addr);
      idx = pgd_index(addr);
      next = pgd_addr_end(addr, vma->vm_end);

      if (pgd_none(*pgd))
        continue;

      seq_printf(m, "├── [PGD %3d] VA: 0x%016lx\n", idx, addr);

      walk_p4d(m, pgd, addr, next);
    }
  }
}

static int pgtree_show(struct seq_file *m, void *v) {
  struct task_struct *task;
  struct mm_struct *mm;
  int pid = READ_ONCE(target_pid);

  task = pgtree_get_task_by_pid(pid);
  if (!task) {
    seq_printf(m, "Error: PID %d not found\n", pid);
    return 0;
  }

  mm = get_task_mm(task);
  if (!mm) {
    seq_printf(m, "Error: PID %d is a kernel thread\n", pid);
    put_task_struct(task);
    return 0;
  }

  seq_printf(m, "Page Table Tree for PID %d (%s)\n", pid, task->comm);
  seq_printf(m, "PGD root: %px\n", mm->pgd);
  seq_puts(m, "Legend: P=Present W=Write X=Exec A=Accessed D=Dirty U=User\n");
  seq_puts(m, "============================================================\n");

  mmap_read_lock(mm);
  walk_pgd(m, mm);
  mmap_read_unlock(mm);

  mmput(mm);
  put_task_struct(task);
  return 0;
}

static int pgtree_open(struct inode *inode, struct file *file) {
  return single_open(file, pgtree_show, NULL);
}

static ssize_t pgtree_write(struct file *file, const char __user *ubuf,
                            size_t count, loff_t *ppos) {
  char buf[16];
  int pid;

  if (count == 0)
    return 0;

  if (count > sizeof(buf) - 1)
    return -EINVAL;

  if (copy_from_user(buf, ubuf, count))
    return -EFAULT;

  buf[count] = '\0';

  if (kstrtoint(buf, 10, &pid) || pid <= 0)
    return -EINVAL;

  WRITE_ONCE(target_pid, pid);
  pr_info("pgtree: target PID set to %d\n", pid);

  return count;
}

static const struct proc_ops pgtree_ops = {
    .proc_open = pgtree_open,
    .proc_read = seq_read,
    .proc_write = pgtree_write,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static int __init pgtree_init(void) {
  if (!proc_create("pgtree", 0644, NULL, &pgtree_ops)) {
    pr_err("pgtree: failed to create /proc/pgtree\n");
    return -ENOMEM;
  }

  pr_info("pgtree: module loaded, /proc/pgtree created\n");
  return 0;
}

static void __exit pgtree_exit(void) {
  remove_proc_entry("pgtree", NULL);
  pr_info("pgtree: module unloaded\n");
}

module_init(pgtree_init);
module_exit(pgtree_exit);