/**
 * @file sinput_btc.c
 * @brief Bluetooth Classic HID transport for the Pico W example.
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

#include "main.h"

#include "pico/cyw43_arch.h"
#include "pico/btstack_chipset_cyw43.h"
#include "btstack.h"

#include "sinput_lib.h"
#include "sinput_lib_hid.h"

/* Bluetooth HID class-of-device used when advertising the controller role. */
#define NS_BTC_COD 0x2508

/* Match the same nominal report cadence used on the USB path. */
static const uint32_t _btc_poll_rate_ms = 8;
static uint32_t _btc_last_hid_report_timestamp_ms = 0;
static const char hid_device_name[] = "Wireless Gamepad";
static const char hid_gap_name[] = "SInput Gamepad Demo";
static bool hid_device_pair_enabled = false;
static btstack_timer_source_t hid_timer;

/* Used to defer CYW43 teardown out of the event callback path. */
volatile bool _btc_deinit_flag = false;

static inline void _btc_reverse_bytes(const uint8_t *in, uint8_t *out, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        out[i] = in[len - 1 - i];
    }
}

/*
 * BTstack persists the link key type alongside the key. Our app-level flash
 * storage only keeps the 16-byte key, so restoring a saved key requires us to
 * pick a type here while testing interoperability with the Switch.
 *
 * Candidates worth trying:
 *   COMBINATION_KEY
 *   UNAUTHENTICATED_COMBINATION_KEY_GENERATED_FROM_P192
 *   AUTHENTICATED_COMBINATION_KEY_GENERATED_FROM_P192
 *   UNAUTHENTICATED_COMBINATION_KEY_GENERATED_FROM_P256
 *   AUTHENTICATED_COMBINATION_KEY_GENERATED_FROM_P256
 */
#define NS_BTC_RESTORE_LINK_KEY_TYPE UNAUTHENTICATED_COMBINATION_KEY_GENERATED_FROM_P192

typedef struct
{
    uint16_t hid_cid;
} ns_btc_sm;

/* Scratch buffers for SDP records published to the host. */
static uint8_t hid_service_buffer[700] = {0};
static uint8_t pnp_service_buffer[700] = {0};
static btstack_packet_callback_registration_t hci_event_callback_registration;
static uint16_t hid_cid = 0;

static bool _ns_btc_array_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

static void _ns_btstack_debug_print(bd_addr_t addr, link_key_type_t link_key_type, link_key_t link_key)
{
    printf("Address: %s\n", bd_addr_to_str(addr));
    printf("Link key type: %u\n", link_key_type);
    printf("Link key: %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
    link_key[0], link_key[1], link_key[2], link_key[3], link_key[4], link_key[5], link_key[6], link_key[7],
    link_key[8], link_key[9], link_key[10], link_key[11], link_key[12], link_key[13], link_key[14], link_key[15]);
}

/* Re-arm packet transmission after a short delay when the 8 ms interval has not elapsed yet. */
static void _hid_timer_handler(btstack_timer_source_t *ts)
{
    (void)ts;

    hid_device_request_can_send_now_event(hid_cid);
}

static inline void _sinput_btc_hid_tunnel(const void *report, uint16_t len)
{
    uint8_t new_report[66] = {0};
    /* Bluetooth HID input traffic is wrapped with the 0xA1 data prefix before the report body. */
    new_report[0] = 0xA1;

    memcpy(&(new_report[1]), report, len);

    if (hid_cid)
    {
        hid_device_send_interrupt_message(hid_cid, new_report, len + 1);
    }
}

/* HID output reports from the host are rebuilt into the 64-byte format expected by NS-LIB-HID. */
uint8_t _sinput_btc_output_report_data[64] = {0};
const uint8_t *_sinput_btc_output_report = _sinput_btc_output_report_data;
static void _sinput_btc_outputreport_handler(uint16_t cid,
                                   hid_report_type_t report_type,
                                   uint16_t report_id,
                                   int report_size, uint8_t *report)
{
    if (report_type != HID_REPORT_TYPE_OUTPUT) return;
    if (cid != hid_cid) return;
    if (!report || report_size == 0) return;

    memset(_sinput_btc_output_report_data, 0, 64);

    _sinput_btc_output_report_data[0] = (uint8_t)report_id;

    if(report_size>63) report_size=63;

    /* BTstack hands us only the payload body, so rebuild the report ID at byte 0. */
    memcpy(&_sinput_btc_output_report_data[1], report, report_size);

    sinput_api_output_tunnel(_sinput_btc_output_report, report_size + 1);
}

