use chrono::{DateTime, Utc};

#[derive(Debug, PartialEq, Eq)]
// FirmwareState tracks firmware upgrade progress for a given device.
pub enum FirmwareState {
    OK, // desired firmware == reported firmware
    PENDING, // The desired firmware version has been changed, but device has not yet heartbeated to pick it up.
    STARTED, // Device has been sent a heartbeat response with new firmware in it
    FAILED // The device sent a new heartbeat message when already in STARTED state with a different firmware version from desired.
}

#[derive(Debug, PartialEq, Eq)]
pub struct DeviceState {
    pub device_id:  u64, // A unique identifier for this device. This is a good choice for primary key.
    pub device_friendly_name: String,
    pub desired_firmware: u32, // The firmware we wish for the device to run. 
    pub reported_firmware: u32, // The firmware version most recently reported by the device in it's heartbeat.
    pub firmware_state: FirmwareState, // The state of any in-progress firmware upgrades.
    pub last_heartbeat: DateTime<Utc>, // The last time the device made a heartbeat request.
    pub expected_heartbeat: DateTime<Utc>, // The next time we expect the device to make a heartbeat (This is after the checkin_interval in the device's config normally, but may be shorter if a firmware update has been started.)
    pub config: DeviceConfig,
}

#[derive(Debug, PartialEq, Eq)]
pub struct DeviceConfig {
    pub checkin_interval: u32 // How often the device should wake up to heartbeat and refresh the display.
}