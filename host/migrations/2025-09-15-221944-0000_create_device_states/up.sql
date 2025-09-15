CREATE TYPE firmware_state AS ENUM ('OK', 'PENDING', 'STARTED', 'FAILED');

CREATE TABLE device_states (
    device_id BIGINT PRIMARY KEY,
    device_friendly_name VARCHAR NOT NULL,
    desired_firmware INTEGER NOT NULL,
    reported_firmware INTEGER NOT NULL,
    firmware_state firmware_state NOT NULL,
    last_heartbeat TIMESTAMP WITH TIME ZONE NOT NULL,
    expected_heartbeat TIMESTAMP WITH TIME ZONE NOT NULL,
    checkin_interval INTEGER NOT NULL
);