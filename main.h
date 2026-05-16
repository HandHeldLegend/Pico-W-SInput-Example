/**
 * @file main.h
 * @brief Shared declarations for the Pico W example application.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    uint32_t magic_byte;
    /* Paired host address saved so Bluetooth reconnect can target the last console. */
    uint8_t host_mac[6];
} sinput_storage_s;

/* Simple single-page settings block used by the example's flash helper. */
#define SINPUT_STORAGE_MAGIC 0xDEADFEED
#define SINPUT_STORAGE_SIZE sizeof(sinput_storage_s)
#define SINPUT_STORAGE_PAGE 0

/* Device identity and pairing storage owned by main.c. */
extern uint8_t device_mac[6];
extern sinput_storage_s device_storage;

/* Flash persistence helpers shared by both USB and Bluetooth loops. */
bool sinput_flash_write(uint8_t *data, uint32_t size, uint32_t page);
bool sinput_flash_read(uint8_t *out, uint32_t size, uint32_t page);
void sinput_flash_task();
void sinput_flash_init();

/* Transport entry points selected at boot. */
void sinput_usb_enter(void);
void sinput_btc_enter(uint8_t device_mac[6], bool pairing_mode);

#endif
