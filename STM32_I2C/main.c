#include "stm32f103xb.h"
#include <stdint.h>
#include <string.h>

/* =========================================================
 * DEVICE ADDRESS
 * ========================================================= */

#define LCD_ADDR 0x27u
#define MPU6050_ADDR 0x68u

/* =========================================================
 * MPU6050 REGISTER
 * ========================================================= */

#define MPU_PWR_MGMT_1 0x6Bu
#define MPU_WHO_AM_I 0x75u
#define MPU_ACCEL_X_H 0x3Bu
#define MPU_ACCEL_X_L 0x3Cu

/* =========================================================
 * LCD PCF8574
 * ========================================================= */

#define LCD_RS 0x01u
#define LCD_EN 0x04u
#define LCD_BACKLIGHT 0x08u

/* =========================================================
 * UART
 * ========================================================= */

#define UART_BAUDRATE 115200u
#define UART_RX_BUFFER_SIZE 64u

/* =========================================================
 * AX
 * ========================================================= */

#define AX_UPDATE_PERIOD_MS 500u

/* =========================================================
 * GLOBAL VARIABLES
 * ========================================================= */

static volatile uint32_t g_ms = 0u;

/* AX stream */
static volatile uint8_t g_ax_stream = 0u;

/* Báo main rằng STOP vừa được bắt bởi UART IRQ */
static volatile uint8_t g_stop_event = 0u;

/* State machine nhận S -> T -> O -> P */
static volatile uint8_t g_stop_state = 0u;

/* UART RX Ring Buffer */
static volatile char uart_rx_buffer[UART_RX_BUFFER_SIZE];

static volatile uint8_t uart_rx_head = 0u;
static volatile uint8_t uart_rx_tail = 0u;

/* =========================================================
 * SYSTICK IRQ
 * ========================================================= */

void SysTick_Handler(void)
{
    g_ms++;
}

/* =========================================================
 * CLOCK
 * ========================================================= */

static uint32_t Get_PCLK1(void)
{
    uint32_t ppre1;

    SystemCoreClockUpdate();

    ppre1 = (RCC->CFGR >> 8) & 0x07u;

    if (ppre1 < 4u)
    {
        return SystemCoreClock;
    }

    if (ppre1 == 4u)
    {
        return SystemCoreClock / 2u;
    }

    if (ppre1 == 5u)
    {
        return SystemCoreClock / 4u;
    }

    if (ppre1 == 6u)
    {
        return SystemCoreClock / 8u;
    }

    return SystemCoreClock / 16u;
}

static uint32_t Get_PCLK2(void)
{
    uint32_t ppre2;

    SystemCoreClockUpdate();

    ppre2 = (RCC->CFGR >> 11) & 0x07u;

    if (ppre2 < 4u)
    {
        return SystemCoreClock;
    }

    if (ppre2 == 4u)
    {
        return SystemCoreClock / 2u;
    }

    if (ppre2 == 5u)
    {
        return SystemCoreClock / 4u;
    }

    if (ppre2 == 6u)
    {
        return SystemCoreClock / 8u;
    }

    return SystemCoreClock / 16u;
}

/* =========================================================
 * DELAY
 * ========================================================= */

static void Delay_ms(uint32_t ms)
{
    volatile uint32_t i;
    uint32_t cycles;

    SystemCoreClockUpdate();

    cycles = SystemCoreClock / 4000u;

    while (ms > 0u)
    {
        for (i = 0u; i < cycles; i++)
        {
            __NOP();
        }

        ms--;
    }
}

/* =========================================================
 * UART1
 *
 * PA9  = TX
 * PA10 = RX
 * ========================================================= */

static void UART1_Init(void)
{
    uint32_t pclk2;

    /* GPIOA clock */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    /* USART1 clock */
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    /* =====================================================
     * PA9 = TX
     * Alternate Function Push Pull
     * ===================================================== */

    GPIOA->CRH &=
        ~(GPIO_CRH_MODE9 |
          GPIO_CRH_CNF9);

    GPIOA->CRH |=
        GPIO_CRH_MODE9 |
        GPIO_CRH_CNF9_1;

    /* =====================================================
     * PA10 = RX
     * Floating Input
     * ===================================================== */

    GPIOA->CRH &=
        ~(GPIO_CRH_MODE10 |
          GPIO_CRH_CNF10);

    GPIOA->CRH |=
        GPIO_CRH_CNF10_0;

    /* =====================================================
     * Baudrate
     * ===================================================== */

    pclk2 = Get_PCLK2();

    USART1->BRR =
        (pclk2 + (UART_BAUDRATE / 2u)) / UART_BAUDRATE;

    /* =====================================================
     * Enable:
     *
     * TX
     * RX
     * RX Interrupt
     * ===================================================== */

    USART1->CR1 =
        USART_CR1_TE |
        USART_CR1_RE |
        USART_CR1_RXNEIE;

    /* Enable USART */
    USART1->CR1 |= USART_CR1_UE;

    /* =====================================================
     * NVIC
     *
     * UART RX interrupt ưu tiên cao
     * ===================================================== */

    NVIC_SetPriority(
        USART1_IRQn,
        0u);

    NVIC_EnableIRQ(
        USART1_IRQn);
}

