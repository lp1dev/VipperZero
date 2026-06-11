from evilcrow import EvilCrowRF

HRS = "0000180d-0000-1000-8000-00805f9b34fb"   # Heart Rate service
HRM = "00002a37-0000-1000-8000-00805f9b34fb"   # Heart Rate Measurement (notify)
BAS = "0000180f-0000-1000-8000-00805f9b34fb"   # Battery service
BAT = "00002a19-0000-1000-8000-00805f9b34fb"   # Battery Level (read)

with EvilCrowRF("/dev/ttyUSB0") as ec:
    print(ec.status())
    info = ec.ble_connect("A4:C1:38:31:1E:4D")

    print(f"connected, mtu={info['mtu']}")

    # one-shot read
    print(ec.ble_gatt("A4:C1:38:31:1E:4D"))
    battery = ec.ble_read(BAS, BAT)
    print(f"battery: {battery[0]}%")

    # request a larger MTU if your protocol sends >20 byte payloads
    ec.ble_mtu(247)

    # write a control point and listen for notifications
    ec.ble_write(HRS, HRM, b"\x01\x00")        # example: enable something
    ec.ble_subscribe(HRS, HRM)
    for n in ec.ble_notifications(timeout=15):
        # parse however your protocol defines it
        print(n["ts"], n["uuid"], n["data"].hex())

    ec.ble_unsubscribe(HRS, HRM)
    ec.ble_disconnect()
