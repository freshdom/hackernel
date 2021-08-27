#include "netlink.h"
#include "process.h"
#include "syscall.h"
#include "util.h"
#include <linux/binfmts.h>
#include <linux/gfp.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/types.h>

struct nla_policy process_policy[PROCESS_A_MAX + 1] = {
	[PROCESS_A_STATUS_CODE] = { .type = NLA_S32 },
	[PROCESS_A_OP_TYPE] = { .type = NLA_U8 },
	[PROCESS_A_NAME] = { .type = NLA_STRING },
	[PROCESS_A_PERM] = { .type = NLA_S32 },
	[PROCESS_A_ID] = { .type = NLA_S32 },
};

DEFINE_HOOK(execve);
DEFINE_HOOK(execveat);

static DECLARE_WAIT_QUEUE_HEAD(wq_process_perm);
static atomic_t atomic_process_id = ATOMIC_INIT(0);

#define PROCESS_PERM_MASK 0xFF
#define PROCESS_PERM_SIZE (PROCESS_PERM_MASK + 1) // 256
#define PROCESS_PERM_HASH(id) (id & (PROCESS_PERM_MASK)) // 散列函数

static void process_perm_head_init(process_perm_head_t *perm_head)
{
	INIT_HLIST_HEAD(&perm_head->head);
	rwlock_init(&perm_head->lock);
}

static process_perm_head_t *process_perm_hlist = NULL;
static DEFINE_RWLOCK(process_perm_hlist_lock);

static int process_perm_init(void)
{
	int idx, error;
	const size_t size = sizeof(process_perm_head_t) * PROCESS_PERM_SIZE;

	write_lock(&process_perm_hlist_lock);
	if (process_perm_hlist) {
		error = -EPERM;
		goto out;
	}

	process_perm_hlist = kmalloc(size, GFP_KERNEL);
	for (idx = 0; idx < PROCESS_PERM_SIZE; ++idx)
		process_perm_head_init(&process_perm_hlist[idx]);
out:
	write_unlock(&process_perm_hlist_lock);

	return 0;
}

static void process_perm_hlist_node_destory(process_perm_head_t *perm_head)
{
	struct process_perm_node *pos;
	struct hlist_node *n;
	write_lock(&perm_head->lock);
	hlist_for_each_entry_safe (pos, n, &perm_head->head, node) {
		hlist_del(&pos->node);
		kfree(pos);
	}
	write_unlock(&perm_head->lock);
}

static int process_perm_destory(void)
{
	size_t idx;
	int error = 0;
	write_lock(&process_perm_hlist_lock);
	if (!process_perm_hlist) {
		error = -EPERM;
		goto out;
	}
	for (idx = 0; idx < PROCESS_PERM_SIZE; ++idx)
		process_perm_hlist_node_destory(&process_perm_hlist[idx]);

	kfree(process_perm_hlist);
	process_perm_hlist = NULL;
out:
	write_unlock(&process_perm_hlist_lock);
	return error;
}

static int process_perm_insert(const process_perm_id_t id)
{
	const size_t size = sizeof(process_perm_node_t);
	const size_t idx = PROCESS_PERM_HASH(id);
	process_perm_head_t *perm_head;
	process_perm_node_t *new;

	read_lock(&process_perm_hlist_lock);
	if (!process_perm_hlist)
		goto out;
	perm_head = &process_perm_hlist[idx];
	new = kmalloc(size, GFP_KERNEL);
	if (!new) {
		LOG("no memory");
		goto out;
	}
	new->id = id;
	new->perm = PROCESS_WATT;

	write_lock(&perm_head->lock);
	hlist_add_head(&new->node, &perm_head->head);
	write_unlock(&perm_head->lock);
out:
	read_unlock(&process_perm_hlist_lock);
	return 0;
}

int process_perm_update(const process_perm_id_t id, const process_perm_t perm)
{
	struct process_perm_node *pos;
	const size_t idx = PROCESS_PERM_HASH(id);
	process_perm_head_t *perm_head;

	read_lock(&process_perm_hlist_lock);
	if (!process_perm_hlist)
		goto out;
	perm_head = &process_perm_hlist[idx];

	write_lock(&perm_head->lock);
	hlist_for_each_entry (pos, &perm_head->head, node) {
		if (pos->id != id)
			continue;

		pos->perm = perm;
		break;
	}
	write_unlock(&perm_head->lock);
out:
	read_unlock(&process_perm_hlist_lock);
	wake_up(&wq_process_perm);

	return 0;
}

