/* Startup: vector table, reset handler, fault handler. */
#include <stdint.h>
#include "regs.h"

extern uint32_t _estack, _etext, _sdata, _edata, _sbss, _ebss;
extern int main(void);
void SysTick_Handler(void);

/* Heaters and fans off - called first thing after reset and in every fault.
 * PA9 = heater right, PA10 = heater left, PB0 = fan left, PB1 = fan right:
 * all four forced low. */
void outputs_safe(void)
{
    RCC_APB2ENR |= (1u << 2) | (1u << 3);               /* GPIOA, GPIOB */
    GPIO_BRR(GPIOB) = (1u << 0) | (1u << 1);
    GPIO_BRR(GPIOA) = (1u << 9) | (1u << 10);
    GPIO_CRL(GPIOB) = (GPIO_CRL(GPIOB) & ~0x000000FFu) | (M_OUT_PP << 0) | (M_OUT_PP << 4);
    GPIO_CRH(GPIOA) = (GPIO_CRH(GPIOA) & ~0x00000FF0u) | (M_OUT_PP << 4) | (M_OUT_PP << 8);
}

void Reset_Handler(void)
{
    outputs_safe();
    uint32_t *src = &_etext, *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;
    main();
    for (;;) ;
}

void Fault_Handler(void)
{
    outputs_safe();
    for (;;) ;   /* the watchdog resets the chip */
}

void USART0_IRQHandler(void);

typedef void (*vector_t)(void);
/* 16 core vectors + interrupts up to USART0 (STM32: USART1, IRQ 37). Unused IRQs are never
 * enabled; should one fire anyway: Fault_Handler -> outputs off. */
__attribute__((section(".isr_vector"), used))
const vector_t vectors[16 + 38] = {
    [0]  = (vector_t)&_estack,
    [1]  = Reset_Handler,
    [2]  = Fault_Handler,   /* NMI */
    [3]  = Fault_Handler,   /* HardFault */
    [4]  = Fault_Handler,   /* MemManage */
    [5]  = Fault_Handler,   /* BusFault */
    [6]  = Fault_Handler,   /* UsageFault */
    [11] = Fault_Handler,   /* SVC */
    [12] = Fault_Handler,   /* DebugMon */
    [14] = Fault_Handler,   /* PendSV */
    [15] = SysTick_Handler,
    [16 ... 16 + 36] = Fault_Handler,
    [16 + 37] = USART0_IRQHandler,
};

/* The compiler may emit memset/memcpy for struct copies. */
void *memset(void *d, int c, unsigned n) { uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = d; const uint8_t *q = s; while (n--) *p++ = *q++; return d; }
