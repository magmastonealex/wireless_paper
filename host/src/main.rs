use async_trait::async_trait;
use coap_lite::{RequestType as Method, CoapRequest, ResponseType};
use coap::{server::RequestHandler, Server};
use tokio::runtime::Runtime;
use std::{cell::RefCell, fs, net::SocketAddr, path::{Path, PathBuf}, sync::{Arc, RwLock}, thread, time::{self, Duration}};
use heatshrink::Config;
use anyhow::anyhow;

use crate::{
    business::{BusinessError, BusinessImpl, DeviceHeartbeatRequest},
    database::DBImpl,
    rest_api::{create_router, AppState}
};

mod business;
mod types;
mod database;
mod schema;
mod rest_api;
mod mock_database;

fn do_img() -> Result<Vec<u8>, anyhow::Error> {
    let raw_img = fs::read("img.bin")?;
    let cfg = match Config::new(11, 8) {
        Ok(c) => c,
        Err(reason) => {
            return Err(anyhow!("failed to make config: {}", reason));
        }
    };
    let mut outvec = vec![0u8; raw_img.len()*2];
    let real_out = match heatshrink::encode(&raw_img, &mut outvec, &cfg) {
        Ok(r) => r,
        Err(e) => {
            return Err(anyhow!("failed to encode: {:?}", e));
        }
    };

    Ok(real_out.to_vec())
}

struct CoapHandler {
    business: BusinessImpl
}

const FW_DIRECTORY: &str = "fw/";

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
                    let now = time::SystemTime::now();
                    let r = do_img().unwrap(); //fs::read("comp.bin").unwrap();
                    let took = now.elapsed().unwrap();
                    println!("cpress to {} bytes in {} ms", r.len(), took.as_millis());
                    message.message.payload = r;
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
    async fn handle_heartbeat_request(&self, req: Vec<u8>) -> Result<Vec<u8>, BusinessError> {
        // This should call out to a helper to decode the request, call the relevant business logic function, encode the response, and return it.
        // Do not place any actual business logic here.
        let r: DeviceHeartbeatRequest = match ciborium::from_reader(&req[..]) {
            Ok(r) => r,
            Err(e) => {
                return Err(BusinessError::BadRequest(anyhow!("decoding failed: {:?}", e)))
            }
        };

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