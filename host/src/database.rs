use thiserror::Error;
use crate::types::{DeviceState};

#[derive(Error, Debug)]
pub enum DatabaseError {
    #[error("Device not found with ID: {device_id}")]
    DeviceNotFound { device_id: u64 },

}

// "database" is a serialization/deserialization layer for DeviceState and DeviceConfig objects.
// It will use the diesel library to save/load objects from Postgres.
pub trait Database {
}


pub struct DBImpl {
}

impl DBImpl {
    pub async fn new(connstr: &str) -> Result<Self, anyhow::Error> {
        Err(anyhow::anyhow!("unimplemented"))
    }
}



//cargo install diesel_cli --no-default-features --features postgres,postgres-bundled
// database URL for testing: psql "postgres://epaper:epaper_password@127.0.0.1/epaper"