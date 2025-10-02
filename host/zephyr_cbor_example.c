/*
 * Zephyr CBOR Example for E-Paper IoT Device Heartbeat
 *
 * This file demonstrates how to use Zephyr's zcbor module to encode and decode
 * heartbeat messages for the e-paper IoT device management system.
 *
 * Message formats:
 *
 * DeviceHeartbeatRequest:
 * {
 *   "device_id": <uint64>,
 *   "current_firmware": <uint32>,
 *   "protocol_version": <uint8>
 * }
 *
 * DeviceHeartbeatResponse:
 * {
 *   "desired_firmware": <uint32>,
 *   "checkin_interval": <uint32>
 * }
 */

#include <zephyr/kernel.h>
#include <zcbor_encode.h>
#include <zcbor_decode.h>
#include <zcbor_common.h>
#include <string.h>

/* Structure definitions matching the Rust types */
struct device_heartbeat_request {
    uint64_t device_id;
    uint32_t current_firmware;
    uint8_t protocol_version;
    int32_t vbat_mv;
};

struct device_heartbeat_response {
    uint32_t desired_firmware;
    uint32_t checkin_interval;
};

/*
 * Encode a DeviceHeartbeatRequest into CBOR format
 *
 * @param req: The request structure to encode
 * @param buffer: Output buffer for CBOR data
 * @param buffer_size: Size of the output buffer
 * @param encoded_size: Returns the actual encoded size
 * @return: 0 on success, negative error code on failure
 */
int encode_heartbeat_request(const struct device_heartbeat_request *req,
                           uint8_t *buffer, size_t buffer_size, size_t *encoded_size)
{
    zcbor_state_t states[4];
    bool success;

    zcbor_new_state(states, ARRAY_SIZE(states), buffer, buffer_size, 1);

    /* Start encoding a map with 3 key-value pairs */
    success = zcbor_map_start_encode(states, 4);
    if (!success) {
        return -ENOMEM;
    }

    /* Encode device_id */
    success = zcbor_tstr_put_lit(states, "device_id") &&
              zcbor_uint64_put(states, req->device_id);
    if (!success) {
        return -ENOMEM;
    }

    /* Encode current_firmware */
    success = zcbor_tstr_put_lit(states, "current_firmware") &&
              zcbor_uint32_put(states, req->current_firmware);
    if (!success) {
        return -ENOMEM;
    }

    /* Encode protocol_version */
    success = zcbor_tstr_put_lit(states, "protocol_version") &&
              zcbor_uint32_put(states, req->protocol_version);
    if (!success) {
        return -ENOMEM;
    }

    /* Encode protocol_version */
    success = zcbor_tstr_put_lit(states, "vbat_mv") &&
              zcbor_int32_put(states, req->vbat_mv);
    if (!success) {
        return -ENOMEM;
    }

    /* End the map */
    success = zcbor_map_end_encode(states, 4);
    if (!success) {
        return -ENOMEM;
    }

    if (encoded_size) {
        *encoded_size = states[0].payload - buffer;
    }

    return 0;
}

/*
 * Decode a DeviceHeartbeatResponse from CBOR format
 *
 * @param buffer: CBOR data to decode
 * @param buffer_size: Size of the CBOR data
 * @param resp: Output structure for decoded response
 * @return: 0 on success, negative error code on failure
 */
int decode_heartbeat_response(const uint8_t *buffer, size_t buffer_size,
                            struct device_heartbeat_response *resp)
{
    zcbor_state_t states[3];
    bool success;
    size_t map_count;
    struct zcbor_string key;
    uint32_t value;

    zcbor_new_state(states, ARRAY_SIZE(states), buffer, buffer_size, 1);

    /* Start decoding the map */
    success = zcbor_map_start_decode(states);
    if (!success) {
        return -EINVAL;
    }

    /* Initialize response structure */
    memset(resp, 0, sizeof(*resp));

    /* Decode map entries - we expect 2 key-value pairs */
    while (zcbor_map_decode_key_val(states, &key, &value)) {
        if (key.len == strlen("desired_firmware") &&
            memcmp(key.value, "desired_firmware", key.len) == 0) {
            /* Decode desired_firmware value */
            success = zcbor_uint32_decode(states, &resp->desired_firmware);
            if (!success) {
                return -EINVAL;
            }
        } else if (key.len == strlen("checkin_interval") &&
                   memcmp(key.value, "checkin_interval", key.len) == 0) {
            /* Decode checkin_interval value */
            success = zcbor_uint32_decode(states, &resp->checkin_interval);
            if (!success) {
                return -EINVAL;
            }
        } else {
            /* Unknown key - skip the value */
            zcbor_any_skip(states, NULL);
        }
    }

    /* End the map */
    success = zcbor_map_end_decode(states);
    if (!success) {
        return -EINVAL;
    }

    return 0;
}

/*
 * Example usage function demonstrating encoding and decoding
 */
