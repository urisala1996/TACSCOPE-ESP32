/* BOOT button on GPIO9. It carries a 10k pull-up to 3V3 on the board and
 * shorts to GND when pressed, so it reads low while held. GPIO9 is also a
 * boot-strapping pin, so we only read it at runtime (never require it held
 * across a reset, which would select download mode). */
#include "driver/gpio.h"
#include "esp_timer.h"
#include "button.h"

#define PIN_BTN     9
#define DEBOUNCE_US 30000

void button_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_BTN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
}

bool button_poll(void)
{
    static bool    stable_down;
    static bool    last_raw;
    static int64_t since_us;

    bool    raw = gpio_get_level(PIN_BTN) == 0;
    int64_t now = esp_timer_get_time();

    if (raw != last_raw) {              /* level changed: restart debounce */
        last_raw = raw;
        since_us = now;
    }
    if (now - since_us > DEBOUNCE_US && raw != stable_down) {
        stable_down = raw;
        if (stable_down) return true;   /* fresh press accepted */
    }
    return false;
}