/* =========================================================
 * UART SEND CHAR
 * ========================================================= */

static void UART1_SendChar(char c)
{
    while ((USART1->SR & USART_SR_TXE) == 0u)
    {
    }

    USART1->DR = (uint8_t)c;
}

/* =========================================================
 * UART SEND STRING
 * ========================================================= */

static void UART1_SendString(const char *str)
{
    while (*str != '\0')
    {
        UART1_SendChar(*str);

        str++;
    }
}

/* =========================================================
 * UART HEX
 * ========================================================= */

static void UART1_PrintHex8(uint8_t value)
{
    static const char hex[] =
        "0123456789ABCDEF";

    UART1_SendChar(
        hex[(value >> 4) & 0x0Fu]);

    UART1_SendChar(
        hex[value & 0x0Fu]);
}

/* =========================================================
 * UART INTEGER
 * ========================================================= */

static void UART1_PrintInt(int16_t value)
{
    char buffer[8];

    uint8_t index = 0u;

    int32_t number;

    number = value;

    if (number < 0)
    {
        UART1_SendChar('-');

        number = -number;
    }

    if (number == 0)
    {
        UART1_SendChar('0');

        return;
    }

    while (number > 0)
    {
        buffer[index++] =
            (char)('0' +
                   (number % 10));

        number /= 10;
    }

    while (index > 0u)
    {
        index--;

        UART1_SendChar(
            buffer[index]);
    }
}

/* =========================================================
 * UART RX INTERRUPT
 *
 * CỰC KỲ QUAN TRỌNG
 *
 * STOP được nhận ngay tại interrupt.
 *
 * Không cần ENTER.
 * ========================================================= */

void USART1_IRQHandler(void)
{
    char c;
    uint8_t next;

    /* =====================================================
     * RXNE
     * ===================================================== */

    if ((USART1->SR & USART_SR_RXNE) != 0u)
    {
        /*
         * Đọc DR
         * -> RXNE được clear.
         */
        c =
            (char)(USART1->DR &
                   0xFFu);

        /* =================================================
         * lowercase -> uppercase
         * ================================================= */

        if ((c >= 'a') &&
            (c <= 'z'))
        {
            c =
                (char)(c - 'a' + 'A');
        }

        /* =================================================
         * STOP STATE MACHINE
         *
         * S -> T -> O -> P
         *
         * Không cần ENTER.
         * ================================================= */

        switch (g_stop_state)
        {
            /* =============================================
             * Wait S
             * ============================================= */

        case 0u:
        {
            if (c == 'S')
            {
                g_stop_state = 1u;
            }

            break;
        }

            /* =============================================
             * Wait T
             * ============================================= */

        case 1u:
        {
            if (c == 'T')
            {
                g_stop_state = 2u;
            }
            else if (c == 'S')
            {
                g_stop_state = 1u;
            }
            else
            {
                g_stop_state = 0u;
            }

            break;
        }

            /* =============================================
             * Wait O
             * ============================================= */

        case 2u:
        {
            if (c == 'O')
            {
                g_stop_state = 3u;
            }
            else if (c == 'S')
            {
                g_stop_state = 1u;
            }
            else
            {
                g_stop_state = 0u;
            }

            break;
        }

            /* =============================================
             * Wait P
             * ============================================= */

        case 3u:
        {
            if (c == 'P')
            {
                /*
                 * =========================
                 * STOP AX NGAY
                 * =========================
                 */

                g_ax_stream = 0u;

                /*
                 * Báo main
                 */
                g_stop_event = 1u;

                /*
                 * Reset detector
                 */
                g_stop_state = 0u;
            }
            else if (c == 'S')
            {
                g_stop_state = 1u;
            }
            else
            {
                g_stop_state = 0u;
            }

            break;
        }

        default:
        {
            g_stop_state = 0u;

            break;
        }
        }

        /* =================================================
         * STORE RX DATA
         *
         * HELP
         * AX
         * SCAN
         * MPU
         * LCD HELLO
         * ================================================= */

        next =
            (uint8_t)((uart_rx_head + 1u) %
                      UART_RX_BUFFER_SIZE);

        if (next != uart_rx_tail)
        {
            uart_rx_buffer[uart_rx_head] = c;

            uart_rx_head = next;
        }
    }
}

