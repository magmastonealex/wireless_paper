
use anyhow::anyhow;
use serde::{Deserialize, Serialize};
use thiserror::Error;
use async_trait::async_trait;

#[derive(Debug, PartialEq, Eq, Deserialize)]
pub struct DeviceHeartbeatRequest {
    pub device_id: u64, // The device will insert it's identifier here. This matches the device_id in DeviceState. Authentication is not required (or supported).
    pub current_firmware: u32, // The device will report it's current firmware. This is equivalent to the "reported firmware" elsewhere in the code.
    pub protocol_version: u8 // The version of the protocol this device supports. This may be used to determine how to shape the response so the device can understand it. For now, this should always be 1.
}

#[derive(Debug, PartialEq, Eq, Serialize)]
pub struct DeviceHeartbeatResponse {
    pub desired_firmware: u32, // The server will respond with the 
    pub checkin_interval: u32 // Number of seconds until the device should wake up again to heartbeat.
}

// BusinessError is a wrapper type representing errors sourced by the business logic layer.
#[derive(Error, Debug)]
pub enum BusinessError {
    #[error("Internal server error")]
    InternalError(#[source] anyhow::Error), // Treat this case as a COAP 5.00 response.

    #[error("Client provided malformed data")]
    BadRequest(#[source] anyhow::Error), // Treat this case as a COAP 4.00 response.
}

pub struct BusinessImpl {

}

impl BusinessImpl {
    /// handle_heartbeat will perform the following actions:
    /// 1. Load the DeviceState object for the device_id included in the request.
    /// 2. Examine the current_firmware included in the request.
    ///   2a. If the current_firmware == desired firmware, set firmware_state to OK.
    ///   2b. If current_firmware != desired firmware, and firmware_state is PENDING, set firmware_state to STARTED and ensure that the desired_firmware is included in the response. Set the expected_heartbeat value of the state to 5 minutes from now.
    ///   2c. If current_firmware != desired firmware, and firmware_state is STARTED, set firmware_state to FAILED and set desired_firmware in the response to match the current firmware so the device does not upgrade.
    ///   2d. If current_firmware != desired firmware, and firmware_state is FAILED, set desired_firmware in the response to match the current firmware so the device does not upgrade.
    /// 3. Set the last_heartbeat of the device state to the current time.
    /// 4. Set the expected_heartbeat to 30 seconds + the checkin_interval inside of the config.
    pub async fn handle_heartbeat(&self, _req: DeviceHeartbeatRequest) -> Result<DeviceHeartbeatResponse, BusinessError> {
        Err(BusinessError::InternalError(anyhow!("unimplemented")))
    }
}