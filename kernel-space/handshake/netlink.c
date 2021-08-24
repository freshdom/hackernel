#include "netlink.h"
#include "handshake.h"
#include "syscall.h"
#include <net/genetlink.h>

extern struct genl_family genl_family;

int handshake_handler(struct sk_buff *skb, struct genl_info *info)
{
	int error = 0;
	unsigned long long syscall_table = 0;
	struct sk_buff *reply = NULL;
	void *head = NULL;
	int code;

	if (!netlink_capable(skb, CAP_SYS_ADMIN)) {
		LOG("netlink_capable failed");
		return -EPERM;
	}

	if (!info->attrs[HANDSHAKE_A_SYS_CALL_TABLE_HEADER]) {
		code = -EINVAL;
		LOG("HANDSHAKE_A_SYS_CALL_TABLE_HEADER failed");
		goto response;
	}

	syscall_table =
		nla_get_u64(info->attrs[HANDSHAKE_A_SYS_CALL_TABLE_HEADER]);
	code = init_sys_call_table(syscall_table);
	portid = info->snd_portid;

response:
	reply = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (unlikely(!reply)) {
		LOG("genlmsg_new failed");
		goto errout;
	}

	head = genlmsg_put_reply(reply, info, &genl_family, 0,
				 HACKERNEL_C_HANDSHAKE);
	if (unlikely(!head)) {
		LOG("genlmsg_put_reply failed");
		goto errout;
	}

	error = nla_put_s32(reply, HANDSHAKE_A_STATUS_CODE, code);
	if (unlikely(error)) {
		LOG("nla_put_s32 failed");
		goto errout;
	}

	genlmsg_end(reply, head);

	// reply指向的内存由 genlmsg_reply 释放
	// 此处调用 nlmsg_free(reply) 会引起内核crash
	error = genlmsg_reply(reply, info);
	if (unlikely(error))
		LOG("genlmsg_reply failed");

	return 0;
errout:
	nlmsg_free(reply);
	return 0;
}