static void _sinput_btc_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t packet_size)
{
    UNUSED(channel);
    UNUSED(packet_size);
    uint8_t status;
    if (packet_type == HCI_EVENT_PACKET)
    {
        switch (packet[0])
        {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING)
                return;

            if (!hid_cid)
            {
                /*
                 * Pairing mode deliberately stays discoverable. Reconnect mode instead
                 * tries to restore the last known host relationship automatically.
                 */
                if (hid_device_pair_enabled)
                {
                    gap_discoverable_control(1);
                }
                else
                {
                    /*
                     * BTstack keeps link keys in its own store. The example mirrors
                     * the last successful key in app flash so it can repopulate that
                     * store after a cold boot before requesting a reconnect.
                     */
                    link_key_t tmp_key;
                    link_key_type_t tmp_key_type;
                    if(device_storage.magic_byte == SINPUT_STORAGE_MAGIC && gap_get_link_key_for_bd_addr(device_storage.host_mac, tmp_key, &tmp_key_type))
                    {
                        printf("Host saved, reconnect attempt...\n");
                        hid_device_connect(device_storage.host_mac, &hid_cid);
                    }
                    else
                    {
                        printf("No Host saved, becoming discoverable...\n");
                        /* No credentials exist yet, so fall back to discoverable mode. */
                        gap_discoverable_control(1);
                    }
                }
            }
            break;

        case HCI_EVENT_LINK_KEY_NOTIFICATION:
            printf("HCI_EVENT_LINK_KEY_NOTIFICATION\n");
            bd_addr_t addr;
            hci_event_link_key_request_get_bd_addr(packet, addr);
        break;

        case HCI_EVENT_USER_CONFIRMATION_REQUEST:
            /* The example currently auto-accepts SSP confirmation. */
            printf("SSP User Confirmation Auto accept\n");
            break;

        case HCI_EVENT_HID_META:
            switch (hci_event_hid_meta_get_subevent_code(packet))
            {
            case HID_SUBEVENT_CONNECTION_OPENED:
                status = hid_subevent_connection_opened_get_status(packet);
                if (status)
                {
                    /* Failed reconnects go back to discoverable mode so the board is recoverable without reflashing. */
                    printf("Connection failed, status 0x%x\n", status);
                    gap_discoverable_control(1);
                    hid_cid = 0;
                    return;
                }

                hid_cid = hid_subevent_connection_opened_get_hid_cid(packet);
                bd_addr_t addr;
                uint8_t link_key[16] = {0};

                hid_subevent_connection_opened_get_bd_addr(packet, addr);

                printf("HID Connected to: %s\n", bd_addr_to_str(addr));
                hid_device_request_can_send_now_event(hid_cid);

                break;
            case HID_SUBEVENT_CONNECTION_CLOSED:
                printf("HID Disconnected\n");
                _btc_deinit_flag = true;
                printf("CYW43 DEINIT FLAG SET.\n");
                break;
            case HID_SUBEVENT_CAN_SEND_NOW:
                if (hid_cid)
                {
                    uint32_t current_time_ms = btstack_run_loop_get_time_ms();
                    uint32_t time_elapsed = current_time_ms - _btc_last_hid_report_timestamp_ms;
                    uint8_t _sinput_btc_report_data[64] = {0};

                    if (time_elapsed >= _btc_poll_rate_ms)
                    {   
                        /* Generate the next controller report only when the poll interval has elapsed. */
                        if(sinput_api_generate_inputreport(_sinput_btc_report_data))
                        {
                            _sinput_btc_hid_tunnel(_sinput_btc_report_data, 64);
                        }
                        else break;

                        _btc_last_hid_report_timestamp_ms = current_time_ms;
                        hid_device_request_can_send_now_event(hid_cid);
                    }
                    else
                    {
                        /* BTstack asked early, so reschedule instead of bursting reports too quickly. */
                        uint32_t time_elapsed = current_time_ms - _btc_last_hid_report_timestamp_ms;
                        uint32_t delay = _btc_poll_rate_ms - time_elapsed;
                        btstack_run_loop_set_timer(&hid_timer, delay);
                        btstack_run_loop_set_timer_handler(&hid_timer, &_hid_timer_handler);
                        btstack_run_loop_add_timer(&hid_timer);
                    }
                }
                break;
            case HID_SUBEVENT_SNIFF_SUBRATING_PARAMS:
            {
                uint16_t max = hid_subevent_sniff_subrating_params_get_host_max_latency(packet);
                uint16_t min = hid_subevent_sniff_subrating_params_get_host_min_timeout(packet);
                printf("Sniff: %d, %d\n", max, min);
            }
            break;

            default:
                break;
            }
            break;
        default:
            break;
        }
    }
}

