#ifndef MY_SYSCALL_HPP
#define MY_SYSCALL_HPP

#include <errno.h>

#define _SYSCALL_BASE_CLOBBERS "cc", "rcx", "r11", "memory"

#define _SYSCALL_ERROR_PROCESS(ret) \
    if (ret >= -4095 && ret <= -1) { \
        errno = -ret; \
        ret = -1; \
    }


#define SYSCALL0(num) \
    ({ \
        long ret; \
        asm volatile("syscall" \
                     : "=a"(ret) /* rax is output */ \
                     : "a"(num)  /* rax is also input */ \
                     : _SYSCALL_BASE_CLOBBERS); \
        _SYSCALL_ERROR_PROCESS(ret); \
        ret; \
    })

#define SYSCALL1(num, arg1) \
    ({ \
        long ret; \
        asm volatile("syscall" \
                     : "=a"(ret) \
                     : "a"(num), "D"((long)arg1) \
                     : _SYSCALL_BASE_CLOBBERS); /* rdi is an input, not clobbered */ \
        _SYSCALL_ERROR_PROCESS(ret); \
        ret; \
    })
    
#define SYSCALL2(num, arg1, arg2) \
	({	\
		long ret; \
		asm volatile("syscall" \
                     : "=a"(ret) \
                     : "a"(num),"D"((long)arg1),"S"((long)arg2) \
                     : _SYSCALL_BASE_CLOBBERS); /* rdi, rsi are inputs */ \
		_SYSCALL_ERROR_PROCESS(ret); \
		ret; \
	})
	
#define SYSCALL3(num, arg1, arg2, arg3) \
	({	\
		long ret; \
		asm volatile("syscall" \
                     : "=a"(ret) \
                     : "a"(num),"D"((long)arg1),"S"((long)arg2),"d"((long)arg3) \
                     : _SYSCALL_BASE_CLOBBERS); /* rdi, rsi, rdx are inputs */ \
		_SYSCALL_ERROR_PROCESS(ret); \
		ret; \
	})

#define SYSCALL4(num, arg1, arg2, arg3, arg4) \
	({	\
		long ret; \
        register long r10 asm("r10") = (long)arg4; \
		asm volatile("syscall" \
                     : "=a"(ret) \
                     : "a"(num),"D"((long)arg1),"S"((long)arg2),"d"((long)arg3),"r"(r10) \
                     : _SYSCALL_BASE_CLOBBERS); /* r10 is an input */ \
		_SYSCALL_ERROR_PROCESS(ret); \
		ret; \
	})

#define SYSCALL5(num, arg1, arg2, arg3, arg4, arg5) \
	({	\
		long ret; \
        register long r10 asm("r10") = (long)arg4; \
        register long r8 asm("r8") = (long)arg5; \
		asm volatile("syscall" \
                     : "=a"(ret) \
                     : "a"(num),"D"((long)arg1),"S"((long)arg2),"d"((long)arg3),"r"(r10),"r"(r8) \
                     : _SYSCALL_BASE_CLOBBERS); /* r10, r8 are inputs */ \
		_SYSCALL_ERROR_PROCESS(ret); \
		ret; \
	})

#define SYSCALL6(num, arg1, arg2, arg3, arg4, arg5, arg6) \
	({	\
		long ret; \
        register long r10 asm("r10") = (long)arg4; \
        register long r8 asm("r8") = (long)arg5; \
        register long r9 asm("r9") = (long)arg6; \
		asm volatile("syscall" \
                     : "=a"(ret) \
                     : "a"(num),"D"((long)arg1),"S"((long)arg2),"d"((long)arg3),"r"(r10),"r"(r8),"r"(r9) \
                     : _SYSCALL_BASE_CLOBBERS); /* r10, r8, r9 are inputs */ \
		_SYSCALL_ERROR_PROCESS(ret); \
		ret; \
	})

#endif // MY_SYSCALL_HPP