#include "stm32f103xb.h"
#include <stdint.h>

/* =========================================================
 * I2C ADDRESS
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
 * ========================================================= */

#define LCD_RS 0x01u
#define LCD_EN 0x04u
#define LCD_BACKLIGHT 0x08u

/* =========================================================
 * DELAY
 * ========================================================= */
static void delay_ms(uint32_t ms)
{
    volatile uint32_t i;

    while (ms--)
    {
        for (i = 0; i < 2000u; i++)
        {
            __NOP();
        }
    }
}

/* =========================================================
 * GPIO I2C
 *
 * I2C1:
 * PB6  = SCL
 * PB7  = SDA
 *
 * I2C2:
 * PB10 = SCL
 * PB11 = SDA
 * ========================================================= */
static void i2c_gpio_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;

    /* ---------------- I2C1 ----------------
     * PB6, PB7
     * AF Open Drain
     */

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

    /* ---------------- I2C2 ----------------
     * PB10, PB11
     * nằm trong CRH
     */

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
 * Generic I2C init
 * Standard Mode = 100 kHz
 * PCLK1 = 8 MHz
 * ========================================================= */
static void i2c_init(I2C_TypeDef *I2Cx)
{
    /*
     * CR2.FREQ = 8 MHz
     */
    I2Cx->CR2 = 8u;

    /*
     * CCR:
     *
     * 8 MHz / (2 * 100 kHz)
     * = 40
     */
    I2Cx->CCR = 40u;

    /*
     * TRISE = FREQ + 1
     *       = 8 + 1
     */
    I2Cx->TRISE = 9u;

    I2Cx->CR1 |= I2C_CR1_PE;
}

static void i2c1_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

    RCC->APB1RSTR |= RCC_APB1RSTR_I2C1RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_I2C1RST;

    i2c_init(I2C1);
}

static void i2c2_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_I2C2EN;

    RCC->APB1RSTR |= RCC_APB1RSTR_I2C2RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_I2C2RST;

    i2c_init(I2C2);
}

/* =========================================================
 * GENERIC I2C WRITE BYTE
 * dùng cho LCD
 * ========================================================= */
static uint8_t i2c_write_byte(
    I2C_TypeDef *I2Cx,
    uint8_t address,
    uint8_t data)
{
    uint32_t timeout;

    /* START */
    I2Cx->CR1 |= I2C_CR1_START;

    timeout = 100000u;

    while (!(I2Cx->SR1 & I2C_SR1_SB))
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    /* Send address + WRITE */
    I2Cx->DR = (uint8_t)(address << 1);

    timeout = 100000u;

    while (!(I2Cx->SR1 & I2C_SR1_ADDR))
    {
        if (I2Cx->SR1 & I2C_SR1_AF)
        {
            I2Cx->SR1 &= ~I2C_SR1_AF;
            I2Cx->CR1 |= I2C_CR1_STOP;

            return 0u;
        }

        if (--timeout == 0u)
        {
            I2Cx->CR1 |= I2C_CR1_STOP;

            return 0u;
        }
    }

    /* Clear ADDR */
    (void)I2Cx->SR1;
    (void)I2Cx->SR2;

    /* Wait TXE */
    timeout = 100000u;

    while (!(I2Cx->SR1 & I2C_SR1_TXE))
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    I2Cx->DR = data;

    /* Wait BTF */
    timeout = 100000u;

    while (!(I2Cx->SR1 & I2C_SR1_BTF))
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    I2Cx->CR1 |= I2C_CR1_STOP;

    return 1u;
}

/* =========================================================
 * I2C WRITE REGISTER
 *
 * dùng cho MPU6050
 * ========================================================= */
static uint8_t i2c_write_register(
    I2C_TypeDef *I2Cx,
    uint8_t address,
    uint8_t reg,
    uint8_t data)
{
    uint32_t timeout;

    /* START */
    I2Cx->CR1 |= I2C_CR1_START;

    timeout = 100000u;

    while (!(I2Cx->SR1 & I2C_SR1_SB))
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    /* Address WRITE */
    I2Cx->DR = (uint8_t)(address << 1);

    timeout = 100000u;

    while (!(I2Cx->SR1 & I2C_SR1_ADDR))
    {
        if (I2Cx->SR1 & I2C_SR1_AF)
        {
            I2Cx->SR1 &= ~I2C_SR1_AF;
            I2Cx->CR1 |= I2C_CR1_STOP;

            return 0u;
        }
    }

    (void)I2Cx->SR1;
    (void)I2Cx->SR2;

    /* Send register */
    while (!(I2Cx->SR1 & I2C_SR1_TXE))
    {
    }

    I2Cx->DR = reg;

    while (!(I2Cx->SR1 & I2C_SR1_TXE))
    {
    }

    /* Send data */
    I2Cx->DR = data;

    while (!(I2Cx->SR1 & I2C_SR1_BTF))
    {
    }

    I2Cx->CR1 |= I2C_CR1_STOP;

    return 1u;
}

