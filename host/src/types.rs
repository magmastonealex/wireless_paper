use chrono::{DateTime, Utc};
use diesel::{Queryable, Insertable, AsChangeset, Identifiable};
use diesel::pg::{Pg, PgValue};
use diesel::serialize::{ToSql, Output};
use diesel::deserialize::{FromSql, Result as DeserializeResult};
use serde::{Serialize, Deserialize};
use crate::schema::{device_states, sql_types::FirmwareState as FirmwareStateSqlType};

#[derive(Debug, PartialEq, Eq, Clone, Serialize, Deserialize)]
#[derive(diesel::expression::AsExpression, diesel::deserialize::FromSqlRow)]
#[diesel(sql_type = FirmwareStateSqlType)]
pub enum FirmwareState {
    OK, // desired firmware == reported firmware
    PENDING, // The desired firmware version has been changed, but device has not yet heartbeated to pick it up.
    STARTED, // Device has been sent a heartbeat response with new firmware in it
    FAILED // The device sent a new heartbeat message when already in STARTED state with a different firmware version from desired.
}

impl ToSql<FirmwareStateSqlType, Pg> for FirmwareState {
    fn to_sql<'b>(&'b self, out: &mut Output<'b, '_, Pg>) -> diesel::serialize::Result {
        let value = match self {
            FirmwareState::OK => "OK",
            FirmwareState::PENDING => "PENDING",
            FirmwareState::STARTED => "STARTED",
            FirmwareState::FAILED => "FAILED",
        };
        ToSql::<diesel::sql_types::Text, Pg>::to_sql(value, &mut out.reborrow())
    }
}

impl FromSql<FirmwareStateSqlType, Pg> for FirmwareState {
    fn from_sql(value: PgValue) -> DeserializeResult<Self> {
        let text = <String as FromSql<diesel::sql_types::Text, Pg>>::from_sql(value)?;
        match text.as_str() {
            "OK" => Ok(FirmwareState::OK),
            "PENDING" => Ok(FirmwareState::PENDING),
            "STARTED" => Ok(FirmwareState::STARTED),
            "FAILED" => Ok(FirmwareState::FAILED),
            _ => Err(format!("Unknown FirmwareState value: {}", text).into()),
        }
    }
}

#[derive(Debug, PartialEq, Eq, Clone, Queryable, Insertable, AsChangeset, Identifiable, Serialize, Deserialize)]
#[diesel(table_name = device_states)]
#[diesel(primary_key(device_id))]
pub struct DeviceState {
    pub device_id: i64, // A unique identifier for this device. This is a good choice for primary key.
    pub device_friendly_name: String,
    pub desired_firmware: i32, // The firmware we wish for the device to run.
    pub reported_firmware: i32, // The firmware version most recently reported by the device in it's heartbeat.
    pub firmware_state: FirmwareState, // The state of any in-progress firmware upgrades.
    pub last_heartbeat: DateTime<Utc>, // The last time the device made a heartbeat request.
    pub expected_heartbeat: DateTime<Utc>, // The next time we expect the device to make a heartbeat (This is after the checkin_interval in the device's config normally, but may be shorter if a firmware update has been started.)
    pub checkin_interval: i32, // How often the device should wake up to heartbeat and refresh the display.
    pub vbat_mv: i32
}