use axum::{
    extract::{Path, State},
    http::StatusCode,
    response::Json,
    routing::{delete, get, post, put},
    Router,
};
use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use tower_http::{cors::CorsLayer, services::ServeDir};

use crate::{
    database::{Database, DatabaseError},
    types::{DeviceState, FirmwareState},
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
}

#[derive(Debug, Serialize, Deserialize)]
pub struct UpdateDeviceRequest {
    pub device_friendly_name: Option<String>,
    pub desired_firmware: Option<i32>,
    pub firmware_state: Option<FirmwareState>,
    pub checkin_interval: Option<i32>,
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
        device.desired_firmware = firmware;
    }
    if let Some(state_val) = request.firmware_state {
        device.firmware_state = state_val;
    }
    if let Some(interval) = request.checkin_interval {
        device.checkin_interval = interval;
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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        mock_database::MockDatabase,
        types::FirmwareState,
    };
    use axum::{
        body::Body,
        http::{Request, StatusCode},
    };
    use chrono::Utc;
    use std::sync::Arc;
    use tower::ServiceExt;

    fn create_test_app() -> Router {
        let mock_db = Arc::new(MockDatabase::new());
        let app_state = AppState { db: mock_db };
        create_router(app_state)
    }

    fn create_test_device(device_id: i64) -> DeviceState {
        let now = Utc::now();
        DeviceState {
            device_id,
            device_friendly_name: format!("Test Device {}", device_id),
            desired_firmware: 100,
            reported_firmware: 100, // Will default to same as desired
            firmware_state: FirmwareState::OK, // Will default to OK
            last_heartbeat: now,
            expected_heartbeat: now + chrono::Duration::seconds(90),
            checkin_interval: 60,
        }
    }

    async fn setup_test_device(app: &Router, device_id: i64) -> DeviceState {
        let device = create_test_device(device_id);

        let request_body = CreateDeviceRequest {
            device_id: device.device_id,
            device_friendly_name: device.device_friendly_name.clone(),
            desired_firmware: device.desired_firmware,
            checkin_interval: device.checkin_interval,
        };

        let request = Request::builder()
            .method("POST")
            .uri("/api/devices")
            .header("content-type", "application/json")
            .body(Body::from(serde_json::to_string(&request_body).unwrap()))
            .unwrap();

        let response = app.clone().oneshot(request).await.unwrap();
        assert_eq!(response.status(), StatusCode::CREATED);

        // Return a device state with the expected defaults
        DeviceState {
            device_id: device.device_id,
            device_friendly_name: device.device_friendly_name,
            desired_firmware: device.desired_firmware,
            reported_firmware: device.desired_firmware, // Will be set to same as desired
            firmware_state: FirmwareState::OK, // Will be set to OK
            last_heartbeat: device.last_heartbeat,
            expected_heartbeat: device.expected_heartbeat,
            checkin_interval: device.checkin_interval,
        }
    }

    #[tokio::test]
    async fn test_list_devices_empty() {
        let app = create_test_app();

        let request = Request::builder()
            .method("GET")
            .uri("/api/devices")
            .body(Body::empty())
            .unwrap();

        let response = app.oneshot(request).await.unwrap();
        assert_eq!(response.status(), StatusCode::OK);

        let body = axum::body::to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let devices: Vec<DeviceState> = serde_json::from_slice(&body).unwrap();
        assert!(devices.is_empty());
    }

    #[tokio::test]
    async fn test_create_device() {
        let app = create_test_app();

        let create_request = CreateDeviceRequest {
            device_id: 1001,
            device_friendly_name: "Test E-Paper Display".to_string(),
            desired_firmware: 100,
            checkin_interval: 60,
        };

        let request = Request::builder()
            .method("POST")
            .uri("/api/devices")
            .header("content-type", "application/json")
            .body(Body::from(serde_json::to_string(&create_request).unwrap()))
            .unwrap();

        let response = app.oneshot(request).await.unwrap();
        assert_eq!(response.status(), StatusCode::CREATED);

        let body = axum::body::to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let device: DeviceState = serde_json::from_slice(&body).unwrap();

        assert_eq!(device.device_id, 1001);
        assert_eq!(device.device_friendly_name, "Test E-Paper Display");
        assert_eq!(device.desired_firmware, 100);
        assert_eq!(device.reported_firmware, 100); // Should default to same as desired
        assert_eq!(device.firmware_state, FirmwareState::OK); // Should default to OK
        assert_eq!(device.checkin_interval, 60);
    }

    #[tokio::test]
    async fn test_get_device() {
        let app = create_test_app();
        let device = setup_test_device(&app, 1002).await;

        let request = Request::builder()
            .method("GET")
            .uri("/api/devices/1002")
            .body(Body::empty())
            .unwrap();

        let response = app.oneshot(request).await.unwrap();
        assert_eq!(response.status(), StatusCode::OK);

        let body = axum::body::to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let retrieved_device: DeviceState = serde_json::from_slice(&body).unwrap();

        assert_eq!(retrieved_device.device_id, device.device_id);
        assert_eq!(retrieved_device.device_friendly_name, device.device_friendly_name);
    }

    #[tokio::test]
    async fn test_get_device_not_found() {
        let app = create_test_app();

        let request = Request::builder()
            .method("GET")
            .uri("/api/devices/9999")
            .body(Body::empty())
            .unwrap();

        let response = app.oneshot(request).await.unwrap();
        assert_eq!(response.status(), StatusCode::NOT_FOUND);

        let body = axum::body::to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let error: ApiError = serde_json::from_slice(&body).unwrap();
        assert!(error.error.contains("Device with ID 9999 not found"));
    }

    #[tokio::test]
    async fn test_list_devices_with_data() {
        let app = create_test_app();

        setup_test_device(&app, 1003).await;
        setup_test_device(&app, 1004).await;

        let request = Request::builder()
            .method("GET")
            .uri("/api/devices")
            .body(Body::empty())
            .unwrap();

        let response = app.oneshot(request).await.unwrap();
        assert_eq!(response.status(), StatusCode::OK);

        let body = axum::body::to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let devices: Vec<DeviceState> = serde_json::from_slice(&body).unwrap();
        assert_eq!(devices.len(), 2);

        let device_ids: Vec<i64> = devices.iter().map(|d| d.device_id).collect();
        assert!(device_ids.contains(&1003));
        assert!(device_ids.contains(&1004));
    }

    #[tokio::test]
    async fn test_update_device() {
        let app = create_test_app();
        setup_test_device(&app, 1005).await;

        let update_request = UpdateDeviceRequest {
            device_friendly_name: Some("Updated E-Paper Display".to_string()),
            desired_firmware: Some(110),
            firmware_state: None,
            checkin_interval: None,
        };

        let request = Request::builder()
            .method("PUT")
            .uri("/api/devices/1005")
            .header("content-type", "application/json")
            .body(Body::from(serde_json::to_string(&update_request).unwrap()))
            .unwrap();

        let response = app.oneshot(request).await.unwrap();
        assert_eq!(response.status(), StatusCode::OK);

        let body = axum::body::to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let updated_device: DeviceState = serde_json::from_slice(&body).unwrap();

        assert_eq!(updated_device.device_id, 1005);
        assert_eq!(updated_device.device_friendly_name, "Updated E-Paper Display");
        assert_eq!(updated_device.desired_firmware, 110);
        assert_eq!(updated_device.reported_firmware, 100); // Should remain unchanged
        assert_eq!(updated_device.checkin_interval, 60); // Should remain unchanged
    }

    #[tokio::test]
    async fn test_update_device_not_found() {
        let app = create_test_app();

        let update_request = UpdateDeviceRequest {
            device_friendly_name: Some("Updated Name".to_string()),
            desired_firmware: None,
            firmware_state: None,
            checkin_interval: None,
        };

        let request = Request::builder()
            .method("PUT")
            .uri("/api/devices/9999")
            .header("content-type", "application/json")
            .body(Body::from(serde_json::to_string(&update_request).unwrap()))
            .unwrap();

        let response = app.oneshot(request).await.unwrap();
        assert_eq!(response.status(), StatusCode::NOT_FOUND);

        let body = axum::body::to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let error: ApiError = serde_json::from_slice(&body).unwrap();
        assert!(error.error.contains("Device with ID 9999 not found"));
    }

    #[tokio::test]
    async fn test_delete_device() {
        let app = create_test_app();
        setup_test_device(&app, 1006).await;

        // Verify device exists
        let get_request = Request::builder()
            .method("GET")
            .uri("/api/devices/1006")
            .body(Body::empty())
            .unwrap();

        let response = app.clone().oneshot(get_request).await.unwrap();
        assert_eq!(response.status(), StatusCode::OK);

        // Delete the device
        let delete_request = Request::builder()
            .method("DELETE")
            .uri("/api/devices/1006")
            .body(Body::empty())
            .unwrap();

        let response = app.clone().oneshot(delete_request).await.unwrap();
        assert_eq!(response.status(), StatusCode::NO_CONTENT);

        // Verify device no longer exists
        let get_request = Request::builder()
            .method("GET")
            .uri("/api/devices/1006")
            .body(Body::empty())
            .unwrap();

        let response = app.oneshot(get_request).await.unwrap();
        assert_eq!(response.status(), StatusCode::NOT_FOUND);
    }

    #[tokio::test]
    async fn test_delete_device_not_found() {
        let app = create_test_app();

        let request = Request::builder()
            .method("DELETE")
            .uri("/api/devices/9999")
            .body(Body::empty())
            .unwrap();

        let response = app.oneshot(request).await.unwrap();
        assert_eq!(response.status(), StatusCode::NOT_FOUND);

        let body = axum::body::to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let error: ApiError = serde_json::from_slice(&body).unwrap();
        assert!(error.error.contains("Device with ID 9999 not found"));
    }

    #[tokio::test]
    async fn test_create_device_defaults_and_update_firmware_states() {
        let app = create_test_app();

        // Test that device creation defaults to OK state
        let create_request = CreateDeviceRequest {
            device_id: 2000,
            device_friendly_name: "Test Device Defaults".to_string(),
            desired_firmware: 100,
            checkin_interval: 60,
        };

        let request = Request::builder()
            .method("POST")
            .uri("/api/devices")
            .header("content-type", "application/json")
            .body(Body::from(serde_json::to_string(&create_request).unwrap()))
            .unwrap();

        let response = app.clone().oneshot(request).await.unwrap();
        assert_eq!(response.status(), StatusCode::CREATED);

        let body = axum::body::to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let device: DeviceState = serde_json::from_slice(&body).unwrap();
        assert_eq!(device.firmware_state, FirmwareState::OK);
        assert_eq!(device.reported_firmware, 100); // Same as desired

        // Test that we can update to other firmware states via PUT
        let firmware_states = vec![
            FirmwareState::PENDING,
            FirmwareState::STARTED,
            FirmwareState::FAILED,
        ];

        for state in firmware_states.iter() {
            let update_request = UpdateDeviceRequest {
                device_friendly_name: None,
                desired_firmware: None,
                firmware_state: Some(state.clone()),
                checkin_interval: None,
            };

            let request = Request::builder()
                .method("PUT")
                .uri("/api/devices/2000")
                .header("content-type", "application/json")
                .body(Body::from(serde_json::to_string(&update_request).unwrap()))
                .unwrap();

            let response = app.clone().oneshot(request).await.unwrap();
            assert_eq!(response.status(), StatusCode::OK);

            let body = axum::body::to_bytes(response.into_body(), usize::MAX).await.unwrap();
            let device: DeviceState = serde_json::from_slice(&body).unwrap();
            assert_eq!(device.firmware_state, *state);
        }
    }

    #[tokio::test]
    async fn test_partial_update_device() {
        let app = create_test_app();
        setup_test_device(&app, 1007).await;

        // Test updating only the friendly name
        let update_request = UpdateDeviceRequest {
            device_friendly_name: Some("Only Name Changed".to_string()),
            desired_firmware: None,
            firmware_state: None,
            checkin_interval: None,
        };

        let request = Request::builder()
            .method("PUT")
            .uri("/api/devices/1007")
            .header("content-type", "application/json")
            .body(Body::from(serde_json::to_string(&update_request).unwrap()))
            .unwrap();

        let response = app.oneshot(request).await.unwrap();
        assert_eq!(response.status(), StatusCode::OK);

        let body = axum::body::to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let updated_device: DeviceState = serde_json::from_slice(&body).unwrap();

        assert_eq!(updated_device.device_friendly_name, "Only Name Changed");
        assert_eq!(updated_device.desired_firmware, 100); // Should remain unchanged
        assert_eq!(updated_device.reported_firmware, 100); // Should remain unchanged
        assert_eq!(updated_device.checkin_interval, 60); // Should remain unchanged
    }
}