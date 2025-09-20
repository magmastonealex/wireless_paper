#pragma once

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/coap_client.h>

/**
 * Result codes for CoAP requests
 */
typedef enum {
    COAP_REQUEST_SUCCESS = 0,
    COAP_REQUEST_TIMEOUT = -1,
    COAP_REQUEST_NETWORK_ERROR = -2,
    COAP_REQUEST_PROTO_ERROR = -3,
    COAP_REQUEST_CALLBACK_ABORT = -4
} coap_request_result_t;

/**
 * Stream callback function type
 *
 * Called for each chunk of data received successfully.
 * Only called for valid data - never for errors.
 *
 * @param data: Pointer to received data
 * @param len: Length of data in bytes
 * @param offset: Byte offset of this chunk in the overall transfer
 * @param user_data: User-provided context pointer
 * @return: 0 to continue, negative to abort the request
 */
typedef int (*coap_stream_callback_t)(
    const uint8_t *data,
    size_t len,
    size_t offset,
    bool is_last,
    void *user_data
);

coap_request_result_t do_coap_request(struct coap_client *client, struct sockaddr *server_addr, const char* path, enum coap_method method, const uint8_t* payload, size_t payload_len, coap_stream_callback_t stream_cb, void* user_data, uint32_t timeout_seconds);