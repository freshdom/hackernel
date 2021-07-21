#ifndef HACKERNEL_NETLINK_KERNEL_SPACE
#define HACKERNEL_NETLINK_KERNEL_SPACE

#include <linux/kernel.h>

#define HACKERNEL_FAMLY_NAME "HACKERNEL"
#define HACKERNEL_FAMLY_VERSION 1

enum {
	HACKERNEL_A_UNSPEC,
	HACKERNEL_A_STATUS_CODE,
	HACKERNEL_A_OP_TYPE,
	HACKERNEL_A_SYS_CALL_TABLE_HEADER,
	HACKERNEL_A_NAME,
	HACKERNEL_A_PERM,
	HACKERNEL_A_EXECVE_ID,
	HACKERNEL_A_PORT,
	__HACKERNEL_A_MAX,
};
#define HACKERNEL_A_MAX (__HACKERNEL_A_MAX - 1)

// 模块对应命令
enum {
	HACKERNEL_C_UNSPEC,
	HACKERNEL_C_HANDSHAKE,
	HACKERNEL_C_PROCESS_PROTECT,
	HACKERNEL_C_FILE_PROTECT,
	HACKERNEL_C_NET_PROTECT,
	__HACKERNEL_C_MAX,
};
#define HACKERNEL_C_MAX (__HACKERNEL_C_MAX - 1)

void netlink_kernel_start(void);
void netlink_kernel_stop(void);

extern struct genl_family genl_family;
extern u32 portid;

#endif