#ifndef HACKERNEL_NETLINK_KERNEL_SPACE
#define HACKERNEL_NETLINK_KERNEL_SPACE

#define HACKERNEL_FAMLY_NAME "HACKERNEL"
#define HACKERNEL_FAMLY_VERSION 1

enum {
	HACKERNEL_A_UNSPEC,
	HACKERNEL_A_CODE, // 状态码(标记握手是否成功)
	HACKERNEL_A_SCTH, // 系统调用表头(握手)
	HACKERNEL_A_NAME, // 进程名(进程保护)或文件名(文件保护)
	HACKERNEL_A_PERM, // 文件权限(文件保护)
	__HACKERNEL_A_MAX,
};
#define HACKERNEL_A_MAX (__HACKERNEL_A_MAX - 1)

enum {
	HACKERNEL_C_UNSPEC,
	HACKERNEL_C_HANDSHAKE,
	HACKERNEL_C_PROCESS_PROTECT,
	HACKERNEL_C_FILE_PROTECT,
	__HACKERNEL_C_MAX,
};
#define HACKERNEL_C_MAX (__HACKERNEL_C_MAX - 1)

#define FILE_PROTECT_ENABLE 1
#define FILE_PROTECT_DISABLE 2
#define FILE_PROTECT_SET 3
#define FILE_PROTECT_NOTIFY 4

void netlink_kernel_start(void);
void netlink_kernel_stop(void);


extern struct genl_family genl_family;

#endif