/*
 *  linux/kernel/exit.c
 *
 *  (C) 1991  Linus Torvalds
 */
// 进程终止和退出的有关处理事宜
// 包含：
// 进程释放
// 会话终止
// 程序退出处理函数
// 杀死进程
// 终止进程
// 挂起进程
// 信号发送
// 通知父进程终止函数
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <asm/segment.h>

int sys_pause(void);
int sys_close(int fd);
// 释放指定进程的任务槽以及任务数据结构占用的内存页面
void release(struct task_struct * p)
{
	int i;

	if (!p)
		return;
	for (i=1 ; i<NR_TASKS ; i++)	// 扫描任务数组
		if (task[i]==p) {	// 如果是p
			task[i]=NULL;	// 置空
			free_page((long)p); // 释放内存页
			schedule();		// 重新调度
			return;
		}
// 存在指定页但是不在任务槽 死机
	panic("trying to release non-existent task");
}

// 向指定任务p 发信号sig
static inline int send_sig(long sig,struct task_struct * p,int priv) // priv是权限
{
	// 不在范围内
	if (!p || sig<1 || sig>32)
		return -EINVAL;
	// 如果强制发送标志
	// 如果当前进程有效用户标识符euid == 进程euid
	// 如果是超级用户(current->euid == 0)
	if (priv || (current->euid==p->euid) || suser())
		p->signal |= (1<<(sig-1));
	else
		return -EPERM;
	return 0;
}

// 终止会话
static void kill_session(void)
{
	struct task_struct **p = NR_TASKS + task; // 指向最末
	
	// 扫描任务指针数组
	while (--p > &FIRST_TASK) {
		// 如果会话session等于当前进程会话号就发送挂断
		if (*p && (*p)->session == current->session) 
			(*p)->signal |= 1<<(SIGHUP-1); // 发送挂断进程信号
	}
}

/*
 * XXX need to check permissions needed to send signals to process
 * groups, etc. etc.  kill() permissions semantics are tricky!
 */
// kill用于向任何进程或者进程组发送任何信号
// 所以有kill -9 X 
int sys_kill(int pid,int sig)
{
	struct task_struct **p = NR_TASKS + task;
	int err, retval = 0;
	
	if (!pid) while (--p > &FIRST_TASK) { // 如果< -1 发给所有进程组
		if (*p && (*p)->pgrp == current->pid) 
			if (err=send_sig(sig,*p,1)) // 强制发信息
				retval = err;
	} else if (pid>0) while (--p > &FIRST_TASK) { // 如果是> 0 就发给它
		if (*p && (*p)->pid == pid) 
			if (err=send_sig(sig,*p,0))
				retval = err;
	} else if (pid == -1) while (--p > &FIRST_TASK) // 如果-1。 发给所有进程
		if (err = send_sig(sig,*p,0))
			retval = err;
	else while (--p > &FIRST_TASK) 
		if (*p && (*p)->pgrp == -pid)
			if (err = send_sig(sig,*p,0))
				retval = err;
	return retval;
}
// 通知父进程
static void tell_father(int pid)
{
	int i;

	if (pid)
		for (i=0;i<NR_TASKS;i++) { // 遍历进程数组表
			if (!task[i])
				continue;
			if (task[i]->pid != pid)
				continue;
			task[i]->signal |= (1<<(SIGCHLD-1)); // 寻找到指定pid 发消息SIGCHLD
			return;
		}
/* if we don't find any fathers, we just release ourselves */
/* This is not really OK. Must change it to make father 1 */
	printk("BAD BAD - no father found\n\r");
	release(current);
}

// 程序退出处理函数
int do_exit(long code)
{
	int i;
// 释放代码段和数据段所占内存页
	free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));
	free_page_tables(get_base(current->ldt[2]),get_limit(0x17));
	for (i=0 ; i<NR_TASKS ; i++)
		if (task[i] && task[i]->father == current->pid) {
			task[i]->father = 1;
			if (task[i]->state == TASK_ZOMBIE)
				/* assumption task[1] is always init */
				(void) send_sig(SIGCHLD, task[1], 1);
		}
	for (i=0 ; i<NR_OPEN ; i++)
		if (current->filp[i])
			sys_close(i);
	iput(current->pwd);
	current->pwd=NULL;
	iput(current->root);
	current->root=NULL;
	iput(current->executable);
	current->executable=NULL;
	if (current->leader && current->tty >= 0)
		tty_table[current->tty].pgrp = 0;
	if (last_task_used_math == current)
		last_task_used_math = NULL;
	if (current->leader)
		kill_session();
	current->state = TASK_ZOMBIE;
	current->exit_code = code;
	tell_father(current->father);
	schedule();
	return (-1);	/* just to suppress warnings */
}
// 系统调用上面那个
int sys_exit(int error_code)
{
	return do_exit((error_code&0xff)<<8);
}
// 系统调用waitpid()
// 挂起当前进程知道pid指定的子进程退出
int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options)
{
	int flag, code;
	struct task_struct ** p;

	verify_area(stat_addr,4);
repeat:
	flag=0;
	for(p = &LAST_TASK ; p > &FIRST_TASK ; --p) { // 遍历
		if (!*p || *p == current) // 空 or 本身 
			continue;
		if ((*p)->father != current->pid) // 父进程非当前进程
			continue;

		// 以下就是当前进程中的子进程 (*p)->father == current->pid
		if (pid>0) {
			if ((*p)->pid != pid) // 不是指定子进程
				continue;
		} else if (!pid) {	// 正在等待进程组号等于pid绝对值的任何子进程
			if ((*p)->pgrp != current->pgrp)
				continue;
		} else if (pid != -1) {
			if ((*p)->pgrp != -pid)
				continue;
		}
		switch ((*p)->state) {
			case TASK_STOPPED:	// 如果停止
				if (!(options & WUNTRACED))  // 并且WUNTRCED置位
					continue;
				put_fs_long(0x7f,stat_addr);
				return (*p)->pid;
			case TASK_ZOMBIE:	// 如果僵死
				current->cutime += (*p)->utime;
				current->cstime += (*p)->stime;
				flag = (*p)->pid;
				code = (*p)->exit_code;
				release(*p);
				put_fs_long(code,stat_addr);
				return flag;
			default:	// 找到过
				flag=1;	// flag=1
				continue;
		}
	}
	if (flag) { // 说明找到过
		if (options & WNOHANG)
			return 0;
		current->state=TASK_INTERRUPTIBLE;
		schedule();
		if (!(current->signal &= ~(1<<(SIGCHLD-1))))
			goto repeat;
		else
			return -EINTR;
	}
	return -ECHILD;
}


