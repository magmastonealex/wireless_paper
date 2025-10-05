use async_trait::async_trait;
use coap_lite::{RequestType as Method, CoapRequest, ResponseType};
use coap::{server::RequestHandler, Server};
use tokio::runtime::Runtime;
use std::{fs, net::SocketAddr, path::PathBuf, sync::Arc, time::{self}};
use heatshrink::Config;
use anyhow::anyhow;

use crate::{
    business::{BusinessError, BusinessImpl, DeviceHeartbeatRequest, DeviceImageRequest}, database::{DBImpl, Database}, image_fetcher::ImageFetcher, rest_api::{create_router, AppState}
};

mod business;
mod types;
mod database;
mod schema;
mod rest_api;
mod image_fetcher;

#[cfg(test)]
mod mock_database;

fn ensure_image_directory() -> Result<(), anyhow::Error> {
    fs::create_dir_all(IMAGE_FILES_DIR)
        .map_err(|e| anyhow!("Failed to create image directory: {}", e))?;
    Ok(())
}

async fn fetch_and_compress_images_for_all_devices(db: Arc<dyn Database + Send + Sync>) -> Result<(), anyhow::Error> {
    println!("Fetching images for all devices...");
    let fetcher = ImageFetcher::new();

    let devices = db.list_all_devices().await
        .map_err(|e| anyhow!("Failed to list devices: {}", e))?;

    let mut success_count = 0;
    let mut skip_count = 0;
    let mut error_count = 0;

    for device in devices {
        // Skip devices without both image_url and display_type set
        let (image_url, display_type) = match (&device.image_url, &device.display_type) {
            (Some(url), Some(dtype)) => (url, dtype),
            _ => {
                skip_count += 1;
                continue;
            }
        };

        println!("Fetching image for device {} from: {}", device.device_id, image_url);

        // Fetch and convert image for this display type
        let raw_img = match fetcher.fetch_and_convert_for_display(image_url, display_type).await {
            Ok(img) => img,
            Err(e) => {
                eprintln!("Failed to fetch image for device {}: {}", device.device_id, e);
                error_count += 1;
                continue;
            }
        };

        println!("Fetched and converted image for device {}: {} bytes", device.device_id, raw_img.len());

        // Compress the image
        let cfg = Config::new(11, 8)
            .map_err(|e| anyhow!("Failed to create heatshrink config: {}", e))?;

        let mut outvec = vec![0u8; raw_img.len() * 2];
        let compressed = match heatshrink::encode(&raw_img, &mut outvec, &cfg) {
            Ok(c) => c,
            Err(e) => {
                eprintln!("Failed to compress image for device {}: {:?}", device.device_id, e);
                error_count += 1;
                continue;
            }
        };

        // Save to device-specific file
        let file_path = format!("{}/{}.bin", IMAGE_FILES_DIR, device.device_id);
        match fs::write(&file_path, compressed) {
            Ok(_) => {
                println!("Compressed image saved to: {} ({} bytes)", file_path, compressed.len());
                success_count += 1;
            }
            Err(e) => {
                eprintln!("Failed to write image for device {}: {}", device.device_id, e);
                error_count += 1;
            }
        }
    }

    println!("Image fetch complete: {} succeeded, {} skipped (no URL/type), {} errors",
             success_count, skip_count, error_count);
    Ok(())
}


struct CoapHandler {
    business: BusinessImpl
}

const FW_DIRECTORY: &str = "fw/";
const IMAGE_FILES_DIR: &str = "image_files";

