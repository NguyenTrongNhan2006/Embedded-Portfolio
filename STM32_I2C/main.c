#include "stm32f103xb.h"
#include <stdint.h>
#include <string.h>

/* =========================================================
 * DEVICE ADDRESS
 * ========================================================= */

#define LCD_ADDR 0x27u
#define MPU6050_ADDR 0x68u

/* =========================================================
 * MPU6050 REGISTERS
 * ========================================================= */

#define MPU_PWR_MGMT_1 0x6Bu
#define MPU_WHO_AM_I 0x75u

#define MPU_ACCEL_X_H 0x3Bu
#define MPU_ACCEL_X_L 0x3Cu

/* =========================================================
 * LCD PCF8574
 *
 * P0 -> RS
 * P1 -> RW
 * P2 -> EN
 * P3 -> Backlight
 * P4 -> D4
 * P5 -> D5
 * P6 -> D6
 * P7 -> D7
 * ========================================================= */

#define LCD_RS 0x01u
#define LCD_EN 0x04u
#define LCD_BACKLIGHT 0x08u

/* =========================================================
 * UART
 * ========================================================= */

#define UART_BAUDRATE 115200u

/* =========================================================
 * AX STREAM PERIOD
 * ========================================================= */

#define AX_UPDATE_PERIOD_MS 500u

/* =========================================================
 * GLOBAL VARIABLES
 * ========================================================= */

/*
 * SysTick tăng mỗi 1 ms.
 */
static volatile uint32_t g_ms = 0u;

/*
 * = 0: không stream AX
 * = 1: stream AX liên tục
 */
static uint8_t g_ax_stream = 0u;

/* =========================================================
 * SYSTICK INTERRUPT
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

    /*
     * PPRE1 nằm ở CFGR[10:8]
     */
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

    /*
     * PPRE2 nằm ở CFGR[13:11]
     */
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
 *
 * Chỉ dùng cho LCD init / timing đơn giản.
 * Không dùng để tạo AX stream.
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
 * 115200 baud
 * ========================================================= */

static void UART1_Init(void)
{
    uint32_t pclk2;

    /* GPIOA clock */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    /* USART1 clock */
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    /* -----------------------------------------------------
     * PA9 = USART1_TX
     * Alternate Function Push-Pull
     * ----------------------------------------------------- */

    GPIOA->CRH &=
        ~(GPIO_CRH_MODE9 |
          GPIO_CRH_CNF9);

    GPIOA->CRH |=
        GPIO_CRH_MODE9 |
        GPIO_CRH_CNF9_1;

    /* -----------------------------------------------------
     * PA10 = USART1_RX
     * Floating Input
     * ----------------------------------------------------- */

    GPIOA->CRH &=
        ~(GPIO_CRH_MODE10 |
          GPIO_CRH_CNF10);

    GPIOA->CRH |=
        GPIO_CRH_CNF10_0;

    /* -----------------------------------------------------
     * Baudrate
     * ----------------------------------------------------- */

    pclk2 = Get_PCLK2();

    USART1->BRR =
        (pclk2 + (UART_BAUDRATE / 2u)) / UART_BAUDRATE;

    /* TX + RX */
    USART1->CR1 =
        USART_CR1_TE |
        USART_CR1_RE;

    /* Enable USART1 */
    USART1->CR1 |= USART_CR1_UE;
}

static void UART1_SendChar(char c)
{
    while ((USART1->SR & USART_SR_TXE) == 0u)
    {
    }

    USART1->DR = (uint8_t)c;
}

static void UART1_SendString(const char *str)
{
    while (*str != '\0')
    {
        UART1_SendChar(*str);

        str++;
    }
}

static void UART1_PrintHex8(uint8_t value)
{
    static const char hex[] =
        "0123456789ABCDEF";

    UART1_SendChar(
        hex[(value >> 4) & 0x0Fu]);

    UART1_SendChar(
        hex[value & 0x0Fu]);
}

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
        buffer[index] =
            (char)('0' +
                   (number % 10));

        index++;

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
 * I2C GPIO
 *
 * I2C1:
 * PB6  = SCL
 * PB7  = SDA
 *
 * I2C2:
 * PB10 = SCL
 * PB11 = SDA
 * ========================================================= */

