#include "stm32f1xx_hal.h"

/* Delay đơn giản bằng vòng lặp */
static void delay_ms(uint32_t ms);

/* Khởi tạo GPIO */
static void GPIO_Init(void);

int main(void)
{
    /*
     * Không gọi HAL_Init() ở giai đoạn này vì project tối giản hiện chưa
     * thêm đầy đủ HAL Cortex/SysTick.
     */

    GPIO_Init();

    while (1)
    {
        /*
         * LED tích hợp trên Blue Pill thường nối vào PC13 theo kiểu active-low:
         * PC13 = LOW  -> LED sáng
         * PC13 = HIGH -> LED tắt
         */
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);

        delay_ms(250); // Delay 250ms
    }
}

static void GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Bước 1: Bật clock cho GPIOC */
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /*
     * Đặt PC13 ở HIGH trước khi cấu hình.
     * Với LED active-low, HIGH tương ứng LED tắt.
     */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    /* Bước 2: Cấu hình PC13 làm Output Push-Pull */
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

static void delay_ms(uint32_t ms)
{
    /*
     * Delay thô để kiểm tra GPIO.
     * Thời gian có thể không chính xác tuyệt đối do phụ thuộc clock
     * và mức tối ưu của compiler.
     */
    for (uint32_t i = 0; i < ms; i++)
    {
        for (volatile uint32_t j = 0; j < 9000U; j++)
        {
            __asm volatile("nop");
        }
    }
}