/* =========================================================
 * READ CHAR FROM RX BUFFER
 *
 * NON-BLOCKING
 * ========================================================= */

static uint8_t UART1_ReadChar(char *c)
{
    if (uart_rx_head ==
        uart_rx_tail)
    {
        return 0u;
    }

    *c =
        uart_rx_buffer[uart_rx_tail];

    uart_rx_tail =
        (uint8_t)((uart_rx_tail + 1u) %
                  UART_RX_BUFFER_SIZE);

    return 1u;
}

/* =========================================================
 * I2C GPIO
 * ========================================================= */

static void I2C_GPIO_Init(void)
{
    RCC->APB2ENR |=
        RCC_APB2ENR_IOPBEN;

    /* =====================================================
     * I2C1
     *
     * PB6 = SCL
     * PB7 = SDA
     *
     * AF Open Drain
     * ===================================================== */

    GPIOB->CRL &=
        ~(GPIO_CRL_MODE6 |
          GPIO_CRL_CNF6 |
          GPIO_CRL_MODE7 |
          GPIO_CRL_CNF7);

    GPIOB->CRL |=
        GPIO_CRL_MODE6 |
        GPIO_CRL_CNF6 |
        GPIO_CRL_MODE7 |
        GPIO_CRL_CNF7;

    /* =====================================================
     * I2C2
     *
     * PB10 = SCL
     * PB11 = SDA
     *
     * AF Open Drain
     * ===================================================== */

    GPIOB->CRH &=
        ~(GPIO_CRH_MODE10 |
          GPIO_CRH_CNF10 |
          GPIO_CRH_MODE11 |
          GPIO_CRH_CNF11);

    GPIOB->CRH |=
        GPIO_CRH_MODE10 |
        GPIO_CRH_CNF10 |
        GPIO_CRH_MODE11 |
        GPIO_CRH_CNF11;
}

/* =========================================================
 * I2C CONFIG
 *
 * 100 kHz
 * ========================================================= */

static void I2C_Config(I2C_TypeDef *I2Cx)
{
    uint32_t pclk1;
    uint32_t freq_mhz;
    uint32_t ccr;

    pclk1 = Get_PCLK1();

    freq_mhz =
        pclk1 /
        1000000u;

    I2Cx->CR2 =
        freq_mhz;

    ccr =
        pclk1 /
        (2u * 100000u);

    if (ccr < 4u)
    {
        ccr = 4u;
    }

    I2Cx->CCR =
        ccr;

    I2Cx->TRISE =
        freq_mhz + 1u;

    /* ACK default */
    I2Cx->CR1 |=
        I2C_CR1_ACK;

    /* Enable */
    I2Cx->CR1 |=
        I2C_CR1_PE;
}

/* =========================================================
 * I2C1 INIT
 * ========================================================= */

static void I2C1_Init(void)
{
    RCC->APB1ENR |=
        RCC_APB1ENR_I2C1EN;

    RCC->APB1RSTR |=
        RCC_APB1RSTR_I2C1RST;

    RCC->APB1RSTR &=
        ~RCC_APB1RSTR_I2C1RST;

    I2C_Config(
        I2C1);
}

/* =========================================================
 * I2C2 INIT
 * ========================================================= */

static void I2C2_Init(void)
{
    RCC->APB1ENR |=
        RCC_APB1ENR_I2C2EN;

    RCC->APB1RSTR |=
        RCC_APB1RSTR_I2C2RST;

    RCC->APB1RSTR &=
        ~RCC_APB1RSTR_I2C2RST;

    I2C_Config(
        I2C2);
}

/* =========================================================
 * CHECK ADDRESS
 * ========================================================= */

