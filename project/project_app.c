#include "project_app.h"
#include "main.h"
#include "i2c_lcd.h"
#include "SEGGER_RTT.h"
#include "sx1278.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

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
    LINK_ACK_TIMEOUT_MS = 1000, /* Wait for the matching ACK after TX completes. */
    LINK_RESULT_WINDOW = 10     /* Keep the last ten completed local requests. */
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

/*--------------------------------[ Temporary reception diagnostics ]--------------------------------*/

static volatile uint32_t radio_irq_count; /* Counter: DIO0 rising edges seen by EXTI. */
static uint32_t radio_diag_ms;            /* Timer: Limit sampling to 500 ms. */
static char radio_diag_last[21];          /* Text: Last diagnostic line displayed. */

static HAL_StatusTypeDef diagnostic_read_register(uint8_t address, uint8_t *value)
{
    uint8_t spi_out[2] = {address, 0};
    uint8_t spi_in[2];
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(&hspi1, spi_out, spi_in, 2, 10);
    HAL_GPIO_WritePin(LORA_NSS_GPIO_Port, LORA_NSS_Pin, GPIO_PIN_SET);
    if (status == HAL_OK)
        *value = spi_in[1];
    return status;
}

/* Read-only snapshots leave IRQ handling to the driver and EXTI callback. */
static void radio_diagnostics_run(void)
{
    uint32_t now = HAL_GetTick();
    if (link_state != LINK_IDLE || (now - radio_diag_ms) < 500)
        return;

    radio_diag_ms = now;
    uint8_t mode, flags;
    char line[21];
    if (diagnostic_read_register(0x01, &mode) == HAL_OK &&
        diagnostic_read_register(0x12, &flags) == HAL_OK)
    {
        unsigned dio = HAL_GPIO_ReadPin(LORA_DIO0_GPIO_Port, LORA_DIO0_Pin) == GPIO_PIN_SET;
        (void)snprintf(line, sizeof(line), "M%02X F%02X D%u I%03u    ",
                       (unsigned)mode, (unsigned)flags, dio,
                       (unsigned)(radio_irq_count % 1000));
    }
    else
        (void)snprintf(line, sizeof(line), "DIAG SPI ERROR      ");

    if (strcmp(line, radio_diag_last) == 0)
        return;

    strcpy(radio_diag_last, line); /* Both buffers hold at most 20 characters. */
    lcd_put_cur(0, 0);
    lcd_send_string(line);
    SEGGER_RTT_WriteString(0, line);
    SEGGER_RTT_WriteString(0, "\r\n");
}

/*--------------------------------[ Local range measurements ]--------------------------------*/

static uint16_t ack_history;   /* Bit 0 is the newest result: 1 for ACK, 0 for failure. */
static uint8_t result_count;   /* Valid entries in the window, capped at ten. */
static int16_t last_ack_dbm;   /* RSSI of the most recent matching reply. */
static uint8_t last_ack_valid; /* A usable RSSI estimate exists for that reply. */

void HAL_GPIO_EXTI_Rising_Callback(uint16_t pin)
{
    if (pin == LORA_DIO0_Pin)
    {
        ++radio_irq_count;
        sx1278_notify_irq(&radio);
    }
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

/*--------------------[ Range measurements on LCD and RTT ]--------------------*/

static void link_show_measurements(void)
{
    char line[21];
    if (last_ack_valid)
        (void)snprintf(line, sizeof(line), "RSSI: %d dBm", (int)last_ack_dbm);
    else
        (void)snprintf(line, sizeof(line), "RSSI: --");

    char lcd_line[21];
    (void)snprintf(lcd_line, sizeof(lcd_line), "%-20s", line);
    lcd_put_cur(1, 0);
    lcd_send_string(lcd_line);
    SEGGER_RTT_WriteString(0, line);
    SEGGER_RTT_WriteString(0, "\r\n");

    unsigned ack_count = 0;
    for (uint8_t i = 0; i < result_count; ++i)
        ack_count += (ack_history >> i) & 1;

    (void)snprintf(line, sizeof(line), "ACK %u/%u", ack_count, (unsigned)result_count);
    (void)snprintf(lcd_line, sizeof(lcd_line), "%-20s", line);
    lcd_put_cur(2, 0);
    lcd_send_string(lcd_line);
    SEGGER_RTT_WriteString(0, line);
    SEGGER_RTT_WriteString(0, "\r\n");
}

/*--------------------[ Record a completed local request ]--------------------*/

static void link_record_result(uint8_t ack_received)
{
    ack_history = ((ack_history << 1) | ack_received) & ((1 << LINK_RESULT_WINDOW) - 1);
    if (result_count < LINK_RESULT_WINDOW)
        ++result_count;

    /* Leave the previous reading visible if this request received no reply. */
    if (ack_received)
        last_ack_valid = sx1278_packet_rssi(&radio, &last_ack_dbm);

    link_show_measurements();
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
    uint8_t packet[LINK_PACKET_SIZE] = {0};
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
            {
                char report[48];
                (void)snprintf(report, sizeof(report), "RX len=%u bytes=%02X %02X %02X\r\n",
                               (unsigned)length, (unsigned)packet[0],
                               (unsigned)packet[1], (unsigned)packet[2]);
                SEGGER_RTT_WriteString(0, report);
            }
            if (length == LINK_PACKET_SIZE)
            {
                uint16_t sequence = ((uint16_t)packet[1] << 8) | packet[2];
                if (packet[0] == LINK_ACK && link_state == LINK_WAIT_ACK &&
                    sequence == active_sequence && (now - link_phase_ms) < LINK_ACK_TIMEOUT_MS)
                {
                    link_state = LINK_IDLE;
                    link_show_status("ACK OK");
                    link_record_result(1);
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
            if (link_state == LINK_REQUEST_TX)
                link_record_result(0);
            link_state = LINK_IDLE;
            link_show_status("TX TIMEOUT");
            break;

        case SX1278_IO_ERROR:
            if (link_state == LINK_REQUEST_TX || link_state == LINK_WAIT_ACK)
                link_record_result(0);
            link_state = LINK_IDLE;
            link_show_status("SPI ERROR");
            break;

        case SX1278_RX_DROPPED:
            link_show_status("RX DROP");
            break;

        default: /* No event to report. */
            break;
    }

    if (link_state == LINK_WAIT_ACK && (now - link_phase_ms) >= LINK_ACK_TIMEOUT_MS)
    {
        link_state = LINK_IDLE;
        link_show_status("TIMEOUT");
        link_record_result(0);
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
    SEGGER_RTT_WriteString(0, "LoRa_SX1278_Test RSSI DIAG 1\r\n");
    lcd_init();
    lcd_put_cur(0, 0);
    lcd_send_string("LoRa RSSI DIAG 1");
    link_show_measurements();
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
    radio_diagnostics_run();
}
