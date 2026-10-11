/*
 * SPDX-FileCopyrightText: 2020-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Variante modificata: usa HardwareSerial di Arduino invece delle funzioni
 * uart_* dirette di ESP-IDF (stesso motivo per cui i pin BOOT/EN usano
 * pinMode/digitalWrite invece di gpio_set_direction/gpio_set_level -- su
 * questa combinazione board/core le chiamate ESP-IDF dirette hanno
 * problemi di instradamento IO_MUX che il layer Arduino non ha).
 */

#include "flasher_config.h"
#include "esp32_port.h"
#include "loader_port_stdio_log.h"
#include "esp_timer.h"
#include <Arduino.h>
#include <stdio.h>
#include <unistd.h>

// Una sola istanza HardwareSerial condivisa: il nostro caso d'uso ha
// sempre una sola porta verso il C6, quindi teniamo la cosa semplice
// invece di generalizzare a piu' istanze indipendenti come l'originale.
static HardwareSerial *s_serial = NULL;

extern "C" {

static esp_loader_error_t esp32_port_init(esp_loader_port_t *port)
{
    esp32_port_t *p = container_of(port, esp32_port_t, port);

    p->_peripheral_needs_deinit = false;

    if (!p->dont_initialize_peripheral) {
        if (s_serial == NULL) {
            s_serial = new HardwareSerial((int)p->uart_port);
        }

        uint32_t rx_buffer_size = p->rx_buffer_size ? p->rx_buffer_size : 20480;
        uint32_t tx_buffer_size = p->tx_buffer_size ? p->tx_buffer_size : 20480;
        s_serial->setRxBufferSize(rx_buffer_size);
        s_serial->setTxBufferSize(tx_buffer_size);

        // HardwareSerial::begin(baud, config, rxPin, txPin)
        s_serial->begin(p->baud_rate, SERIAL_8N1, (int)p->uart_rx_pin, (int)p->uart_tx_pin);

        p->_peripheral_needs_deinit = true;
    }

    // Vedi nota in cima al file: pinMode/digitalWrite invece delle
    // funzioni gpio_* dirette.
    pinMode(p->reset_pin, OUTPUT);
    pinMode(p->boot_pin, OUTPUT);

    printf("[esf-diag] port init: uart=%lu tx=%d rx=%d reset(EN)=%d boot=%d baud=%lu\n",
           (unsigned long)p->uart_port, (int)p->uart_tx_pin, (int)p->uart_rx_pin,
           (int)p->reset_pin, (int)p->boot_pin, (unsigned long)p->baud_rate);
    printf("[esf-diag] readback subito dopo init: RESET(EN)=%d BOOT=%d\n",
           digitalRead(p->reset_pin), digitalRead(p->boot_pin));

    return ESP_LOADER_SUCCESS;
}


static void esp32_port_deinit(esp_loader_port_t *port)
{
    esp32_port_t *p = container_of(port, esp32_port_t, port);

    if (p->_peripheral_needs_deinit && s_serial != NULL) {
        s_serial->end();
        p->_peripheral_needs_deinit = false;
    }
}


static esp_loader_error_t esp32_uart_write(esp_loader_port_t *port, const uint8_t *data, uint16_t size, uint32_t timeout)
{
    (void)port;
    (void)timeout;

    size_t written = s_serial->write(data, size);
    s_serial->flush();

    return (written == size) ? ESP_LOADER_SUCCESS : ESP_LOADER_ERROR_FAIL;
}


static esp_loader_error_t esp32_uart_read(esp_loader_port_t *port, uint8_t *data, uint16_t size, uint32_t timeout)
{
    (void)port;

    // readBytes() di Stream/HardwareSerial gestisce gia' correttamente ed
    // efficientemente l'attesa con timeout (a differenza di un ciclo
    // "fatto a mano" su available() che spreca CPU e puo' interagire male
    // con il driver UART sottostante).
    s_serial->setTimeout(timeout);
    size_t got = s_serial->readBytes((char *)data, size);

    return (got == size) ? ESP_LOADER_SUCCESS : ESP_LOADER_ERROR_TIMEOUT;
}


static void esp32_uart_delay_ms(esp_loader_port_t *port, uint32_t ms)
{
    (void)port;
    usleep(ms * 1000);
}


static void esp32_uart_start_timer(esp_loader_port_t *port, uint32_t ms)
{
    esp32_port_t *p = container_of(port, esp32_port_t, port);
    p->_time_end = esp_timer_get_time() + (int64_t)ms * 1000;
}


static uint32_t esp32_uart_remaining_time(esp_loader_port_t *port)
{
    esp32_port_t *p = container_of(port, esp32_port_t, port);
    int64_t remaining = (p->_time_end - esp_timer_get_time()) / 1000;
    return (remaining > 0) ? (uint32_t)remaining : 0;
}


static void esp32_uart_reset_target(esp_loader_port_t *port)
{
    esp32_port_t *p = container_of(port, esp32_port_t, port);
    printf("[esf-diag] reset_target: RESET(EN) pin=%d -> LOW\n", (int)p->reset_pin);
    digitalWrite(p->reset_pin, SERIAL_FLASHER_RESET_INVERT ? HIGH : LOW);
    usleep(5000);
    printf("[esf-diag] readback RESET(EN) (dopo 5ms)=%d\n", digitalRead(p->reset_pin));
    usleep(SERIAL_FLASHER_RESET_HOLD_TIME_MS * 1000);
    printf("[esf-diag] reset_target: RESET(EN) -> HIGH\n");
    digitalWrite(p->reset_pin, SERIAL_FLASHER_RESET_INVERT ? LOW : HIGH);
    usleep(5000);
    printf("[esf-diag] readback RESET(EN) (dopo 5ms)=%d\n", digitalRead(p->reset_pin));
}


static void esp32_uart_enter_bootloader(esp_loader_port_t *port)
{
    esp32_port_t *p = container_of(port, esp32_port_t, port);

    // Sequenza allineata a quella del progetto originale (verificata
    // funzionante): BOOT basso per primo, poi EN basso 5ms dopo, hold
    // 50ms, EN alto, hold 80ms con BOOT ancora basso, poi rilascio di
    // entrambi.
    printf("[esf-diag] enter_bootloader: BOOT pin=%d -> LOW\n", (int)p->boot_pin);
    digitalWrite(p->boot_pin, SERIAL_FLASHER_BOOT_INVERT ? HIGH : LOW);
    usleep(5000);
    printf("[esf-diag] readback BOOT (dopo 5ms)=%d\n", digitalRead(p->boot_pin));

    printf("[esf-diag] enter_bootloader: RESET(EN) pin=%d -> LOW\n", (int)p->reset_pin);
    digitalWrite(p->reset_pin, SERIAL_FLASHER_RESET_INVERT ? HIGH : LOW);
    usleep(5000);
    printf("[esf-diag] readback RESET(EN) (dopo 5ms)=%d\n", digitalRead(p->reset_pin));

    usleep(SERIAL_FLASHER_RESET_HOLD_TIME_MS * 1000);

    printf("[esf-diag] enter_bootloader: RESET(EN) -> HIGH\n");
    digitalWrite(p->reset_pin, SERIAL_FLASHER_RESET_INVERT ? LOW : HIGH);
    usleep(5000);
    printf("[esf-diag] readback RESET(EN) (dopo 5ms)=%d\n", digitalRead(p->reset_pin));

    usleep(SERIAL_FLASHER_BOOT_HOLD_TIME_MS * 1000);

    printf("[esf-diag] enter_bootloader: BOOT -> HIGH\n");
    digitalWrite(p->boot_pin, SERIAL_FLASHER_BOOT_INVERT ? LOW : HIGH);
    usleep(5000);
    printf("[esf-diag] readback BOOT (dopo 5ms)=%d\n", digitalRead(p->boot_pin));
}


static esp_loader_error_t esp32_uart_change_rate(esp_loader_port_t *port, uint32_t baudrate)
{
    (void)port;
    if (s_serial == NULL) {
        return ESP_LOADER_ERROR_FAIL;
    }
    s_serial->updateBaudRate(baudrate);
    return ESP_LOADER_SUCCESS;
}


const esp_loader_port_ops_t esp32_uart_ops = {
    .init                     = esp32_port_init,
    .deinit                   = esp32_port_deinit,
    .enter_bootloader         = esp32_uart_enter_bootloader,
    .reset_target             = esp32_uart_reset_target,
    .start_timer              = esp32_uart_start_timer,
    .remaining_time           = esp32_uart_remaining_time,
    .delay_ms                 = esp32_uart_delay_ms,
    .log                      = loader_port_stdio_log,
    .log_hex                  = loader_port_stdio_log_hex,
    .change_transmission_rate = esp32_uart_change_rate,
    .write                    = esp32_uart_write,
    .read                     = esp32_uart_read,
};

} // extern "C"
