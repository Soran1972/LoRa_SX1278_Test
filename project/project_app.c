#include "project_app.h"
#include "main.h"
#include "i2c_lcd.h"
#include "SEGGER_RTT.h"
#include "sx1278.h"
#include <inttypes.h>
#include <stdio.h>

static uint32_t heartbeat_last_ms; /* Timer: Last heartbeat transition. */
#define TEST_DEBOUNCE_MS 30

static uint32_t test_changed_ms;
static uint32_t test_count;
static GPIO_PinState test_sample;
static GPIO_PinState test_stable;

extern SPI_HandleTypeDef hspi1;
static sx1278_t radio;

void HAL_GPIO_EXTI_Rising_Callback(uint16_t pin)
{
    if (pin == LORA_DIO0_Pin)
        sx1278_notify_irq(&radio);
}

static void show_radio_version(void)
{
    const sx1278_config_t config = {
        .spi = &hspi1,
        .nss_port = LORA_NSS_GPIO_Port,
        .nss_pin = LORA_NSS_Pin,
        .reset_port = LORA_RESET_GPIO_Port,
        .reset_pin = LORA_RESET_Pin
    };
    uint8_t version;
    char line[21];
    HAL_StatusTypeDef status = sx1278_init(&radio, &config, &version);
    (void)snprintf(line, sizeof(line), "SX1278 %02X %s", (unsigned)version,
                   status == HAL_OK ? "OK" : "FAIL");
    lcd_put_cur(3, 0);
    lcd_send_string(line);
    SEGGER_RTT_WriteString(0, line);
    SEGGER_RTT_WriteString(0, "\r\n");
}

static void test_show_count(void)
{
    char line[19];
    (void)snprintf(line, sizeof(line), "TEST: %" PRIu32 "\r\n", test_count);
    SEGGER_RTT_WriteString(0, line);

    (void)snprintf(line, sizeof(line), "TEST: %-10" PRIu32, test_count);
    lcd_put_cur(2, 0);
    lcd_send_string(line);
}

static void test_button_run(uint32_t now)
{
    GPIO_PinState sample = HAL_GPIO_ReadPin(TEST_GPIO_Port, TEST_Pin);
    if (sample != test_sample)
    {
        test_sample = sample;
        test_changed_ms = now;
    }

    if (test_sample != test_stable && (now - test_changed_ms) >= TEST_DEBOUNCE_MS)
    {
        test_stable = test_sample;
        if (test_stable == GPIO_PIN_RESET)
        {
            ++test_count;
            test_show_count();
        }
    }
}

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
    test_sample = HAL_GPIO_ReadPin(TEST_GPIO_Port, TEST_Pin);
    test_stable = test_sample;
    test_show_count();
    show_radio_version();
    heartbeat_last_ms = HAL_GetTick();
}

/*======================================================================================================================
 *  Function  : project_app_run
 *  Purpose   : Service the heartbeat and debounce the TEST button.
 *====================================================================================================================*/
void project_app_run(void)
{
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - heartbeat_last_ms) >= 500)
    {
        heartbeat_last_ms = now;
        HAL_GPIO_TogglePin(HB_LED_GPIO_Port, HB_LED_Pin);
    }
    test_button_run(now);
    uint8_t length;
    (void)sx1278_service(&radio, NULL, 0, &length);
}
