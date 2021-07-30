#include "command.h"
#include "util.h"

/**
 * 在这里面构造netlink协议包,发送到内核
 */

static int update_net_protect_status(uint8_t status) {

    struct nl_msg *msg;

    msg = nlmsg_alloc();
    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, famid, 0, NLM_F_REQUEST, HACKERNEL_C_NET_PROTECT, HACKERNEL_FAMLY_VERSION);
    nla_put_u8(msg, NET_A_OP_TYPE, status);
    nl_send_auto(nlsock, msg);

    return 0;
}

int net_policy_insert(const net_policy_t &policy) {
    struct nl_msg *msg;

    msg = nlmsg_alloc();
    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, famid, 0, NLM_F_REQUEST, HACKERNEL_C_NET_PROTECT, HACKERNEL_FAMLY_VERSION);
    nla_put_u8(msg, NET_A_OP_TYPE, NET_PROTECT_INSERT);

    nla_put_s32(msg, NET_A_ID, policy.id);
    nla_put_s8(msg, NET_A_PRIORITY, policy.priority);

    nla_put_u32(msg, NET_A_ADDR_SRC_BEGIN, policy.addr.src.begin);
    nla_put_u32(msg, NET_A_ADDR_SRC_END, policy.addr.src.end);
    nla_put_u32(msg, NET_A_ADDR_DST_BEGIN, policy.addr.dst.begin);
    nla_put_u32(msg, NET_A_ADDR_DST_END, policy.addr.dst.end);

    nla_put_u16(msg, NET_A_PORT_SRC_BEGIN, policy.port.src.begin);
    nla_put_u16(msg, NET_A_PORT_SRC_END, policy.port.src.end);
    nla_put_u16(msg, NET_A_PORT_DST_BEGIN, policy.port.dst.begin);
    nla_put_u16(msg, NET_A_PORT_DST_END, policy.port.dst.end);

    nla_put_u8(msg, NET_A_PROTOCOL_BEGIN, policy.protocol.begin);
    nla_put_u8(msg, NET_A_PROTOCOL_END, policy.protocol.end);

    nla_put_u32(msg, NET_A_RESPONSE, policy.response);
    nla_put_s32(msg, NET_A_ENABLED, policy.enabled);

    nl_send_auto(nlsock, msg);

    return 0;
}
int net_policy_delete(policy_id_t id) {
    struct nl_msg *msg;

    msg = nlmsg_alloc();
    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, famid, 0, NLM_F_REQUEST, HACKERNEL_C_NET_PROTECT, HACKERNEL_FAMLY_VERSION);
    nla_put_u8(msg, NET_A_OP_TYPE, NET_PROTECT_DELETE);
    nla_put_u32(msg, NET_A_ID, id);
    nl_send_auto(nlsock, msg);

    return 0;
}

int enable_net_protect() { return update_net_protect_status(NET_PROTECT_ENABLE); }
int disable_net_protect() { return update_net_protect_status(NET_PROTECT_DISABLE); }
