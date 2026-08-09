#include "stm32f103xb.h"
#include "system_stm32f1xx.h"

#define HSE_VALUE           8000000U    /* External oscillator 8MHz */
#define HSE_STARTUP_TIMEOUT 0x5000U     /* HSE startup timeout */

/* System core clock frequency */
uint32_t SystemCoreClock = 72000000U;

/**
  * @brief  System Clock Configuration
  *         - HSE: 8 MHz
  *         - SYSCLK: 72 MHz (HSE * 9 via PLL)
  *         - HCLK: 72 MHz  
  *         - PCLK1: 36 MHz (APB1, max 36MHz)
  *         - PCLK2: 72 MHz (APB2)
  */
void SystemInit(void) {
    __IO uint32_t startup_counter = 0;
    __IO uint32_t hse_status = 0;
    
    /* Reset RCC clock configuration to default state */
    /* Set HSION bit */
    RCC->CR |= RCC_CR_HSION;
    
    /* Reset SW, HPRE, PPRE1, PPRE2, ADCPRE and MCO bits */
    RCC->CFGR &= 0xF0FF0000U;
    
    /* Reset HSEON, CSSON and PLLON bits */
    RCC->CR &= 0xFEF6FFFFU;
    
    /* Reset HSEBYP bit */
    RCC->CR &= 0xFFFBFFFFU;
    
    /* Reset PLLSRC, PLLXTPRE, PLLMUL and USBPRE bits */
    RCC->CFGR &= 0xFF80FFFFU;
    
    /* Disable all interrupts and clear pending bits */
    RCC->CIR = 0x009F0000U;
    
    /* Configure the System clock to 72 MHz */
    /* Enable HSE */
    RCC->CR |= RCC_CR_HSEON;
    
    /* Wait until HSE is ready or timeout */
    do {
        hse_status = RCC->CR & RCC_CR_HSERDY;
        startup_counter++;
    } while ((hse_status == 0) && (startup_counter != HSE_STARTUP_TIMEOUT));
    
    if ((RCC->CR & RCC_CR_HSERDY) != 0) {
        hse_status = 0x01;
    } else {
        hse_status = 0x00;
    }
    
    if (hse_status == 0x01) {
        /* Enable Prefetch Buffer */
        FLASH->ACR |= FLASH_ACR_PRFTBE;
        
        /* Flash 2 wait states (required for 48 < SYSCLK ≤ 72 MHz) */
        FLASH->ACR &= ~FLASH_ACR_LATENCY;
        FLASH->ACR |= FLASH_ACR_LATENCY_2;
        
        /* HCLK = SYSCLK (AHB prescaler = /1) */
        RCC->CFGR |= RCC_CFGR_HPRE_DIV1;
        
        /* PCLK2 = HCLK (APB2 prescaler = /1, 72 MHz) */
        RCC->CFGR |= RCC_CFGR_PPRE2_DIV1;
        
        /* PCLK1 = HCLK/2 (APB1 prescaler = /2, 36 MHz) */
        RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;
        
        /* ADC prescaler = PCLK2/6 (12 MHz for ADC, max 14 MHz) */
        RCC->CFGR |= RCC_CFGR_ADCPRE_DIV6;
        
        /* USB prescaler = PLL/1.5 (48 MHz for USB) */
        RCC->CFGR &= ~RCC_CFGR_USBPRE;
        
        /* PLL configuration: PLLCLK = HSE * 9 = 72 MHz */
        /* PLLSRC = HSE (bit 16 = 1) */
        /* PLLMUL = 9 (bits 18-21 = 0111) */
        RCC->CFGR &= ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL);
        RCC->CFGR |= (RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9);
        
        /* Enable PLL */
        RCC->CR |= RCC_CR_PLLON;
        
        /* Wait until PLL is ready */
        while ((RCC->CR & RCC_CR_PLLRDY) == 0) {
            /* Wait */
        }
        
        /* Select PLL as system clock source */
        RCC->CFGR &= ~RCC_CFGR_SW;
        RCC->CFGR |= RCC_CFGR_SW_PLL;
        
        /* Wait until PLL is used as system clock source */
        while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
            /* Wait */
        }
    } else {
        /* If HSE fails to start, use HSI (8 MHz) */
        SystemCoreClock = 8000000U;
    }
}

/**
  * @brief  Update SystemCoreClock variable
  */
void SystemCoreClockUpdate(void) {
    uint32_t tmp = 0, pllmull = 0, pllsource = 0;
    
    /* Get SYSCLK source */
    tmp = RCC->CFGR & RCC_CFGR_SWS;
    
    switch (tmp) {
        case RCC_CFGR_SWS_HSI:  /* HSI used as system clock */
            SystemCoreClock = 8000000U;
            break;
            
        case RCC_CFGR_SWS_HSE:  /* HSE used as system clock */
            SystemCoreClock = HSE_VALUE;
            break;
            
        case RCC_CFGR_SWS_PLL:  /* PLL used as system clock */
            /* Get PLL clock source and multiplication factor */
            pllmull = RCC->CFGR & RCC_CFGR_PLLMULL;
            pllsource = RCC->CFGR & RCC_CFGR_PLLSRC;
            
            /* Get PLL multiplication factor */
            pllmull = (pllmull >> 18) + 2;
            if (pllmull > 16) pllmull = 16;
            
            if (pllsource == 0) {
                /* HSI oscillator clock divided by 2 selected as PLL clock entry */
                SystemCoreClock = (8000000U >> 1) * pllmull;
            } else {
                /* HSE selected as PLL clock entry */
                if ((RCC->CFGR & RCC_CFGR_PLLXTPRE) != 0) {
                    /* HSE oscillator clock divided by 2 */
                    SystemCoreClock = (HSE_VALUE >> 1) * pllmull;
                } else {
                    SystemCoreClock = HSE_VALUE * pllmull;
                }
            }
            break;
            
        default:
            SystemCoreClock = 8000000U;
            break;
    }
}
