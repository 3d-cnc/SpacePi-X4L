/* Start: Vektortabelle, Reset-Handler, Fehler-Handler. */
#include <stdint.h>
#include "regs.h"

extern uint32_t _estack, _etext, _sdata, _edata, _sbss, _ebss;
extern int main(void);
void SysTick_Handler(void);

/* Heizung und Luefter aus - wird als Allererstes und in jedem Fehlerfall aufgerufen.
 * PA9 = Heizung rechts, PA10 = Heizung links, PB0 = Luefter links, PB1 = Luefter rechts:
 * alle vier fest auf Low. */
void ausgaenge_sicher(void)
{
    RCC_APB2ENR |= (1u << 2) | (1u << 3);               /* GPIOA, GPIOB */
    GPIO_BRR(GPIOB) = (1u << 0) | (1u << 1);
    GPIO_BRR(GPIOA) = (1u << 9) | (1u << 10);
    GPIO_CRL(GPIOB) = (GPIO_CRL(GPIOB) & ~0x000000FFu) | (M_OUT_PP << 0) | (M_OUT_PP << 4);
    GPIO_CRH(GPIOA) = (GPIO_CRH(GPIOA) & ~0x00000FF0u) | (M_OUT_PP << 4) | (M_OUT_PP << 8);
}

void Reset_Handler(void)
{
    ausgaenge_sicher();
    uint32_t *q = &_etext, *z = &_sdata;
    while (z < &_edata) *z++ = *q++;
    for (z = &_sbss; z < &_ebss; ) *z++ = 0;
    main();
    for (;;) ;
}

void Fehler_Handler(void)
{
    ausgaenge_sicher();
    for (;;) ;   /* Watchdog setzt zurueck */
}

void USART0_IRQHandler(void);

typedef void (*vektor_t)(void);
/* 16 Kern-Vektoren + Interrupts bis USART0 (STM32: USART1, IRQ 37). Nicht benutzte IRQs
 * sind nie freigegeben; falls doch einer kommt: Fehler_Handler -> Ausgaenge aus. */
__attribute__((section(".isr_vector"), used))
const vektor_t vektoren[16 + 38] = {
    [0]  = (vektor_t)&_estack,
    [1]  = Reset_Handler,
    [2]  = Fehler_Handler,   /* NMI */
    [3]  = Fehler_Handler,   /* HardFault */
    [4]  = Fehler_Handler,   /* MemManage */
    [5]  = Fehler_Handler,   /* BusFault */
    [6]  = Fehler_Handler,   /* UsageFault */
    [11] = Fehler_Handler,   /* SVC */
    [12] = Fehler_Handler,   /* DebugMon */
    [14] = Fehler_Handler,   /* PendSV */
    [15] = SysTick_Handler,
    [16 ... 16 + 36] = Fehler_Handler,
    [16 + 37] = USART0_IRQHandler,
};

/* Der Compiler darf memset/memcpy fuer Struktur-Kopien erzeugen. */
void *memset(void *d, int c, unsigned n) { uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = d; const uint8_t *q = s; while (n--) *p++ = *q++; return d; }
