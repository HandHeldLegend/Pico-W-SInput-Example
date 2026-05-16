#ifndef _PICO_BTSTACK_BTSTACK_CONFIG_H
#define _PICO_BTSTACK_BTSTACK_CONFIG_H

// BTstack features that can be enabled
#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_CENTRAL
#define ENABLE_L2CAP_LE_CREDIT_BASED_FLOW_CONTROL_MODE
//#define ENABLE_LOG_INFO 
#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP
#define ENABLE_SCO_OVER_HCI

/* Fixed-size resource counts sized for one controller-class device on constrained hardware. */
#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_PAYLOAD_SIZE 128

#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4
#define MAX_NR_AVDTP_CONNECTIONS 0
#define MAX_NR_AVDTP_STREAM_ENDPOINTS 1
#define MAX_NR_AVRCP_CONNECTIONS 2
#define MAX_NR_BNEP_CHANNELS 0
#define MAX_NR_BNEP_SERVICES 1
#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES 3
#define MAX_NR_GATT_CLIENTS 1
#define MAX_NR_HCI_CONNECTIONS 3
#define MAX_NR_HID_HOST_CONNECTIONS 1
#define MAX_NR_HIDS_CLIENTS 1
#define MAX_NR_HFP_CONNECTIONS 0
#define MAX_NR_L2CAP_CHANNELS 4
#define MAX_NR_L2CAP_SERVICES 3
#define MAX_NR_RFCOMM_CHANNELS 1
#define MAX_NR_RFCOMM_MULTIPLEXERS 1
#define MAX_NR_RFCOMM_SERVICES 1
#define MAX_NR_SERVICE_RECORD_ITEMS 4
#define MAX_NR_SM_LOOKUP_ENTRIES 3
#define MAX_NR_WHITELIST_ENTRIES 16
#define MAX_NR_LE_DEVICE_DB_ENTRIES 16

/* Limit controller-side buffering so the CYW43 shared bus is not overrun under bursty traffic. */
#define MAX_NR_CONTROLLER_ACL_BUFFERS 8
#define MAX_NR_CONTROLLER_SCO_PACKETS 0

/* Flow control further reduces the chance of overrunning the Pico W radio transport. */
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN 1024
#define HCI_HOST_ACL_PACKET_NUM 2
#define HCI_HOST_SCO_PACKET_LEN 0
#define HCI_HOST_SCO_PACKET_NUM 0

/* Allow BTstack to keep its own key database in flash alongside the example's app-owned sector. */
#define NVM_NUM_DEVICE_DB_ENTRIES 16
#define NVM_NUM_LINK_KEYS 16

/* Avoid heap allocation by using a fixed-size ATT database. */
#define MAX_ATT_DB_SIZE 512

/* BTstack HAL integration points supplied by the Pico SDK and this example. */
#define HAVE_EMBEDDED_TIME_MS

/* Map btstack_assert() onto the Pico SDK's assert implementation. */
#define HAVE_ASSERT

/* Give slower controllers longer to finish HCI reset before retrying. */
#define HCI_RESET_RESEND_TIMEOUT_MS 1000

#define ENABLE_SOFTWARE_AES128
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS

#define HAVE_BTSTACK_STDIN

#ifdef ENABLE_CLASSIC
#define ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE
#endif
#endif  // MICROPY_INCLUDED_EXTMOD_BTSTACK_BTSTACK_CONFIG_H