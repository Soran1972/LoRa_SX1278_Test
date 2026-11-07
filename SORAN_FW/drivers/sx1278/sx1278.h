#pragma once

#include "stm32g0xx_hal.h"

/*--------------------------------[ Board configuration and driver state ]--------------------------------*/

typedef struct
{
    SPI_HandleTypeDef *spi;         /* Initialized HAL SPI instance. */
    GPIO_TypeDef *nss_port;         /* NSS GPIO port. */
    uint16_t nss_pin;               /* Active-low chip-select pin. */
    GPIO_TypeDef *reset_port;       /* RESET GPIO port. */
    uint16_t reset_pin;             /* Active-low radio reset pin. */
} sx1278_config_t;

typedef struct
{
    sx1278_config_t config;         /* Copy of the board wiring configuration. */
    volatile uint8_t irq_pending;   /* Set from EXTI, consumed in the main loop. */
    uint8_t transmitting;           /* A TX operation is awaiting completion. */
    uint8_t ready;                  /* Radio initialized and available. */
    uint32_t tx_started_ms;         /* HAL tick recorded when TX starts. */
} sx1278_t;

/*--------------------------------[ Service events ]--------------------------------*/

typedef enum
{
    SX1278_IDLE,                /* No event to report. */
    SX1278_RX_DONE,             /* Payload copied to the caller buffer. */
    SX1278_TX_DONE,             /* Packet sent and RX restored. */
    SX1278_TX_TIMEOUT,          /* TX completion missed its deadline and RX was restored. */
    SX1278_RX_DROPPED,          /* CRC failed or the payload did not fit the caller buffer. */
    SX1278_IO_ERROR             /* SPI transfer or radio recovery failed. */
} sx1278_event_t;

/*--------------------------------[ Public API ]--------------------------------*/

/* Use the same 433 MHz, SF7, BW125, CR4/5 settings on both radios. */
HAL_StatusTypeDef sx1278_init(sx1278_t *radio, const sx1278_config_t *config, uint8_t *version);
HAL_StatusTypeDef sx1278_send(sx1278_t *radio, const uint8_t *data, uint8_t length);
/* Call from the DIO0 rising-edge callback. This only sets a flag. */
void sx1278_notify_irq(sx1278_t *radio);

/* Call in the main loop. RX_DONE sets length to the bytes copied into data. */
sx1278_event_t sx1278_service(sx1278_t *radio, uint8_t *data, uint8_t capacity, uint8_t *length);
