use chrono::Utc;
use crate::database::{Database, DBImpl};
use crate::types::{DeviceState, FirmwareState};

pub async fn test_database_operations() -> Result<(), Box<dyn std::error::Error>> {
    let conn_str = "postgres://epaper:epaper_password@127.0.0.1/epaper";
    let db = DBImpl::new(conn_str).await?;

    let test_device = DeviceState {
        device_id: 12345,
        device_friendly_name: "Test Device".to_string(),
        desired_firmware: 100,
        reported_firmware: 100,
        firmware_state: FirmwareState::OK,
        last_heartbeat: Utc::now(),
        expected_heartbeat: Utc::now(),
        checkin_interval: 30,
    };

    println!("Testing database operations...");

    println!("1. Creating device state...");
    db.create_device_state(&test_device).await?;
    println!("   ✓ Device created successfully");

    println!("2. Reading device state...");
    let retrieved_device = db.get_device_state(12345).await?;
    println!("   ✓ Device retrieved: {}", retrieved_device.device_friendly_name);

    println!("3. Updating device state...");
    let mut updated_device = retrieved_device;
    updated_device.firmware_state = FirmwareState::PENDING;
    updated_device.desired_firmware = 101;
    db.update_device_state(&updated_device).await?;
    println!("   ✓ Device updated successfully");

    println!("4. Verifying update...");
    let final_device = db.get_device_state(12345).await?;
    assert_eq!(final_device.firmware_state, FirmwareState::PENDING);
    assert_eq!(final_device.desired_firmware, 101);
    println!("   ✓ Update verified");

    println!("5. Testing save (upsert) operation...");
    let mut save_device = final_device;
    save_device.firmware_state = FirmwareState::STARTED;
    db.save_device_state(&save_device).await?;
    println!("   ✓ Device saved successfully");

    let saved_device = db.get_device_state(12345).await?;
    assert_eq!(saved_device.firmware_state, FirmwareState::STARTED);
    println!("   ✓ Save operation verified");

    println!("All database tests passed! ✅");
    Ok(())
}