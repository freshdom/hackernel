/* SPDX-License-Identifier: GPL-2.0 */
#include "netlink.h"
#include "file.h"
#include "handshake.h"
#include "net.h"
#include "process.h"
#include "syscall.h"
#include "util.h"
#include <net/genetlink.h>
#include <net/netlink.h>

u32 g_portid = 0;

extern struct nla_policy handshake_policy[HANDSHAKE_A_MAX + 1];
extern struct nla_policy file_policy[FILE_A_MAX + 1];
extern struct nla_policy process_policy[PROCESS_A_MAX + 1];
extern struct nla_policy net_policy[NET_A_MAX + 1];

static struct genl_ops genl_ops[] = {
	{
		.cmd = HACKERNEL_C_HANDSHAKE,
		.doit = handshake_handler,
		.policy = handshake_policy,
		.maxattr = HANDSHAKE_A_MAX,
	},
	{
		.cmd = HACKERNEL_C_FILE_PROTECT,
		.doit = file_protect_handler,
		.policy = file_policy,
		.maxattr = FILE_A_MAX,
	},
	{
		.cmd = HACKERNEL_C_PROCESS_PROTECT,
		.doit = process_protect_handler,
		.policy = process_policy,
		.maxattr = PROCESS_A_MAX,
	},
	{
		.cmd = HACKERNEL_C_NET_PROTECT,
		.doit = net_protect_handler,
		.policy = net_policy,
		.maxattr = NET_A_MAX,
	},
};

struct genl_family genl_family = {
	.name = HACKERNEL_FAMLY_NAME,
	.version = HACKERNEL_FAMLY_VERSION,
	.module = THIS_MODULE,
	.ops = genl_ops,
	.n_ops = ARRAY_SIZE(genl_ops),
};

void netlink_kernel_start(void)
{
	int error = 0;

	error = genl_register_family(&genl_family);
	if (error)
		LOG("genl_register_family failed");
}

void netlink_kernel_stop(void)
{
	int error = 0;

	error = genl_unregister_family(&genl_family);
	if (error)
		LOG("genl_unregister_family failed");
}