/* =========================================================
 * I2C READ REGISTER - 1 BYTE
 * ========================================================= */
static uint8_t i2c_read_register(
    I2C_TypeDef *I2Cx,
    uint8_t address,
    uint8_t reg)
{
    uint8_t data;

    /* =====================================================
     * STEP 1: gửi địa chỉ register muốn đọc
     * ===================================================== */

    I2Cx->CR1 |= I2C_CR1_START;

    while (!(I2Cx->SR1 & I2C_SR1_SB))
    {
    }

    /* Address WRITE */
    I2Cx->DR = (uint8_t)(address << 1);

    while (!(I2Cx->SR1 & I2C_SR1_ADDR))
    {
    }

    (void)I2Cx->SR1;
    (void)I2Cx->SR2;

    while (!(I2Cx->SR1 & I2C_SR1_TXE))
    {
    }

    I2Cx->DR = reg;

    while (!(I2Cx->SR1 & I2C_SR1_BTF))
    {
    }

    /* =====================================================
     * STEP 2: Repeated START
     * ===================================================== */

    I2Cx->CR1 |= I2C_CR1_START;

    while (!(I2Cx->SR1 & I2C_SR1_SB))
    {
    }

    /* Address READ */
    I2Cx->DR =
        (uint8_t)((address << 1) | 1u);

    while (!(I2Cx->SR1 & I2C_SR1_ADDR))
    {
    }

    /*
     * Đọc 1 byte:
     * disable ACK trước khi clear ADDR
     */
    I2Cx->CR1 &= ~I2C_CR1_ACK;

    (void)I2Cx->SR1;
    (void)I2Cx->SR2;

    /* Generate STOP */
    I2Cx->CR1 |= I2C_CR1_STOP;

    while (!(I2Cx->SR1 & I2C_SR1_RXNE))
    {
    }

    data = (uint8_t)I2Cx->DR;

    /* Enable ACK lại */
    I2Cx->CR1 |= I2C_CR1_ACK;

    return data;
}

/* =========================================================
 * LCD FUNCTIONS
 * LCD nằm trên I2C1
 * ========================================================= */

static void lcd_expander_write(uint8_t data)
{
    i2c_write_byte(
        I2C1,
        LCD_ADDR,
        (uint8_t)(data | LCD_BACKLIGHT));
}

static void lcd_pulse_enable(uint8_t data)
{
    lcd_expander_write(
        (uint8_t)(data | LCD_EN));

    delay_ms(1u);

    lcd_expander_write(
        (uint8_t)(data & ~LCD_EN));

    delay_ms(1u);
}

static void lcd_write_nibble(
    uint8_t nibble,
    uint8_t rs)
{
    uint8_t data;

    data =
        (uint8_t)((nibble & 0x0Fu) << 4);

    if (rs != 0u)
    {
        data |= LCD_RS;
    }

    lcd_expander_write(data);

    lcd_pulse_enable(data);
}

static void lcd_command(uint8_t command)
{
    lcd_write_nibble(
        (uint8_t)(command >> 4),
        0u);

    lcd_write_nibble(
        (uint8_t)(command & 0x0Fu),
        0u);

    delay_ms(2u);
}

static void lcd_data(uint8_t data)
{
    lcd_write_nibble(
        (uint8_t)(data >> 4),
        1u);

    lcd_write_nibble(
        (uint8_t)(data & 0x0Fu),
        1u);

    delay_ms(1u);
}

static void lcd_init(void)
{
    delay_ms(50u);

    lcd_write_nibble(0x03u, 0u);
    delay_ms(5u);

    lcd_write_nibble(0x03u, 0u);
    delay_ms(5u);

    lcd_write_nibble(0x03u, 0u);
    delay_ms(5u);

    lcd_write_nibble(0x02u, 0u);

    /* 4-bit, 2 line */
    lcd_command(0x28u);

    /* Display OFF */
    lcd_command(0x08u);

    /* Clear */
    lcd_command(0x01u);

    delay_ms(5u);

    /* Entry mode */
    lcd_command(0x06u);

    /* Display ON */
    lcd_command(0x0Cu);
}