static void I2C_GPIO_Init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;

    /* -----------------------------------------------------
     * PB6 / PB7
     * I2C1
     * Alternate Function Open Drain
     * ----------------------------------------------------- */

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

    /* -----------------------------------------------------
     * PB10 / PB11
     * I2C2
     * Alternate Function Open Drain
     * ----------------------------------------------------- */

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
 * GENERIC I2C CONFIG
 *
 * Standard Mode = 100 kHz
 * ========================================================= */

static void I2C_Config(I2C_TypeDef *I2Cx)
{
    uint32_t pclk1;
    uint32_t freq_mhz;
    uint32_t ccr;

    pclk1 = Get_PCLK1();

    freq_mhz =
        pclk1 / 1000000u;

    /*
     * Peripheral clock MHz
     */
    I2Cx->CR2 = freq_mhz;

    /*
     * Standard Mode:
     *
     * CCR =
     * PCLK1 / (2 * 100 kHz)
     */
    ccr =
        pclk1 /
        (2u * 100000u);

    if (ccr < 4u)
    {
        ccr = 4u;
    }

    I2Cx->CCR = ccr;

    /*
     * Standard mode:
     *
     * TRISE = FREQ + 1
     */
    I2Cx->TRISE =
        freq_mhz + 1u;

    /*
     * ACK enable
     */
    I2Cx->CR1 |= I2C_CR1_ACK;

    /*
     * Enable peripheral
     */
    I2Cx->CR1 |= I2C_CR1_PE;
}

static void I2C1_Init(void)
{
    RCC->APB1ENR |=
        RCC_APB1ENR_I2C1EN;

    /*
     * Reset I2C1
     */
    RCC->APB1RSTR |=
        RCC_APB1RSTR_I2C1RST;

    RCC->APB1RSTR &=
        ~RCC_APB1RSTR_I2C1RST;

    I2C_Config(I2C1);
}

static void I2C2_Init(void)
{
    RCC->APB1ENR |=
        RCC_APB1ENR_I2C2EN;

    /*
     * Reset I2C2
     */
    RCC->APB1RSTR |=
        RCC_APB1RSTR_I2C2RST;

    RCC->APB1RSTR &=
        ~RCC_APB1RSTR_I2C2RST;

    I2C_Config(I2C2);
}

/* =========================================================
 * CHECK I2C ADDRESS
 *
 * return:
 * 1 = ACK
 * 0 = NACK / timeout
 * ========================================================= */

static uint8_t I2C_CheckAddress(
    I2C_TypeDef *I2Cx,
    uint8_t address)
{
    uint32_t timeout;

    I2Cx->SR1 &= ~I2C_SR1_AF;

    /*
     * START
     */
    I2Cx->CR1 |= I2C_CR1_START;

    timeout = 100000u;

    while ((I2Cx->SR1 & I2C_SR1_SB) == 0u)
    {
        if (--timeout == 0u)
        {
            I2Cx->CR1 |= I2C_CR1_STOP;

            return 0u;
        }
    }

    /*
     * Address + WRITE
     */
    I2Cx->DR =
        (uint8_t)(address << 1);

    timeout = 100000u;

    while (timeout > 0u)
    {
        /*
         * ACK
         */
        if ((I2Cx->SR1 & I2C_SR1_ADDR) != 0u)
        {
            /*
             * Clear ADDR
             */
            (void)I2Cx->SR1;
            (void)I2Cx->SR2;

            /*
             * STOP
             */
            I2Cx->CR1 |= I2C_CR1_STOP;

            return 1u;
        }

        /*
         * NACK
         */
        if ((I2Cx->SR1 & I2C_SR1_AF) != 0u)
        {
            I2Cx->SR1 &=
                ~I2C_SR1_AF;

            I2Cx->CR1 |=
                I2C_CR1_STOP;

            return 0u;
        }

        timeout--;
    }

    I2Cx->CR1 |= I2C_CR1_STOP;

    return 0u;
}

/* =========================================================
 * SCAN I2C BUS
 * ========================================================= */

static void I2C_Scan(
    I2C_TypeDef *I2Cx,
    const char *bus_name)
{
    uint8_t address;

    uint8_t found = 0u;

    UART1_SendString(
        "\r\n============================\r\n");

    UART1_SendString(
        "Scanning ");

    UART1_SendString(
        bus_name);

    UART1_SendString(
        "\r\n");

    UART1_SendString(
        "============================\r\n");

    for (address = 0x08u;
         address <= 0x77u;
         address++)
    {
        if (I2C_CheckAddress(
                I2Cx,
                address) != 0u)
        {
            UART1_SendString(
                "Found device: 0x");

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
            "No I2C device found\r\n");
    }

    UART1_SendString(
        "Scan complete\r\n");
}