static uint8_t I2C_CheckAddress(
    I2C_TypeDef *I2Cx,
    uint8_t address)
{
    uint32_t timeout;

    I2Cx->SR1 &=
        ~I2C_SR1_AF;

    /* START */
    I2Cx->CR1 |=
        I2C_CR1_START;

    timeout =
        100000u;

    while ((I2Cx->SR1 &
            I2C_SR1_SB) == 0u)
    {
        if (--timeout == 0u)
        {
            I2Cx->CR1 |=
                I2C_CR1_STOP;

            return 0u;
        }
    }

    /* Address Write */
    I2Cx->DR =
        (uint8_t)(address << 1);

    timeout =
        100000u;

    while (timeout > 0u)
    {
        /* ACK */
        if ((I2Cx->SR1 &
             I2C_SR1_ADDR) != 0u)
        {
            (void)I2Cx->SR1;
            (void)I2Cx->SR2;

            I2Cx->CR1 |=
                I2C_CR1_STOP;

            return 1u;
        }

        /* NACK */
        if ((I2Cx->SR1 &
             I2C_SR1_AF) != 0u)
        {
            I2Cx->SR1 &=
                ~I2C_SR1_AF;

            I2Cx->CR1 |=
                I2C_CR1_STOP;

            return 0u;
        }

        timeout--;
    }

    I2Cx->CR1 |=
        I2C_CR1_STOP;

    return 0u;
}

/* =========================================================
 * I2C SCANNER
 * ========================================================= */

static void I2C_Scan(
    I2C_TypeDef *I2Cx,
    const char *name)
{
    uint8_t address;
    uint8_t found = 0u;

    UART1_SendString(
        "\r\n------------------------\r\n");

    UART1_SendString(
        "Scanning ");

    UART1_SendString(
        name);

    UART1_SendString(
        "\r\n");

    for (address = 0x08u;
         address <= 0x77u;
         address++)
    {
        if (I2C_CheckAddress(
                I2Cx,
                address) != 0u)
        {
            UART1_SendString(
                "Found: 0x");

            UART1_PrintHex8(
                address);

            UART1_SendString(
                "\r\n");

            found = 1u;
        }
    }

    if (found == 0u)
    {
        UART1_SendString(
            "No device found\r\n");
    }

    UART1_SendString(
        "Scan complete\r\n");
}

/* =========================================================
 * I2C WRITE BYTE
 *
 * LCD PCF8574
 * ========================================================= */

