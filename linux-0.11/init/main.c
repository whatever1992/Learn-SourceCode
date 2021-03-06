/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
// *.h默认目录在include/。如果不是UNIX的标准头文件，需要指明所在目录，并用双引号括住
// unistd.h 如果定义了__LIBRARY__，会包含系统调用号和内嵌汇编代码

#include <unistd.h>
#include <time.h>	// define "tm" struct and some functions about time

/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 */

// _syscall0 0个参数
// _syscall1 1个参数
// _syscall2 2个参数
// _syscall3 3个参数     
static inline _syscall0(int,fork)         	// in inlcude/unistd.h
/*
	#define _syscall0(type,name) \
	type name(void) \
	{ \
	long __res; \
	__asm__ volatile ("int $0x80" \
		: "=a" (__res) \
		: "0" (__NR_##name)); \
	if (__res >= 0) \
		return (type) __res; \
	errno = -__res; \
	return -1; \
	}
*/
// 转化 static inline _syscall0(int,fork)
/*
static inline int fork(void)
{
	long __res;
	__asm__volatile ("int $0x80" : "=a" (__res) : "0" (__NR_fork));
	if (__res >= 0)
		return (int) __res;
	errno = -__res;
	return -1;
}
*/
static inline _syscall0(int,pause)					// int pause() 系统调用：暂停进程的执行，直到收到一个信号
static inline _syscall1(int,setup,void *,BIOS)		// int setup(void * BIOS)系统调用，用于linux初始化（仅在这个程序中被调用）
static inline _syscall0(int,sync)					// int sync() 系统调用：更新文件系统


#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <asm/system.h>
#include <asm/io.h>

#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include <linux/fs.h>

static char printbuf[1024];

extern int vsprintf();
extern void init(void);
extern void blk_dev_init(void);
extern void chr_dev_init(void);
extern void hd_init(void);
extern void floppy_init(void);
extern void mem_init(long start, long end);
extern long rd_init(long mem_start, int length);
extern long kernel_mktime(struct tm * tm);
extern long startup_time;

/*
 * This is set up by the setup-routine at boot-time
 */
#define EXT_MEM_K (*(unsigned short *)0x90002)
#define DRIVE_INFO (*(struct drive_info *)0x90080)
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC)

/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 */
// CMOS_READ(0);CMOS_READ(2); to read the real time 内嵌汇编, BCD的
#define CMOS_READ(addr) ({ \
outb_p(0x80|addr,0x70); \  // 和 0x80与 没意义
inb_p(0x71); \
})

// change BCD to Bin
#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)

static void time_init(void)
{
	struct tm time; 	// #include <time.h>

	do {
		time.tm_sec = CMOS_READ(0);
		time.tm_min = CMOS_READ(2);
		time.tm_hour = CMOS_READ(4);
		time.tm_mday = CMOS_READ(7);
		time.tm_mon = CMOS_READ(8);
		time.tm_year = CMOS_READ(9);
	} while (time.tm_sec != CMOS_READ(0));
	BCD_TO_BIN(time.tm_sec); //  ((time.tm_sec)=((time.tm_sec)&15) + ((time.tm_sec)>>4)*10)
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--;
	startup_time = kernel_mktime(&time);
}

static long memory_end = 0;
static long buffer_memory_end = 0;
static long main_memory_start = 0;

struct drive_info { char dummy[32]; } drive_info;

