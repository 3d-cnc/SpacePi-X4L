/* Register des GD32F303CBT6 (registergleich mit STM32F103, daher STM32-Namen).
 * Nur was die Firmware wirklich benutzt. */
#ifndef REGS_H
#define REGS_H
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))

/* Takt (RCU) */
#define RCC_CR       REG(0x40021000)
#define RCC_CFGR     REG(0x40021004)
#define RCC_APB2ENR  REG(0x40021018)
#define RCC_APB1ENR  REG(0x4002101C)
#define RCC_CSR      REG(0x40021024)
#define FLASH_ACR    REG(0x40022000)
#define FLASH_KEYR   REG(0x40022004)
#define FLASH_SR     REG(0x4002200C)
#define FLASH_CR     REG(0x40022010)
#define FLASH_AR     REG(0x40022014)

/* GPIO */
#define GPIOA 0x40010800u
#define GPIOB 0x40010C00u
#define GPIO_CRL(p)  REG((p) + 0x00)
#define GPIO_CRH(p)  REG((p) + 0x04)
#define GPIO_IDR(p)  REG((p) + 0x08)
#define GPIO_ODR(p)  REG((p) + 0x0C)
#define GPIO_BSRR(p) REG((p) + 0x10)
#define GPIO_BRR(p)  REG((p) + 0x14)
#define AFIO_MAPR    REG(0x40010004)

/* Pin-Modi (4 Bit je Pin: CNF[1:0] MODE[1:0]) */
#define M_ANALOG   0x0u   /* Analogeingang */
#define M_FLOAT    0x4u   /* Eingang ohne Pull */
#define M_PULL     0x8u   /* Eingang mit Pull (ODR: 1 = hoch, 0 = runter) */
#define M_OUT_PP   0x2u   /* Ausgang Push-Pull, 2 MHz */
#define M_OUT_OD   0x6u   /* Ausgang Open-Drain, 2 MHz */
#define M_AF_PP    0xBu   /* Alternativfunktion Push-Pull, 50 MHz */

/* USART0 (STM32: USART1) */
#define USART_SR   REG(0x40013800)
#define USART_DR   REG(0x40013804)
#define USART_BRR  REG(0x40013808)
#define USART_CR1  REG(0x4001380C)

/* ADC0 (STM32: ADC1) */
#define ADC_SR     REG(0x40012400)
#define ADC_CR1    REG(0x40012404)
#define ADC_CR2    REG(0x40012408)
#define ADC_SMPR2  REG(0x40012410)
#define ADC_SQR1   REG(0x4001242C)
#define ADC_SQR3   REG(0x40012434)
#define ADC_DR     REG(0x4001244C)

/* Unabhaengiger Watchdog (FWDGT / IWDG) */
#define IWDG_KR    REG(0x40003000)
#define IWDG_PR    REG(0x40003004)
#define IWDG_RLR   REG(0x40003008)
#define IWDG_SR    REG(0x4000300C)

/* Cortex-M */
#define SYST_CSR   REG(0xE000E010)
#define SYST_RVR   REG(0xE000E014)
#define SYST_CVR   REG(0xE000E018)
#define DEMCR      REG(0xE000EDFC)
#define DWT_CTRL   REG(0xE0001000)
#define DWT_CYCCNT REG(0xE0001004)

/* NVIC: Interrupt-Freigabe (ISER0 = IRQ 0..31, ISER1 = IRQ 32..63) */
#define NVIC_ISER1 REG(0xE000E104)
#define USART0_IRQ 37

#endif
