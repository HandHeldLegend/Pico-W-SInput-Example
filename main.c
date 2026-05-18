/**
 * @file main.c
 * @brief Example application entry point and SINPUT-LIB-HID hook implementations.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

#include "main.h"

#include "sinput_lib.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "hardware/timer.h"

static const uint SINPUT_BT_BOOT_PIN = 0;
static const uint SINPUT_BT_PAIR_BOOT_PIN = 1;
static const uint SINPUT_A_BUTTON_PIN = 14;
static const uint SINPUT_B_BUTTON_PIN = 15;

typedef enum
{
    SINPUT_BOOT_USB,
    SINPUT_BOOT_BTC,
    SINPUT_BOOT_BTCPAIR,
} sinput_boot_mode_t;

/* Example controller address used for descriptor identity and Bluetooth bring-up. */
uint8_t device_mac[6] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE4, 0xF2};
sinput_storage_s  device_storage = {0};

static sinput_boot_mode_t sinput_get_boot_mode(void)
{
    /* GP0/GP1 act as mode straps. The button inputs also use pull-ups, so all inputs are active-low. */
    gpio_init(SINPUT_BT_BOOT_PIN);
    gpio_set_dir(SINPUT_BT_BOOT_PIN, GPIO_IN);
    gpio_pull_up(SINPUT_BT_BOOT_PIN);

    gpio_init(SINPUT_BT_PAIR_BOOT_PIN);
    gpio_set_dir(SINPUT_BT_PAIR_BOOT_PIN, GPIO_IN);
    gpio_pull_up(SINPUT_BT_PAIR_BOOT_PIN);

    gpio_init(SINPUT_A_BUTTON_PIN);
    gpio_set_dir(SINPUT_A_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(SINPUT_A_BUTTON_PIN);

    gpio_init(SINPUT_B_BUTTON_PIN);
    gpio_set_dir(SINPUT_B_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(SINPUT_B_BUTTON_PIN);

    if (!gpio_get(SINPUT_BT_PAIR_BOOT_PIN))
    {
        return SINPUT_BOOT_BTCPAIR;
    }
    else if (!gpio_get(SINPUT_BT_BOOT_PIN))
    {
        return SINPUT_BOOT_BTC;
    }

    return SINPUT_BOOT_USB;
}

/* ------------------------------------------------------------------------- *
 * SINPUT weak hook overrides (see external/SINPUT-LIB-HID/sinput_lib.c)
 *
 * The library calls these while building each 64-byte HID input report and when
 * decoding host output commands. Return true from a getter when you write fresh
 * samples into *out; return false to reuse the library's last cached value for
 * that hook (for get_input, buttons/sticks/triggers are updated together).
 * Keep work light: TinyUSB/BT callbacks may run from tight contexts — defer heavy
 * I²C/SPI IMU reads to a background producer if needed and copy here.
 * ------------------------------------------------------------------------- */

/* Last host requests — useful in a debugger; production firmware drives PWM/LEDs from these. */
static volatile sinput_stereo_rumble_s s_demo_last_rumble;
static volatile sinput_stereo_haptics_s s_demo_last_haptics;
static volatile uint8_t s_demo_player_leds = 0;
static volatile uint32_t s_demo_joystick_rgb = 0;

/* Slow phase for a synthetic gyro demo when you have no IMU wired yet. */
static uint32_t s_demo_motion_phase;

void sinput_api_hook_set_rumble(sinput_stereo_rumble_s rumble)
{
    /*
     * Classic ERM-style rumble: apply left/right amplitude (0…255) to motor drivers.
     * If .brake is set, short the motor or decay faster (host-dependent semantics).
     * Map both sides to a single actuator if you only have one rumble motor.
     */
    s_demo_last_rumble = rumble;
}

void sinput_api_hook_set_haptics(sinput_stereo_haptics_s haptics)
{
    /*
     * HD / dual-band haptics: each side has two frequency/amplitude pairs.
     * Typical: run through your haptic controller chip, or approximate on ERM by
     * mapping frequency+amplitude to effective duty cycle.
     */
    s_demo_last_haptics = haptics;
}

void sinput_api_hook_set_player_leds(uint8_t player_number)
{
    /* Player index from host (often 0–3). Light the matching quadrant on your LED row. */
    s_demo_player_leds = player_number;
}

void sinput_api_hook_set_joystick_rgb(uint32_t rgb_value)
{
    /*
     * 32-bit RGB (layout is product-defined by the vendor report — treat as 0x00RRGGBB
     * unless your PC tooling specifies otherwise). Drive WS2812 or RGB PWM channels.
     */
    s_demo_joystick_rgb = rgb_value;
}

bool sinput_api_hook_get_power(sinput_power_s *status)
{
    /*
     * Report battery% and connection/charging state read from your fuel gauge or PMIC.
     * Here we publish a static “full battery” state as a neutral baseline; switch to
     * SINPUT_CONNSTAT_PLUGGED_* when USB VBUS is present, etc.
     */
    status->charge_percent = 100;
    status->connection_status = SINPUT_CONNSTAT_UNPLUGGED;
    return true;
}

bool sinput_api_hook_get_input(sinput_input_s*out)
{
    /*
     * Fill sinput_input_s: .buttons (GPIO), .joysticks, .triggers in one hook.
     * Button bits match SDL-style cluster names (south/east/west/north = face).
     * Demo: GP14 → south (“A”), GP15 → east (“B”); expand with more gpio_get().
     * Pins were pull-up’d in sinput_get_boot_mode(); active-low here.
     */
    memset(out, 0, sizeof(*out));
    out->buttons.south = (gpio_get(SINPUT_A_BUTTON_PIN) == 0);
    out->buttons.east = (gpio_get(SINPUT_B_BUTTON_PIN) == 0);

    /*
     * Typical: read dual ADC channels per stick, 12 bit ADC readings, apply deadzone/invert
     * per axis. Centered sticks for this demo so the host sees idle axes.
     */
    out->joysticks.left.x = 0;
    out->joysticks.left.y = 0;
    out->joysticks.right.x = 0;
    out->joysticks.right.y = 0;

    /*
     * Raw 12-bit domain before packing (library clamps to ~4095). Read hall sensors or
     * ADC on LT/RT, or use digital buttons and map to 0 / MAX.
     */
    out->triggers.left = 0;
    out->triggers.right = 0;
    return true;
    return true;
}

bool sinput_api_hook_get_motion(sinput_motion_s *out)
{
    /*
     * Gyro (dps) + accel (g) vectors and a host-facing timestamp in microseconds.
     * Replace the synthetic sweep below with IMU reads aligned to cfg.motion ranges
     * from sinput_api_init(). We only advertise gyro in this example’s cfg; accel
     * stays zero.
     */
    out->timestamp_us = time_us_64();
    s_demo_motion_phase++;
    int16_t sweep = (int16_t)((s_demo_motion_phase % 2000) - 1000);
    out->gyro.x = sweep / 10;
    out->gyro.y = 0;
    out->gyro.z = 0;
    out->accel.x = 0;
    out->accel.y = 0;
    out->accel.z = 0;
    return true;
}

bool sinput_api_hook_get_touchpads(sinput_touchpads_s *out)
{
    /*
     * Return true with coordinates/pressure for each pad if your device exposes them.
     * This demo’s sinput_device_cfg_s does not enable touchpads—return false so the
     * library keeps any prior cached touch state untouched.
     */
    (void)out;
    return false;
}

void sinput_app_save_host(uint8_t host_mac[6])
{
    memcpy(device_storage.host_mac, host_mac, 6);
    sinput_flash_write((uint8_t*) &device_storage, SINPUT_STORAGE_SIZE, SINPUT_STORAGE_PAGE);
}

int main()
{
    sinput_boot_mode_t boot_mode = sinput_get_boot_mode();
    stdio_init_all();
    sleep_ms(500);
    printf("Pico-W Booted!\n\n");

    sinput_flash_init();

    /* Load the example's persisted host MAC and link key, if they were written previously. */
    sinput_flash_read((uint8_t*) &device_storage, SINPUT_STORAGE_SIZE, SINPUT_STORAGE_PAGE);

    /*
     * A missing magic value means this sector has never been initialized by the
     * example, so start with a clean settings block before any pairing occurs.
     */
    if(device_storage.magic_byte != SINPUT_STORAGE_MAGIC)
    {
        memset(&device_storage, 0, SINPUT_STORAGE_SIZE);
        device_storage.magic_byte = SINPUT_STORAGE_MAGIC;
    }

    sinput_device_cfg_s cfg = 
    {
        .buttons = 
        {
            .sewn = true,
            .start_select = true,
            .dpad = true,
            .grips_upper = true,
            .triggers = true,
        },

        .joysticks = 
        {
            .left = true,
            .right = true,
        },

        .motion = 
        {
            .gyroscope = true,
            .gyroscope_dps_range = 2000,
        }
    };

    memcpy(cfg.mac_address, device_mac, 6);

    sinput_api_init(&cfg);

    switch(boot_mode)
    {
        case SINPUT_BOOT_USB:
        printf("Entering USB mode\n");
        sinput_usb_enter();
        break;

        case SINPUT_BOOT_BTC:
        printf("Entering Bluetooth mode (reconnect)\n");
        sinput_btc_enter(device_mac, false);
        break;

        case SINPUT_BOOT_BTCPAIR:
        printf("Entering Bluetooth mode (pair)\n");
        sinput_btc_enter(device_mac, true);
        break;
    }
}