#[async_trait]
impl RequestHandler for CoapHandler {
    async fn handle_request(
        &self,
        mut request: Box<CoapRequest<SocketAddr>>,
    ) -> Box<CoapRequest<SocketAddr>>{
        // Log data about the request we got.
        match request.get_method() {
            &Method::Get => println!("request by get {}", request.get_path()),
            &Method::Post => println!("request by post {} ({} bytes)", request.get_path(), request.message.payload.len()),
            &Method::Put => println!("request by put {} ({} bytes)", request.get_path(), request.message.payload.len()),
            _ => println!("request by other method"),
        };

        // Check for options of interest.
        for (oid, data) in request.message.options() {
            println!("Got option: {}, data: {:#?}", oid, data);
        }

        // mux the request based on path, calling out to other methods.
        let path = request.get_path();
        match request.response {
            Some(ref mut message) => {
                if path == "hb" {
                    // Heartbeat request was received. We'll extract device_id from the payload.
                    let resp = self.handle_heartbeat_request(request.message.payload.clone()).await;
                    match resp {
                        Ok(body) => {
                            println!("Responding OK with {} bytes", body.len());
                            message.message.payload = body;
                        },
                        Err(BusinessError::BadRequest(e)) => {
                            println!("Bad request: {:?} ", e);
                            message.set_status(ResponseType::BadRequest);
                        },
                        Err(BusinessError::InternalError(e)) => {
                            println!("Internal Error: {:?} ", e);
                            message.set_status(ResponseType::InternalServerError);
                        }
                    }
                } else if path.starts_with("fw/") {
                    let resp = self.handle_firmware_request(&path).await;
                    match resp {
                        Ok(body) => {
                            println!("Responding OK with {} bytes", body.len());
                            message.message.payload = body;
                        },
                        Err(BusinessError::BadRequest(e)) => {
                            println!("Bad request: {:?} ", e);
                            message.set_status(ResponseType::BadRequest);
                        },
                        Err(BusinessError::InternalError(e)) => {
                            println!("Internal Error: {:?} ", e);
                            message.set_status(ResponseType::InternalServerError);
                        }
                    }
                } else if path == "img" {
                    let resp = self.handle_image_request(request.message.payload.clone()).await;
                    match resp {
                        Ok(body) => {
                            println!("Responding OK with {} bytes", body.len());
                            message.message.payload = body;
                        },
                        Err(BusinessError::BadRequest(e)) => {
                            println!("Bad request: {:?} ", e);
                            message.set_status(ResponseType::BadRequest);
                        },
                        Err(BusinessError::InternalError(e)) => {
                            println!("Internal Error: {:?} ", e);
                            message.set_status(ResponseType::InternalServerError);
                        }
                    }
                } else {
                    message.set_status(ResponseType::NotFound);
                }
            },
            _ => {
                println!("Non-confirmable reqponse!?");
            }
        };
        return request
    
    }
}

impl CoapHandler {
    async fn handle_image_request(&self, req: Vec<u8>) -> Result<Vec<u8>, BusinessError> {
        let r: DeviceImageRequest = match ciborium::from_reader(&req[..]) {
            Ok(r) => r,
            Err(e) => {
                println!("No req, or decoding failed: {:?}", e);
                return Err(BusinessError::BadRequest(anyhow!("decoding failed: {:?}", e)))
            }
        };

        println!("Got image request from device: {:#?}", r);

        // Construct device-specific image file path
        let file_path = format!("{}/{}.bin", IMAGE_FILES_DIR, r.device_id);

        // Read and return the device-specific compressed image
        let compressed_img = fs::read(&file_path).map_err(|e| {
            eprintln!("Failed to read image file for device {}: {}", r.device_id, e);
            BusinessError::InternalError(anyhow!("Image file not found for device {}: {}", r.device_id, e))
        })?;

        println!("Serving image file {} ({} bytes) to device {}", file_path, compressed_img.len(), r.device_id);
        Ok(compressed_img)
    }

    async fn handle_firmware_request(&self, urlpath: &str) -> Result<Vec<u8>, BusinessError> {
        let binpath = match urlpath.split_once("fw/") {
            Some((_, fwver)) => {
                if fwver.contains("/") || fwver.contains("..") {
                    return Err(BusinessError::BadRequest(anyhow!("path contains forbidden characters")));
                }
                println!("Got request for firmware version: {:?}", fwver);
                let mut fwpath = PathBuf::from(FW_DIRECTORY);
                fwpath.push(fwver);

                fwpath
            }
            None => {
                return Err(BusinessError::BadRequest(anyhow!("path contains forbidden characters")));
            }
        };

        println!("Sending {:?}", binpath);

        match fs::read(binpath) {
            Ok(b) => Ok(b),
            Err(e) => {
                Err(BusinessError::InternalError(e.into()))
            }
        }
    }

