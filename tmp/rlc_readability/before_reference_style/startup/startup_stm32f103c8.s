; STM32F103C8 启动入口。__Vectors 位于 Flash 起点 0x08000000。
; 栈与堆由链接器放入 SRAM。初始栈顶是 __initial_sp，SysTick 向量转到板级计时模块。
; Reset_Handler 调用 ARM C 运行时，随后进入 USER/main.c。本模块不写 OLED。

                PRESERVE8
                THUMB

STACK_SIZE      EQU     0x00000800
HEAP_SIZE       EQU     0x00000100

                AREA    STACK, NOINIT, READWRITE, ALIGN=3
Stack_Mem       SPACE   STACK_SIZE
__initial_sp

                AREA    HEAP, NOINIT, READWRITE, ALIGN=3
__heap_base
Heap_Mem        SPACE   HEAP_SIZE
__heap_limit

                AREA    RESET, DATA, READONLY, ALIGN=8
                IMPORT  SysTick_Handler
                EXPORT  __Vectors
                EXPORT  __Vectors_End
                EXPORT  __Vectors_Size

__Vectors       DCD     __initial_sp
                DCD     Reset_Handler
                DCD     Default_Handler       ; NMI
                DCD     Default_Handler       ; HardFault
                DCD     Default_Handler       ; MemManage
                DCD     Default_Handler       ; BusFault
                DCD     Default_Handler       ; UsageFault
                DCD     0, 0, 0, 0
                DCD     Default_Handler       ; SVCall
                DCD     Default_Handler       ; DebugMonitor
                DCD     0
                DCD     Default_Handler       ; PendSV
                DCD     SysTick_Handler
                ; 本版仅使用 SysTick 中断，DMA 完成状态由采集模块轮询。
                GBLA    irq_index
irq_index       SETA    0
                WHILE   irq_index < 43
                DCD     Default_Handler
irq_index       SETA    irq_index + 1
                WEND
__Vectors_End
__Vectors_Size  EQU     __Vectors_End - __Vectors

                AREA    |.text|, CODE, READONLY
                IMPORT  __main
                IMPORT  __use_no_semihosting
                EXPORT  Reset_Handler
                EXPORT  _sys_exit

Reset_Handler   PROC
                LDR     r0, =0xE000ED08       ; SCB VTOR
                LDR     r1, =__Vectors
                STR     r1, [r0]
                DSB
                ISB
                LDR     r0, =__main
                BX      r0
                ENDP

Default_Handler PROC
                B       Default_Handler
                ENDP

_sys_exit       PROC
                B       _sys_exit
                ENDP

                IMPORT  __use_two_region_memory
                EXPORT  __user_initial_stackheap
__user_initial_stackheap PROC
                LDR     r0, =__heap_base
                LDR     r1, =__initial_sp
                LDR     r2, =__heap_limit
                LDR     r3, =Stack_Mem
                BX      lr
                ENDP

                END
