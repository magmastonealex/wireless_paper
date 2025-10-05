use axum::{
    extract::{Path, State},
    http::StatusCode,
    response::Json,
    routing::{delete, get, post, put},
    Router,
};
use chrono::Utc;
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use tower_http::{cors::CorsLayer, services::ServeDir};

use crate::{
    database::{Database, DatabaseError},
    types::{DeviceState, FirmwareState, DisplayType, Rotation},
};

#[derive(Debug, Clone)]
pub struct AppState {
    pub db: Arc<dyn Database + Send + Sync>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct CreateDeviceRequest {
    pub device_id: i64,
    pub device_friendly_name: String,
    pub desired_firmware: i32,
    pub checkin_interval: i32,
    pub image_url: Option<String>,
    pub display_type: Option<DisplayType>,
    pub rotation: Option<Rotation>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct UpdateDeviceRequest {
    pub device_friendly_name: Option<String>,
    pub desired_firmware: Option<i32>,
    pub firmware_state: Option<FirmwareState>,
    pub checkin_interval: Option<i32>,
    pub image_url: Option<String>,
    pub display_type: Option<DisplayType>,
    pub rotation: Option<Rotation>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct ApiError {
    pub error: String,
}

impl From<DatabaseError> for (StatusCode, Json<ApiError>) {
    fn from(err: DatabaseError) -> Self {
        let (status, message) = match err {
            DatabaseError::DeviceNotFound { device_id } => (
                StatusCode::NOT_FOUND,
                format!("Device with ID {} not found", device_id),
            ),
            DatabaseError::ConnectionError(_) => (
                StatusCode::INTERNAL_SERVER_ERROR,
                "Database connection error".to_string(),
            ),
            DatabaseError::QueryError(_) => (
                StatusCode::INTERNAL_SERVER_ERROR,
                "Database query error".to_string(),
            ),
            DatabaseError::MigrationError(_) => (
                StatusCode::INTERNAL_SERVER_ERROR,
                "Database migration error".to_string(),
            ),
        };

        (
            status,
            Json(ApiError { error: message }),
        )
    }
}

pub fn create_router(state: AppState) -> Router {
    Router::new()
        // API routes
        .route("/api/devices", get(list_devices))
        .route("/api/devices", post(create_device))
        .route("/api/devices/:id", get(get_device))
        .route("/api/devices/:id", put(update_device))
        .route("/api/devices/:id", delete(delete_device))
        // Static file serving
        .nest_service("/", ServeDir::new("web"))
        .layer(CorsLayer::permissive())
        .with_state(state)
}

async fn list_devices(
    State(state): State<AppState>,
) -> Result<Json<Vec<DeviceState>>, (StatusCode, Json<ApiError>)> {
    let devices = state.db.list_all_devices().await?;
    Ok(Json(devices))
}

async fn get_device(
    State(state): State<AppState>,
    Path(device_id): Path<u64>,
) -> Result<Json<DeviceState>, (StatusCode, Json<ApiError>)> {
    let device = state.db.get_device_state(device_id).await?;
    Ok(Json(device))
}

async fn create_device(
    State(state): State<AppState>,
    Json(request): Json<CreateDeviceRequest>,
) -> Result<(StatusCode, Json<DeviceState>), (StatusCode, Json<ApiError>)> {
    let now = Utc::now();
    let device_state = DeviceState {
        device_id: request.device_id,
        device_friendly_name: request.device_friendly_name,
        desired_firmware: request.desired_firmware,
        reported_firmware: request.desired_firmware, // Default to same as desired
        firmware_state: FirmwareState::OK, // Default to OK state
        last_heartbeat: now,
        expected_heartbeat: now + chrono::Duration::seconds(request.checkin_interval as i64 + 30),
        checkin_interval: request.checkin_interval,
        vbat_mv: 1500,
        image_url: request.image_url,
        display_type: request.display_type,
        rotation: request.rotation.unwrap_or(Rotation::ROTATE_0),
    };

    state.db.create_device_state(&device_state).await?;
    Ok((StatusCode::CREATED, Json(device_state)))
}

async fn update_device(
    State(state): State<AppState>,
    Path(device_id): Path<u64>,
    Json(request): Json<UpdateDeviceRequest>,
) -> Result<Json<DeviceState>, (StatusCode, Json<ApiError>)> {
    let mut device = state.db.get_device_state(device_id).await?;

    if let Some(name) = request.device_friendly_name {
        device.device_friendly_name = name;
    }
    if let Some(firmware) = request.desired_firmware {
        if device.reported_firmware != firmware {
            device.firmware_state = FirmwareState::PENDING;
        } else {
            device.firmware_state = FirmwareState::OK;
        }
        device.desired_firmware = firmware;
    }

    if let Some(interval) = request.checkin_interval {
        device.checkin_interval = interval;
    }

    if request.image_url.is_some() {
        device.image_url = request.image_url;
    }

    if request.display_type.is_some() {
        device.display_type = request.display_type;
    }

    if let Some(rotation) = request.rotation {
        device.rotation = rotation;
    }

    state.db.update_device_state(&device).await?;
    Ok(Json(device))
}

async fn delete_device(
    State(state): State<AppState>,
    Path(device_id): Path<u64>,
) -> Result<StatusCode, (StatusCode, Json<ApiError>)> {
    state.db.delete_device_state(device_id).await?;
    Ok(StatusCode::NO_CONTENT)
}