use sqlx::{postgres::PgPoolOptions, Pool, Postgres};
use thiserror::Error;
use async_trait::async_trait;
use crate::types::{DeviceState, DeviceStateRow};

#[derive(Error, Debug)]
pub enum DatabaseError {
    #[error("Device not found with ID: {device_id}")]
    DeviceNotFound { device_id: u64 },

    #[error("Database connection error")]
    ConnectionError(#[from] sqlx::Error),

}

// "database" is a serialization/deserialization layer for DeviceState and DeviceConfig objects.
// It will use the sqlx library to save and load objects from PostgreSQL.
#[async_trait]
pub trait Database {
    async fn get_device_state(&self, device_id: u64) -> Result<DeviceState, DatabaseError>;
    async fn add_new_device(&self, device_state: DeviceState) -> Result<(), DatabaseError>;
}


pub struct DBImpl {
    pub pool: Pool<Postgres>
}

impl DBImpl {
    pub async fn new(connstr: &str) -> Result<Self, anyhow::Error> {
        let pool = PgPoolOptions::new().max_connections(5).connect(connstr).await?;

        sqlx::migrate!().run(&pool).await?;

        Ok(DBImpl {
            pool
        })
    }
}


// database URL for testing: psql "postgres://epaper:epaper_password@127.0.0.1/epaper"