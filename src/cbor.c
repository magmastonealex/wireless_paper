#include <zephyr/kernel.h>
#include <zcbor_encode.h>
#include <zcbor_decode.h>
#include <zcbor_common.h>
#include <string.h>
#include "cbor.h"


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