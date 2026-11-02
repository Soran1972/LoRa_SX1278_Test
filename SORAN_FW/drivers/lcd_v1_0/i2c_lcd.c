
/** Put this in the src folder **/

#include "i2c_lcd.h"

#include <math.h>

extern I2C_HandleTypeDef hi2c1;  // change your handler here accordingly

#define SLAVE_ADDRESS_LCD 0x4E // change this according to ur setup
int8_t BK =0  ;// use  for ON OFF back light

void lcd_send_cmd (char cmd)
{
  char data_u, data_l;
	uint8_t data_t[4];
	data_u = (cmd&0xf0);
	data_l = ((cmd<<4)&0xf0);
	data_t[0] = (data_u|0x0C);  //en=1, rs=0
	data_t[1] = (data_u|0x08);  //en=0, rs=0
	data_t[2] = (data_l|0x0C);  //en=1, rs=0
	data_t[3] = (data_l|0x08);  //en=0, rs=0
	HAL_I2C_Master_Transmit (&hi2c1, SLAVE_ADDRESS_LCD,(uint8_t *) data_t, 4, 100);
}

void lcd_send_data (char data)
{
	char data_u, data_l;
	uint8_t data_t[4];
	data_u = (data&0xf0);
	data_l = ((data<<4)&0xf0);
	data_t[0] = (data_u|0x0D);  //en=1, rs=0
	data_t[1] = (data_u|0x09);  //en=0, rs=0
	data_t[2] = (data_l|0x0D);  //en=1, rs=0
	data_t[3] = (data_l|0x09);  //en=0, rs=0
	HAL_I2C_Master_Transmit (&hi2c1, SLAVE_ADDRESS_LCD,(uint8_t *) data_t, 4, 100);
}

void lcd_clear (void)
{
	//lcd_send_cmd (0x01);
	 for (int i=0; i<80; i++)
	 {
		lcd_send_data (' ');
	 }
}
//******************** LCD for 16x2**************************************
/*
void lcd_put_cur(int row, int col)
{
    switch (row)
    {
        case 0:
            col |= 0x80;
            break;
        case 1:
            col |= 0xC0;
            break;
    }

    lcd_send_cmd (col);
}
*/


//____________________________________________________________________________
//*****************************for LCD 20x4***************************************
void lcd_put_cur(int row, int col)
{
    switch (row)
    {
        case 0:
            col=(0x80|(0x00+col));//i adeed  col
            break;
        case 1:
            col=(0x80|(0x40+col));
            break;
        case 2:
        	col =(0x80|(0x14+col));
        	  break;
        case 3:
           	col =(0x80|(0x54+col));
         	  break;
    }

    lcd_send_cmd (col);
}
//**************************************************************

void lcd_init (void)
{
	// 4 bit initialisation
	HAL_Delay(50);  // wait for >40ms
	lcd_send_cmd (0x30);
	HAL_Delay(5);  // wait for >4.1ms
	lcd_send_cmd (0x30);
	HAL_Delay(1);  // wait for >100us
	lcd_send_cmd (0x30);
	HAL_Delay(10);
	lcd_send_cmd (0x20);  // 4bit mode
	HAL_Delay(10);

  // dislay initialisation
	lcd_send_cmd (0x28); // Function set --> DL=0 (4 bit mode), N = 1 (2 line display) F = 0 (5x8 characters)
	HAL_Delay(1);
	lcd_send_cmd (0x08); //Display on/off control --> D=0,C=0, B=0  ---> display off
	HAL_Delay(1);
	lcd_send_cmd (0x01);  // clear display
	HAL_Delay(1);
	HAL_Delay(1);
	lcd_send_cmd (0x06); //Entry mode set --> I/D = 1 (increment cursor) & S = 0 (no shift)
	HAL_Delay(1);
	lcd_send_cmd (0x0C); //Display on/off control --> D = 1, C and B = 0. (Cursor and blink, last two bits)
}

void lcd_send_string (char *str)
{
	while (*str) lcd_send_data (*str++);
}
//*******************************************************************************************
// this functions added by SORAN ELE
// ON OFF LCD backlight HD44780U PCF8574T ic P3 conected to NPN trnasistor Collercor connected to back light
// 0 = OFF back light , 1= ON Backlight


void lcd_bk_off(void) //back light OFF
{

	BK= 0B11110011;// bit2 E --disable pin
	HAL_I2C_Master_Transmit (&hi2c1, SLAVE_ADDRESS_LCD,(uint8_t *) &BK, 1, 100);//send BK one Byte .Bit3 if = 0 Back Light Transisor OFF
}

void lcd_bk_on(void)//back light ON
{

	BK=0B11111011;// bit2 E --disable pin
	HAL_I2C_Master_Transmit (&hi2c1, SLAVE_ADDRESS_LCD,(uint8_t *) &BK, 1, 100);//send BK one Byte .Bit3 if = 1 Back Light Transisor ON

}
//***************** (show unsigned integer in default position) *************************** by GPT chat

