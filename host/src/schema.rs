// @generated automatically by Diesel CLI.

pub mod sql_types {
    #[derive(diesel::sql_types::SqlType)]
    #[diesel(postgres_type(name = "display_type"))]
    pub struct DisplayType;

    #[derive(diesel::sql_types::SqlType)]
    #[diesel(postgres_type(name = "firmware_state"))]
    pub struct FirmwareState;
}

diesel::table! {
    use diesel::sql_types::*;
    use super::sql_types::FirmwareState;
    use super::sql_types::DisplayType;

    device_states (device_id) {
        device_id -> Int8,
        device_friendly_name -> Varchar,
        desired_firmware -> Int4,
        reported_firmware -> Int4,
        firmware_state -> FirmwareState,
        last_heartbeat -> Timestamptz,
        expected_heartbeat -> Timestamptz,
        checkin_interval -> Int4,
        vbat_mv -> Int4,
        image_url -> Nullable<Varchar>,
        display_type -> Nullable<DisplayType>,
    }
}
