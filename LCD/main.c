#include "stm32f1xx_hal.h"
#include "LCD.h"

void GPIO_Init(void);


int main(void)
{
    HAL_Init();
    GPIO_Init();

    /* D0 den D7 nam tren GPIOA. */
    myLCD.PORT_DATA = GPIOA;
    myLCD.D0 = GPIO_PIN_0;
    myLCD.D1 = GPIO_PIN_1;
    myLCD.D2 = GPIO_PIN_2;
    myLCD.D3 = GPIO_PIN_3;
    myLCD.D4 = GPIO_PIN_4;
    myLCD.D5 = GPIO_PIN_5;
    myLCD.D6 = GPIO_PIN_6;
    myLCD.D7 = GPIO_PIN_7;

    /* RS, RW va EN nam tren GPIOB. */
    myLCD.PORT_R = GPIOB;
    myLCD.RS = GPIO_PIN_0;
    myLCD.RW = GPIO_PIN_1;
    myLCD.En = GPIO_PIN_10;

    LCD_Init();
    LCD_Set_Cursor(1, 0);
    LCD_Print("NHAN CR7 SIUU !");

    while (1)
    {
    }
}

void GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* Dua tat ca chan LCD ve muc 0 truoc khi cau hinh. */
    HAL_GPIO_WritePin(GPIOA,
                      GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
                          GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7,
                      GPIO_PIN_RESET);

    HAL_GPIO_WritePin(GPIOB,
                      GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_10,
                      GPIO_PIN_RESET);

    /* Cau hinh PA0 den PA7 lam output. */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
                          GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* Cau hinh PB0, PB1 va PB10 lam output. */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

/* Ngat SysTick tang bien thoi gian cho HAL_Delay(). */
void SysTick_Handler(void)
{
    HAL_IncTick();
}