    // Handle a heartbeat request.
    // req is the CBOR-encoded payload, which should match a DeviceHeartbeatRequest structure once decoded.
    // The return value is a Result, which in the OK case contains a CBOR-encoded equivalent to the DeviceHeartbeatResponse structure. In the failure case, a BusinessError can be returned which will result in a non-200 status code sent to the client.
    // In the future, I want to rework this to not use CBOR. It's "fine" on the rust/python side, but the C libraries for encode/decode are rather painful.
    async fn handle_heartbeat_request(&self, req: Vec<u8>) -> Result<Vec<u8>, BusinessError> {
        // This should call out to a helper to decode the request, call the relevant business logic function, encode the response, and return it.
        // Do not place any actual business logic here.
        let r: DeviceHeartbeatRequest = match ciborium::from_reader(&req[..]) {
            Ok(r) => r,
            Err(e) => {
                return Err(BusinessError::BadRequest(anyhow!("decoding failed: {:?}", e)))
            }
        };

        println!("Got heartbeat request: {:#?}", r);

        match self.business.handle_heartbeat(r).await {
            Ok(resp) => {
                let mut buf = Vec::new(); // at some point consider sharing a buffer or something to avoid all these small allocs.
                match ciborium::into_writer(&resp, &mut buf) {
                    Ok(_) => {
                        Ok(buf)
                    },
                    Err(e) => {
                        Err(BusinessError::InternalError(anyhow!("encoding failed: {:?}", e)))
                    }
                }
            },
            Err(e) => Err(e)
        }
    }
}

fn main() {
    let coap_addr = "[::]:5683";
    let http_addr = "0.0.0.0:8080";

    Runtime::new().unwrap().block_on(async move {
        // Create shared database instance
        let db_conn_str = "postgres://epaper:epaper_password@127.0.0.1/epaper";
        let shared_db = Arc::new(DBImpl::new(db_conn_str).await.unwrap());

        // Ensure image directory exists
        if let Err(e) = ensure_image_directory() {
            eprintln!("Failed to create image directory: {}", e);
        }

        // Fetch initial images for all devices
        if let Err(e) = fetch_and_compress_images_for_all_devices(shared_db.clone()).await {
            eprintln!("Failed to fetch initial images: {}", e);
        }

        // Start periodic image fetching task
        let db_for_task = shared_db.clone();
        tokio::spawn(async move {
            let mut interval = tokio::time::interval(tokio::time::Duration::from_secs(30 * 60)); // 30 minutes
            interval.tick().await; // Skip first tick (we already fetched at startup)

            loop {
                interval.tick().await;
                if let Err(e) = fetch_and_compress_images_for_all_devices(db_for_task.clone()).await {
                    eprintln!("Failed to fetch periodic images: {}", e);
                }
            }
        });

        // Create CoAP server
        let coap_server = Server::new_udp(coap_addr).unwrap();
        println!("CoAP server up on {}", coap_addr);
        let coap_handler = CoapHandler {
            business: BusinessImpl {
                db: shared_db.clone(),
            }
        };

        // Create HTTP server
        let app_state = AppState { db: shared_db };
        let app = create_router(app_state);
        let listener = tokio::net::TcpListener::bind(http_addr).await.unwrap();
        println!("HTTP server up on {}", http_addr);

        // Run both servers concurrently
        tokio::select! {
            result = coap_server.run(coap_handler) => {
                if let Err(e) = result {
                    eprintln!("CoAP server error: {}", e);
                }
            }
            result = axum::serve(listener, app) => {
                if let Err(e) = result {
                    eprintln!("HTTP server error: {}", e);
                }
            }
        }
    });
}