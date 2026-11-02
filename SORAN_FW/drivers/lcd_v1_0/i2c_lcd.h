#include "stm32g0xx_hal.h"
#define I2C_LCD_VERSION_MAJOR    1
#define I2C_LCD_VERSION_MINOR    0

void lcd_init (void);   // initialize lcd

void lcd_send_cmd (char cmd);  // send command to the lcd

void lcd_send_data (char data);  // send data to the lcd

void lcd_send_string (char *str);  // send string to the lcd

void lcd_put_cur(int row, int col);  // put cursor at the entered position row (0 or 1), col (0-15);

void lcd_clear (void);

void lcd_bk_off(void);// OFF LCD backlight Creatd BY SORAN ELE

void lcd_bk_on(void);//ON LCD back light Creatd BY SORAN ELE

void lcd_send_integer(uint32_t num);
void lcd_send_integer_pos(uint32_t num, uint8_t row, uint8_t col);//send unsigned integer with postion
void lcd_send_float_pos(float num, uint8_t row, uint8_t col, uint8_t decimal_places);// show float
void lcd_send_float(float num, uint8_t decimal_places); //show flosat without defind postion inside unction
uint8_t lcd_self_check(void);
void lcd_binary_8(uint8_t value) ; //2025
