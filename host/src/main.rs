use async_trait::async_trait;
use coap_lite::{RequestType as Method, CoapRequest, ResponseType};
use coap::{server::RequestHandler, Server};
use tokio::runtime::Runtime;
use std::{cell::RefCell, fs, net::SocketAddr, sync::{Arc, RwLock}, thread, time::{self, Duration}};
use heatshrink::Config;
use anyhow::anyhow;

use crate::business::{BusinessError, BusinessImpl, DeviceHeartbeatRequest};

mod business;
mod types;
mod database;

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

#[async_trait]
impl RequestHandler for CoapHandler {
    async fn handle_request(
        &self,
        mut request: Box<CoapRequest<SocketAddr>>,
    ) -> Box<CoapRequest<SocketAddr>>{
        // Log data about the request we got.
        match request.get_method() {
            &Method::Get => println!("request by get {}", request.get_path()),
            &Method::Post => println!("request by post {}", String::from_utf8(request.message.payload.clone()).unwrap()),
            &Method::Put => println!("request by put {}", String::from_utf8(request.message.payload.clone()).unwrap()),
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
                    // Heartbeat request was received.
                    let resp = self.handle_heartbeat_request(123u64, request.message.payload.clone()).await;
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

    // Handle a heartbeat request.
    // device_id is the unique identifier for the device.
    // req is the CBOR-encoded payload, which should match a DeviceHeartbeatRequest structure once decoded.
    // The return value is a Result, which in the OK case contains a CBOR-encoded equivalent to the DeviceHeartbeatResponse structure. In the failure case, a BusinessError can be returned which will result in a non-200 status code sent to the client.
    async fn handle_heartbeat_request(&self, _device_id: u64, req: Vec<u8>) -> Result<Vec<u8>, BusinessError> {
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
    let addr = "[::]:5683";

	Runtime::new().unwrap().block_on(async move {
        let server = Server::new_udp(addr).unwrap();
        println!("Server up on {}", addr);
        let handler  = CoapHandler{
            business: BusinessImpl { }
        };
        server.run(handler).await.unwrap();
    });
}