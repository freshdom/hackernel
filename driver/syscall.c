#include "syscall.h"
#include "file.h"
#include "fperm.h"
#include "process.h"
#include "util.h"
#include <asm/special_insns.h>
#include <net/net_namespace.h>

sys_call_ptr_t *g_sys_call_table = NULL;

int init_sys_call_table(u64 sys_call_table)
{
	if (g_sys_call_table)
		return -1;
	if (!sys_call_table)
		return -1;
	g_sys_call_table = (sys_call_ptr_t *)sys_call_table;
	return 0;
}

int enable_process_protect(void)
{
	int error;
	error = replace_execve();
	if (error) {
		LOG("replace_execve failed");
	}
	return error;
}

int disable_process_protect(void)
{
	int error;
	error = restore_execve();
	if (error) {
		LOG("restore_execve failed");
	}
	return error;
}

int enable_file_protect(void)
{
	int error;
	error = replace_open();
	if (error) {
		LOG("replace_open failed");
	}
	error = replace_openat();
	if (error) {
		LOG("replace_openat failed");
	}
	error = replace_unlinkat();
	if (error) {
		LOG("replace_unlinkat failed");
	}
	error = replace_renameat2();
	if (error) {
		LOG("replace_renameat2 failed");
	}
	return error;
}

int disable_file_protect(void)
{
	int error;
	error = restore_open();
	if (error) {
	}
	error = restore_openat();
	if (error) {
	}
	error = restore_unlinkat();
	if (error) {
	}
	error = restore_renameat2();
	if (error) {
	}
	return error;
}

static inline void write_cr0_forced(unsigned long val)
{
	unsigned long __force_order;
	asm volatile("mov %0, %%cr0" : "+r"(val), "+m"(__force_order));
}

void enable_write_protection(void)
{
	write_cr0_forced(read_cr0() | 0x00010000);
}

void disable_write_protection(void)
{
	write_cr0_forced(read_cr0() & ~0x00010000);
}