void sinput_btc_enter(uint8_t device_mac[6], bool pairing_mode)
{
    /* Bring up the Pico W wireless stack before any BTstack objects are configured. */
    cyw43_arch_init();

    /* GAP identity and policy configuration. */
    gap_set_bondable_mode(1);
    gap_set_class_of_device(NS_BTC_COD);
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH | LM_LINK_POLICY_ENABLE_SNIFF_MODE);
    gap_set_local_name("SInput Gamepad Demo");
    gap_set_allow_role_switch(true);

    hci_set_chipset(btstack_chipset_cyw43_instance());

    l2cap_init();
    sm_init();

    sdp_init();

    const uint8_t* sinput_hid_descriptor = NULL;
    uint16_t sinput_hid_descriptor_len = 0;
    uint16_t sinput_hid_vid = 0;
    uint16_t sinput_hid_pid = 0;

    sinput_hid_get_descriptor_params(&sinput_hid_descriptor, &sinput_hid_descriptor_len, NULL, NULL, &sinput_hid_vid, &sinput_hid_pid);

    /* Publish the HID descriptor the host expects for this configured NS-LIB-HID transport. */
    hid_sdp_record_t hid_sdp_record = {
        .hid_device_subclass = NS_BTC_COD,  // Device Subclass HID
        .hid_country_code = 33,             // Country Code
        .hid_virtual_cable = 1,             // HID Virtual Cable
        .hid_remote_wake = 1,               // HID Remote Wake
        .hid_reconnect_initiate = 1,        // HID Reconnect Initiate
        .hid_normally_connectable = 0,      // HID Normally Connectable
        .hid_boot_device = 0,               // HID Boot Device
        .hid_ssr_host_max_latency = 0xFFFF, // = x * 0.625ms
        .hid_ssr_host_min_timeout = 0xFFFF,
        .hid_supervision_timeout = 3200,                 // HID Supervision Timeout
        .hid_descriptor         = sinput_hid_descriptor,     // HID Descriptor
        .hid_descriptor_size    = sinput_hid_descriptor_len, // HID Descriptor Length
        .device_name = hid_gap_name};                    // Device Name

    /* HID service is mandatory so the console can discover report descriptors and HID capabilities. */
    memset(hid_service_buffer, 0, sizeof(hid_service_buffer));
    hid_create_sdp_record(hid_service_buffer, sdp_create_service_record_handle(), &hid_sdp_record);
    sdp_register_service(hid_service_buffer);

    /* Device ID / PnP service improves compatibility with newer hosts that expect it during discovery. */
    memset(pnp_service_buffer, 0, sizeof(pnp_service_buffer));
    device_id_create_sdp_record(pnp_service_buffer, sdp_create_service_record_handle(), DEVICE_ID_VENDOR_ID_SOURCE_USB,
                                sinput_hid_vid, sinput_hid_pid, 0x0100);
    sdp_register_service(pnp_service_buffer);

    hid_device_init(0, sinput_hid_descriptor_len, sinput_hid_descriptor);

    /* Accept shortened reports so hosts that trim trailing zeros still interoperate. */
    hid_device_accept_truncated_hid_reports(true);

    /* Funnel both raw HCI events and HID meta-events through the same state machine. */
    hci_event_callback_registration.callback = &_sinput_btc_packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);
    hid_device_register_packet_handler(&_sinput_btc_packet_handler);

    hid_device_register_report_data_callback(&_sinput_btc_outputreport_handler);

    hid_device_pair_enabled = pairing_mode;

    /* Use the example's configured address before making the controller visible to other devices. */
    hci_power_control(HCI_POWER_ON);
    hci_set_bd_addr(device_mac);

    /* Main Bluetooth loop stays tiny: flash writes and any deferred teardown happen here. */
    for(;;)
    {
        sinput_flash_task();

        if(_btc_deinit_flag)
        {
            _btc_deinit_flag = false;
            cyw43_arch_deinit();
            printf("CYW43 DEINIT OK.\n");
        }
    }
}
