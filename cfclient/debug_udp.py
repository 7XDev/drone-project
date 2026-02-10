"""
Debug script to monitor UDP data flow from ESP32 drone.
Run this instead of cfclient to see exactly what happens.
"""
import socket
import time
import binascii
import threading

DRONE_IP = '192.168.43.42'
DRONE_PORT = 2390
LOCAL_PORT = 2399

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('', LOCAL_PORT))
    sock.settimeout(5.0)
    addr = (DRONE_IP, DRONE_PORT)

    # Send connection init packet
    print(f"Sending init packet to {addr}...")
    sock.sendto(b'\xFF\x01\x01\x01', addr)

    packet_count = 0
    last_print = time.time()
    start = time.time()
    last_recv = time.time()

    print("Listening for packets...")
    print("=" * 60)

    try:
        while True:
            try:
                data, recv_addr = sock.recvfrom(1024)
                now = time.time()
                packet_count += 1
                last_recv = now
                elapsed = now - start

                # Decode header
                if len(data) > 1:
                    header = data[0]
                    port = (header >> 4) & 0x0F
                    channel = header & 0x03
                    payload_hex = binascii.hexlify(data[1:min(10, len(data))]).decode()
                    
                    # Print every packet for first 5 seconds, then summary
                    if elapsed < 5 or (now - last_print) > 2.0:
                        print(f"[{elapsed:7.2f}s] #{packet_count:4d} | port={port} ch={channel} | len={len(data):3d} | {payload_hex}...")
                        last_print = now

            except socket.timeout:
                now = time.time()
                gap = now - last_recv
                print(f"[{now - start:7.2f}s] TIMEOUT! No data for {gap:.1f}s (total packets: {packet_count})")

                # Try sending a keep-alive / null commander packet
                # Port 3 (Commander), channel 0
                # header = (3 << 4) | 0 = 0x30
                # setpoint: roll=0, pitch=0, yaw=0, thrust=0
                import struct
                keepalive = struct.pack('<BfffH', 0x30, 0.0, 0.0, 0.0, 0)
                cksum = sum(keepalive) % 256
                keepalive += bytes([cksum])
                print(f"  -> Sending keepalive commander packet...")
                sock.sendto(keepalive, addr)

    except KeyboardInterrupt:
        print(f"\n\nStopped. Total packets: {packet_count}, duration: {time.time()-start:.1f}s")
    finally:
        sock.sendto(b'\xFF\x01\x02\x02', addr)
        sock.close()

if __name__ == '__main__':
    main()
