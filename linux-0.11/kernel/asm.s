/*
 *  linux/kernel/asm.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * asm.s contains the low-level code for most hardware faults.
 * page_exception is handled by the mm, so that isn't here. This
 * file also handles (hopefully) fpu-exceptions due to TS-bit, as
 * the fpu must be properly saved/resored. This hasn't been tested.
 */

// 对Intel保留中断 int0-int16 的处理
// 全局函数名声明，原型在traps.c中
.globl _divide_error,_debug,_nmi,_int3,_overflow,_bounds,_invalid_op
.globl _double_fault,_coprocessor_segment_overrun
.globl _invalid_TSS,_segment_not_present,_stack_segment
.globl _general_protection,_coprocessor_error,_irq13,_reserved

// 下面这段程序处理无出错号的情况
// int0 --- 处理被0除 出错的情况，类型：错误（Fault） 错误号：无
// 执行DIV或者IDIV时，若除数为0,CPU产生这个异常，当EAX（或者AX，AL）容纳不了
// 一个合法除操作的结果也会产生一个异常
// 那个 $_do_divide_error 实际上是一个C语言函数do_divide_error()编译后生成模块中对应的名称
// 函数'do_divide_error'在traps.c中实现
_divide_error:
	pushl $_do_divide_error		// 首先将要调用的函数地址入栈
no_error_code:					// 无出错号处理的入口处
	xchgl %eax,(%esp)			// _do_divide_error的地址 -> eax  ： eax被交换入栈
	pushl %ebx
	pushl %ecx
	pushl %edx
	pushl %edi
	pushl %esi
	pushl %ebp
	push %ds					// ！！！16位的段寄存器入栈后也要占用4个字节
	push %es
	push %fs
	pushl $0		# "error code" 	// 将数值0作为出错码入栈
	lea 44(%esp),%edx
	pushl %edx
	movl $0x10,%edx
	mov %dx,%ds
	mov %dx,%es
	mov %dx,%fs
//
//
//
//
	call *%eax
	addl $8,%esp
	pop %fs
	pop %es
	pop %ds
	popl %ebp
	popl %esi
	popl %edi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax					// 弹出原来的eax中的内容
	iret

// int1 --- debug调试中断入口点，处理过程同上，类型：错误/陷阱（Falut/Trap），错误号：无
// 当EFLAGS中TF标志置位时引发的中断。当发现硬件断点或者开启了指令跟踪陷阱或者任务交换陷阱，或者
// 调试寄存器访问无效（错误），CPU会产生该异常 
_debug:
	pushl $_do_int3		# _do_debug
	jmp no_error_code

// int2
_nmi:
	pushl $_do_nmi
	jmp no_error_code

// int3
_int3:
	pushl $_do_int3
	jmp no_error_code

// int4
_overflow:
	pushl $_do_overflow
	jmp no_error_code

// int5
_bounds:
	pushl $_do_bounds
	jmp no_error_code

// int6
_invalid_op:
	pushl $_do_invalid_op
	jmp no_error_code

// int9
_coprocessor_segment_overrun:
	pushl $_do_coprocessor_segment_overrun
	jmp no_error_code

// int 15
_reserved:
	pushl $_do_reserved
	jmp no_error_code

// int45
_irq13:
	pushl %eax
	xorb %al,%al
	outb %al,$0xF0
	movb $0x20,%al
	outb %al,$0x20
	jmp 1f
1:	jmp 1f
1:	outb %al,$0xA0
	popl %eax
	jmp _coprocessor_error

// 以下中断在调用时CPU会在中断返回地址之后将出错号压入堆栈，因此返回时将出错号弹出

// int8
_double_fault:
	pushl $_do_double_fault
error_code:
	xchgl %eax,4(%esp)		# error code <-> %eax
	xchgl %ebx,(%esp)		# &function <-> %ebx
	pushl %ecx
	pushl %edx
	pushl %edi
	pushl %esi
	pushl %ebp
	push %ds
	push %es
	push %fs
	pushl %eax			# error code
	lea 44(%esp),%eax		# offset
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	call *%ebx
	addl $8,%esp
	pop %fs
	pop %es
	pop %ds
	popl %ebp
	popl %esi
	popl %edi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax
	iret

// int10
_invalid_TSS:
	pushl $_do_invalid_TSS
	jmp error_code

// int11
_segment_not_present:
	pushl $_do_segment_not_present
	jmp error_code

// int12
_stack_segment:
	pushl $_do_stack_segment
	jmp error_code

// int13
_general_protection:
	pushl $_do_general_protection
	jmp error_code

