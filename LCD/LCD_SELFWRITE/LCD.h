#ifndef LCD_H
#define LCD_H

#include <stdint.h>
#include "stm32f1xx_hal.h"

#define MDATA 1
#define MCMD 0

typedef struct
{
    GPIO_TypeDef *PORT_DATA;
    GPIO_TypeDef *PORT_R;

    uint16_t D0;
    uint16_t D1;
    uint16_t D2;
    uint16_t D3;
    uint16_t D4;
    uint16_t D5;
    uint16_t D6;
    uint16_t D7;

    uint16_t RS;
    uint16_t RW;
    uint16_t En;
} LCD;

/* Bien myLCD duoc dinh nghia mot lan trong main.c. */
extern LCD myLCD;

void LCD_Init(void);
void LCD_Write_Byte(uint8_t data, uint8_t mode);
void LCD_Write_cmd(uint8_t data);
void LCD_Write_data(uint8_t data);
void LCD_Print(const char *str);
void LCD_Set_Cursor(uint8_t row, uint8_t column);

#endif