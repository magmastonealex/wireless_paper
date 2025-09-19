#pragma once

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

int encode_heartbeat_request(const struct device_heartbeat_request *req, uint8_t *buffer, size_t buffer_size, size_t *encoded_size);
int decode_heartbeat_response(const uint8_t *buffer, size_t buffer_size,struct device_heartbeat_response *resp);