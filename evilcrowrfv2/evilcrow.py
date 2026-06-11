"""
evilcrow.py - host-side client for the Evil Crow RF V2 serial CLI.

Wraps the line-based command interface (rx / stoprx / tx / jammer /
stopjammer / status / echo / reboot) exposed by the serial-only firmware.

Requires: pyserial  (pip install pyserial)

Example:
    from evilcrow import EvilCrowRF

    with EvilCrowRF("/dev/ttyUSB0") as ec:
        print(ec.status())
        ec.rx(module=1, freq=433.92, mod=2, rxbw=200, deviation=0, datarate=5)
        for frame in ec.listen(timeout=30):
            print(frame.count, frame.raw_corrected[:8], "...")
            break
        ec.stop_rx()
"""

from __future__ import annotations

import time
import re
from dataclasses import dataclass, field
from typing import Iterator, Optional

import serial  # pyserial


# Modulation codes as used by the firmware / old web dropdowns.
MOD_2FSK = 0
MOD_ASK_OOK = 2


class EvilCrowError(RuntimeError):
    """Raised when the device returns an ERR line for a command."""


@dataclass
class RxFrame:
    """A decoded receive frame echoed by the device when echo is on."""
    count: int = 0                       # raw sample count
    samples: list[int] = field(default_factory=list)        # raw timings (us)
    corrected_count: int = 0             # "Rawdata corrected" count
    raw_corrected: list[int] = field(default_factory=list)  # cleaned timings (us)
    samples_per_symbol: Optional[int] = None
    text: str = ""                       # full raw block as received

    @property
    def csv(self) -> str:
        """Corrected timings as the comma string the tx command expects."""
        return ",".join(str(v) for v in self.raw_corrected)