static void lcd_clear(void)
{
    lcd_command(0x01u);

    delay_ms(3u);
}

static void lcd_set_cursor(
    uint8_t row,
    uint8_t col)
{
    uint8_t addr;

    if (row == 0u)
    {
        addr = col;
    }
    else
    {
        addr = (uint8_t)(0x40u + col);
    }

    lcd_command(
        (uint8_t)(0x80u | addr));
}

static void lcd_print(const char *str)
{
    while (*str != '\0')
    {
        lcd_data((uint8_t)*str);

        str++;
    }
}

/* =========================================================
 * INTEGER -> STRING
 * tránh cần sprintf()
 * ========================================================= */
static void lcd_print_int(int16_t value)
{
    char buffer[8];
    uint8_t index = 0u;
    uint8_t i;

    uint16_t number;

    if (value < 0)
    {
        lcd_data('-');

        number =
            (uint16_t)(-value);
    }
    else
    {
        number =
            (uint16_t)value;
    }

    if (number == 0u)
    {
        lcd_data('0');

        return;
    }

    while (number > 0u)
    {
        buffer[index++] =
            (char)('0' + (number % 10u));

        number /= 10u;
    }

    for (i = index; i > 0u; i--)
    {
        lcd_data(
            (uint8_t)buffer[i - 1u]);
    }
}

/* =========================================================
 * MPU6050
 * MPU nằm trên I2C2
 * ========================================================= */

static uint8_t mpu6050_init(void)
{
    uint8_t who_am_i;

    /*
     * WHO_AM_I
     * bình thường = 0x68
     */
    who_am_i =
        i2c_read_register(
            I2C2,
            MPU6050_ADDR,
            MPU_WHO_AM_I);

    if (who_am_i != 0x68u)
    {
        return 0u;
    }

    /*
     * MPU6050 mặc định đang sleep.
     *
     * PWR_MGMT_1 = 0
     * -> wake up
     */
    i2c_write_register(
        I2C2,
        MPU6050_ADDR,
        MPU_PWR_MGMT_1,
        0x00u);

    delay_ms(100u);

    return 1u;
}

/* =========================================================
 * Đọc Accel X
 * ========================================================= */
static int16_t mpu6050_read_accel_x(void)
{
    uint8_t high_byte;
    uint8_t low_byte;

    int16_t value;

    high_byte =
        i2c_read_register(
            I2C2,
            MPU6050_ADDR,
            MPU_ACCEL_X_H);

    low_byte =
        i2c_read_register(
            I2C2,
            MPU6050_ADDR,
            MPU_ACCEL_X_L);

    value =
        (int16_t)(((uint16_t)high_byte << 8) |
                  low_byte);

    return value;
}

/* =========================================================
 * MAIN
 * ========================================================= */
int main(void)
{
    uint8_t mpu_ok;

    int16_t accel_x;

    /* GPIO cho cả I2C1 + I2C2 */
    i2c_gpio_init();

    /*
     * I2C1
     *
     * PB6 = SCL
     * PB7 = SDA
     *
     * dành cho LCD
     */
    i2c1_init();

    /*
     * I2C2
     *
     * PB10 = SCL
     * PB11 = SDA
     *
     * dành cho MPU6050
     */
    i2c2_init();

    /* LCD */
    lcd_init();

    lcd_clear();

    /* MPU6050 */
    mpu_ok = mpu6050_init();

    if (mpu_ok == 0u)
    {
        lcd_set_cursor(0u, 0u);

        lcd_print("MPU ERROR");

        lcd_set_cursor(1u, 0u);

        lcd_print("Check I2C2");

        while (1)
        {
        }
    }

    /* MPU OK */
    lcd_set_cursor(0u, 0u);

    lcd_print("MPU6050 OK");

    while (1)
    {
        /*
         * Đọc Accelerometer X
         */
        accel_x =
            mpu6050_read_accel_x();

        /*
         * Dòng thứ 2
         */
        lcd_set_cursor(1u, 0u);

        lcd_print("AX:");

        lcd_print_int(accel_x);

        /*
         * Xóa phần số cũ còn dư
         */
        lcd_print("      ");

        delay_ms(300u);
    }
}