void heartbeat_cbor_example(void)
{
    uint8_t buffer[256];
    size_t encoded_size;
    int ret;

    /* Create a sample heartbeat request */
    struct device_heartbeat_request req = {
        .device_id = 1001,
        .current_firmware = 100,
        .protocol_version = 1
    };

    /* Encode the request */
    ret = encode_heartbeat_request(&req, buffer, sizeof(buffer), &encoded_size);
    if (ret < 0) {
        printk("Failed to encode heartbeat request: %d\n", ret);
        return;
    }

    printk("Encoded heartbeat request (%zu bytes):\n", encoded_size);
    for (size_t i = 0; i < encoded_size; i++) {
        printk("%02x ", buffer[i]);
    }
    printk("\n");

    /* Example: decode a response (normally this would come from the server) */
    /* This represents: {"desired_firmware": 110, "checkin_interval": 60} */
    uint8_t response_cbor[] = {
        0xa2,  /* map(2) */
        0x70,  /* text(16) "desired_firmware" */
        'd', 'e', 's', 'i', 'r', 'e', 'd', '_',
        'f', 'i', 'r', 'm', 'w', 'a', 'r', 'e',
        0x18, 0x6e,  /* unsigned(110) */
        0x70,  /* text(16) "checkin_interval" */
        'c', 'h', 'e', 'c', 'k', 'i', 'n', '_',
        'i', 'n', 't', 'e', 'r', 'v', 'a', 'l',
        0x18, 0x3c   /* unsigned(60) */
    };

    struct device_heartbeat_response resp;
    ret = decode_heartbeat_response(response_cbor, sizeof(response_cbor), &resp);
    if (ret < 0) {
        printk("Failed to decode heartbeat response: %d\n", ret);
        return;
    }

    printk("Decoded heartbeat response:\n");
    printk("  desired_firmware: %u\n", resp.desired_firmware);
    printk("  checkin_interval: %u\n", resp.checkin_interval);
}

/*
 * Alternative implementation using zcbor code generation
 *
 * For production use, you would typically generate the encoding/decoding
 * functions from a CDDL (Concise Data Definition Language) schema using
 * the zcbor code generator tool.
 *
 * Example CDDL schema for the heartbeat messages:
 *
 * device-heartbeat-request = {
 *   "device_id": uint,
 *   "current_firmware": uint,
 *   "protocol_version": uint
 * }
 *
 * device-heartbeat-response = {
 *   "desired_firmware": uint,
 *   "checkin_interval": uint
 * }
 *
 * Then run: zcbor code -c heartbeat.cddl --output-c heartbeat_cbor.c --output-h heartbeat_cbor.h
 *
 * This would generate optimized encode/decode functions like:
 * - encode_device_heartbeat_request()
 * - decode_device_heartbeat_response()
 */

/*
 * Integration with CoAP in Zephyr
 *
 * This example shows how you might integrate the CBOR encoding with
 * Zephyr's CoAP client to send heartbeat requests.
 */
#ifdef CONFIG_COAP

#include <zephyr/net/coap.h>
#include <zephyr/net/socket.h>

int send_heartbeat_coap(const struct device_heartbeat_request *req,
                       struct device_heartbeat_response *resp,
                       const char *server_addr, uint16_t server_port)
{
    uint8_t request_buffer[256];
    uint8_t response_buffer[256];
    size_t request_size;
    struct coap_packet request, response;
    struct sockaddr_in6 server;
    int sock, ret;

    /* Encode the heartbeat request */
    ret = encode_heartbeat_request(req, request_buffer, sizeof(request_buffer), &request_size);
    if (ret < 0) {
        return ret;
    }

    /* Create socket */
    sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return -errno;
    }

    /* Setup server address */
    memset(&server, 0, sizeof(server));
    server.sin6_family = AF_INET6;
    server.sin6_port = htons(server_port);
    inet_pton(AF_INET6, server_addr, &server.sin6_addr);

    /* Initialize CoAP request */
    ret = coap_packet_init(&request, response_buffer, sizeof(response_buffer),
                          1, COAP_TYPE_CON, 8, coap_next_token(),
                          COAP_METHOD_POST, coap_next_id());
    if (ret < 0) {
        goto cleanup;
    }

    /* Add URI path */
    ret = coap_packet_append_option(&request, COAP_OPTION_URI_PATH, "hb", 2);
    if (ret < 0) {
        goto cleanup;
    }

    /* Add CBOR payload */
    ret = coap_packet_append_payload_marker(&request);
    if (ret < 0) {
        goto cleanup;
    }

    ret = coap_packet_append_payload(&request, request_buffer, request_size);
    if (ret < 0) {
        goto cleanup;
    }

    /* Send the request */
    ret = sendto(sock, request.data, request.offset, 0,
                (struct sockaddr *)&server, sizeof(server));
    if (ret < 0) {
        goto cleanup;
    }

    /* Receive response (simplified - should include timeout handling) */
    ret = recv(sock, response_buffer, sizeof(response_buffer), 0);
    if (ret < 0) {
        goto cleanup;
    }

    /* Parse CoAP response */
    ret = coap_packet_parse(&response, response_buffer, ret, NULL, 0);
    if (ret < 0) {
        goto cleanup;
    }

    /* Extract and decode CBOR payload */
    const uint8_t *payload;
    uint16_t payload_len;

    payload = coap_packet_get_payload(&response, &payload_len);
    if (payload) {
        ret = decode_heartbeat_response(payload, payload_len, resp);
    } else {
        ret = -EINVAL;
    }

cleanup:
    close(sock);
    return ret;
}

#endif /* CONFIG_COAP */