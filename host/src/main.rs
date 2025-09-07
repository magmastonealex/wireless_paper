use coap_lite::{RequestType as Method, CoapRequest, CoapOption};
use coap::Server;
use tokio::runtime::Runtime;
use std::{fs, net::SocketAddr};


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