
use std::fmt::Debug;
use std::sync::Arc;

use anyhow::anyhow;
use serde::{Deserialize, Serialize};
use thiserror::Error;
use chrono::{Utc, Duration};

use crate::database::{Database, DatabaseError};
use crate::types::FirmwareState;

#[derive(Debug, PartialEq, Eq, Deserialize)]
pub struct DeviceHeartbeatRequest {
    pub device_id: u64, // The device will insert it's identifier here. This matches the device_id in DeviceState. Authentication is not required (or supported).
    pub current_firmware: u32, // The device will report it's current firmware. This is equivalent to the "reported firmware" elsewhere in the code.
    pub vbat_mv: i32, // measured battery voltage
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

#[derive(Debug)]
pub struct BusinessImpl {
    pub db: Arc<dyn Database + Send + Sync>,
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
    pub async fn handle_heartbeat(&self, req: DeviceHeartbeatRequest) -> Result<DeviceHeartbeatResponse, BusinessError> {
        // 1. Load the DeviceState object for the device_id included in the request
        let mut device_state = self.db.get_device_state(req.device_id).await
            .map_err(|e| match e {
                DatabaseError::DeviceNotFound { .. } =>
                    BusinessError::BadRequest(anyhow!("Device not found: {}", req.device_id)),
                _ => BusinessError::InternalError(anyhow!("Database error: {}", e))
            })?;

        let now = Utc::now();
        let current_firmware = req.current_firmware as i32;

        // 2. Examine the current_firmware and handle different cases
        let (new_firmware_state, desired_firmware_for_response) = match (
            current_firmware == device_state.desired_firmware,
            &device_state.firmware_state
        ) {
            // 2a. If current_firmware == desired firmware, set firmware_state to OK
            (true, _) => (FirmwareState::OK, device_state.desired_firmware),

            // 2b. If current_firmware != desired firmware, and firmware_state is PENDING
            (false, FirmwareState::PENDING) => {
                // Set firmware_state to STARTED and set expected_heartbeat to 5 minutes from now
                device_state.expected_heartbeat = now + Duration::minutes(5);
                (FirmwareState::STARTED, device_state.desired_firmware)
            },

            // 2c. If current_firmware != desired firmware, and firmware_state is STARTED
            (false, FirmwareState::STARTED) => {
                // Set firmware_state to FAILED and set desired_firmware to match current_firmware
                device_state.desired_firmware = current_firmware;
                (FirmwareState::FAILED, current_firmware)
            },

            // 2d. If current_firmware != desired firmware, and firmware_state is FAILED
            (false, FirmwareState::FAILED) => {
                // Set desired_firmware to match current_firmware so device does not upgrade
                device_state.desired_firmware = current_firmware;
                (FirmwareState::FAILED, current_firmware)
            },

            // Handle OK state where firmware doesn't match (shouldn't happen normally)
            (false, FirmwareState::OK) => {
                // Treat as PENDING to start upgrade process
                device_state.expected_heartbeat = now + Duration::minutes(5);
                (FirmwareState::STARTED, device_state.desired_firmware)
            }
        };

        // Update the device state
        device_state.firmware_state = new_firmware_state;
        device_state.reported_firmware = current_firmware;

        // 3. Set the last_heartbeat to the current time
        device_state.last_heartbeat = now;
        device_state.vbat_mv = req.vbat_mv;

        // 4. Set the expected_heartbeat to 30 seconds + checkin_interval (if not already set above)
        if device_state.firmware_state != FirmwareState::STARTED {
            device_state.expected_heartbeat = now + Duration::seconds(600 + device_state.checkin_interval as i64);
        }

        // Save the updated device state
        self.db.update_device_state(&device_state).await
            .map_err(|e| BusinessError::InternalError(anyhow!("Failed to update device state: {}", e)))?;

        // Return the response
        Ok(DeviceHeartbeatResponse {
            desired_firmware: desired_firmware_for_response as u32,
            checkin_interval: device_state.checkin_interval as u32,
        })
    }
}


#[cfg(test)]
mod tests {
    use super::*;
    use chrono::{Utc, Duration};
    use crate::{mock_database::MockDatabase, types::DeviceState};

    fn create_test_device(device_id: u64, desired_firmware: i32, reported_firmware: i32, firmware_state: FirmwareState) -> DeviceState {
        let now = Utc::now();
        DeviceState {
            device_id: device_id as i64,
            device_friendly_name: format!("Test Device {}", device_id),
            desired_firmware,
            reported_firmware,
            firmware_state,
            last_heartbeat: now - Duration::minutes(1),
            expected_heartbeat: now + Duration::seconds(30),
            checkin_interval: 60,
            vbat_mv: 1500,
        }
    }