void main(void)		/* This really IS void, no error here. */
{			/* The startup routine assumes (well, ...) this */ 
// in head.s 136行：    
//		after_page_tables:
//			pushl $0		# These are the parameters to main :-)
//			pushl $0
//			pushl $0

/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */
 	ROOT_DEV = ORIG_ROOT_DEV;		// in fs/super.c : int ROOT_DEV = 0;
 	drive_info = DRIVE_INFO;		// #define DRIVE_INFO (*(struct drive_info *)0x90080) 复制90080处的键盘参数表
	memory_end = (1<<20) + (EXT_MEM_K<<10);		//内存大小=1Mb + 扩展内存(k)*1024字节 
												// #define EXT_MEM_K (*(unsigned short *)0x90002)
	memory_end &= 0xfffff000;		// 忽略不到4Kb的内存
	if (memory_end > 16*1024*1024)
		memory_end = 16*1024*1024;
	if (memory_end > 12*1024*1024) 
		buffer_memory_end = 4*1024*1024;
	else if (memory_end > 6*1024*1024)
		buffer_memory_end = 2*1024*1024;
	else
		buffer_memory_end = 1*1024*1024;
	main_memory_start = buffer_memory_end;
#ifdef RAMDISK
	main_memory_start += rd_init(main_memory_start, RAMDISK*1024);
#endif
	mem_init(main_memory_start,memory_end);		// mm/memory.c 		主存区初始化
	trap_init();								// kernel/traps.c 	设置硬件中断向量
	blk_dev_init();								
	chr_dev_init();
	tty_init();
	time_init();
	sched_init();								// kernel/sched.c
	buffer_init(buffer_memory_end);				// fs/buffer.c
	hd_init();
	floppy_init();
	sti();
	move_to_user_mode();
	if (!fork()) {		/* we count on this going ok */
		init();
	}
/*
 *   NOTE!!   For any other task 'pause()' would mean we have to get a
 * signal to awaken, but task0 is the sole exception (see 'schedule()')
 * as task 0 gets activated at every idle moment (when no other tasks
 * can run). For task0 'pause()' just means we go check if some other
 * task can run, and if not we return here.
 */
// pause()意味我们必须等待收到一个信号才返回就绪状态，但任务0（task0）是唯一的例外，
// 任务0在认识空闲时间（没有其他任务）都会被激活
	for(;;) pause(); 	// pause() in kernel/sched.c 将任务0转换成可中断等待状态 再执行调度函数
						// 当调度函数发现系统中没有其他任务可以运行时，就会切换到任务0
}

// 格式化信息
static int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	write(1,printbuf,i=vsprintf(printbuf, fmt, args));
	va_end(args);
	return i;
}

// 读取并执行 /etc/rc 文件时所使用的命令行参数和环境参数
static char * argv_rc[] = { "/bin/sh", NULL };
static char * envp_rc[] = { "HOME=/", NULL };

static char * argv[] = { "-/bin/sh",NULL };
static char * envp[] = { "HOME=/usr/root", NULL };

// 在任务0第一次创建子进程（任务1）：shell
// 对shell的环境进行初始化，然后以登录shell方式加载该程序并执行  ??
void init(void)
{
	int pid,i;

	setup((void *) &drive_info);

	// 用只读方式打开 /dev/tty0：终端控制台 第一次打开文件
	(void) open("/dev/tty0",O_RDWR,0);	// open in "fcntl.h" 文件描述符为0 回忆CTRL-ALT-F1
	(void) dup(0); 	// 复制句柄0 stdout 变为句柄1 
	(void) dup(0);	// 复制句柄0 stderr 变为句柄2
	printf("%d buffers = %d bytes buffer space\n\r",NR_BUFFERS,
		NR_BUFFERS*BLOCK_SIZE);
	printf("Free mem: %d bytes\n\r",memory_end-main_memory_start);

	// 创建任务2：这里的sh是非交互式的执行完rc后就立即退出，进程2结束
	if (!(pid=fork())) {		// fork()在子进程为0 在父进程中为子进程的标识号pid
		close(0);		// 关闭句柄0 即stdin
		if (open("/etc/rc",O_RDONLY,0))	// 用只读方式打开 /etc/rc， 以上两步目的是stdin重定向到/etc/rc, 就可以让/bin/sh运行rc中指令了
			_exit(1);		// 文件打开失败则退出   操作未许可  lib/_exit.c  
		execve("/bin/sh",argv_rc,envp_rc);	// fs.exec.c 
		_exit(2);			// execve()失败则退出   文件或目录不存在
	}
	// 父进程执行的语句
	if (pid>0)	
		while (pid != wait(&i))	// &i 存放返回状态信息的位置 如果wait()返回值不等于子进程号就等待
			/* nothing */;
		


	while (1) {
		if ((pid=fork())<0) {
			printf("Fork failed in init\r\n");
			continue;
		}
		if (!pid) {			// 新的子进程
			close(0);close(1);close(2);
			setsid();		// 创建新的会话期
			(void) open("/dev/tty0",O_RDWR,0);
			(void) dup(0);
			(void) dup(0);
			_exit(execve("/bin/sh",argv,envp));
		}
		while (1)
			if (pid == wait(&i))
				break;
		printf("\n\rchild %d died with code %04x\n\r",pid,i);
		sync();			// 同步操作，刷新缓冲区
	}
	_exit(0);	/* NOTE! _exit, not exit() */
	// exit() 和 _exit() 都是正常终止一个函数
	// _exit()是sys_exit系统调用
	// 但 exit() 是普通函数库里的一个函数，有一些清除工作：调用执行各终止处理程序，关闭所有标准IO
	// 然后调用sys_exit
}
