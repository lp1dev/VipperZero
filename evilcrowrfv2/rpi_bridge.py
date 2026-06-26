#!/usr/bin/env python3
"""
Vita USB Serial ↔ EvilCrowRFv2
"""

import usb.core
import usb.util
import serial
import sys
import time
import binascii

VID, PID = 0x054c, 0x069b
EP_OUT = 0x02
EP_IN  = 0x83

SERIAL_PORT = "/dev/ttyUSB0"
BAUDRATE = 115200
SERIAL_TIMEOUT = 0.05
USB_READ_TIMEOUT = 100   # ms
RECONNECT_DELAY = 3

# Send to Vita in small chunks with a breathing delay
USB_WRITE_CHUNK = 32          # bytes per write
USB_WRITE_DELAY = 0.010       # 10 ms between chunks

def hexdump(data, prefix=""):
    if not data:
        return "(empty)"
    show = data[:64]
    more = f"... ({len(data)} bytes total)" if len(data) > 64 else ""
    hex_part = binascii.hexlify(show, ' ').decode()
    ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in show)
    return f"{prefix}{hex_part}  |{ascii_part}|{more}"

def wait_for_vita():
    print("Waiting for PS Vita Type D (054c:069b)...")
    while True:
        dev = usb.core.find(idVendor=VID, idProduct=PID)
        if dev is not None:
            return dev
        time.sleep(1)

def setup_usb(dev):
    for iface in (0, 1):
        try:
            if dev.is_kernel_driver_active(iface):
                dev.detach_kernel_driver(iface)
        except usb.core.USBError:
            pass
    usb.util.claim_interface(dev, 0)
    usb.util.claim_interface(dev, 1)
    print("Interfaces claimed.")
    try:
        dev.ctrl_transfer(0x21, 0x22, 0x0003, 0)
        print("DTR/RTS asserted.")
    except usb.core.USBError as e:
        print(f"DTR/RTS note: {e}")

def release_usb(dev):
    for iface in (1, 0):
        try:
            usb.util.release_interface(dev, iface)
        except:
            pass

def vita_write(dev, data):
    """Send data to Vita in small chunks with delays."""
    for i in range(0, len(data), USB_WRITE_CHUNK):
        chunk = data[i:i+USB_WRITE_CHUNK]
        dev.write(EP_OUT, chunk, timeout=2000)
        time.sleep(USB_WRITE_DELAY)

def bridge(dev, ser):
    print("Bridge active. Ctrl+C to stop.\n")
    try:
        while True:
            # Read from Vita
            try:
                data = dev.read(EP_IN, 64, timeout=USB_READ_TIMEOUT)
                if data:
                    print(f"VITA -> SERIAL ({len(data)} bytes):")
                    print(hexdump(data, prefix="  "))
                    ser.write(data)
                    ser.flush()
            except usb.core.USBError as e:
                if e.errno == 110 or "timeout" in str(e).lower():
                    pass
                else:
                    print(f"USB read error: {e}")
                    raise

            # Read from serial, send to Vita in small chunks
            if ser.in_waiting:
                buf = ser.read(ser.in_waiting)
                if buf:
                    print(f"SERIAL -> VITA ({len(buf)} bytes):")
                    print(hexdump(buf, prefix="  "))
                    vita_write(dev, buf)

            time.sleep(0.001)
    except KeyboardInterrupt:
        print("\nStopped by user.")
        raise

def main():
    ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=SERIAL_TIMEOUT)
    print(f"Opened {SERIAL_PORT} at {BAUDRATE} baud.")

    while True:
        dev = wait_for_vita()
        try:
            setup_usb(dev)
        except usb.core.USBError as e:
            print(f"Setup failed: {e}, retrying...")
            time.sleep(RECONNECT_DELAY)
            continue

        try:
            bridge(dev, ser)
        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"Bridge error: {e}")
        finally:
            release_usb(dev)
            print(f"Waiting {RECONNECT_DELAY}s before reconnect...")
            time.sleep(RECONNECT_DELAY)

    ser.close()
    print("Done.")

if __name__ == "__main__":
    main()