    fn create_business_impl(mock_db: MockDatabase) -> BusinessImpl {
        BusinessImpl {
            db: Arc::new(mock_db),
        }
    }

    #[tokio::test]
    async fn test_heartbeat_firmware_matches_sets_ok() {
        // Test case 2a: current_firmware == desired firmware -> set to OK
        let mock_db = MockDatabase::new();
        let device = create_test_device(1, 100, 90, FirmwareState::PENDING);
        mock_db.insert_device(device);

        let business = create_business_impl(mock_db);

        let request = DeviceHeartbeatRequest {
            device_id: 1,
            current_firmware: 100, // Matches desired firmware
            protocol_version: 1,
            vbat_mv: 900,
        };

        let response = business.handle_heartbeat(request).await.unwrap();

        assert_eq!(response.desired_firmware, 100);
        assert_eq!(response.checkin_interval, 60);

        // Verify device state was updated
        let updated_device = business.db.get_device_state(1).await.unwrap();
        assert_eq!(updated_device.firmware_state, FirmwareState::OK);
        assert_eq!(updated_device.reported_firmware, 100);
        assert_eq!(updated_device.vbat_mv, 900);
    }

    #[tokio::test]
    async fn test_heartbeat_pending_to_started() {
        // Test case 2b: current_firmware != desired firmware, state PENDING -> STARTED
        let mock_db = MockDatabase::new();
        let device = create_test_device(2, 200, 100, FirmwareState::PENDING);
        mock_db.insert_device(device);

        let business = create_business_impl(mock_db);

        let request = DeviceHeartbeatRequest {
            device_id: 2,
            current_firmware: 100, // Different from desired (200)
            protocol_version: 1,
            vbat_mv: 1500,
        };

        let response = business.handle_heartbeat(request).await.unwrap();

        assert_eq!(response.desired_firmware, 200); // Should return desired firmware
        assert_eq!(response.checkin_interval, 60);

        // Verify device state was updated
        let updated_device = business.db.get_device_state(2).await.unwrap();
        assert_eq!(updated_device.firmware_state, FirmwareState::STARTED);
        assert_eq!(updated_device.reported_firmware, 100);

        // Expected heartbeat should be set to 5 minutes from now
        let time_diff = updated_device.expected_heartbeat - updated_device.last_heartbeat;
        assert!(time_diff >= Duration::minutes(4) && time_diff <= Duration::minutes(6));
    }

    #[tokio::test]
    async fn test_heartbeat_started_to_failed() {
        // Test case 2c: current_firmware != desired firmware, state STARTED -> FAILED
        let mock_db = MockDatabase::new();
        let device = create_test_device(3, 300, 200, FirmwareState::STARTED);
        mock_db.insert_device(device);

        let business = create_business_impl(mock_db);

        let request = DeviceHeartbeatRequest {
            device_id: 3,
            current_firmware: 200, // Different from desired (300)
            protocol_version: 1,
            vbat_mv: 1500,
        };

        let response = business.handle_heartbeat(request).await.unwrap();

        assert_eq!(response.desired_firmware, 200); // Should match current firmware
        assert_eq!(response.checkin_interval, 60);

        // Verify device state was updated
        let updated_device = business.db.get_device_state(3).await.unwrap();
        assert_eq!(updated_device.firmware_state, FirmwareState::FAILED);
        assert_eq!(updated_device.reported_firmware, 200);
        assert_eq!(updated_device.desired_firmware, 200); // Should be set to current firmware
    }

    #[tokio::test]
    async fn test_heartbeat_failed_stays_failed() {
        // Test case 2d: current_firmware != desired firmware, state FAILED -> FAILED
        let mock_db = MockDatabase::new();
        let device = create_test_device(4, 400, 300, FirmwareState::FAILED);
        mock_db.insert_device(device);

        let business = create_business_impl(mock_db);

        let request = DeviceHeartbeatRequest {
            device_id: 4,
            current_firmware: 300, // Different from desired (400)
            protocol_version: 1,
            vbat_mv: 1500,
        };

        let response = business.handle_heartbeat(request).await.unwrap();

        assert_eq!(response.desired_firmware, 300); // Should match current firmware
        assert_eq!(response.checkin_interval, 60);

        // Verify device state was updated
        let updated_device = business.db.get_device_state(4).await.unwrap();
        assert_eq!(updated_device.firmware_state, FirmwareState::FAILED);
        assert_eq!(updated_device.reported_firmware, 300);
        assert_eq!(updated_device.desired_firmware, 300); // Should be set to current firmware
    }

