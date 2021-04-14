#include "sys_execve.h"
#include "syscall.h"
#include <linux/syscalls.h>

static sys_call_ptr_t real_execve = NULL;

asmlinkage u64 custom_execve(const struct pt_regs *regs)
{
	char *pathname;
	// 这种指针最后一个指针指向一个空字符串 "\0"
	char **argv;
	char **envp;
	
	pathname = (char *)regs->di;
	argv = (char **)regs->si;
	envp = (char **)regs->dx;

	printk("hackernel: filename=%s", pathname);
	return real_execve(regs);
}

int replace_execve(void)
{
	if (!g_sys_call_table) {
		printk("hackernel: g_sys_call_table must be initialized before calling replace_execve\n");
	}
	real_execve = g_sys_call_table[__NR_execve];
	disable_write_protect();
	g_sys_call_table[__NR_execve] = &custom_execve;
	enable_write_protect();
	return 0;
}

int restore_execve(void)
{
	if (!g_sys_call_table || !real_execve) {
		return 0;
	}
	disable_write_protect();
	g_sys_call_table[__NR_execve] = real_execve;
	enable_write_protect();
	return 0;
}
