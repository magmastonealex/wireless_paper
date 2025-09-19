use std::collections::HashMap;
use std::sync::{Arc, Mutex};

use async_trait::async_trait;

use crate::{
    database::{Database, DatabaseError},
    types::DeviceState,
};

#[derive(Debug)]
pub struct MockDatabase {
    devices: Arc<Mutex<HashMap<u64, DeviceState>>>,
}

impl MockDatabase {
    pub fn new() -> Self {
        Self {
            devices: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    pub fn insert_device(&self, device: DeviceState) {
        let mut devices = self.devices.lock().unwrap();
        devices.insert(device.device_id as u64, device);
    }

    pub fn get_device(&self, device_id: u64) -> Option<DeviceState> {
        let devices = self.devices.lock().unwrap();
        devices.get(&device_id).cloned()
    }

    pub fn clear(&self) {
        let mut devices = self.devices.lock().unwrap();
        devices.clear();
    }
}

#[async_trait]
impl Database for MockDatabase {
    async fn get_device_state(&self, device_id: u64) -> Result<DeviceState, DatabaseError> {
        let devices = self.devices.lock().unwrap();
        devices.get(&device_id).cloned()
            .ok_or(DatabaseError::DeviceNotFound { device_id })
    }

    async fn update_device_state(&self, device_state: &DeviceState) -> Result<(), DatabaseError> {
        let mut devices = self.devices.lock().unwrap();
        let device_id = device_state.device_id as u64;

        if devices.contains_key(&device_id) {
            devices.insert(device_id, device_state.clone());
            Ok(())
        } else {
            Err(DatabaseError::DeviceNotFound { device_id })
        }
    }

    async fn create_device_state(&self, device_state: &DeviceState) -> Result<(), DatabaseError> {
        let mut devices = self.devices.lock().unwrap();
        let device_id = device_state.device_id as u64;
        devices.insert(device_id, device_state.clone());
        Ok(())
    }

    async fn list_all_devices(&self) -> Result<Vec<DeviceState>, DatabaseError> {
        let devices = self.devices.lock().unwrap();
        Ok(devices.values().cloned().collect())
    }

    async fn delete_device_state(&self, device_id: u64) -> Result<(), DatabaseError> {
        let mut devices = self.devices.lock().unwrap();

        if devices.remove(&device_id).is_some() {
            Ok(())
        } else {
            Err(DatabaseError::DeviceNotFound { device_id })
        }
    }
}