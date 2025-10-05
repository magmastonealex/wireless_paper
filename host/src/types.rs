use chrono::{DateTime, Utc};
use diesel::{Queryable, Insertable, AsChangeset, Identifiable};
use diesel::pg::{Pg, PgValue};
use diesel::serialize::{ToSql, Output};
use diesel::deserialize::{FromSql, Result as DeserializeResult};
use serde::{Serialize, Deserialize};
use crate::schema::{device_states, sql_types::{FirmwareState as FirmwareStateSqlType, DisplayType as DisplayTypeSqlType}};

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

#[derive(Debug, PartialEq, Eq, Clone, Serialize, Deserialize)]
#[derive(diesel::expression::AsExpression, diesel::deserialize::FromSqlRow)]
#[diesel(sql_type = DisplayTypeSqlType)]
pub enum DisplayType {
    #[serde(rename = "EPD_TYPE_GDEY029T71H")]
    EPD_TYPE_GDEY029T71H, // 2.9 b/w (not working at the moment)
    #[serde(rename = "EPD_TYPE_GDEM035F51")]
    EPD_TYPE_GDEM035F51,  // 3.5 4-color
    #[serde(rename = "EPD_TYPE_GDEY029F51")]
    EPD_TYPE_GDEY029F51,  // 2.9 4-color
    #[serde(rename = "EPD_TYPE_GDEM075F52")]
    EPD_TYPE_GDEM075F52,  // 7.5 4-color
    #[serde(rename = "EPD_TYPE_WS_75_V2B")]
    EPD_TYPE_WS_75_V2B,   // 7.5 2-color + red
}

#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum PixelFormat {
    Rykw2Bit,
    Kw1Bit,
}

impl DisplayType {
    pub fn get_display_dimensions(&self) -> (u32, u32) {
        match self {
            DisplayType::EPD_TYPE_GDEY029T71H => (384, 168),  // 2.9" B/W
            DisplayType::EPD_TYPE_GDEM035F51 => (184, 384),   // 3.5" 4-color
            DisplayType::EPD_TYPE_GDEY029F51 => (384, 168),   // 2.9" 4-color
            DisplayType::EPD_TYPE_GDEM075F52 => (800, 480),   // 7.5" 4-color
            DisplayType::EPD_TYPE_WS_75_V2B => (800, 480),    // 7.5" 2-color + red
        }
    }
    pub fn get_pixel_format(&self) -> PixelFormat {
        match self {
            DisplayType::EPD_TYPE_GDEY029T71H => PixelFormat::Kw1Bit,  // 2.9" B/W
            DisplayType::EPD_TYPE_GDEM035F51 => PixelFormat::Rykw2Bit,   // 3.5" 4-color
            DisplayType::EPD_TYPE_GDEY029F51 => PixelFormat::Rykw2Bit,   // 2.9" 4-color
            DisplayType::EPD_TYPE_GDEM075F52 => PixelFormat::Rykw2Bit,   // 7.5" 4-color
            DisplayType::EPD_TYPE_WS_75_V2B => PixelFormat::Kw1Bit,    // 7.5" 2-color + red
        }
    }
}

impl ToSql<DisplayTypeSqlType, Pg> for DisplayType {
    fn to_sql<'b>(&'b self, out: &mut Output<'b, '_, Pg>) -> diesel::serialize::Result {
        let value = match self {
            DisplayType::EPD_TYPE_GDEY029T71H => "EPD_TYPE_GDEY029T71H",
            DisplayType::EPD_TYPE_GDEM035F51 => "EPD_TYPE_GDEM035F51",
            DisplayType::EPD_TYPE_GDEY029F51 => "EPD_TYPE_GDEY029F51",
            DisplayType::EPD_TYPE_GDEM075F52 => "EPD_TYPE_GDEM075F52",
            DisplayType::EPD_TYPE_WS_75_V2B => "EPD_TYPE_WS_75_V2B",
        };
        ToSql::<diesel::sql_types::Text, Pg>::to_sql(value, &mut out.reborrow())
    }
}

impl FromSql<DisplayTypeSqlType, Pg> for DisplayType {
    fn from_sql(value: PgValue) -> DeserializeResult<Self> {
        let text = <String as FromSql<diesel::sql_types::Text, Pg>>::from_sql(value)?;
        match text.as_str() {
            "EPD_TYPE_GDEY029T71H" => Ok(DisplayType::EPD_TYPE_GDEY029T71H),
            "EPD_TYPE_GDEM035F51" => Ok(DisplayType::EPD_TYPE_GDEM035F51),
            "EPD_TYPE_GDEY029F51" => Ok(DisplayType::EPD_TYPE_GDEY029F51),
            "EPD_TYPE_GDEM075F52" => Ok(DisplayType::EPD_TYPE_GDEM075F52),
            "EPD_TYPE_WS_75_V2B" => Ok(DisplayType::EPD_TYPE_WS_75_V2B),
            _ => Err(format!("Unknown DisplayType value: {}", text).into()),
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
    pub vbat_mv: i32,
    pub image_url: Option<String>, // URL from which to fetch the image for this device
    pub display_type: Option<DisplayType>, // The type of e-paper display attached to this device
}