static uint8_t I2C_WriteByte(
    I2C_TypeDef *I2Cx,
    uint8_t address,
    uint8_t data)
{
    uint32_t timeout;

    /* START */
    I2Cx->CR1 |=
        I2C_CR1_START;

    timeout =
        100000u;

    while ((I2Cx->SR1 &
            I2C_SR1_SB) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    /* Address write */
    I2Cx->DR =
        (uint8_t)(address << 1);

    timeout =
        100000u;

    while ((I2Cx->SR1 &
            I2C_SR1_ADDR) == 0u)
    {
        if ((I2Cx->SR1 &
             I2C_SR1_AF) != 0u)
        {
            I2Cx->SR1 &=
                ~I2C_SR1_AF;

            I2Cx->CR1 |=
                I2C_CR1_STOP;

            return 0u;
        }

        if (--timeout == 0u)
        {
            I2Cx->CR1 |=
                I2C_CR1_STOP;

            return 0u;
        }
    }

    /* Clear ADDR */
    (void)I2Cx->SR1;
    (void)I2Cx->SR2;

    /* TXE */
    timeout =
        100000u;

    while ((I2Cx->SR1 &
            I2C_SR1_TXE) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    I2Cx->DR =
        data;

    /* BTF */
    timeout =
        100000u;

    while ((I2Cx->SR1 &
            I2C_SR1_BTF) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    I2Cx->CR1 |=
        I2C_CR1_STOP;

    return 1u;
}

/* =========================================================
 * I2C WRITE REGISTER
 * ========================================================= */

static uint8_t I2C_WriteRegister(
    I2C_TypeDef *I2Cx,
    uint8_t address,
    uint8_t reg,
    uint8_t data)
{
    uint32_t timeout;

    /* START */
    I2Cx->CR1 |=
        I2C_CR1_START;

    timeout =
        100000u;

    while ((I2Cx->SR1 &
            I2C_SR1_SB) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    /* ADDRESS WRITE */
    I2Cx->DR =
        (uint8_t)(address << 1);

    timeout =
        100000u;

    while ((I2Cx->SR1 &
            I2C_SR1_ADDR) == 0u)
    {
        if ((I2Cx->SR1 &
             I2C_SR1_AF) != 0u)
        {
            I2Cx->SR1 &=
                ~I2C_SR1_AF;

            I2Cx->CR1 |=
                I2C_CR1_STOP;

            return 0u;
        }

        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    /* Clear ADDR */
    (void)I2Cx->SR1;
    (void)I2Cx->SR2;

    /* Register */
    timeout =
        100000u;

    while ((I2Cx->SR1 &
            I2C_SR1_TXE) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    I2Cx->DR =
        reg;

    /* Data */
    timeout =
        100000u;

    while ((I2Cx->SR1 &
            I2C_SR1_TXE) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    I2Cx->DR =
        data;

    /* BTF */
    timeout =
        100000u;

    while ((I2Cx->SR1 &
            I2C_SR1_BTF) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    I2Cx->CR1 |=
        I2C_CR1_STOP;

    return 1u;
}

/* =========================================================
 * I2C READ REGISTER
 * ========================================================= */

static uint8_t I2C_ReadRegister(
    I2C_TypeDef *I2Cx,
    uint8_t address,
    uint8_t reg)
{
    uint8_t data;

    uint32_t timeout;

    /* =====================================================
     * START
     * ===================================================== */

    I2Cx->CR1 |=
        I2C_CR1_START;

    timeout =
        100000u;

    while ((I2Cx->SR1 &
            I2C_SR1_SB) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    /* =====================================================
     * ADDRESS WRITE
     * ===================================================== */

    I2Cx->DR =
        (uint8_t)(address << 1);

    timeout =
        100000u;

    while ((I2Cx->SR1 &
            I2C_SR1_ADDR) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    (void)I2Cx->SR1;
    (void)I2Cx->SR2;

    /* =====================================================
     * REGISTER
     * ===================================================== */

    timeout =
        100000u;

    while ((I2Cx->SR1 &
            I2C_SR1_TXE) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    I2Cx->DR =
        reg;

    timeout =
        100000u;

    while ((I2Cx->SR1 &
            I2C_SR1_BTF) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    /* =====================================================
     * REPEATED START
     * ===================================================== */

    I2Cx->CR1 |=
        I2C_CR1_START;

    timeout =
        100000u;

    while ((I2Cx->SR1 &
            I2C_SR1_SB) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    /* =====================================================
     * ADDRESS READ
     * ===================================================== */

    I2Cx->DR =
        (uint8_t)((address << 1) |
                  1u);

    timeout =
        100000u;

    while ((I2Cx->SR1 &
            I2C_SR1_ADDR) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    /*
     * Single byte:
     * disable ACK BEFORE clear ADDR
     */
    I2Cx->CR1 &=
        ~I2C_CR1_ACK;

    (void)I2Cx->SR1;
    (void)I2Cx->SR2;

    /* STOP */
    I2Cx->CR1 |=
        I2C_CR1_STOP;

    /* RXNE */
    timeout =
        100000u;

    while ((I2Cx->SR1 &
            I2C_SR1_RXNE) == 0u)
    {
        if (--timeout == 0u)
        {
            I2Cx->CR1 |=
                I2C_CR1_ACK;

            return 0u;
        }
    }

    data =
        (uint8_t)I2Cx->DR;

    /*
     * ACK back on
     */
    I2Cx->CR1 |=
        I2C_CR1_ACK;

    return data;
}

/* =========================================================
 * LCD
 * ========================================================= */

static void LCD_ExpanderWrite(
    uint8_t data)
{
    (void)I2C_WriteByte(
        I2C1,
        LCD_ADDR,
        (uint8_t)(data |
                  LCD_BACKLIGHT));
}

static void LCD_PulseEnable(
    uint8_t data)
{
    LCD_ExpanderWrite(
        (uint8_t)(data |
                  LCD_EN));

    Delay_ms(1u);

    LCD_ExpanderWrite(
        (uint8_t)(data &
                  ~LCD_EN));

    Delay_ms(1u);
}

static void LCD_WriteNibble(
    uint8_t nibble,
    uint8_t rs)
{
    uint8_t data;

    data =
        (uint8_t)((nibble & 0x0Fu)
                  << 4);

    if (rs != 0u)
    {
        data |=
            LCD_RS;
    }

    LCD_ExpanderWrite(
        data);

    LCD_PulseEnable(
        data);
}

static void LCD_Command(
    uint8_t command)
{
    LCD_WriteNibble(
        (uint8_t)(command >> 4),
        0u);

    LCD_WriteNibble(
        (uint8_t)(command &
                  0x0Fu),
        0u);

    Delay_ms(2u);
}

static void LCD_Data(
    uint8_t data)
{
    LCD_WriteNibble(
        (uint8_t)(data >> 4),
        1u);

    LCD_WriteNibble(
        (uint8_t)(data &
                  0x0Fu),
        1u);

    Delay_ms(1u);
}

static void LCD_Init(void)
{
    Delay_ms(50u);

    LCD_WriteNibble(
        0x03u,
        0u);

    Delay_ms(5u);

    LCD_WriteNibble(
        0x03u,
        0u);

    Delay_ms(5u);

    LCD_WriteNibble(
        0x03u,
        0u);

    Delay_ms(5u);

    /*
     * 4 bit mode
     */
    LCD_WriteNibble(
        0x02u,
        0u);

    /*
     * 4 bit
     * 2 line
     */
    LCD_Command(
        0x28u);

    /*
     * Display OFF
     */
    LCD_Command(
        0x08u);

    /*
     * Clear
     */
    LCD_Command(
        0x01u);

    Delay_ms(5u);

    /*
     * Entry mode
     */
    LCD_Command(
        0x06u);

    /*
     * Display ON
     */
    LCD_Command(
        0x0Cu);
}

static void LCD_Clear(void)
{
    LCD_Command(
        0x01u);

    Delay_ms(3u);
}

static void LCD_SetCursor(
    uint8_t row,
    uint8_t column)
{
    uint8_t address;

    if (row == 0u)
    {
        address =
            column;
    }
    else
    {
        address =
            (uint8_t)(0x40u +
                      column);
    }

    LCD_Command(
        (uint8_t)(0x80u |
                  address));
}

static void LCD_Print(
    const char *str)
{
    while (*str != '\0')
    {
        LCD_Data(
            (uint8_t)*str);

        str++;
    }
}

static void LCD_PrintInt(
    int16_t value)
{
    char buffer[8];

    uint8_t index = 0u;

    int32_t number;

    number =
        value;

    if (number < 0)
    {
        LCD_Data('-');

        number =
            -number;
    }

    if (number == 0)
    {
        LCD_Data('0');

        return;
    }

    while (number > 0)
    {
        buffer[index++] =
            (char)('0' +
                   (number % 10));

        number /=
            10;
    }

    while (index > 0u)
    {
        index--;

        LCD_Data(
            (uint8_t)
                buffer[index]);
    }
}

/* =========================================================
 * MPU6050 INIT
 * ========================================================= */

static uint8_t MPU6050_Init(void)
{
    uint8_t who;

    who =
        I2C_ReadRegister(
            I2C2,
            MPU6050_ADDR,
            MPU_WHO_AM_I);

    UART1_SendString(
        "WHO_AM_I = 0x");

    UART1_PrintHex8(
        who);

    UART1_SendString(
        "\r\n");

    if (who != 0x68u)
    {
        return 0u;
    }

    /*
     * Wake MPU6050
     */
    if (I2C_WriteRegister(
            I2C2,
            MPU6050_ADDR,
            MPU_PWR_MGMT_1,
            0x00u) == 0u)
    {
        return 0u;
    }

    Delay_ms(
        100u);

    return 1u;
}

/* =========================================================
 * READ ACCEL X
 * ========================================================= */

static int16_t MPU6050_ReadAX(void)
{
    uint8_t high;
    uint8_t low;

    high =
        I2C_ReadRegister(
            I2C2,
            MPU6050_ADDR,
            MPU_ACCEL_X_H);

    /*
     * STOP có thể xảy ra ngay đây
     * nhờ USART IRQ.
     */

    low =
        I2C_ReadRegister(
            I2C2,
            MPU6050_ADDR,
            MPU_ACCEL_X_L);

    return (int16_t)(((uint16_t)high << 8) |
                     low);
}

/* =========================================================
 * SHOW AX
 *
 * UART + LCD
 * ========================================================= */

static void Show_AX(void)
{
    int16_t ax;

    ax =
        MPU6050_ReadAX();

    /*
     * Nếu STOP vừa tới trong lúc
     * đọc I2C -> thoát ngay.
     */
    if (g_ax_stream == 0u)
    {
        return;
    }

    /* =====================================================
     * UART
     * ===================================================== */

    UART1_SendString(
        "AX = ");

    UART1_PrintInt(
        ax);

    UART1_SendString(
        "\r\n");

    /*
     * STOP có thể đến
     * trong lúc UART đang TX.
     */
    if (g_ax_stream == 0u)
    {
        return;
    }

    /* =====================================================
     * LCD
     * ===================================================== */

    LCD_SetCursor(
        1u,
        0u);

    LCD_Print(
        "                ");

    /*
     * STOP có thể đến
     * trong lúc LCD update.
     */
    if (g_ax_stream == 0u)
    {
        return;
    }

    LCD_SetCursor(
        1u,
        0u);

    LCD_Print(
        "AX: ");

    LCD_PrintInt(
        ax);
}

/* =========================================================
 * PROCESS COMMAND
 * ========================================================= */

static void Process_Command(
    const char *command)
{
    uint8_t who;

    /* =====================================================
     * HELP
     * ===================================================== */

    if (strcmp(
            command,
            "HELP") == 0)
    {
        UART1_SendString(
            "\r\nCOMMANDS\r\n");

        UART1_SendString(
            "HELP       - Show commands\r\n");

        UART1_SendString(
            "SCAN       - Scan I2C buses\r\n");

        UART1_SendString(
            "MPU        - MPU WHO_AM_I\r\n");

        UART1_SendString(
            "AX         - Start AX stream\r\n");

        UART1_SendString(
            "STOP       - Stop AX immediately\r\n");

        UART1_SendString(
            "LCD HELLO  - LCD Hello World\r\n");
    }

    /* =====================================================
     * SCAN
     * ===================================================== */

    else if (strcmp(
                 command,
                 "SCAN") == 0)
    {
        I2C_Scan(
            I2C1,
            "I2C1 PB6/PB7");

        I2C_Scan(
            I2C2,
            "I2C2 PB10/PB11");
    }

    /* =====================================================
     * MPU
     * ===================================================== */

    else if (strcmp(
                 command,
                 "MPU") == 0)
    {
        who =
            I2C_ReadRegister(
                I2C2,
                MPU6050_ADDR,
                MPU_WHO_AM_I);

        UART1_SendString(
            "WHO_AM_I = 0x");

        UART1_PrintHex8(
            who);

        UART1_SendString(
            "\r\n");

        if (who == 0x68u)
        {
            UART1_SendString(
                "MPU6050 OK\r\n");
        }
        else
        {
            UART1_SendString(
                "MPU6050 ERROR\r\n");
        }
    }

    /* =====================================================
     * AX
     *
     * START CONTINUOUS MODE
     * ===================================================== */

    else if (strcmp(
                 command,
                 "AX") == 0)
    {
        /*
         * Reset STOP detector
         */
        g_stop_state =
            0u;

        g_stop_event =
            0u;

        /*
         * Start AX
         */
        g_ax_stream =
            1u;

        UART1_SendString(
            "\r\nAX STREAM START\r\n");

        UART1_SendString(
            "Update: 500 ms\r\n");

        UART1_SendString(
            "Send STOP to stop\r\n");
    }

    /* =====================================================
     * STOP
     *
     * Backup command.
     *
     * Normal STOP thực tế đã được
     * bắt ngay trong USART IRQ.
     * ===================================================== */

    else if (strcmp(
                 command,
                 "STOP") == 0)
    {
        g_ax_stream =
            0u;

        UART1_SendString(
            "AX STREAM STOP\r\n");
    }

    /* =====================================================
     * LCD HELLO
     * ===================================================== */

    else if (strcmp(
                 command,
                 "LCD HELLO") == 0)
    {
        LCD_Clear();

        LCD_SetCursor(
            0u,
            0u);

        LCD_Print(
            "Hello World");

        LCD_SetCursor(
            1u,
            0u);

        LCD_Print(
            "STM32 I2C UART");

        UART1_SendString(
            "LCD updated\r\n");
    }

    /* =====================================================
     * UNKNOWN
     * ===================================================== */

    else
    {
        UART1_SendString(
            "Unknown command\r\n");

        UART1_SendString(
            "Type HELP\r\n");
    }
}

/* =========================================================
 * MAIN
 * ========================================================= */

int main(void)
{
    char command[32];

    uint8_t index = 0u;

    char c;

    uint8_t mpu_ok;

    uint32_t last_ax_time = 0u;

    /* =====================================================
     * CLOCK
     * ===================================================== */

    SystemCoreClockUpdate();

    /* =====================================================
     * SYSTICK 1 ms
     * ===================================================== */

    if (SysTick_Config(
            SystemCoreClock /
            1000u) != 0u)
    {
        while (1)
        {
        }
    }

    /* =====================================================
     * UART
     * ===================================================== */

    UART1_Init();

    UART1_SendString(
        "\r\n");

    UART1_SendString(
        "================================\r\n");

    UART1_SendString(
        " STM32F103 LCD + MPU + UART IRQ\r\n");

    UART1_SendString(
        "================================\r\n");

    UART1_SendString(
        "UART1 PA9 TX / PA10 RX\r\n");

    UART1_SendString(
        "115200 baud\r\n");

    /* =====================================================
     * I2C
     * ===================================================== */

    I2C_GPIO_Init();

    I2C1_Init();

    I2C2_Init();

    /* =====================================================
     * INITIAL SCAN
     * ===================================================== */

    I2C_Scan(
        I2C1,
        "I2C1 PB6/PB7");

    I2C_Scan(
        I2C2,
        "I2C2 PB10/PB11");

    /* =====================================================
     * LCD
     * ===================================================== */

    LCD_Init();

    LCD_Clear();

    LCD_SetCursor(
        0u,
        0u);

    LCD_Print(
        "STM32 UART IRQ");

    LCD_SetCursor(
        1u,
        0u);

    LCD_Print(
        "Starting...");

    /* =====================================================
     * MPU6050
     * ===================================================== */

    mpu_ok =
        MPU6050_Init();

    if (mpu_ok != 0u)
    {
        UART1_SendString(
            "MPU6050 initialized OK\r\n");

        LCD_SetCursor(
            0u,
            0u);

        LCD_Print(
            "MPU6050 OK      ");

        LCD_SetCursor(
            1u,
            0u);

        LCD_Print(
            "Send AX         ");
    }
    else
    {
        UART1_SendString(
            "MPU6050 INIT ERROR\r\n");

        LCD_SetCursor(
            0u,
            0u);

        LCD_Print(
            "MPU ERROR       ");

        LCD_SetCursor(
            1u,
            0u);

        LCD_Print(
            "Check I2C2      ");
    }

    /* =====================================================
     * COMMAND MODE
     * ===================================================== */

    UART1_SendString(
        "\r\nCOMMAND MODE\r\n");

    UART1_SendString(
        "Type HELP\r\n");

    UART1_SendString(
        "> ");

    /* =====================================================
     * SUPER LOOP
     * ===================================================== */

    while (1)
    {
        /* =================================================
         * STOP EVENT
         *
         * Event này được tạo bởi USART1 IRQ.
         * ================================================= */

        if (g_stop_event != 0u)
        {
            /*
             * Clear event
             */
            g_stop_event =
                0u;

            /*
             * AX chắc chắn OFF
             */
            g_ax_stream =
                0u;

            /*
             * Bỏ chữ STOP còn dư
             * trong RX buffer.
             */
            uart_rx_tail =
                uart_rx_head;

            /*
             * Reset command
             */
            index =
                0u;

            /*
             * Reset STOP detector
             */
            g_stop_state =
                0u;

            UART1_SendString(
                "\r\n");

            UART1_SendString(
                "============================\r\n");

            UART1_SendString(
                " AX STOPPED BY UART INTERRUPT\r\n");

            UART1_SendString(
                "============================\r\n");

            UART1_SendString(
                "> ");
        }

        /* =================================================
         * UART COMMAND RECEIVE
         *
         * NON BLOCKING
         * ================================================= */

        if (UART1_ReadChar(
                &c) != 0u)
        {
            /*
             * lowercase -> uppercase
             */
            if ((c >= 'a') &&
                (c <= 'z'))
            {
                c =
                    (char)(c -
                           'a' +
                           'A');
            }

            /* =============================================
             * ENTER
             * ============================================= */

            if ((c == '\r') ||
                (c == '\n'))
            {
                if (index > 0u)
                {
                    command[index] =
                        '\0';

                    UART1_SendString(
                        "\r\n");

                    Process_Command(
                        command);

                    index =
                        0u;

                    UART1_SendString(
                        "\r\n> ");
                }
            }

            /* =============================================
             * BACKSPACE
             * ============================================= */

            else if (
                (c == '\b') ||
                ((uint8_t)c == 0x7Fu))
            {
                if (index > 0u)
                {
                    index--;

                    UART1_SendString(
                        "\b \b");
                }
            }

            /* =============================================
             * NORMAL CHARACTER
             * ============================================= */

            else
            {
                if (index <
                    (sizeof(command) - 1u))
                {
                    command[index++] =
                        c;

                    UART1_SendChar(
                        c);
                }
            }
        }

        /* =================================================
         * AX STREAM TASK
         * ================================================= */

        if (g_ax_stream != 0u)
        {
            if ((uint32_t)(g_ms -
                           last_ax_time) >=
                AX_UPDATE_PERIOD_MS)
            {
                last_ax_time =
                    g_ms;

                /*
                 * Kiểm tra flag trước khi chạy.
                 */
                if (g_ax_stream != 0u)
                {
                    Show_AX();
                }
            }
        }
    }
}