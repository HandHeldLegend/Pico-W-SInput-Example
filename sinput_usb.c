/**
 * @file sinput_usb.c
 * @brief TinyUSB transport path for the Pico W example.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

#include "main.h"
#include "tusb_config.h"
#include "tusb.h"

#include "sinput_lib.h"
#include "sinput_lib_hid.h"

/*
 * TinyUSB asks for UTF-16 string descriptors one index at a time. Keep the source
 * strings in ASCII here and convert them in tud_descriptor_string_cb().
 */
const char *global_string_descriptor[] = {
    (char[]){0x09, 0x04}, /* 0: supported language = English (0x0409). */
    "HHL",                /* 1: manufacturer string. */
    "SInput Gamepad Demo",/* 2: product string. */
    "000000",             /* 3: placeholder serial, should be replaced with a unique ID. */
};

/* Report transmission is gated by both TinyUSB readiness and the 8 ms frame cadence. */
volatile static bool _usb_ready = false;
volatile static bool _frame_ready = false;

uint8_t _usb_report_data[64] = {0};

/* Primary USB runtime loop. */
void sinput_usb_enter(void)
{
    tusb_init();

    for(;;)
    {
        /* Pump TinyUSB and service any deferred flash writes from pairing callbacks. */
        tud_task();
        sinput_flash_task();

        if(!_usb_ready)
        {
            /* TinyUSB only accepts an IN report when the HID endpoint is ready again. */
            _usb_ready = tud_hid_ready();
        }

        if(_usb_ready && _frame_ready)
        {
            _usb_ready = false;
            _frame_ready = false;

            if(sinput_api_generate_inputreport(_usb_report_data))
            {
                /* Byte 0 is the report ID; the remaining 63 bytes are the payload body. */
                tud_hid_report(_usb_report_data[0], &_usb_report_data[1], 63);
            }
        }
    }
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;

    /* The library owns the transport-specific USB configuration descriptor bytes. */
    const uint8_t *config_descriptor;
    sinput_hid_get_descriptor_params(NULL, NULL, &config_descriptor, NULL, NULL, NULL);
    return config_descriptor;
}

/* Invoked by TinyUSB when the host requests the standard USB device descriptor. */
uint8_t const *tud_descriptor_device_cb(void)
{
    const uint8_t* device_descriptor = (uint8_t const *) sinput_hid_get_device_descriptor();
    return device_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)reqlen;
    (void)report_type;

    return 0;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;

    /* The HID report descriptor also comes directly from NS-LIB-HID for the active transport. */
    const uint8_t *report_descriptor;
    sinput_hid_get_descriptor_params(&report_descriptor, NULL, NULL, NULL, NULL, NULL);

    return report_descriptor;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    static uint16_t _desc_str[64];
    uint8_t chr_count;

    if (index == 0)
    {
        memcpy(&_desc_str[1], global_string_descriptor[0], 2);
        chr_count = 1;
    }
    else
    {
        const char *str = global_string_descriptor[index];

        /* TinyUSB's helper buffer is fixed, so cap the converted string length. */
        chr_count = strlen(str);
        if (chr_count > 31)
            chr_count = 31;

        /* Convert the ASCII source strings into UTF-16LE code units for USB. */
        for (uint8_t i = 0; i < chr_count; i++)
        {
            _desc_str[1 + i] = str[i];
        }
    }

    /* Word 0 packs the descriptor type and total byte length as TinyUSB expects. */
    _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);
    return _desc_str;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
    if (!report_id && report_type == HID_REPORT_TYPE_OUTPUT)
    {
        /* Forward host command traffic into the protocol engine immediately. */
        sinput_api_output_tunnel(buffer, bufsize);
    }
}

/* Enabled once the device is mounted so Start-of-Frame interrupts can pace report generation. */
static bool sofen = false;
void tud_mount_cb()
{
    tud_sof_cb_enable(false);
    tud_sof_cb_enable(true);
    sofen = true;
}

/*
 * USB SOF runs every 1 ms. Counting frames here gives the example a simple,
 * host-synchronized way to stay close to the expected 8 ms controller interval.
 */
void tud_sof_cb(uint32_t frame_count_ext) 
{
    (void)frame_count_ext;

    static uint8_t frame_counter = 0;
    frame_counter++;
    if(frame_counter >= 7)
    {
        frame_counter = 0;
        _frame_ready |= true;
    }
}