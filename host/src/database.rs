use thiserror::Error;
use diesel::prelude::*;
use diesel::pg::PgConnection;
use diesel_migrations::{embed_migrations, EmbeddedMigrations, MigrationHarness};
use async_trait::async_trait;
use crate::types::DeviceState;
use crate::schema::device_states;

pub const MIGRATIONS: EmbeddedMigrations = embed_migrations!();

#[derive(Error, Debug)]
pub enum DatabaseError {
    #[error("Device not found with ID: {device_id}")]
    DeviceNotFound { device_id: u64 },
    #[error("Database connection error: {0}")]
    ConnectionError(#[from] diesel::ConnectionError),
    #[error("Database query error: {0}")]
    QueryError(#[from] diesel::result::Error),
    #[error("Migration error: {0}")]
    MigrationError(#[from] Box<dyn std::error::Error + Send + Sync>),
}

#[async_trait]
pub trait Database: Send + Sync {
    async fn get_device_state(&self, device_id: u64) -> Result<DeviceState, DatabaseError>;
    async fn save_device_state(&self, device_state: &DeviceState) -> Result<(), DatabaseError>;
    async fn update_device_state(&self, device_state: &DeviceState) -> Result<(), DatabaseError>;
    async fn create_device_state(&self, device_state: &DeviceState) -> Result<(), DatabaseError>;
}

pub struct DBImpl {
    connection_string: String,
}

impl DBImpl {
    pub async fn new(connstr: &str) -> Result<Self, anyhow::Error> {
        let mut conn = PgConnection::establish(connstr)
            .map_err(|e| anyhow::anyhow!("Failed to connect to database: {}", e))?;

        println!("Running embedded migrations...");
        conn.run_pending_migrations(MIGRATIONS)
            .map_err(|e| anyhow::anyhow!("Failed to run migrations: {}", e))?;
        println!("Migrations completed successfully");

        let _ = diesel::sql_query("SELECT 1")
            .execute(&mut conn)
            .map_err(|e| anyhow::anyhow!("Database connection test failed: {}", e))?;

        Ok(Self {
            connection_string: connstr.to_string(),
        })
    }

    fn get_connection(&self) -> Result<PgConnection, DatabaseError> {
        PgConnection::establish(&self.connection_string)
            .map_err(DatabaseError::ConnectionError)
    }
}

#[async_trait]
impl Database for DBImpl {
    async fn get_device_state(&self, device_id: u64) -> Result<DeviceState, DatabaseError> {
        let mut conn = self.get_connection()?;

        device_states::table
            .filter(device_states::device_id.eq(device_id as i64))
            .first::<DeviceState>(&mut conn)
            .map_err(|e| match e {
                diesel::result::Error::NotFound => DatabaseError::DeviceNotFound { device_id },
                _ => DatabaseError::QueryError(e),
            })
    }

    async fn save_device_state(&self, device_state: &DeviceState) -> Result<(), DatabaseError> {
        let mut conn = self.get_connection()?;

        diesel::insert_into(device_states::table)
            .values(device_state)
            .on_conflict(device_states::device_id)
            .do_update()
            .set(device_state)
            .execute(&mut conn)
            .map_err(DatabaseError::QueryError)?;

        Ok(())
    }

    async fn update_device_state(&self, device_state: &DeviceState) -> Result<(), DatabaseError> {
        let mut conn = self.get_connection()?;

        let updated_device = device_state
            .save_changes::<DeviceState>(&mut conn)
            .map_err(|e| match e {
                diesel::result::Error::NotFound => DatabaseError::DeviceNotFound {
                    device_id: device_state.device_id as u64
                },
                _ => DatabaseError::QueryError(e),
            })?;

        // The updated_device contains the result, but we don't need to return it for this method
        let _ = updated_device;
        Ok(())
    }

    async fn create_device_state(&self, device_state: &DeviceState) -> Result<(), DatabaseError> {
        let mut conn = self.get_connection()?;

        diesel::insert_into(device_states::table)
            .values(device_state)
            .execute(&mut conn)
            .map_err(DatabaseError::QueryError)?;

        Ok(())
    }
}

//cargo install diesel_cli --no-default-features --features postgres,postgres-bundled
// database URL for testing: psql "postgres://epaper:epaper_password@127.0.0.1/epaper"