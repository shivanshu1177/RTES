#!/usr/bin/env python3
"""
Simple UDP multicast receiver for RTES market data
Usage: python md_recv.py --group 239.0.0.1 --port 9999
"""

import socket
import struct
import argparse
import signal
import sys

running = True

def signal_handler(sig, frame):
    global running
    running = False
    print("\nShutting down...")

def main():
    parser = argparse.ArgumentParser(description='RTES Market Data Receiver')
    parser.add_argument('--group', default='239.0.0.1', help='Multicast group')
    parser.add_argument('--port', type=int, default=9999, help='Port number')
    args = parser.parse_args()
    
    signal.signal(signal.SIGINT, signal_handler)
    
    # Create socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    # Bind to any address
    sock.bind(('', args.port))
    
    # Join multicast group
    mreq = struct.pack('4sl', socket.inet_aton(args.group), socket.INADDR_ANY)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    
    print(f"Listening on {args.group}:{args.port}")
    print("Press Ctrl+C to stop\n")
    
    messages_received = 0
    expected_sequence = 1
    gaps_detected = 0
    
    try:
        while running:
            try:
                data, addr = sock.recvfrom(1024)
                if len(data) < 24:  # Minimum header size
                    continue
                
                # Parse header: type(4) + length(4) + sequence(8) + timestamp(8)
                msg_type, length, sequence, timestamp = struct.unpack('<LLQQ', data[:24])
                
                # Check for gaps
                if sequence != expected_sequence and messages_received > 0:
                    gaps_detected += 1
                    print(f"GAP: Expected {expected_sequence}, got {sequence}")
                
                expected_sequence = sequence + 1
                messages_received += 1
                
                # Parse message based on type
                if msg_type == 201:  # BBO_UPDATE
                    if len(data) >= 56:  # BBOUpdateMessage size
                        symbol = data[24:32].rstrip(b'\x00').decode('ascii')
                        bid_price, bid_qty, ask_price, ask_qty = struct.unpack('<QQQQ', data[32:64])
                        print(f"BBO {symbol} Bid:{bid_price/10000:.2f}x{bid_qty} Ask:{ask_price/10000:.2f}x{ask_qty} Seq:{sequence}")
                
                elif msg_type == 202:  # TRADE_UPDATE
                    if len(data) >= 49:  # TradeUpdateMessage size
                        trade_id = struct.unpack('<Q', data[24:32])[0]
                        symbol = data[32:40].rstrip(b'\x00').decode('ascii')
                        quantity, price, side = struct.unpack('<QQB', data[40:57])
                        side_str = "BUY" if side == 1 else "SELL"
                        print(f"TRADE {symbol} ID:{trade_id} {quantity}@{price/10000:.2f} {side_str} Seq:{sequence}")
                
                elif msg_type == 203:  # DEPTH_UPDATE
                    symbol = data[24:32].rstrip(b'\x00').decode('ascii')
                    num_bids, num_asks = struct.unpack('<BB', data[32:34])
                    print(f"DEPTH {symbol} Bids:{num_bids} Asks:{num_asks} Seq:{sequence}")
                
                else:
                    print(f"Unknown message type: {msg_type}")
                    
            except socket.timeout:
                continue
            except Exception as e:
                print(f"Error: {e}")
                break
                
    except KeyboardInterrupt:
        pass
    
    print(f"\nStatistics:")
    print(f"Messages received: {messages_received}")
    print(f"Gaps detected: {gaps_detected}")
    
    sock.close()

if __name__ == '__main__':
    main()