    #[tokio::test]
    async fn test_heartbeat_ok_firmware_mismatch_starts_upgrade() {
        // Edge case: OK state but firmware doesn't match -> start upgrade process
        let mock_db = MockDatabase::new();
        let device = create_test_device(5, 500, 400, FirmwareState::OK);
        mock_db.insert_device(device);

        let business = create_business_impl(mock_db);

        let request = DeviceHeartbeatRequest {
            device_id: 5,
            current_firmware: 400, // Different from desired (500)
            protocol_version: 1,
            vbat_mv: 1500,
        };

        let response = business.handle_heartbeat(request).await.unwrap();

        assert_eq!(response.desired_firmware, 500); // Should return desired firmware
        assert_eq!(response.checkin_interval, 60);

        // Verify device state was updated
        let updated_device = business.db.get_device_state(5).await.unwrap();
        assert_eq!(updated_device.firmware_state, FirmwareState::STARTED);
        assert_eq!(updated_device.reported_firmware, 400);

        // Expected heartbeat should be set to 5 minutes from now
        let time_diff = updated_device.expected_heartbeat - updated_device.last_heartbeat;
        assert!(time_diff >= Duration::minutes(4) && time_diff <= Duration::minutes(6));
    }

    #[tokio::test]
    async fn test_heartbeat_updates_timestamps() {
        // Test that last_heartbeat and expected_heartbeat are properly updated
        let mock_db = MockDatabase::new();
        let old_time = Utc::now() - Duration::hours(1);
        let mut device = create_test_device(6, 100, 100, FirmwareState::OK);
        device.last_heartbeat = old_time;
        device.expected_heartbeat = old_time;
        mock_db.insert_device(device);

        let business = create_business_impl(mock_db);

        let request = DeviceHeartbeatRequest {
            device_id: 6,
            current_firmware: 100,
            protocol_version: 1,
            vbat_mv: 1500,
        };

        let before_request = Utc::now();
        let _response = business.handle_heartbeat(request).await.unwrap();
        let after_request = Utc::now();

        // Verify timestamps were updated
        let updated_device = business.db.get_device_state(6).await.unwrap();

        // last_heartbeat should be updated to current time
        assert!(updated_device.last_heartbeat >= before_request);
        assert!(updated_device.last_heartbeat <= after_request);

        // expected_heartbeat should be set to last_heartbeat + 30 seconds + checkin_interval
        let expected_next = updated_device.last_heartbeat + Duration::seconds(90); // 30 + 60
        let time_diff = (updated_device.expected_heartbeat - expected_next).abs();
        assert!(time_diff <= Duration::seconds(1)); // Allow for small timing differences
    }

    #[tokio::test]
    async fn test_heartbeat_device_not_found() {
        // Test error handling for non-existent device
        let mock_db = MockDatabase::new();
        let business = create_business_impl(mock_db);

        let request = DeviceHeartbeatRequest {
            device_id: 999, // Non-existent device
            current_firmware: 100,
            protocol_version: 1,
            vbat_mv: 1500,
        };

        let result = business.handle_heartbeat(request).await;

        assert!(result.is_err());
        match result.unwrap_err() {
            BusinessError::BadRequest(err) => {
                assert!(err.to_string().contains("Device not found: 999"));
            },
            _ => panic!("Expected BadRequest error"),
        }
    }

    #[tokio::test]
    async fn test_successful_firmware_upgrade_flow() {
        // Test complete firmware upgrade flow: PENDING -> STARTED -> OK
        let mock_db = MockDatabase::new();
        let device = create_test_device(7, 200, 100, FirmwareState::PENDING);
        mock_db.insert_device(device);

        let business = create_business_impl(mock_db);

        // Step 1: Device reports old firmware, should transition to STARTED
        let request1 = DeviceHeartbeatRequest {
            device_id: 7,
            current_firmware: 100,
            protocol_version: 1,
            vbat_mv: 1500,
        };

        let response1 = business.handle_heartbeat(request1).await.unwrap();
        assert_eq!(response1.desired_firmware, 200);

        let device_after_step1 = business.db.get_device_state(7).await.unwrap();
        assert_eq!(device_after_step1.firmware_state, FirmwareState::STARTED);

        // Step 2: Device reports new firmware, should transition to OK
        let request2 = DeviceHeartbeatRequest {
            device_id: 7,
            current_firmware: 200, // Now matches desired
            protocol_version: 1,
            vbat_mv: 1500,
        };

        let response2 = business.handle_heartbeat(request2).await.unwrap();
        assert_eq!(response2.desired_firmware, 200);

        let device_after_step2 = business.db.get_device_state(7).await.unwrap();
        assert_eq!(device_after_step2.firmware_state, FirmwareState::OK);
        assert_eq!(device_after_step2.reported_firmware, 200);
    }
}