void lcd_send_integer(uint32_t num) {
    // Max number of digits for a 32-bit unsigned integer is 10
    char buffer[11];
    int i = 0;

    // Convert integer to string
    do {
        buffer[i++] = num % 10 + '0'; // Convert the digit to ASCII character
        num /= 10; // Shift the number right by one digit
    } while (num);

    // Send digits in reverse order
    while (i--) {
        lcd_send_data(buffer[i]); // Send each digit to the LCD
    }
}
//****************(show unsigned integer in defined position )********************************
void lcd_send_integer_pos(uint32_t num, uint8_t row, uint8_t col) {
    // Max number of digits for a 32-bit unsigned integer is 10
    char buffer[11]; // إنشاء مصفوفة لتخزين الأحرف
    int i = 0;

    // Convert integer to string
    do {
        buffer[i++] = num % 10 + '0'; // تحويل الرقم إلى حرف ASCII
        num /= 10; // نقل الرقم لليمين بنسبة رقم واحد
    } while (num);

    // Send digits in reverse order
    while (i--) {
  //      LCD_set_cursor(row, col); // تحديد موقع العرض
        lcd_put_cur( row,  col);
        lcd_send_data(buffer[i]); // إرسال كل رقم إلى شاشة العرض LCD
        col++; // زيادة العمود بعد إرسال كل رقم
    }
}
//*********************** (show float defined position inside function)****************
void lcd_send_float_pos(float num, uint8_t row, uint8_t col, uint8_t decimal_places) {
    // تحويل القيمة العشرية إلى نص
    char buffer[16]; // مصفوفة لتخزين الأحرف
    int whole_part, fractional_part;

    // تقسيم القيمة إلى الجزء الصحيح والجزء العشري
    whole_part = (int)num;
    fractional_part = (int)((num - whole_part) * pow(10, decimal_places));

    // تحويل الجزء الصحيح إلى نص
    int i = 0;
    do {
        buffer[i++] = whole_part % 10 + '0'; // تحويل الرقم إلى حرف ASCII
        whole_part /= 10; // نقل الرقم لليمين بنسبة رقم واحد
    } while (whole_part);

    // إرسال الجزء الصحيح إلى شاشة العرض
    while (i--) {
    	 lcd_put_cur( row,  col); // تحديد موقع العرض
        lcd_send_data(buffer[i]); // إرسال كل رقم إلى شاشة العرض LCD
        col++; // زيادة العمود بعد إرسال كل رقم
    }

    // إرسال النقطة العشرية إلى شاشة العرض
    lcd_put_cur( row,  col++);
    lcd_send_data('.');

    // تحويل الجزء العشري إلى نص
    i = 0;
    while (decimal_places--) {
        buffer[i++] = fractional_part % 10 + '0'; // تحويل الرقم إلى حرف ASCII
        fractional_part /= 10; // نقل الرقم لليمين بنسبة رقم واحد
    }

    // إرسال الجزء العشري إلى شاشة العرض
    while (i--) {
    	lcd_put_cur( row,  col); // تحديد موقع العرض
        lcd_send_data(buffer[i]); // إرسال كل رقم إلى شاشة العرض LCD
        col++; // زيادة العمود بعد إرسال كل رقم
    }
}
//*********************(show float without defined position inside function)*************************************************************

void lcd_send_float(float num, uint8_t decimal_places) {
    // Convert the floating-point value to an integer
    int whole_part, fractional_part;
    char buffer[16]; // Buffer to store characters

    // Split the value into integer and fractional parts
    whole_part = (int)num;
    fractional_part = (int)((num - whole_part) * pow(10, decimal_places));

    // Convert the whole part to a string
    int i = 0;
    do {
        buffer[i++] = whole_part % 10 + '0'; // Convert the digit to ASCII character
        whole_part /= 10; // Shift the number right by one digit
    } while (whole_part);

    // Send the whole part to the LCD
    while (i--) {
        lcd_send_data(buffer[i]); // Send each digit to the LCD
    }

    // Send the decimal point to the LCD
    lcd_send_data('.');

    // Convert the fractional part to a string and send it to the LCD
    i = decimal_places;
    do {
        buffer[--i] = fractional_part % 10 + '0'; // Convert the digit to ASCII character
        fractional_part /= 10; // Shift the number right by one digit
    } while (fractional_part && i);

    // Send the fractional part to the LCD
    while (i < decimal_places) {
        lcd_send_data(buffer[i]); // Send each digit to the LCD
        i++;
    }
}

//********************************************************************************************************
// ================================================================
// 🔍 LCD Self Check Function
// ---------------------------------------------------------------
//  - Checks if LCD (PCF8574 module) responds correctly on I2C
//  - If communication fails, it reinitializes the LCD automatically
//  - Displays "LCD OK" once at startup
//  - Returns: 1 = LCD OK, 0 = LCD Reinitialized after error
// ================================================================
uint8_t lcd_self_check(void)
{
    uint8_t result;
    static uint8_t first_run = 1;  // Show message only at startup

    // Try to send a dummy command (Clear Display)
    result = HAL_I2C_Master_Transmit(&hi2c1, SLAVE_ADDRESS_LCD, (uint8_t[]){0x00}, 1, 50);


    if (result != HAL_OK)
    {
        // LCD not responding — reinitialize it
        lcd_init();
        HAL_Delay(10);
        lcd_clear();
        lcd_put_cur(0, 0);
        lcd_send_string("LCD Reinit...");
        return 0;  // LCD was reinitialized
    }

    // Show "LCD OK" only the first time after power-up
    if (first_run)
    {
        first_run = 0;
        lcd_clear();
        lcd_put_cur(0, 0);
        lcd_send_string("LCD OK");
        HAL_Delay(500);
        lcd_clear();
    }

    return 1;  // LCD OK
}

//*************************************8 bit print*******************************************************************
void lcd_binary_8(uint8_t value) {
     char str[9]; // 8 بت + null terminator
     for(int i = 7; i >= 0; i--) {
         str[7 - i] = (value & (1 << i)) ? '1' : '0';
     }
     str[8] = '\0';  // نهاية السطر
     lcd_send_string(str);
 }
