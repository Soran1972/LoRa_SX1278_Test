#include "project_app.h"
#include "main.h"
#include "i2c_lcd.h"
#include "SEGGER_RTT.h"

static uint32_t heartbeat_last_ms; /* Timer: Last heartbeat transition. */

/*======================================================================================================================
 *  Function  : project_app_init
 *  Purpose   : Initialize test diagnostics and show the LCD startup screen.
 *====================================================================================================================*/
void project_app_init(void)
{
    SEGGER_RTT_Init();
    SEGGER_RTT_WriteString(0, "LoRa_SX1278_Test started\r\n");
    lcd_init();
    lcd_put_cur(0, 0);
    lcd_send_string("LoRa SX1278 Test");
    lcd_put_cur(1, 0);
    lcd_send_string("LCD ready" );
    heartbeat_last_ms = HAL_GetTick();
}

/*======================================================================================================================
 *  Function  : project_app_run
 *  Purpose   : Run the heartbeat without blocking background services.
 *====================================================================================================================*/
void project_app_run(void)
{
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - heartbeat_last_ms) >= 500)
    {
        heartbeat_last_ms = now;
        HAL_GPIO_TogglePin(HB_LED_GPIO_Port, HB_LED_Pin);
    }
}
