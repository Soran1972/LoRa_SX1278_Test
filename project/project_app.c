#include "project_app.h"
#include "main.h"
#include "i2c_lcd.h"
#include "SEGGER_RTT.h"
#include "sx1278.h"
#include <inttypes.h>
#include <stdio.h>

/*--------------------------------[ Local test inputs ]--------------------------------*/

static uint32_t heartbeat_last_ms; /* Timer: Last heartbeat transition. */
#define TEST_DEBOUNCE_MS 30

static uint32_t test_changed_ms;
static uint32_t test_count;
static GPIO_PinState test_sample;
static GPIO_PinState test_stable;

/*--------------------------------[ Radio test protocol ]--------------------------------*/

enum
{
    LINK_PACKET_SIZE = 3,      /* One type byte and a 16-bit sequence. */
    LINK_REQUEST = 0x01,       /* Test request triggered by the button. */
    LINK_ACK = 0x02,           /* Reply containing the request sequence. */
    LINK_REPLY_DELAY_MS = 60,  /* Allow the requesting radio to return to RX. */
    LINK_ACK_TIMEOUT_MS = 1000 /* Wait for the matching ACK after TX completes. */
};

typedef enum
{
    LINK_IDLE,          /* Ready to start or answer a test. */
    LINK_REQUEST_TX,    /* Request is on the air. */
    LINK_WAIT_ACK,      /* Request sent; wait for its matching ACK. */
    LINK_REPLY_PENDING, /* A reply is queued for the turnaround delay. */
    LINK_REPLY_TX       /* Reply is on the air. */
} link_state_t;

extern SPI_HandleTypeDef hspi1;
static link_state_t link_state;  /* State: Current request or reply phase. */
static uint16_t next_sequence;   /* Counter: Sequence of local test requests. */
static uint16_t active_sequence; /* Value: Sequence of the active request or reply. */
static uint32_t link_phase_ms;   /* Timer: Start of ACK wait or reply delay. */
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

/*--------------------[ Display and RTT status ]--------------------*/

static void link_show_status(const char *status)
{
    char line[21];
    (void)snprintf(line, sizeof(line), "%s #%u", status, (unsigned)active_sequence);
    SEGGER_RTT_WriteString(0, line);
    SEGGER_RTT_WriteString(0, "\r\n");

    /* Replace the full row so shorter messages cannot leave old characters. */
    char lcd_line[21];
    (void)snprintf(lcd_line, sizeof(lcd_line), "%-20s", line);
    lcd_put_cur(3, 0);
    lcd_send_string(lcd_line);
}

/*--------------------[ Encode one request or reply ]--------------------*/

static HAL_StatusTypeDef link_send(uint8_t type, uint16_t sequence)
{
    const uint8_t packet[LINK_PACKET_SIZE] = {
        type,
        (uint8_t)(sequence >> 8), /* Sequence high byte first. */
        (uint8_t)sequence
    };
    return sx1278_send(&radio, packet, sizeof(packet));
}

/*--------------------[ Start a request on a button press ]--------------------*/

static void link_start_request(void)
{
    if (link_state != LINK_IDLE)
        return;

    active_sequence = ++next_sequence;
    if (link_send(LINK_REQUEST, active_sequence) == HAL_OK)
        link_state = LINK_REQUEST_TX;
    else
        link_show_status("SEND ERROR");
}

/*--------------------[ Process radio events in the main loop ]--------------------*/

static void link_service_run(void)
{
    uint8_t packet[LINK_PACKET_SIZE];
    uint8_t length;
    sx1278_event_t event = sx1278_service(&radio, packet, sizeof(packet), &length);
    uint32_t now = HAL_GetTick();

    switch (event)
    {
        case SX1278_TX_DONE:
            if (link_state == LINK_REQUEST_TX)
            {
                link_state = LINK_WAIT_ACK;
                link_phase_ms = now;
                link_show_status("REQ SENT");
            }
            else if (link_state == LINK_REPLY_TX)
            {
                link_state = LINK_IDLE;
                link_show_status("ACK SENT");
            }
            break;

        case SX1278_RX_DONE:
            if (length == LINK_PACKET_SIZE)
            {
                uint16_t sequence = ((uint16_t)packet[1] << 8) | packet[2];
                if (packet[0] == LINK_ACK && link_state == LINK_WAIT_ACK &&
                    sequence == active_sequence && (now - link_phase_ms) < LINK_ACK_TIMEOUT_MS)
                {
                    link_state = LINK_IDLE;
                    link_show_status("ACK OK");
                }
                else if (packet[0] == LINK_REQUEST && link_state == LINK_IDLE)
                {
                    active_sequence = sequence;
                    link_state = LINK_REPLY_PENDING;
                    link_phase_ms = now;
                }
            }
            break;

        case SX1278_TX_TIMEOUT:
            link_state = LINK_IDLE;
            link_show_status("TX TIMEOUT");
            break;

        case SX1278_IO_ERROR:
            link_state = LINK_IDLE;
            link_show_status("SPI ERROR");
            break;

        default: /* No event, or the driver rejected a packet. */
            break;
    }

    if (link_state == LINK_WAIT_ACK && (now - link_phase_ms) >= LINK_ACK_TIMEOUT_MS)
    {
        link_state = LINK_IDLE;
        link_show_status("TIMEOUT");
    }
    else if (link_state == LINK_REPLY_PENDING && (now - link_phase_ms) >= LINK_REPLY_DELAY_MS)
    {
        HAL_StatusTypeDef status = link_send(LINK_ACK, active_sequence);
        if (status == HAL_OK)
            link_state = LINK_REPLY_TX;
        else if (status != HAL_BUSY)
        {
            link_state = LINK_IDLE;
            link_show_status("REPLY ERROR");
        }
    }
}

/*--------------------------------[ TEST button debounce ]--------------------------------*/

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
            link_start_request();
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
 *  Purpose   : Service radio events, heartbeat and the TEST button.
 *====================================================================================================================*/
void project_app_run(void)
{
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - heartbeat_last_ms) >= 500)
    {
        heartbeat_last_ms = now;
        HAL_GPIO_TogglePin(HB_LED_GPIO_Port, HB_LED_Pin);
    }
    link_service_run();
    test_button_run(now);
}
