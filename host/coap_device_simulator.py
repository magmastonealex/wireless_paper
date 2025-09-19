#!/usr/bin/env python3
"""
CoAP Device Simulator for E-Paper IoT Server

This script simulates an e-paper display device that sends a heartbeat message
to the IoT management server via CoAP protocol.

Usage:
    python3 coap_device_simulator.py <host> <port> [device_id] [current_firmware]

Example:
    python3 coap_device_simulator.py localhost 5683
    python3 coap_device_simulator.py 127.0.0.1 5683 1001 100
"""

import asyncio
import sys
import cbor2
from aiocoap import Context, Message, POST
from aiocoap.numbers.codes import Code


async def send_heartbeat(host, port, device_id=1001, current_firmware=100):
    """Send a single heartbeat request to the server"""
    # Create the heartbeat request matching DeviceHeartbeatRequest in Rust
    heartbeat_request = {
        "device_id": device_id,
        "current_firmware": current_firmware,
        "protocol_version": 1
    }

    # Encode the request as CBOR
    payload = cbor2.dumps(heartbeat_request)

    # Create CoAP context
    context = await Context.create_client_context()

    try:
        # Create CoAP message
        request = Message(
            code=POST,
            payload=payload,
            uri=f"coap://{host}:{port}/hb"
        )

        print(f"Sending heartbeat for device {device_id} with firmware {current_firmware}")
        print(f"Payload: {heartbeat_request}")

        # Send the request
        response = await context.request(request).response

        print(f"Response code: {response.code}")

        if response.code == Code.CONTENT:
            # Decode the CBOR response
            response_data = cbor2.loads(response.payload)
            print(f"Response payload: {response_data}")

            # Extract response fields matching DeviceHeartbeatResponse
            desired_firmware = response_data.get("desired_firmware")
            checkin_interval = response_data.get("checkin_interval")

            print(f"Server wants firmware version: {desired_firmware}")
            print(f"Next checkin in: {checkin_interval} seconds")

            return response_data
        else:
            print(f"Error response: {response.code}")
            if response.payload:
                print(f"Error payload: {response.payload.decode('utf-8', errors='ignore')}")
            return None

    except Exception as e:
        print(f"Error sending heartbeat: {e}")
        return None

    finally:
        await context.shutdown()


async def main():
    if len(sys.argv) < 3:
        print("Usage: python3 coap_device_simulator.py <host> <port> [device_id] [current_firmware]")
        print("Example: python3 coap_device_simulator.py localhost 5683 1001 100")
        sys.exit(1)

    host = sys.argv[1]
    port = int(sys.argv[2])
    device_id = int(sys.argv[3]) if len(sys.argv) > 3 else 1001
    current_firmware = int(sys.argv[4]) if len(sys.argv) > 4 else 100

    print(f"CoAP Device Simulator")
    print(f"Target: coap://{host}:{port}/hb")
    print(f"Device ID: {device_id}")
    print(f"Current Firmware: {current_firmware}")
    print("=" * 50)

    await send_heartbeat(host, port, device_id, current_firmware)


if __name__ == "__main__":
    asyncio.run(main())