class EvilCrowRF:
    def __init__(
        self,
        port: str,
        baudrate: int = 115200,
        timeout: float = 1.0,
        reset_on_open: bool = True,
    ):
        """
        port           e.g. "/dev/ttyUSB0", "/dev/tty.SLAB_USBtoUART", "COM5"
        baudrate       must match SERIAL_BAUD in the firmware (default 115200)
        timeout        per-read timeout in seconds
        reset_on_open  let DTR/RTS reset the ESP32 on connect (set False to
                       keep the running session alive across reconnects)
        """
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.reset_on_open = reset_on_open
        self._ser: Optional[serial.Serial] = None

    # ---- connection management --------------------------------------------

    def open(self) -> "EvilCrowRF":
        s = serial.Serial()
        s.port = self.port
        s.baudrate = self.baudrate
        s.timeout = self.timeout
        if not self.reset_on_open:
            # Suppress the auto-reset the CP2102N triggers via DTR/RTS->EN/IO0.
            s.dtr = False
            s.rts = False
        s.open()
        self._ser = s

        if self.reset_on_open:
            # Board reboots on open; wait for it to come back up.
            time.sleep(1.5)
        self.flush_input()
        # Best-effort: swallow the startup banner if present.
        self._read_for(0.3)
        return self

    def close(self) -> None:
        if self._ser and self._ser.is_open:
            self._ser.close()
        self._ser = None

    def __enter__(self) -> "EvilCrowRF":
        return self.open()

    def __exit__(self, *exc) -> None:
        self.close()

    @property
    def ser(self) -> serial.Serial:
        if not self._ser or not self._ser.is_open:
            raise EvilCrowError("port not open - call open() or use a 'with' block")
        return self._ser

    def flush_input(self) -> None:
        self.ser.reset_input_buffer()

    # ---- low-level IO ------------------------------------------------------

    def _write_line(self, line: str) -> None:
        self.ser.write((line + "\n").encode("ascii", errors="replace"))
        self.ser.flush()

    def _read_for(self, duration: float) -> str:
        """Read whatever arrives within `duration` seconds."""
        end = time.time() + duration
        buf = []
        while time.time() < end:
            chunk = self.ser.readline()
            if chunk:
                buf.append(chunk.decode("utf-8", errors="replace"))
            else:
                # idle tick; keep waiting until the window closes
                pass
        return "".join(buf)

    def _command(self, line: str, settle: float = 0.25) -> str:
        """Send a command and return the immediate response text."""
        self.flush_input()
        self._write_line(line)
        resp = self._read_for(settle)
        if re.search(r"^ERR\b", resp, re.MULTILINE):
            msg = next((l for l in resp.splitlines() if l.startswith("ERR")), resp)
            raise EvilCrowError(msg.strip())
        return resp

    def _command_until(self, line: str, max_wait: float = 30.0) -> str:
        """
        Send a command and read until an OK/ERR line arrives, or max_wait
        elapses. Better than _command() for variable-duration ops like BLE
        connect, where the response time depends on the peer.
        """
        self.flush_input()
        self._write_line(line)
        deadline = time.time() + max_wait
        buf = []
        while time.time() < deadline:
            chunk = self.ser.readline()
            if not chunk:
                continue
            text = chunk.decode("utf-8", errors="replace")
            buf.append(text)
            stripped = text.lstrip()
            if stripped.startswith("OK") or stripped.startswith("ERR"):
                break
        resp = "".join(buf)
        if re.search(r"^ERR\b", resp, re.MULTILINE):
            msg = next((l for l in resp.splitlines() if l.lstrip().startswith("ERR")), resp)
            raise EvilCrowError(msg.strip())
        return resp

    # ---- commands ----------------------------------------------------------

    def help(self) -> str:
        return self._command("help", settle=0.4)

    def status(self) -> dict:
        """Return device status as a dict (parsed from the status block)."""
        resp = self._command("status", settle=0.4)
        out: dict[str, str] = {}
        for line in resp.splitlines():
            if ":" in line and not line.startswith("---"):
                k, _, v = line.partition(":")
                out[k.strip()] = v.strip()
        return out

    def rx(
        self,
        module: int,
        freq: float,
        mod: int,
        rxbw: float,
        deviation: float,
        datarate: int,
    ) -> str:
        """Start receiving. mod: 0=2-FSK, 2=ASK/OOK. module: 1 or 2."""
        self._check_module(module)
        return self._command(
            f"rx {module} {freq:g} {mod} {rxbw:g} {deviation:g} {datarate}"
        )

    def stop_rx(self) -> str:
        return self._command("stoprx")

    def tx(
        self,
        module: int,
        freq: float,
        mod: int,
        deviation: float,
        raw,
    ) -> str:
        """
        Transmit raw timings. `raw` may be a CSV string or an iterable of ints
        (microsecond on/off durations). mod: 0=2-FSK, 2=ASK/OOK.
        """
        self._check_module(module)
        if not isinstance(raw, str):
            raw = ",".join(str(int(v)) for v in raw)
        raw = raw.replace(" ", "")
        if not raw:
            raise EvilCrowError("tx: empty raw data")
        # TX can take a while to clock out; give it room before reading.
        return self._command(
            f"tx {module} {freq:g} {mod} {deviation:g} {raw}", settle=1.0
        )

    def jammer(self, module: int, freq: float, power: int) -> str:
        """Start the jammer (power 0-12). Runs until stop_jammer()."""
        self._check_module(module)
        if not 0 <= power <= 12:
            raise EvilCrowError("jammer: power must be 0-12")
        return self._command(f"jammer {module} {freq:g} {power}")

    def stop_jammer(self) -> str:
        return self._command("stopjammer")

    def echo(self, on: bool) -> str:
        """Toggle whether decoded RX frames stream over serial."""
        return self._command(f"echo {'on' if on else 'off'}")

    # ---- presets ----------------------------------------------------------

    def presets(self) -> str:
        return self._command("presets", settle=0.4)

    def rx_preset(self, module: int, freq: float, preset: str) -> str:
        """Start RX using a named CC1101 preset (AM270/AM650/FM238/FM4768)."""
        self._check_module(module)
        return self._command(f"rxp {module} {freq:g} {preset}")

    def tx_preset(self, module: int, freq: float, preset: str, raw) -> str:
        self._check_module(module)
        if not isinstance(raw, str):
            raw = ",".join(str(int(v)) for v in raw)
        raw = raw.replace(" ", "")
        return self._command(f"txp {module} {freq:g} {preset} {raw}", settle=1.0)

    # ---- scanner ----------------------------------------------------------

    def scan(self, module: int = 1, threshold_dbm: int = -75) -> list[dict]:
        """
        RSSI sweep of the common sub-GHz frequencies. Returns a list of
        {freq, rssi, active} dicts. Receive-only.
        """
        self._check_module(module)
        resp = self._command(f"scan {module} {threshold_dbm}", settle=2.0)
        out = []
        for line in resp.splitlines():
            m = re.search(r"([\d.]+)\s*MHz\s+rssi=(-?\d+)", line)
            if m:
                out.append({
                    "freq": float(m.group(1)),
                    "rssi": int(m.group(2)),
                    "active": "*ACTIVE*" in line,
                })
        return out

    # ---- Flipper .sub file management -------------------------------------

    def sub_save(self, name: str) -> str:
        """Save the last decoded RX frame to /SUBGHZ/<name>.sub on the SD card."""
        return self._command(f"subsave {name}", settle=0.6)

    def sub_list(self) -> list[str]:
        """Return the names of .sub files on the SD card."""
        resp = self._command("sublist", settle=0.6)
        names = []
        for line in resp.splitlines():
            m = re.match(r"\s+(\S+\.sub)\b", line)
            if m:
                names.append(m.group(1))
        return names

    def sub_send(self, module: int, name: str) -> str:
        """Load a saved .sub and transmit it (frequency/preset from its header)."""
        self._check_module(module)
        return self._command(f"subsend {module} {name}", settle=1.0)

    def sub_delete(self, name: str) -> str:
        return self._command(f"subdel {name}")

    # ---- nRF24 (receive-only, requires soldered module) -------------------

    def nrf_scan(
        self,
        ch_start: int = 0,
        ch_end: int = 125,
        dwell_us: int = 200,
        passes: int = 40,
    ) -> list[dict]:
        """
        Sweep nRF24 channels and return active ones as [{ch, freq_mhz, hits, passes}, ...].
        Active channels are also appended to /NRF/scan.log on the SD card.
        """
        resp = self._command(
            f"nrfscan {ch_start} {ch_end} {dwell_us} {passes}",
            settle=max(2.0, passes * (ch_end - ch_start + 1) * dwell_us / 1_000_000 + 1.5),
        )
        out = []
        for line in resp.splitlines():
            m = re.search(r"ch=(\d+)\s*\((\d+)\s*MHz\)\s+hits=(\d+)/(\d+)", line)
            if m:
                out.append({
                    "ch": int(m.group(1)),
                    "freq_mhz": int(m.group(2)),
                    "hits": int(m.group(3)),
                    "passes": int(m.group(4)),
                })
        return out

    def nrf_log(self, clear: bool = False) -> str:
        return self._command("nrflog clear" if clear else "nrflog", settle=0.6)

    # ---- BLE (built-in ESP32 radio) ---------------------------------------

    def ble_scan(self, seconds: int = 8) -> list[dict]:
        """
        BLE advertisement scan. Returns [{mac, rssi, name, services}, ...].
        Devices are also appended to /BLE/scan.log on the SD card.
        """
        resp = self._command(f"blescan {int(seconds)}", settle=seconds + 2.0)
        out = []
        for line in resp.splitlines():
            m = re.search(r"([0-9a-fA-F:]{17})\s+rssi=(-?\d+)\s+name=(.+?)(?:\s+svcs=(.+))?$", line)
            if m:
                out.append({
                    "mac": m.group(1),
                    "rssi": int(m.group(2)),
                    "name": m.group(3).strip(),
                    "services": (m.group(4) or "").split("|") if m.group(4) else [],
                })
        return out

    def ble_gatt(self, mac: str) -> dict:
        """
        Connect to a BLE device, enumerate services/characteristics, disconnect.
        Returns {services: [{uuid, characteristics: [{uuid, props}]}], counts}.
        """
        resp = self._command(f"blegatt {mac}", settle=10.0)
        services: list[dict] = []
        current: Optional[dict] = None
        for line in resp.splitlines():
            ms = re.match(r"\s*service\s+(\S+)", line)
            if ms:
                current = {"uuid": ms.group(1), "characteristics": []}
                services.append(current)
                continue
            mc = re.match(r"\s*char\s+(\S+)\s+props=(\S*)", line)
            if mc and current is not None:
                current["characteristics"].append({"uuid": mc.group(1), "props": mc.group(2)})
        return {
            "services": services,
            "service_count": len(services),
            "characteristic_count": sum(len(s["characteristics"]) for s in services),
        }

    def ble_log(self, clear: bool = False) -> str:
        return self._command("blelog clear" if clear else "blelog", settle=0.6)

    # ---- BLE GATT client (persistent connection across calls) -------------

    @staticmethod
    def _to_hex(data) -> str:
        """Accept bytes, list[int], or hex-ish string; return clean hex."""
        if isinstance(data, (bytes, bytearray)):
            return data.hex()
        if isinstance(data, str):
            return "".join(c for c in data if c in "0123456789abcdefABCDEF")
        return bytes(data).hex()

    def ble_connect(self, mac: str, timeout: float = 30.0) -> dict:
        """Open a persistent BLE connection. Returns {mac, mtu}."""
        resp = self._command_until(f"bleconnect {mac}", max_wait=timeout)
        m = re.search(r"OK bleconnect:\s+(\S+)\s+mtu=(\d+)", resp)
        if not m:
            raise EvilCrowError(f"ble_connect: unexpected response: {resp.strip()}")
        return {"mac": m.group(1), "mtu": int(m.group(2))}

    def ble_disconnect(self) -> str:
        return self._command_until("bledisconnect", max_wait=5.0)

    def ble_status(self) -> dict:
        resp = self._command("blestatus", settle=0.3)
        out: dict[str, str] = {}
        for line in resp.splitlines():
            if ":" in line:
                k, _, v = line.partition(":")
                out[k.strip()] = v.strip()
        return out

    def ble_mtu(self, new_mtu: Optional[int] = None) -> int:
        resp = self._command_until(f"blemtu {new_mtu}" if new_mtu else "blemtu", max_wait=5.0)
        m = re.search(r"OK blemtu:\s+(\d+)", resp)
        return int(m.group(1)) if m else 0

    def ble_read(self, service_uuid: str, char_uuid: str) -> bytes:
        """Read a characteristic. Returns raw bytes."""
        resp = self._command_until(f"bleread {service_uuid} {char_uuid}", max_wait=10.0)
        m = re.search(r"OK bleread:\s+len=(\d+)\s+hex=([0-9a-fA-F]*)", resp)
        if not m:
            raise EvilCrowError(f"ble_read failed: {resp.strip()}")
        return bytes.fromhex(m.group(2))

    def ble_write(self, service_uuid: str, char_uuid: str, data, with_response: bool = True) -> int:
        """Write a characteristic. data: bytes / list[int] / hex string. Returns bytes written."""
        hexs = self._to_hex(data)
        if not hexs:
            raise EvilCrowError("ble_write: empty payload")
        cmd = "blewrite" if with_response else "blewriten"
        resp = self._command_until(f"{cmd} {service_uuid} {char_uuid} {hexs}", max_wait=10.0)
        m = re.search(r"wrote\s+(\d+)\s+byte", resp)
        if not m:
            raise EvilCrowError(f"ble_write failed: {resp.strip()}")
        return int(m.group(1))

    def ble_subscribe(self, service_uuid: str, char_uuid: str) -> str:
        return self._command_until(f"blesub {service_uuid} {char_uuid}", max_wait=10.0)

    def ble_unsubscribe(self, service_uuid: str, char_uuid: str) -> str:
        return self._command_until(f"bleunsub {service_uuid} {char_uuid}", max_wait=5.0)

    def ble_notifications(self, timeout: Optional[float] = None) -> Iterator[dict]:
        """
        Yield incoming notifications as {ts, uuid, data} dicts. Requires a
        prior ble_subscribe(). Stops after `timeout` seconds of silence,
        or runs until KeyboardInterrupt if timeout is None.

        Example:
            ec.ble_subscribe(SVC, CHR)
            for n in ec.ble_notifications(timeout=10):
                print(n["uuid"], n["data"].hex())
        """
        deadline = None if timeout is None else time.time() + timeout
        while deadline is None or time.time() < deadline:
            line = self.ser.readline().decode("utf-8", errors="replace")
            if not line:
                continue
            if deadline is not None:
                deadline = time.time() + timeout       # reset idle timer
            m = re.match(
                r"NOTIF\s+t=(\d+)\s+uuid=(\S+)\s+len=(\d+)\s+hex=([0-9a-fA-F]*)",
                line.strip(),
            )
            if m:
                yield {
                    "ts": int(m.group(1)),
                    "uuid": m.group(2),
                    "data": bytes.fromhex(m.group(4)),
                }

    # ---- binary representation --------------------------------------------

    def tx_binary(self, module: int, freq: float, preset: str, te_us: int, bits: str) -> str:
        """Transmit an OOK binary symbol stream (te_us per symbol)."""
        self._check_module(module)
        bits = "".join(c for c in bits if c in "01")
        if not bits:
            raise EvilCrowError("tx_binary: empty bitstring")
        return self._command(f"txbin {module} {freq:g} {preset} {int(te_us)} {bits}", settle=1.0)

    def last_binary(self, te_us: Optional[int] = None) -> dict:
        """
        Return the last decoded RX frame as a binary symbol string:
        {bits, te, symbol_count}. Pass te_us to force a symbol period.
        """
        cmd = "lastbin" if te_us is None else f"lastbin {int(te_us)}"
        resp = self._command(cmd, settle=0.6)
        bits, te, count = "", None, 0
        for line in resp.splitlines():
            m = re.search(r"Binary \(te=(\d+)us\):\s*([01]+)", line)
            if m:
                te = int(m.group(1)); bits = m.group(2)
            m2 = re.match(r"Symbol:\s*(\d+)", line.strip())
            if m2:
                count = int(m2.group(1))
        return {"bits": bits, "te": te, "symbol_count": count}

    # ---- record / replay signal library (host-side, JSON) -----------------

    def record(
        self,
        path: str,
        frames: int = 1,
        timeout: float = 30.0,
        freq: Optional[float] = None,
        preset: Optional[str] = None,
    ) -> int:
        """
        Capture up to `frames` decoded RX frames and append them to a JSON
        library file at `path`. RX must already be active (via rx/rx_preset),
        or pass freq+preset to start an OOK capture here. Returns count saved.
        """
        import json
        import os

        if freq is not None and preset is not None:
            self.rx_preset(1, freq, preset)

        try:
            lib = []
            if os.path.exists(path):
                with open(path) as fh:
                    lib = json.load(fh)

            saved = 0
            for frame in self.listen(timeout=timeout):
                lib.append({
                    "freq": freq,
                    "preset": preset,
                    "count": frame.corrected_count,
                    "samples_per_symbol": frame.samples_per_symbol,
                    "raw_corrected": frame.raw_corrected,
                    "csv": frame.csv,
                })
                saved += 1
                if saved >= frames:
                    break

            with open(path, "w") as fh:
                json.dump(lib, fh, indent=2)
            return saved
        finally:
            self.stop_rx()

    def replay(
        self,
        path: str,
        module: int = 1,
        freq: Optional[float] = None,
        mod: int = MOD_ASK_OOK,
        deviation: float = 0,
        index: Optional[int] = None,
        delay_s: float = 0.5,
    ) -> int:
        """
        Replay frames from a JSON library created by record(). By default sends
        every frame in order; pass `index` to send just one. `freq` overrides
        the stored frequency. Returns the number of frames transmitted.
        """
        import json

        with open(path) as fh:
            lib = json.load(fh)
        if index is not None:
            lib = [lib[index]]

        sent = 0
        for entry in lib:
            f = freq if freq is not None else entry.get("freq")
            if f is None:
                raise EvilCrowError("replay: no frequency in library; pass freq=")
            raw = entry.get("csv") or ",".join(str(v) for v in entry.get("raw_corrected", []))
            if not raw:
                continue
            self.tx(module=module, freq=f, mod=mod, deviation=deviation, raw=raw)
            sent += 1
            time.sleep(delay_s)
        return sent

    def reboot(self) -> None:
        try:
            self._write_line("reboot")
        finally:
            time.sleep(0.2)

    # ---- RX streaming ------------------------------------------------------

    def listen(self, timeout: Optional[float] = None) -> Iterator[RxFrame]:
        """
        Yield RxFrame objects as they arrive. Requires rx() to be active and
        echo on (default). Stops after `timeout` seconds of no activity, or
        runs forever if timeout is None. Call stop_rx() when done.
        """
        block: list[str] = []
        deadline = None if timeout is None else time.time() + timeout
        while deadline is None or time.time() < deadline:
            line = self.ser.readline().decode("utf-8", errors="replace")
            if not line:
                continue
            if deadline is not None:
                deadline = time.time() + timeout  # reset idle timer on activity
            if line.strip() == "--- end frame ---":
                frame = self._parse_frame("".join(block))
                block = []
                if frame is not None:
                    yield frame
            else:
                block.append(line)

    @staticmethod
    def _parse_frame(text: str) -> Optional[RxFrame]:
        f = RxFrame(text=text)
        lines = text.splitlines()
        section = None
        for i, line in enumerate(lines):
            s = line.strip()
            m = re.match(r"Count=(\d+)", s)
            if m:
                if section == "corrected":
                    f.corrected_count = int(m.group(1))
                else:
                    f.count = int(m.group(1))
                continue
            if s.startswith("Samples/Symbol:"):
                try:
                    f.samples_per_symbol = int(s.split(":", 1)[1])
                except ValueError:
                    pass
                continue
            if s.startswith("Rawdata corrected:"):
                section = "corrected"
                continue
            if "," in s and re.match(r"^[\d,]+$", s):
                vals = [int(x) for x in s.split(",") if x]
                if section == "corrected":
                    f.raw_corrected = vals
                elif not f.samples:
                    f.samples = vals
        if not f.samples and not f.raw_corrected:
            return None
        return f

    # ---- helpers -----------------------------------------------------------

    @staticmethod
    def _check_module(module: int) -> None:
        if module not in (1, 2):
            raise EvilCrowError("module must be 1 or 2")


# ---- tiny demo / smoke test -----------------------------------------------

if __name__ == "__main__":
    import argparse

    ap = argparse.ArgumentParser(description="Evil Crow RF V2 serial demo")
    ap.add_argument("port", nargs="?", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    with EvilCrowRF(args.port, baudrate=args.baud) as ec:
        print("status:", ec.status())
        print("starting RX on module 1 @ 433.92 MHz (OOK)...")
        ec.rx(module=1, freq=433.92, mod=MOD_ASK_OOK, rxbw=200, deviation=0, datarate=5)
        print("listening 20s - trigger a transmitter near the device...")
        try:
            for frame in ec.listen(timeout=20):
                print(f"  frame: count={frame.count} "
                      f"corrected={frame.corrected_count} "
                      f"sps={frame.samples_per_symbol}")
                print(f"  csv: {frame.csv[:80]}{'...' if len(frame.csv) > 80 else ''}")
        except KeyboardInterrupt:
            pass
        finally:
            print("stop:", ec.stop_rx().strip())
