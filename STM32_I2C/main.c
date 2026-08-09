#include "stm32f103xb.h"
#include <stdint.h>

/* =========================================================
 * LCD1602 + PCF8574
 * PB6 = I2C1_SCL
 * PB7 = I2C1_SDA
 * ========================================================= */

#define LCD_I2C_ADDRESS 0x27u

/*
 * Mapping PCF8574 phổ biến:
 *
 * P0 -> RS
 * P1 -> RW
 * P2 -> EN
 * P3 -> Backlight
 * P4 -> D4
 * P5 -> D5
 * P6 -> D6
 * P7 -> D7
 */

#define LCD_RS 0x01u
#define LCD_RW 0x02u
#define LCD_EN 0x04u
#define LCD_BACKLIGHT 0x08u

/* =========================================================
 * Delay đơn giản
 * Giả sử STM32 chạy khoảng 8 MHz
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
 * I2C GPIO
 * PB6 = SCL
 * PB7 = SDA
 * ========================================================= */

static void i2c1_gpio_init(void)
{
    /* Enable GPIOB clock */
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;

    /*
     * PB6, PB7:
     * MODE = 11 : Output 50 MHz
     * CNF  = 11 : Alternate Function Open Drain
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
}

/* =========================================================
 * I2C1 Init
 * Standard Mode ~100 kHz
 *
 * Giả sử PCLK1 = 8 MHz
 * ========================================================= */

static void i2c1_init(void)
{
    /* Enable I2C1 clock */
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

    /* Reset I2C1 */
    RCC->APB1RSTR |= RCC_APB1RSTR_I2C1RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_I2C1RST;

    /*
     * PCLK1 = 8 MHz
     */
    I2C1->CR2 = 8u;

    /*
     * Standard mode 100 kHz
     *
     * CCR = 8 MHz / (2 * 100 kHz)
     *     = 40
     */
    I2C1->CCR = 40u;

    /*
     * TRISE = FREQ + 1
     *       = 8 + 1
     */
    I2C1->TRISE = 9u;

    /* Enable I2C */
    I2C1->CR1 |= I2C_CR1_PE;
}

/* =========================================================
 * Gửi 1 byte tới PCF8574
 * ========================================================= */

static uint8_t i2c1_write_byte(uint8_t address, uint8_t data)
{
    uint32_t timeout;

    /* ---------- START ---------- */

    I2C1->CR1 |= I2C_CR1_START;

    timeout = 100000u;

    while (!(I2C1->SR1 & I2C_SR1_SB))
    {
        if (--timeout == 0u)
        {
            return 0u;
        }
    }

    /* ---------- ADDRESS ---------- */

    I2C1->DR = (uint8_t)(address << 1);

    timeout = 100000u;

    while (!(I2C1->SR1 & I2C_SR1_ADDR))
    {
        if (I2C1->SR1 & I2C_SR1_AF)
        {
            I2C1->SR1 &= ~I2C_SR1_AF;

            I2C1->CR1 |= I2C_CR1_STOP;

            return 0u;
        }

        if (--timeout == 0u)
        {
            I2C1->CR1 |= I2C_CR1_STOP;

            return 0u;
        }
    }

    /*
     * Clear ADDR:
     * đọc SR1 sau đó SR2
     */
    (void)I2C1->SR1;
    (void)I2C1->SR2;

    /* ---------- DATA ---------- */

    timeout = 100000u;

    while (!(I2C1->SR1 & I2C_SR1_TXE))
    {
        if (--timeout == 0u)
        {
            I2C1->CR1 |= I2C_CR1_STOP;

            return 0u;
        }
    }

    I2C1->DR = data;

    /* Chờ truyền xong */
    timeout = 100000u;

    while (!(I2C1->SR1 & I2C_SR1_BTF))
    {
        if (--timeout == 0u)
        {
            I2C1->CR1 |= I2C_CR1_STOP;

            return 0u;
        }
    }

    /* ---------- STOP ---------- */

    I2C1->CR1 |= I2C_CR1_STOP;

    return 1u;
}

/* =========================================================
 * LCD low-level
 * ========================================================= */

static void lcd_expander_write(uint8_t data)
{
    i2c1_write_byte(
        LCD_I2C_ADDRESS,
        (uint8_t)(data | LCD_BACKLIGHT));
}

/*
 * Tạo cạnh Enable:
 *
 * EN = 1
 * EN = 0
 *
 * LCD sẽ chốt dữ liệu.
 */

static void lcd_pulse_enable(uint8_t data)
{
    lcd_expander_write(
        (uint8_t)(data | LCD_EN));

    delay_ms(1u);

    lcd_expander_write(
        (uint8_t)(data & ~LCD_EN));

    delay_ms(1u);
}

/*
 * Gửi 4 bit cao tới LCD
 */

static void lcd_write_nibble(uint8_t nibble, uint8_t mode)
{
    uint8_t data;

    /*
     * D4-D7 nằm trên P4-P7 của PCF8574
     */
    data = (uint8_t)((nibble & 0x0Fu) << 4);

    if (mode != 0u)
    {
        data |= LCD_RS;
    }

    lcd_expander_write(data);

    lcd_pulse_enable(data);
}

/* =========================================================
 * LCD Command
 * ========================================================= */

static void lcd_send_command(uint8_t command)
{
    /*
     * RS = 0 -> command
     */

    lcd_write_nibble(
        (uint8_t)(command >> 4),
        0u);

    lcd_write_nibble(
        (uint8_t)(command & 0x0Fu),
        0u);

    delay_ms(2u);
}

/* =========================================================
 * LCD Data
 * ========================================================= */

static void lcd_send_data(uint8_t data)
{
    /*
     * RS = 1 -> character/data
     */

    lcd_write_nibble(
        (uint8_t)(data >> 4),
        1u);

    lcd_write_nibble(
        (uint8_t)(data & 0x0Fu),
        1u);

    delay_ms(1u);
}

/* =========================================================
 * LCD Init
 * ========================================================= */

static void lcd_init(void)
{
    /*
     * Đợi LCD ổn định sau khi cấp nguồn
     */
    delay_ms(50u);

    /*
     * Chuỗi đặc biệt để chuyển LCD sang 4-bit mode
     */

    lcd_write_nibble(0x03u, 0u);
    delay_ms(5u);

    lcd_write_nibble(0x03u, 0u);
    delay_ms(5u);

    lcd_write_nibble(0x03u, 0u);
    delay_ms(5u);

    /*
     * 0x02 -> 4-bit mode
     */
    lcd_write_nibble(0x02u, 0u);

    /*
     * Function Set
     *
     * 0x28:
     * 4-bit
     * 2 lines
     * 5x8 font
     */
    lcd_send_command(0x28u);

    /*
     * Display OFF
     */
    lcd_send_command(0x08u);

    /*
     * Clear Display
     */
    lcd_send_command(0x01u);

    delay_ms(5u);

    /*
     * Entry Mode
     *
     * Cursor tự tăng
     */
    lcd_send_command(0x06u);

    /*
     * Display ON
     * Cursor OFF
     * Blink OFF
     */
    lcd_send_command(0x0Cu);
}

/* =========================================================
 * Clear LCD
 * ========================================================= */

static void lcd_clear(void)
{
    lcd_send_command(0x01u);

    delay_ms(3u);
}

/* =========================================================
 * Set cursor
 *
 * row = 0 -> dòng 1
 * row = 1 -> dòng 2
 * ========================================================= */

static void lcd_set_cursor(uint8_t row, uint8_t column)
{
    uint8_t address;

    if (row == 0u)
    {
        address = column;
    }
    else
    {
        address = (uint8_t)(0x40u + column);
    }

    lcd_send_command(
        (uint8_t)(0x80u | address));
}

/* =========================================================
 * Print String
 * ========================================================= */

static void lcd_print(const char *str)
{
    while (*str != '\0')
    {
        lcd_send_data((uint8_t)*str);

        str++;
    }
}

/* =========================================================
 * MAIN
 * ========================================================= */

int main(void)
{
    /*
     * PB6 -> SCL
     * PB7 -> SDA
     */

    i2c1_gpio_init();

    i2c1_init();

    /*
     * Khởi tạo LCD
     */
    lcd_init();

    /*
     * Xóa LCD
     */
    lcd_clear();

    /*
     * Dòng 1, cột 1
     */
    lcd_set_cursor(0u, 0u);

    /*
     * In chuỗi
     */
    lcd_print("Hello World");

    while (1)
    {
        /*
         * Không cần làm gì thêm
         */
    }
}