static process_perm_t process_perm_search(const process_perm_id_t id)
{
	struct process_perm_node *pos;
	const size_t idx = PROCESS_PERM_HASH(id);
	process_perm_t perm = PROCESS_INVAILD;
	process_perm_head_t *perm_head;

	read_lock(&process_perm_hlist_lock);
	if (!process_perm_hlist)
		goto out;
	perm_head = &process_perm_hlist[idx];

	read_lock(&perm_head->lock);
	hlist_for_each_entry (pos, &perm_head->head, node) {
		if (pos->id != id)
			continue;

		perm = pos->perm;
		break;
	}
	read_unlock(&perm_head->lock);
out:
	read_unlock(&process_perm_hlist_lock);
	return perm;
}

static int process_perm_delele(const process_perm_id_t id)
{
	struct process_perm_node *victim;
	struct hlist_node *n;
	const size_t idx = PROCESS_PERM_HASH(id);
	process_perm_head_t *perm_head;

	read_lock(&process_perm_hlist_lock);
	if (!process_perm_hlist)
		goto out;
	perm_head = &process_perm_hlist[idx];

	write_lock(&perm_head->lock);
	hlist_for_each_entry_safe (victim, n, &perm_head->head, node) {
		if (victim->id != id)
			continue;

		hlist_del(&victim->node);
		kfree(victim);
		break;
	}
	write_unlock(&perm_head->lock);
out:
	read_unlock(&process_perm_hlist_lock);
	return 0;
}

static int condition_process_perm(process_perm_id_t id)
{
	return process_perm_search(id);
}

static process_perm_t process_protect_status(char *params)
{
	int error;
	static process_perm_id_t id;
	process_perm_t retval = PROCESS_INVAILD;
	const long timeout = msecs_to_jiffies(100U);

	id = atomic_inc_return(&atomic_process_id);

	error = process_perm_insert(id);
	if (error) {
		LOG("process_perm_insert failed");
		goto out;
	}

	error = process_protect_report_to_userspace(id, params);
	if (error) {
		LOG("process_protect_report_to_userspace failed");
		goto out;
	}

	wait_event_timeout(wq_process_perm, condition_process_perm(id),
			   timeout);

	retval = process_perm_search(id);

out:
	process_perm_delele(id);
	return retval;
}

// TODO: 添加对内存申请是否成功的校验
static int sys_execveat_helper(int dirfd, char __user *pathname,
			       char __user *__user *argv,
			       char __user *__user *envp, int flag)
{
	char *cmd, *params, *msg;
	int error = 0, len;
	process_perm_t perm = PROCESS_INVAILD;

	if (!portid)
		goto out;

	msg = kzalloc(MAX_ARG_STRLEN, GFP_KERNEL);

	cmd = get_absolute_path_alloc(dirfd, pathname);
	strcat(msg, cmd);
	kfree(cmd);

	params = kzalloc(MAX_ARG_STRLEN, GFP_KERNEL);
	len = parse_argv((const char *const *)argv, params, MAX_ARG_STRLEN);
	if (len < 0)
		goto out;

	if (len > 0) {
		strcat(msg, ASCII_US_STR);
		strcat(msg, params);
	}

	kfree(params);

	msg = adjust_path(msg);

	perm = process_protect_status(msg);
	if (perm == PROCESS_REJECT)
		error = -EPERM;

out:
	kfree(msg);
	return error;
}

static asmlinkage u64 sys_execve_hook(struct pt_regs *regs)
{
	char *pathname = (char *)HKSC_ARGV_ONE;
	char **argv = (char **)HKSC_ARGV_TWO;
	char **envp = (char **)HKSC_ARGV_THREE;

	if (sys_execveat_helper(AT_FDCWD, pathname, argv, envp, 0))
		return -EPERM;

	return hk_sys_execve(regs);
}

static asmlinkage u64 sys_execveat_hook(struct pt_regs *regs)
{
	int dirfd = (int)HKSC_ARGV_ONE;
	char *pathname = (char *)HKSC_ARGV_TWO;
	char **argv = (char **)HKSC_ARGV_THREE;
	char **envp = (char **)HKSC_ARGV_THREE;
	int flags = (int)HKSC_ARGV_FOUR;

	if (sys_execveat_helper(dirfd, pathname, argv, envp, flags))
		return -EPERM;

	return hk_sys_execveat(regs);
}
int enable_process_protect(void)
{
	process_perm_init();
	REG_HOOK(execve);
	REG_HOOK(execveat);
	return 0;
}

int disable_process_protect(void)
{
	UNREG_HOOK(execve);
	UNREG_HOOK(execveat);
	process_perm_destory();
	return 0;
}
