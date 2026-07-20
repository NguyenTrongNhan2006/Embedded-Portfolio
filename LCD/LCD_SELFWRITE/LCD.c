#include "LCD.h"

/* Cap phat bo nho cho myLCD tai duy nhat file nay. */
LCD myLCD;

void LCD_Write_Byte(uint8_t data, uint8_t mode)
{
    /* RW = 0: che do ghi. */
    HAL_GPIO_WritePin(myLCD.PORT_R, myLCD.RW, GPIO_PIN_RESET);

    /* RS = 1: du lieu, RS = 0: lenh. */
    if (mode == MDATA)
    {
        HAL_GPIO_WritePin(myLCD.PORT_R, myLCD.RS, GPIO_PIN_SET);
    }
    else
    {
        HAL_GPIO_WritePin(myLCD.PORT_R, myLCD.RS, GPIO_PIN_RESET);
    }

    HAL_GPIO_WritePin(myLCD.PORT_DATA, myLCD.D0,
                      (data & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(myLCD.PORT_DATA, myLCD.D1,
                      (data & 0x02) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(myLCD.PORT_DATA, myLCD.D2,
                      (data & 0x04) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(myLCD.PORT_DATA, myLCD.D3,
                      (data & 0x08) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(myLCD.PORT_DATA, myLCD.D4,
                      (data & 0x10) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(myLCD.PORT_DATA, myLCD.D5,
                      (data & 0x20) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(myLCD.PORT_DATA, myLCD.D6,
                      (data & 0x40) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(myLCD.PORT_DATA, myLCD.D7,
                      (data & 0x80) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    /* Tao xung Enable. */
    HAL_GPIO_WritePin(myLCD.PORT_R, myLCD.En, GPIO_PIN_RESET);
    HAL_Delay(1);

    HAL_GPIO_WritePin(myLCD.PORT_R, myLCD.En, GPIO_PIN_SET);
    HAL_Delay(1);

    HAL_GPIO_WritePin(myLCD.PORT_R, myLCD.En, GPIO_PIN_RESET);
    HAL_Delay(1);
}

void LCD_Write_cmd(uint8_t data)
{
    LCD_Write_Byte(data, MCMD);
}

void LCD_Write_data(uint8_t data)
{
    LCD_Write_Byte(data, MDATA);
}

void LCD_Print(const char *str)
{
    while (*str != '\0')
    {
        LCD_Write_data((uint8_t)*str);
        str++;
    }
}

void LCD_Set_Cursor(uint8_t row, uint8_t column)
{
    if (column > 15)
    {
        column = 15;
    }

    if (row == 1)
    {
        LCD_Write_cmd(0x80 + column);
    }
    else if (row == 2)
    {
        LCD_Write_cmd(0xC0 + column);
    }
}

void LCD_Init(void)
{
    HAL_GPIO_WritePin(myLCD.PORT_R, myLCD.RW, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(myLCD.PORT_R, myLCD.RS, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(myLCD.PORT_R, myLCD.En, GPIO_PIN_RESET);

    HAL_Delay(50);

    LCD_Write_cmd(0x30);
    HAL_Delay(5);

    LCD_Write_cmd(0x30);
    HAL_Delay(2);

    LCD_Write_cmd(0x30);
    HAL_Delay(2);

    LCD_Write_cmd(0x38); /* 8 bit, 2 dong, font 5x8. */
    HAL_Delay(2);

    LCD_Write_cmd(0x08); /* Tat man hinh. */
    HAL_Delay(2);

    LCD_Write_cmd(0x01); /* Xoa man hinh. */
    HAL_Delay(3);

    LCD_Write_cmd(0x06); /* Con tro di sang phai. */
    HAL_Delay(2);

    LCD_Write_cmd(0x0C); /* Bat man hinh, tat con tro. */
    HAL_Delay(2);
}