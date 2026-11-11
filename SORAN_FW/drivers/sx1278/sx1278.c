#include "sx1278.h"
#include <string.h>

/*--------------------------------[ LoRa register addresses ]--------------------------------*/

enum
{
    REG_FIFO = 0x00,            /* FIFO data port. */
    REG_MODE = 0x01,            /* Modulation and operating mode. */
    REG_FRF = 0x06,             /* First byte of the carrier frequency word. */
    REG_PA = 0x09,              /* Power amplifier output and level. */
    REG_FIFO_PTR = 0x0D,        /* Current FIFO access address. */
    REG_TX_BASE = 0x0E,         /* Transmit FIFO base address. */
    REG_RX_BASE = 0x0F,         /* Receive FIFO base address. */
    REG_RX_START = 0x10,        /* FIFO start address of the last received packet. */
    REG_IRQ = 0x12,             /* Latched IRQ flags. Write ones to clear them. */
    REG_RX_LENGTH = 0x13,       /* Payload length of the last received packet. */
    REG_PACKET_SNR = 0x19,      /* Last packet SNR in signed quarter-dB units. */
    REG_PACKET_RSSI = 0x1A,     /* Last packet RSSI before LF offset correction. */
    REG_CONFIG1 = 0x1D,         /* Bandwidth, coding rate and header mode. */
    REG_CONFIG2 = 0x1E,         /* Spreading factor and payload CRC. */
    REG_PREAMBLE_MSB = 0x20,    /* High byte of the preamble length. */
    REG_TX_LENGTH = 0x22,       /* Number of payload bytes to transmit. */
    REG_CONFIG3 = 0x26,         /* Low data rate optimization and automatic gain control. */
    REG_SYNC = 0x39,            /* LoRa sync word. */
    REG_DIO_MAP = 0x40,         /* Interrupt mapping for DIO pins. */
    REG_VERSION = 0x42          /* Silicon revision register. */
};

/*--------------------------------[ Mode values and event flags ]--------------------------------*/

enum
{
    MODE_FSK_SLEEP = 0x08,      /* FSK sleep state before switching to LoRa. */
    MODE_LORA_SLEEP = 0x88,     /* LoRa sleep state with low-frequency mode selected. */
    MODE_STANDBY = 0x89,        /* LoRa standby for FIFO and mode setup. */
    MODE_TX = 0x8B,             /* Transmit one LoRa packet. */
    MODE_RX = 0x8D,             /* Listen continuously for LoRa packets. */
    IRQ_RX_DONE = 0x40,         /* Packet reception completed. */
    IRQ_CRC_ERROR = 0x20,       /* Received payload failed its CRC check. */
    IRQ_TX_DONE = 0x08,         /* Packet transmission completed. */
    SPI_TIMEOUT_MS = 10,        /* Maximum wait per HAL SPI call. */
    TX_TIMEOUT_MS = 5000        /* Covers up to 255 bytes at SF10, BW125, CR4/8. */
};

/*--------------------------------[ SPI register and FIFO access ]--------------------------------*/

/*--------------------[ Chip select ]--------------------*/
/* NSS stays low for the entire SPI transaction. */
static void select_radio(sx1278_t *radio, GPIO_PinState state)
{
    HAL_GPIO_WritePin(radio->config.nss_port, radio->config.nss_pin, state);
}

/*--------------------[ Single register write ]--------------------*/
/* Send the write address and data byte in the same transaction. */
static HAL_StatusTypeDef write_reg(sx1278_t *radio, uint8_t reg, uint8_t value)
{
    uint8_t spi_out[] = {reg | 0x80, value}; /* Bit 7 selects a register write. */
    select_radio(radio, GPIO_PIN_RESET);
    HAL_StatusTypeDef status = HAL_SPI_Transmit(radio->config.spi, spi_out, sizeof(spi_out), SPI_TIMEOUT_MS);
    select_radio(radio, GPIO_PIN_SET);
    return status;
}

/*--------------------[ Single register read ]--------------------*/
/* Send the read address, then clock out the value with a dummy byte. */
static HAL_StatusTypeDef read_reg(sx1278_t *radio, uint8_t reg, uint8_t *value)
{
    uint8_t spi_out[] = {reg, 0};
    uint8_t spi_in[2];
    select_radio(radio, GPIO_PIN_RESET);
    HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(radio->config.spi, spi_out, spi_in, sizeof(spi_out), SPI_TIMEOUT_MS);
    select_radio(radio, GPIO_PIN_SET);
    if (status == HAL_OK)
        *value = spi_in[1]; /* The first byte arrived during the address phase. */
    return status;
}

