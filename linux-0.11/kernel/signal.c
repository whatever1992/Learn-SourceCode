/*
 *  linux/kernel/signal.c
 *
 *  (C) 1991  Linus Torvalds
 */

// 关于信号处理
// 信号是一种“软中断”

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <signal.h>

// sigaction的结构
// signal.h
//struct sigaction {
//	void (*sa_handler)(int);	// 信号处理句柄
//	sigset_t sa_mask;			// 信号屏蔽码
//	int sa_flags;				// 信号选项标志
//	void (*sa_restorer)(void);	// 信号恢复函数指针
//};

volatile void do_exit(int error_code);

// 获取当前任务信号屏蔽位
int sys_sgetmask()	// sgetmask -> signal get mask
{
	return current->blocked;
}
// 设置信号屏蔽位。SIGKILL不能被屏蔽
int sys_ssetmask(int newmask)
{
	int old=current->blocked;

	current->blocked = newmask & ~(1<<(SIGKILL-1));
	return old;
}
// 复制 sigaction 到fs的数据段to处。从内核空间复制到用户任务数据段
static inline void save_old(char * from,char * to)
{
	int i;

	verify_area(to, sizeof(struct sigaction));
	for (i=0 ; i< sizeof(struct sigaction) ; i++) {
		put_fs_byte(*from,to);
		from++;
		to++;
	}
}

static inline void get_new(char * from,char * to)
{
	int i;

	for (i=0 ; i< sizeof(struct sigaction) ; i++)
		*(to++) = get_fs_byte(from++);
}
// signal系统调用。类似sigaction()
// 为指定信号安装新的信号句柄或者默认句柄
// (void) signal(SIGINT, handler) 
// void handler(int sig)
int sys_signal(int signum, long handler, long restorer)
{
	struct sigaction tmp;
	// 范围在1和32间
	if (signum<1 || signum>32 || signum==SIGKILL)// 不能是SIGKILL，不能被进程捕获
		return -1;
	tmp.sa_handler = (void (*)(int)) handler;
	tmp.sa_mask = 0;
	tmp.sa_flags = SA_ONESHOT | SA_NOMASK;
	tmp.sa_restorer = (void (*)(void)) restorer;	// 保存回复处理函数指针
	handler = (long) current->sigaction[signum-1].sa_handler; // 取该信号原来的处理句柄，设置该信号的sigaction
	current->sigaction[signum-1] = tmp;
	return handler; // 返回原信号句柄
}

// sigaction()系统调用
int sys_sigaction(int signum, const struct sigaction * action,
	struct sigaction * oldaction)
{
	struct sigaction tmp;

	if (signum<1 || signum>32 || signum==SIGKILL)
		return -1;
	tmp = current->sigaction[signum-1];
	get_new((char *) action,
		(char *) (signum-1+current->sigaction));
	if (oldaction)
		save_old((char *) &tmp,(char *) oldaction);
	if (current->sigaction[signum-1].sa_flags & SA_NOMASK)
		current->sigaction[signum-1].sa_mask = 0;
	else
		current->sigaction[signum-1].sa_mask |= (1<<(signum-1));
	return 0;
}
// 信号预处理
void do_signal(long signr,long eax, long ebx, long ecx, long edx,
	long fs, long es, long ds,
	long eip, long cs, long eflags,
	unsigned long * esp, long ss)
{
	unsigned long sa_handler;
	long old_eip=eip;
	struct sigaction * sa = current->sigaction + signr - 1;
	int longs;
	unsigned long * tmp_esp;

	sa_handler = (unsigned long) sa->sa_handler;
	if (sa_handler==1)
		return;
	if (!sa_handler) {
		if (signr==SIGCHLD)
			return;
		else
			do_exit(1<<(signr-1));
	}
	if (sa->sa_flags & SA_ONESHOT)
		sa->sa_handler = NULL;
	*(&eip) = sa_handler;
	longs = (sa->sa_flags & SA_NOMASK)?7:8;
	*(&esp) -= longs;
	verify_area(esp,longs*4);
	tmp_esp=esp;
	put_fs_long((long) sa->sa_restorer,tmp_esp++);
	put_fs_long(signr,tmp_esp++);
	if (!(sa->sa_flags & SA_NOMASK))
		put_fs_long(current->blocked,tmp_esp++);
	put_fs_long(eax,tmp_esp++);
	put_fs_long(ecx,tmp_esp++);
	put_fs_long(edx,tmp_esp++);
	put_fs_long(eflags,tmp_esp++);
	put_fs_long(old_eip,tmp_esp++);
	current->blocked |= sa->sa_mask;
}
