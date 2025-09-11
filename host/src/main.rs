use coap_lite::{RequestType as Method, CoapRequest, ResponseType};
use coap::Server;
use tokio::runtime::Runtime;
use std::{fs, net::SocketAddr, thread, time::{self, Duration}};
use heatshrink::Config;
use anyhow::anyhow;

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

fn main() {
    let addr = "[::]:5683";
	Runtime::new().unwrap().block_on(async move {
        let server = Server::new_udp(addr).unwrap();
        println!("Server up on {}", addr);

        server.run(|mut request: Box<CoapRequest<SocketAddr>>| async {
            match request.get_method() {
                &Method::Get => println!("request by get {}", request.get_path()),
                &Method::Post => println!("request by post {}", String::from_utf8(request.message.payload.clone()).unwrap()),
                &Method::Put => println!("request by put {}", String::from_utf8(request.message.payload.clone()).unwrap()),
                _ => println!("request by other method"),
            };

            for (oid, data) in request.message.options() {
                println!("Got option: {}, data: {:#?}", oid, data);
            }

            let path = request.get_path();
            match request.response {
                Some(ref mut message) => {
                    if path == "fw/00000001" {
                        message.message.payload = fs::read("test.bin").unwrap();
                    } else if path == "img" {
                        println!("here");
                        let now = time::SystemTime::now();
                        let r = do_img().unwrap(); //fs::read("comp.bin").unwrap();
                        let took = now.elapsed().unwrap();
                        println!("cpress to {} bytes in {} ms", r.len(), took.as_millis());
                        message.message.payload = r;
                    } else {
                        message.message.payload = b"OK".to_vec();
                    }
                },
                _ => {
                    println!("Non-confirmable reqponse!?");
                }
            };

            return request
        }).await.unwrap();
    });
}