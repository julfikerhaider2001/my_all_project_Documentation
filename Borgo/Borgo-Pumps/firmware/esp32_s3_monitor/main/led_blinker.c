#include "led_blinker.h"
#include "esp_timer.h"

static void led_write(led_blinker_t *b, bool on)
{
    bool physical_high = b->active_high ? on : !on;
    gpio_set_level(b->pin, physical_high ? 1 : 0);
}

void led_blinker_init(led_blinker_t *b, gpio_num_t pin, bool active_high)
{
    b->pin         = pin;
    b->active_high = active_high;
    b->repeats     = 0;
    b->in_pause    = false;
    b->count       = 0;

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    led_write(b, false);
}

void led_blinker_solid(led_blinker_t *b, bool on)
{
    b->repeats  = 0;
    b->in_pause = false;
    led_write(b, on);
}

void led_blinker_blink(led_blinker_t *b, uint32_t on_ms, uint32_t off_ms,
                       int repeats, uint32_t pause_ms)
{
    if (b->repeats == repeats && b->on_ms == on_ms &&
        b->off_ms == off_ms && b->pause_ms == pause_ms) {
        return;
    }
    b->on_ms     = on_ms;
    b->off_ms    = off_ms;
    b->repeats   = repeats;
    b->pause_ms  = pause_ms;
    b->count     = 0;
    b->in_pause  = false;
    b->state     = true;
    b->last_change_us = esp_timer_get_time();
    led_write(b, true);
}

void led_blinker_update(led_blinker_t *b)
{
    if (b->repeats == 0) return;

    int64_t now = esp_timer_get_time();

    if (b->in_pause) {
        if ((now - b->last_change_us) >= (int64_t)(b->pause_ms * 1000)) {
            b->in_pause = false;
            b->count    = 0;
            b->state    = true;
            b->last_change_us = now;
            led_write(b, true);
        }
        return;
    }

    uint32_t dur = b->state ? b->on_ms : b->off_ms;
    if ((now - b->last_change_us) < (int64_t)(dur * 1000)) return;

    b->state = !b->state;
    led_write(b, b->state);
    b->last_change_us = now;

    if (!b->state) {
        b->count++;
        if (b->repeats > 0 && b->count >= b->repeats) {
            if (b->pause_ms > 0) {
                b->in_pause    = true;
                b->last_change_us = now;
                b->count = 0;
            } else {
                b->repeats = 0;
                led_write(b, false);
            }
        }
    }
}
