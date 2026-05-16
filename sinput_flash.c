/**
 * @file ns_flash.c
 * @brief Minimal flash-backed persistence used by the Pico W example.
 *
 * Author: Mitchell Cairns
 * Copyright (c) 2026 Hand Held Legend, LLC.
 *
 * Licensed under the Creative Commons Attribution-NonCommercial 4.0 International
 * License (CC BY-NC 4.0). Non-commercial use with attribution; commercial use
 * requires permission from Hand Held Legend, LLC. Licensing inquiries:
 * support@handheldlegend.com
 * Full terms: https://creativecommons.org/licenses/by-nc/4.0/legalcode
 *
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#include "main.h"

#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/structs/xip_ctrl.h"

#include "pico/btstack_flash_bank.h"

/*
 * Reserve one sector immediately ahead of BTstack's flash-bank storage so the
 * example can persist its own pairing structure without colliding with BTstack.
 */
#define FLASH_START_OFFSET (PICO_FLASH_BANK_STORAGE_OFFSET - FLASH_SECTOR_SIZE)

/* Flash writes are queued so callers can request persistence from callbacks without blocking there. */
volatile bool _flash_go = false;
uint8_t *_write_from = NULL;
volatile uint32_t _write_size = 0;
volatile uint32_t _write_offset = 0;

uint32_t _get_sector_offset_read(uint32_t page)
{
    /* XIP reads use an absolute memory-mapped address. */
    uint32_t target_offset = XIP_BASE + FLASH_START_OFFSET - (page * FLASH_SECTOR_SIZE);
    return target_offset;
}

uint32_t _get_sector_offset_write(uint32_t page)
{
    /* Erase/program operations use the raw flash offset instead of the XIP alias. */
    uint32_t target_offset = FLASH_START_OFFSET - (page * FLASH_SECTOR_SIZE);
    return target_offset;
}

/* Queue a single-sector write request for later execution from ns_flash_task(). */
bool sinput_flash_write(uint8_t *data, uint32_t size, uint32_t page) 
{
    if(_flash_go) return false;
    if(size > FLASH_SECTOR_SIZE) return false;

    _write_from = data;
    _write_size = size;
    _write_offset = _get_sector_offset_write(page);
    _flash_go = true;

    return true;
}

void _flash_safe_write(void * params)
{
    (void)params;

    /*
     * The RP2040 can only program erased flash, so rebuild a full sector image
     * in RAM, erase the target sector, then program the completed sector back.
     */
    uint8_t thisPage[FLASH_SECTOR_SIZE] = {0x00};
    memcpy(thisPage, _write_from, _write_size);
    flash_range_erase(_write_offset, FLASH_SECTOR_SIZE);
    flash_range_program(_write_offset, thisPage, FLASH_SECTOR_SIZE);
}

bool sinput_flash_read(uint8_t *out, uint32_t size, uint32_t page) 
{
    if(size > FLASH_SECTOR_SIZE) return false;

    uint32_t offset = _get_sector_offset_read(page);
    const uint8_t *flash_target_contents = (const uint8_t *) (offset);
    memcpy(out, flash_target_contents, size);
    return true;
}

/* Initialize Pico's flash-safe execution helpers before any queued writes run. */
void sinput_flash_init()
{
    uint core = get_core_num();
    if(core==0)
    {
        flash_safe_execute_core_init();
    }
}

/* Service pending writes from the main transport loops. */
void sinput_flash_task()
{
    if(_flash_go)
    {
        flash_safe_execute(_flash_safe_write, NULL, UINT32_MAX);

        _write_from = NULL;
        _write_size = 0;
        _write_offset = 0;

        _flash_go = false;
    }
}