/*--------------------[ FIFO burst transfer ]--------------------*/
/* Keep NSS low between the FIFO command and payload transfer. */
static HAL_StatusTypeDef fifo_transfer(sx1278_t *radio, const uint8_t *fifo_write_data, uint8_t *fifo_read_data, uint8_t length)
{
    uint8_t command = fifo_write_data ? 0x80 : REG_FIFO; /* A non-null write buffer selects a FIFO write. */
    SPI_HandleTypeDef *spi = radio->config.spi;
    select_radio(radio, GPIO_PIN_RESET);
    HAL_StatusTypeDef status = HAL_SPI_Transmit(spi, &command, 1, SPI_TIMEOUT_MS);
    if (status == HAL_OK)
    {
        if (fifo_write_data)
            status = HAL_SPI_Transmit(spi, fifo_write_data, length, SPI_TIMEOUT_MS);
        else
        {
            memset(fifo_read_data, 0, length); /* Clock the FIFO out using dummy bytes. */
            status = HAL_SPI_TransmitReceive(spi, fifo_read_data, fifo_read_data, length, SPI_TIMEOUT_MS);
        }
    }
    select_radio(radio, GPIO_PIN_SET);
    return status;
}

/*--------------------------------[ Radio mode transitions ]--------------------------------*/

/*--------------------[ Restore continuous RX ]--------------------*/
/* Clear stale IRQs and map DIO0 to RxDone before restarting RX. */
static HAL_StatusTypeDef enter_rx(sx1278_t *radio)
{
    if (write_reg(radio, REG_MODE, MODE_STANDBY) != HAL_OK ||
        write_reg(radio, REG_IRQ, 0xFF) != HAL_OK ||
        write_reg(radio, REG_DIO_MAP, 0x00) != HAL_OK || /* DIO0 signals RxDone. */
        write_reg(radio, REG_MODE, MODE_RX) != HAL_OK)
    {
        radio->ready = 0;
        return HAL_ERROR;
    }
    radio->transmitting = 0;
    return HAL_OK;
}

/*--------------------------------[ Public radio operations ]--------------------------------*/

/*======================================================================================================================
 *  Function  : sx1278_init
 *  Purpose   : Reset the radio, verify its version and start continuous reception.
 *======================================================================================================================*/