/* =========================================================
 * I2C WRITE ONE BYTE
 *
 * Used by LCD PCF8574
 * ========================================================= */

static uint8_t I2C_WriteByte(
    I2C_TypeDef *I2Cx,
    uint8_t address,
    uint8_t data)
{
    uint32_t timeout;

    /*
     * START
     */
    I2Cx->CR1 |= I2C_CR1_START;

    timeout = 100000u;

    while ((I2Cx->SR1 & I2C_SR1_SB) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    /*
     * Address + WRITE
     */
    I2Cx->DR =
        (uint8_t)(address << 1);

    timeout = 100000u;

    while ((I2Cx->SR1 & I2C_SR1_ADDR) == 0u)
    {
        if ((I2Cx->SR1 & I2C_SR1_AF) != 0u)
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

    /*
     * Clear ADDR
     */
    (void)I2Cx->SR1;
    (void)I2Cx->SR2;

    /*
     * Wait TXE
     */
    timeout = 100000u;

    while ((I2Cx->SR1 & I2C_SR1_TXE) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    /*
     * Send data
     */
    I2Cx->DR = data;

    /*
     * Wait BTF
     */
    timeout = 100000u;

    while ((I2Cx->SR1 & I2C_SR1_BTF) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    /*
     * STOP
     */
    I2Cx->CR1 |= I2C_CR1_STOP;

    return 1u;
}

/* =========================================================
 * I2C WRITE REGISTER
 *
 * Used by MPU6050
 * ========================================================= */

static uint8_t I2C_WriteRegister(
    I2C_TypeDef *I2Cx,
    uint8_t address,
    uint8_t reg,
    uint8_t data)
{
    uint32_t timeout;

    /* START */
    I2Cx->CR1 |= I2C_CR1_START;

    timeout = 100000u;

    while ((I2Cx->SR1 & I2C_SR1_SB) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    /* Address + WRITE */
    I2Cx->DR =
        (uint8_t)(address << 1);

    timeout = 100000u;

    while ((I2Cx->SR1 & I2C_SR1_ADDR) == 0u)
    {
        if ((I2Cx->SR1 & I2C_SR1_AF) != 0u)
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

    /*
     * Clear ADDR
     */
    (void)I2Cx->SR1;
    (void)I2Cx->SR2;

    /*
     * Send register
     */
    timeout = 100000u;

    while ((I2Cx->SR1 & I2C_SR1_TXE) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    I2Cx->DR = reg;

    /*
     * Send data
     */
    timeout = 100000u;

    while ((I2Cx->SR1 & I2C_SR1_TXE) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    I2Cx->DR = data;

    /*
     * Wait BTF
     */
    timeout = 100000u;

    while ((I2Cx->SR1 & I2C_SR1_BTF) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    /*
     * STOP
     */
    I2Cx->CR1 |= I2C_CR1_STOP;

    return 1u;
}

/* =========================================================
 * I2C READ 1 REGISTER
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

    I2Cx->CR1 |= I2C_CR1_START;

    timeout = 100000u;

    while ((I2Cx->SR1 & I2C_SR1_SB) == 0u)
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

    timeout = 100000u;

    while ((I2Cx->SR1 & I2C_SR1_ADDR) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    /*
     * Clear ADDR
     */
    (void)I2Cx->SR1;
    (void)I2Cx->SR2;

    /* =====================================================
     * SEND REGISTER
     * ===================================================== */

    timeout = 100000u;

    while ((I2Cx->SR1 & I2C_SR1_TXE) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    I2Cx->DR = reg;

    timeout = 100000u;

    while ((I2Cx->SR1 & I2C_SR1_BTF) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    /* =====================================================
     * REPEATED START
     * ===================================================== */

    I2Cx->CR1 |= I2C_CR1_START;

    timeout = 100000u;

    while ((I2Cx->SR1 & I2C_SR1_SB) == 0u)
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

    timeout = 100000u;

    while ((I2Cx->SR1 & I2C_SR1_ADDR) == 0u)
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    /*
     * Single-byte receive
     *
     * Disable ACK trước khi
     * clear ADDR.
     */
    I2Cx->CR1 &= ~I2C_CR1_ACK;

    /*
     * Clear ADDR
     */
    (void)I2Cx->SR1;
    (void)I2Cx->SR2;

    /*
     * STOP
     */
    I2Cx->CR1 |= I2C_CR1_STOP;

    /*
     * Wait RXNE
     */
    timeout = 100000u;

    while ((I2Cx->SR1 & I2C_SR1_RXNE) == 0u)
    {
        if (--timeout == 0u)
        {
            I2Cx->CR1 |= I2C_CR1_ACK;

            return 0u;
        }
    }

    data =
        (uint8_t)I2Cx->DR;

    /*
     * Enable ACK lại
     */
    I2Cx->CR1 |= I2C_CR1_ACK;

    return data;
}

/* =========================================================
 * LCD1602
 *
 * I2C1
 * PB6 / PB7
 * ========================================================= */

static void LCD_ExpanderWrite(uint8_t data)
{
    (void)I2C_WriteByte(
        I2C1,
        LCD_ADDR,
        (uint8_t)(data |
                  LCD_BACKLIGHT));
}

static void LCD_PulseEnable(uint8_t data)
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
        data |= LCD_RS;
    }

    LCD_ExpanderWrite(data);

    LCD_PulseEnable(data);
}

static void LCD_Command(uint8_t command)
{
    LCD_WriteNibble(
        (uint8_t)(command >> 4),
        0u);

    LCD_WriteNibble(
        (uint8_t)(command & 0x0Fu),
        0u);

    Delay_ms(2u);
}

static void LCD_Data(uint8_t data)
{
    LCD_WriteNibble(
        (uint8_t)(data >> 4),
        1u);

    LCD_WriteNibble(
        (uint8_t)(data & 0x0Fu),
        1u);

    Delay_ms(1u);
}

static void LCD_Init(void)
{
    /*
     * LCD power-up
     */
    Delay_ms(50u);

    /*
     * HD44780 init sequence
     */
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
     * 4-bit mode
     */
    LCD_WriteNibble(
        0x02u,
        0u);

    /*
     * 4-bit
     * 2 lines
     * 5x8 font
     */
    LCD_Command(0x28u);

    /*
     * Display OFF
     */
    LCD_Command(0x08u);

    /*
     * Clear
     */
    LCD_Command(0x01u);

    Delay_ms(5u);

    /*
     * Entry Mode
     */
    LCD_Command(0x06u);

    /*
     * Display ON
     * Cursor OFF
     */
    LCD_Command(0x0Cu);
}

static void LCD_Clear(void)
{
    LCD_Command(0x01u);

    Delay_ms(3u);
}

static void LCD_SetCursor(
    uint8_t row,
    uint8_t column)
{
    uint8_t address;

    if (row == 0u)
    {
        address = column;
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

static void LCD_Print(const char *str)
{
    while (*str != '\0')
    {
        LCD_Data(
            (uint8_t)*str);

        str++;
    }
}

static void LCD_PrintInt(int16_t value)
{
    char buffer[8];

    uint8_t index = 0u;

    int32_t number;

    number = value;

    if (number < 0)
    {
        LCD_Data('-');

        number = -number;
    }

    if (number == 0)
    {
        LCD_Data('0');

        return;
    }

    while (number > 0)
    {
        buffer[index] =
            (char)('0' +
                   (number % 10));

        index++;

        number /= 10;
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
 * MPU6050
 *
 * I2C2
 * PB10 / PB11
 * ========================================================= */

static uint8_t MPU6050_Init(void)
{
    uint8_t who;

    /*
     * WHO_AM_I
     */
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

    /*
     * MPU6050 expected:
     * WHO_AM_I = 0x68
     */
    if (who != 0x68u)
    {
        return 0u;
    }

    /*
     * Wake MPU6050.
     *
     * Default is sleep mode.
     */
    if (I2C_WriteRegister(
            I2C2,
            MPU6050_ADDR,
            MPU_PWR_MGMT_1,
            0x00u) == 0u)
    {
        return 0u;
    }

    Delay_ms(100u);

    return 1u;
}

/* =========================================================
 * MPU6050 READ ACCEL X
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

    /*
     * Read MPU
     */
    ax =
        MPU6050_ReadAX();

    /* =====================================================
     * UART OUTPUT
     * ===================================================== */

    UART1_SendString(
        "AX = ");

    UART1_PrintInt(
        ax);

    UART1_SendString(
        "\r\n");

    /* =====================================================
     * LCD OUTPUT
     * ===================================================== */

    LCD_SetCursor(
        1u,
        0u);

    /*
     * Clear second line.
     */
    LCD_Print(
        "                ");

    LCD_SetCursor(
        1u,
        0u);

    LCD_Print(
        "AX: ");

    LCD_PrintInt(
        ax);
}

/* =========================================================
 * COMMAND PROCESSOR
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
            "\r\nAVAILABLE COMMANDS\r\n");

        UART1_SendString(
            "HELP       - Show commands\r\n");

        UART1_SendString(
            "SCAN       - Scan I2C1 + I2C2\r\n");

        UART1_SendString(
            "MPU        - Read WHO_AM_I\r\n");

        UART1_SendString(
            "AX         - Start AX continuous mode\r\n");

        UART1_SendString(
            "STOP       - Stop AX continuous mode\r\n");

        UART1_SendString(
            "LCD HELLO  - Show Hello World\r\n");
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
     * START STREAM
     * ===================================================== */

    else if (strcmp(
                 command,
                 "AX") == 0)
    {
        g_ax_stream = 1u;

        UART1_SendString(
            "AX continuous mode START\r\n");

        UART1_SendString(
            "Update period = 500 ms\r\n");

        UART1_SendString(
            "Type STOP to stop\r\n");
    }

    /* =====================================================
     * STOP
     * ===================================================== */

    else if (strcmp(
                 command,
                 "STOP") == 0)
    {
        g_ax_stream = 0u;

        UART1_SendString(
            "AX continuous mode STOP\r\n");
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
     * SYSTICK
     *
     * 1 ms tick
     * ===================================================== */

    if (SysTick_Config(
            SystemCoreClock / 1000u) != 0u)
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
        " STM32F103 I2C + MPU + UART\r\n");

    UART1_SendString(
        "================================\r\n");

    UART1_SendString(
        "UART1 PA9 TX / PA10 RX\r\n");

    UART1_SendString(
        "Baudrate: 115200\r\n");

    /* =====================================================
     * I2C
     * ===================================================== */

    I2C_GPIO_Init();

    /*
     * LCD
     */
    I2C1_Init();

    /*
     * MPU6050
     */
    I2C2_Init();

    /* =====================================================
     * INITIAL I2C SCAN
     * ===================================================== */

    I2C_Scan(
        I2C1,
        "I2C1 PB6/PB7");

    I2C_Scan(
        I2C2,
        "I2C2 PB10/PB11");

    /* =====================================================
     * LCD INIT
     * ===================================================== */

    LCD_Init();

    LCD_Clear();

    LCD_SetCursor(
        0u,
        0u);

    LCD_Print(
        "STM32 I2C UART");

    LCD_SetCursor(
        1u,
        0u);

    LCD_Print(
        "Starting...");

    /* =====================================================
     * MPU6050 INIT
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
            "Type AX UART    ");
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
     * UART COMMAND MODE
     * ===================================================== */

    UART1_SendString(
        "\r\nUART COMMAND MODE\r\n");

    UART1_SendString(
        "Type HELP to show commands\r\n");

    UART1_SendString(
        "> ");

    /* =====================================================
     * SUPER LOOP
     * ===================================================== */

    while (1)
    {
        /* =================================================
         * UART RECEIVE
         *
         * NON-BLOCKING
         *
         * Không dùng UART1_GetChar() blocking nữa.
         * ================================================= */

        if ((USART1->SR & USART_SR_RXNE) != 0u)
        {
            c =
                (char)(USART1->DR &
                       0xFFu);

            /* ---------------------------------------------
             * lowercase -> uppercase
             * --------------------------------------------- */

            if ((c >= 'a') &&
                (c <= 'z'))
            {
                c =
                    (char)(c -
                           'a' +
                           'A');
            }

            /* ---------------------------------------------
             * ENTER
             * --------------------------------------------- */

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

                    index = 0u;

                    UART1_SendString(
                        "\r\n> ");
                }
            }

            /* ---------------------------------------------
             * BACKSPACE
             * --------------------------------------------- */

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

            /* ---------------------------------------------
             * NORMAL CHARACTER
             * --------------------------------------------- */

            else
            {
                if (index <
                    (sizeof(command) - 1u))
                {
                    command[index] =
                        c;

                    index++;

                    /*
                     * Echo
                     */
                    UART1_SendChar(
                        c);
                }
            }
        }

        /* =================================================
         * AX CONTINUOUS MODE
         *
         * Non-blocking timing
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
                 * Read MPU + update UART + LCD
                 */
                Show_AX();
            }
        }
    }
}