HAL_StatusTypeDef sx1278_init(sx1278_t *radio, const sx1278_config_t *config, uint8_t *version)
{
    static const uint8_t settings[][2] = {
        {REG_MODE, MODE_FSK_SLEEP}, {REG_MODE, MODE_LORA_SLEEP},
        {REG_MODE, MODE_STANDBY},
        {REG_FRF, 0x6C}, {REG_FRF + 1, 0x40}, {REG_FRF + 2, 0x00}, /* 433 MHz */
        {REG_PA, 0x8F}, /* PA_BOOST, +17 dBm with the default PA DAC. */
        {REG_TX_BASE, 0}, {REG_RX_BASE, 0}, /* FIFO is shared between modes. */
        {REG_CONFIG1, 0x78}, /* BW 125 kHz, CR 4/8, explicit header. */
        {REG_CONFIG2, 0xA4}, /* SF10 with payload CRC enabled. */
        {REG_CONFIG3, 0x04}, /* Automatic gain control enabled. */
        {REG_PREAMBLE_MSB, 0}, {REG_PREAMBLE_MSB + 1, 8}, /* Eight preamble symbols. */
        {REG_SYNC, 0x12} /* Sync word shared by both ends of the link. */
    };

    *version = 0;
    radio->packet_rssi_valid = 0;
    radio->ready = 0;
    radio->irq_pending = 0;
    radio->config = *config;
    select_radio(radio, GPIO_PIN_SET);
    HAL_GPIO_WritePin(config->reset_port, config->reset_pin, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(config->reset_port, config->reset_pin, GPIO_PIN_SET);
    HAL_Delay(6);

    if (read_reg(radio, REG_VERSION, version) != HAL_OK || *version != 0x12)
        return HAL_ERROR;

    for (size_t i = 0; i < sizeof(settings) / sizeof(settings[0]); ++i)
    {
        if (write_reg(radio, settings[i][0], settings[i][1]) != HAL_OK)
            return HAL_ERROR;
    }

    radio->ready = enter_rx(radio) == HAL_OK;
    return radio->ready ? HAL_OK : HAL_ERROR;
}

/*======================================================================================================================
 *  Function  : sx1278_send
 *  Purpose   : Copy a payload to the FIFO and start transmission.
 *======================================================================================================================*/
HAL_StatusTypeDef sx1278_send(sx1278_t *radio, const uint8_t *data, uint8_t length)
{
    if (!radio->ready || !data || !length)
        return HAL_ERROR;
    if (radio->transmitting || radio->irq_pending)
        return HAL_BUSY;

    if (write_reg(radio, REG_MODE, MODE_STANDBY) != HAL_OK ||
        write_reg(radio, REG_FIFO_PTR, 0) != HAL_OK ||
        fifo_transfer(radio, data, NULL, length) != HAL_OK ||
        write_reg(radio, REG_TX_LENGTH, length) != HAL_OK ||
        write_reg(radio, REG_IRQ, 0xFF) != HAL_OK ||
        write_reg(radio, REG_DIO_MAP, 0x40) != HAL_OK ||
        write_reg(radio, REG_MODE, MODE_TX) != HAL_OK)
    {
        (void)enter_rx(radio);
        return HAL_ERROR;
    }

    radio->tx_started_ms = HAL_GetTick();
    radio->transmitting = 1;
    return HAL_OK;
}

void sx1278_notify_irq(sx1278_t *radio)
{
    radio->irq_pending = 1;
}

/*--------------------[ Received packet strength ]--------------------*/

static void sample_packet_rssi(sx1278_t *radio)
{
    uint8_t snr_raw, rssi_raw;
    radio->packet_rssi_valid = 0;
    if (read_reg(radio, REG_PACKET_SNR, &snr_raw) != HAL_OK ||
        read_reg(radio, REG_PACKET_RSSI, &rssi_raw) != HAL_OK)
        return;

    /* Semtech datasheet section 5.5.5, low-frequency port at 433 MHz. */
    int16_t snr_quarters = snr_raw >= 128 ? (int16_t)snr_raw - 256 : snr_raw;
    int16_t rssi_dbm = -164 + rssi_raw;
    if (snr_quarters < 0)
        rssi_dbm += snr_quarters / 4;
    else
        rssi_dbm += rssi_raw / 15; /* 16/15 slope correction for nonnegative SNR. */

    radio->packet_rssi_dbm = rssi_dbm;
    radio->packet_rssi_valid = 1;
}

/*======================================================================================================================
 *  Function  : sx1278_packet_rssi
 *  Purpose   : Read the RSSI estimate saved with the last copied packet.
 *======================================================================================================================*/
uint8_t sx1278_packet_rssi(const sx1278_t *radio, int16_t *dbm)
{
    if (!radio->packet_rssi_valid || !dbm)
        return 0;

    *dbm = radio->packet_rssi_dbm;
    return 1;
}

/*======================================================================================================================
 *  Function  : sx1278_service
 *  Purpose   : Handle DIO0 events and return the radio to RX after TX.
 *======================================================================================================================*/
sx1278_event_t sx1278_service(sx1278_t *radio, uint8_t *data, uint8_t capacity, uint8_t *length)
{
    *length = 0;
    if (!radio->ready)
        return SX1278_IDLE;

    uint8_t timed_out = radio->transmitting &&
                        (HAL_GetTick() - radio->tx_started_ms >= TX_TIMEOUT_MS);
    if (!radio->irq_pending && !timed_out)
        return SX1278_IDLE;

    radio->irq_pending = 0;
    uint8_t flags;
    if (read_reg(radio, REG_IRQ, &flags) != HAL_OK)
    {
        (void)enter_rx(radio);
        return SX1278_IO_ERROR;
    }

    if (radio->transmitting)
    {
        if (!(flags & IRQ_TX_DONE) && !timed_out)
            return SX1278_IDLE;
        if (enter_rx(radio) != HAL_OK)
            return SX1278_IO_ERROR;
        return (flags & IRQ_TX_DONE) ? SX1278_TX_DONE : SX1278_TX_TIMEOUT;
    }

    if (!(flags & IRQ_RX_DONE))
        return SX1278_IDLE;

    sx1278_event_t event = SX1278_RX_DROPPED;
    uint8_t size, start;
    if (write_reg(radio, REG_MODE, MODE_STANDBY) != HAL_OK ||
        read_reg(radio, REG_RX_LENGTH, &size) != HAL_OK)
        event = SX1278_IO_ERROR;
    else if (!(flags & IRQ_CRC_ERROR) && data && size && size <= capacity)
    {
        if (read_reg(radio, REG_RX_START, &start) != HAL_OK ||
            write_reg(radio, REG_FIFO_PTR, start) != HAL_OK ||
            fifo_transfer(radio, NULL, data, size) != HAL_OK)
            event = SX1278_IO_ERROR;
        else
        {
            sample_packet_rssi(radio); /* Capture metrics before restarting RX. */
            event = SX1278_RX_DONE;
        }
    }

    if (enter_rx(radio) != HAL_OK)
        return SX1278_IO_ERROR;
    if (event == SX1278_RX_DONE)
        *length = size;
    return event;
}
