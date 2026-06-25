/*
 * firmware.ino  -  Evil Crow RF V2  (serial-only build, WiFi/web panel removed)
 *
 * Single self-contained sketch. DELETE serial_cli.ino from the sketch folder
 * before compiling this - the CLI is now merged in here, and keeping both
 * files would cause duplicate-symbol errors.
 *
 * Hardware note: the EC RF V2 is a classic ESP32 (WROOM-32) behind a CP2102N
 * USB-UART bridge. It enumerates as /dev/ttyUSB0 (cp210x driver), NOT ttyACM*.
 * Open your serial monitor at SERIAL_BAUD below, newline line-ending.
 *
 * The classic ESP32 has no USB peripheral, so the device cannot be a USB host.
 * Host mode would require external hardware (e.g. a MAX3421E over SPI) or a
 * different MCU (ESP32-S2/S3 with USB-OTG). See notes from the assistant.
 */

#include "ELECHOUSE_CC1101_SRC_DRV.h"
#include <SPI.h>
#include "SD.h"

// nRF24 (optional, receive-only channel scanner)
#include <nRF24L01.h>
#include <RF24.h>

// BLE (built into the ESP32, no extra hardware)
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>

#define SERIAL_BAUD 115200
#define EVILCROW_FW_VERSION "1.0.0"

// Buttons and LED on the EC RF V2 board
#define LED      32
#define BUTTON1  34   // BLE enable (press at boot, or any time, to spin up Bluedroid)
#define BUTTON2  35   // reserved

// nRF24 wiring (you solder these to a free header on the V2)
//   CE  -> GPIO 32     CSN -> GPIO 33
//   SCK -> 14 (shared with CC1101)   MISO -> 12   MOSI -> 13
// Power the module from 3.3V; add a 10uF cap across VCC/GND if you see resets.
#define NRF_CE_PIN  32
#define NRF_CSN_PIN 33

// Defined here, immediately after includes, so the Arduino IDE's auto-
// generated function prototypes (which it injects near the top of the
// sketch) can resolve the type wherever findPreset() is declared.
struct Preset {
  const char *name;
  int   mod;        // 2 = ASK/OOK, 0 = 2-FSK
  float rxbw;       // kHz
  float deviation;  // kHz
  int   datarate;   // kBaud
};

// MicroSD slot pins
#define SD_SCLK 18
#define SD_MISO 19
#define SD_MOSI 23
#define SD_SS   22
SPIClass sdspi(VSPI);

// SPI Pins
int sck_pin = 14;
int miso_pin = 12;
int mosi_pin = 13;

// CC1101 Module 1
int rx_pin1 = 4;
int tx_pin1 = 2;
int cs_pin1 = 5;

// CC1101 Module 2
int rx_pin2 = 26;
int tx_pin2 = 25;
int cs_pin2 = 27;

// RF variables
#define RECEIVE_ATTR IRAM_ATTR
#define samplesize 2000
int error_toleranz = 200;
const int minsample = 30;
unsigned long sample[samplesize];
unsigned long samplesmooth[samplesize];
String lastSampleSmooth;
int  lastIndex;
int samplecount;
static unsigned long lastTime = 0;
int mod;
float deviation;
int datarate;
float frequency;
float setrxbw;
int power_jammer;
byte jammer[] = { 0xff, 0xff };
const size_t jammer_len = sizeof(jammer) / sizeof(jammer[0]);
long data_to_send[2000];

// Other variables
String OutputLog;

// File
File logs;

// Runtime state (shared by RF code + serial CLI)
String tmp_module;
String raw_rx = "0";
String jammer_tx = "0";
String transmit;
bool serialRxEcho = true;   // stream decoded RX frames over USB serial

// nRF24 state (receive-only)
RF24 nrf(NRF_CE_PIN, NRF_CSN_PIN);
bool nrfPresent = false;

// BLE GATT client state - one persistent connection across commands.
BLEClient *bleClient = nullptr;
String     bleConnectedMac = "";
bool       bleNotifyStream = false;     // when true, notifications stream to serial

// Pending-notifications ring buffer (filled from ISR-ish callback, drained in loop).
struct BleNotif {
  char     uuid[40];
  uint8_t  data[64];
  size_t   len;
  uint32_t ts;
};
#define BLE_NOTIF_QUEUE 16
static volatile BleNotif bleNotifQ[BLE_NOTIF_QUEUE];
static volatile uint8_t  bleNotifHead = 0, bleNotifTail = 0;

static void bleNotifyCallback(BLERemoteCharacteristic *rc,
                              uint8_t *data, size_t len, bool isNotify) {
  uint8_t next = (bleNotifHead + 1) % BLE_NOTIF_QUEUE;
  if (next == bleNotifTail) return;     // queue full, drop
  BleNotif *slot = (BleNotif*)&bleNotifQ[bleNotifHead];
  String uuid = String(rc->getUUID().toString().c_str());
  strncpy(slot->uuid, uuid.c_str(), sizeof(slot->uuid) - 1);
  slot->uuid[sizeof(slot->uuid) - 1] = 0;
  slot->len = len > sizeof(slot->data) ? sizeof(slot->data) : len;
  memcpy(slot->data, data, slot->len);
  slot->ts = millis();
  bleNotifHead = next;
}

void appendFile(fs::FS &fs, const char * path, const char * message, String messagestring){
  logs = fs.open(path, FILE_APPEND);
  if(!logs){
    //Serial.println("Failed to open file for appending");
    return;
  }
  if(logs.print(message)|logs.print(messagestring)){
    //Serial.println("Message appended");
  } else {
    //Serial.println("Append failed");
  }
  logs.close();
}

void deleteFile(fs::FS &fs, const char * path){
  //Serial.printf("Deleting file: %s\n", path);
  if(fs.remove(path)){
    //Serial.println("File deleted");
  } else {
    //Serial.println("Delete failed");
  }
}

bool checkReceived(void) {
  delay(1);
  if (samplecount >= minsample && micros() - lastTime > 100000) {
    detachInterrupt(rx_pin1);
    detachInterrupt(rx_pin2);
    return true;
  } else {
    return false;
  }
}

void printReceived() {
  OutputLog = "";
  appendFile(SD, "/logs.txt", "-------------------------------------------------------\n", "");
  //Serial.print("Count=");
  //Serial.println(samplecount);
  OutputLog += "\n";
  OutputLog += "Count=";
  OutputLog += String(samplecount);
  OutputLog += "\n";

  for (int i = 0; i < samplecount; i++) {
    //Serial.print(sample[i]);
    //Serial.print(",");
    OutputLog += String(sample[i]);
    OutputLog += ",";
  }
  //Serial.println();
  //Serial.println();
  OutputLog += "\n";
  appendFile(SD, "/logs.txt", NULL, OutputLog.c_str());
}

void RECEIVE_ATTR receiver() {
  const long time = micros();
  const unsigned int duration = time - lastTime;

  if (duration > 100000) {
    samplecount = 0;
  }

  if (duration >= 100 && samplecount < samplesize) {
    sample[samplecount++] = duration;
  }

  /*if (duration >= 100) {
    sample[samplecount++] = duration;
  }*/

  if (samplecount >= samplesize) {
    samplecount = samplesize - 1;
    bool received = checkReceived();
    if (received) {}
    detachInterrupt(rx_pin1);
    detachInterrupt(rx_pin2);
  }

  if (mod == 0) {
    if (samplecount == 1 and digitalRead(rx_pin2) != HIGH){
      samplecount = 0;
    }

    else if (samplecount == 1 and digitalRead(rx_pin1) != HIGH){
      samplecount = 0;
    }
  }

  lastTime = time;
}

void signalanalyse(){
  OutputLog = "";
  #define signalstorage 10

  int signalanz=0;
  int timingdelay[signalstorage];
  //float pulse[signalstorage];
  long signaltimings[signalstorage*2];
  int signaltimingscount[signalstorage];
  long signaltimingssum[signalstorage];
  long signalsum=0;
  
  for (int i = 0; i<signalstorage; i++){
    signaltimings[i*2] = 100000;
    signaltimings[i*2+1] = 0;
    signaltimingscount[i] = 0;
    signaltimingssum[i] = 0;
  }
  for (int i = 1; i<samplecount; i++){
    signalsum+=sample[i];
  }

  for (int p = 0; p<signalstorage; p++){

  for (int i = 1; i<samplecount; i++){
    if (p==0){
      if (sample[i]<signaltimings[p*2]){
        signaltimings[p*2]=sample[i];
      }
    }else{
      if (sample[i]<signaltimings[p*2] && sample[i]>signaltimings[p*2-1]){
        signaltimings[p*2]=sample[i];
      }
    }
  }

  for (int i = 1; i<samplecount; i++){
    if (sample[i]<signaltimings[p*2]+error_toleranz && sample[i]>signaltimings[p*2+1]){
      signaltimings[p*2+1]=sample[i];
    }
  }

  for (int i = 1; i<samplecount; i++){
    if (sample[i]>=signaltimings[p*2] && sample[i]<=signaltimings[p*2+1]){
      signaltimingscount[p]++;
      signaltimingssum[p]+=sample[i];
    }
  }
  }
  int firstsample = signaltimings[0];
  signalanz=signalstorage;

  for (int i = 0; i<signalstorage; i++){
    if (signaltimingscount[i] == 0){
      signalanz=i;
      i=signalstorage;
    }
  }

  for (int s=1; s<signalanz; s++){
  for (int i=0; i<signalanz-s; i++){
    if (signaltimingscount[i] < signaltimingscount[i+1]){
      int temp1 = signaltimings[i*2];
      int temp2 = signaltimings[i*2+1];
      int temp3 = signaltimingssum[i];
      int temp4 = signaltimingscount[i];
      signaltimings[i*2] = signaltimings[(i+1)*2];
      signaltimings[i*2+1] = signaltimings[(i+1)*2+1];
      signaltimingssum[i] = signaltimingssum[i+1];
      signaltimingscount[i] = signaltimingscount[i+1];
      signaltimings[(i+1)*2] = temp1;
      signaltimings[(i+1)*2+1] = temp2;
      signaltimingssum[i+1] = temp3;
      signaltimingscount[i+1] = temp4;
    }
  }
  }

  for (int i=0; i<signalanz; i++){
    timingdelay[i] = signaltimingssum[i]/signaltimingscount[i];
  }

  if (firstsample == sample[1] and firstsample < timingdelay[0]){
    sample[1] = timingdelay[0];
  }

  bool lastbin=0;
  for (int i=1; i<samplecount; i++){
    float r = (float)sample[i]/timingdelay[0];
    int calculate = r;
    r = r-calculate;
    r*=10;
    if (r>=5){calculate+=1;}
    if (calculate>0){
      if (lastbin==0){
        lastbin=1;
      }else{
      lastbin=0;
    }
      if (lastbin==0 && calculate>8){
        //Serial.print(" [Pause: ");
        //Serial.print(sample[i]);
        //Serial.println(" samples]");
        OutputLog += " [Pause: ";
        OutputLog += String(sample[i]);
        OutputLog += " samples]\n";

      } else{
        for (int b=0; b<calculate; b++){
          //Serial.print(lastbin);
          OutputLog += String(lastbin);
        }
      }
    }
  }
  //Serial.println();
  //Serial.print("Samples/Symbol: ");
  //Serial.println(timingdelay[0]);
  //Serial.println();
  OutputLog += "\nSamples/Symbol: ";
  OutputLog += String(timingdelay[0]);
  OutputLog += "\n\n";

  int smoothcount=0;

  for (int i=1; i<samplecount; i++){
    float r = (float)sample[i]/timingdelay[0];
    int calculate = r;
    r = r-calculate;
    r*=10;
    if (r>=5){calculate+=1;}
    if (calculate>0){
      samplesmooth[smoothcount] = calculate*timingdelay[0];
      smoothcount++;
    }
  }
  appendFile(SD, "/logs.txt", NULL, "\n");
  //Serial.println("Rawdata corrected:");
  //Serial.print("Count=");
  //Serial.println(smoothcount+1);
  lastIndex = smoothcount - 1;
  lastSampleSmooth = samplesmooth[lastIndex];

  OutputLog += "Rawdata corrected:\n";
  OutputLog += "Count=";
  OutputLog += String(smoothcount + 1);
  OutputLog += "\n";

  for (int i=0; i<smoothcount; i++){
    //Serial.print(samplesmooth[i]);
    //Serial.print(",");
    OutputLog += String(samplesmooth[i]);
    OutputLog += ",";
  }
  //Serial.println();
  //Serial.println();
  appendFile(SD, "/logs.txt", NULL, OutputLog.c_str());
  appendFile(SD, "/logs.txt", NULL, "\n");
  appendFile(SD, "/logs.txt", "-------------------------------------------------------\n", "");
  return;
}

void enableReceive() {
  rx_pin1 = digitalPinToInterrupt(rx_pin1);
  rx_pin2 = digitalPinToInterrupt(rx_pin2);
  pinMode(rx_pin1, INPUT);
  pinMode(rx_pin2, INPUT);
  ELECHOUSE_cc1101.SetRx();
  samplecount = 0;
  attachInterrupt(rx_pin1, receiver, CHANGE);
  attachInterrupt(rx_pin2, receiver, CHANGE);
}


// ============================================================================
//  USB serial command interface
//  Line-based, SERIAL_BAUD, newline-terminated. Mirrors the RF operations the
//  old web panel exposed (rx / stoprx / tx / jammer / stopjammer / status).
// ============================================================================

static String serialLine = "";

// idx-th whitespace separated token (0-based), or "" if absent.
static String tok(const String &s, int idx) {
  int count = 0, i = 0, n = s.length();
  while (i < n) {
    while (i < n && s[i] == ' ') i++;
    int start = i;
    while (i < n && s[i] != ' ') i++;
    if (i > start) {
      if (count == idx) return s.substring(start, i);
      count++;
    }
  }
  return "";
}

// Everything from the idx-th token to end of line (raw CSV, preserves content).
static String restFrom(const String &s, int idx) {
  int count = 0, i = 0, n = s.length();
  while (i < n) {
    while (i < n && s[i] == ' ') i++;
    int start = i;
    while (i < n && s[i] != ' ') i++;
    if (i > start) {
      if (count == idx) return s.substring(start);
      count++;
    }
  }
  return "";
}

static void cliHelp() {
  Serial.println(F("Evil Crow RF V2 - serial CLI"));
  Serial.println(F("modulation: 0=2-FSK 2=ASK/OOK   module: 1|2"));
  Serial.println(F("  help"));
  Serial.println(F("  status"));
  Serial.println(F("  rx <module> <freq> <mod> <rxbw> <deviation> <datarate>   e.g. rx 1 433.92 2 200 0 5"));
  Serial.println(F("  stoprx"));
  Serial.println(F("  tx <module> <freq> <mod> <deviation> <raw_csv>           e.g. tx 1 433.92 2 0 400,300,400,300"));
  Serial.println(F("  jammer <module> <freq> <power>                           power 0-12"));
  Serial.println(F("  stopjammer"));
  Serial.println(F("  echo on|off"));
  Serial.println(F("  presets                                                  list CC1101 presets"));
  Serial.println(F("  rxp <module> <freq> <preset>                             receive using a preset"));
  Serial.println(F("  txp <module> <freq> <preset> <raw_csv>                   transmit using a preset"));
  Serial.println(F("  scan [module] [rssi_dbm]                                 RSSI sweep of common freqs"));
  Serial.println(F("  subsave <name>                                           save last RX as Flipper .sub"));
  Serial.println(F("  sublist                                                  list saved .sub files"));
  Serial.println(F("  subsend <module> <name>                                  transmit a saved .sub"));
  Serial.println(F("  subdel <name>                                            delete a saved .sub"));
  Serial.println(F("  txbin <module> <freq> <preset> <te_us> <bits>            transmit OOK binary stream"));
  Serial.println(F("  lastbin [te_us]                                          show last RX as binary symbols"));
  Serial.println(F("  nrfscan [start] [end] [dwell_us] [passes]                nRF24 RX-only channel scan (needs module)"));
  Serial.println(F("  nrflog [clear]                                           show / clear /NRF/scan.log"));
  Serial.println(F("  blescan [seconds]                                        BLE advertisement scan (built-in radio)"));
  Serial.println(F("  blegatt <MAC>                                            list services + characteristics of a device"));
  Serial.println(F("  blelog [clear]                                           show / clear /BLE/scan.log"));
  Serial.println(F("  bleconnect <MAC>                                         connect (persistent across commands)"));
  Serial.println(F("  bledisconnect                                            drop the active connection"));
  Serial.println(F("  blestatus                                                connection / MTU / notif stream state"));
  Serial.println(F("  bleread <svc_uuid> <char_uuid>                           read a characteristic (hex out)"));
  Serial.println(F("  blewrite <svc_uuid> <char_uuid> <hex>                    write with response"));
  Serial.println(F("  blewriten <svc_uuid> <char_uuid> <hex>                   write without response"));
  Serial.println(F("  blesub <svc_uuid> <char_uuid>                            subscribe to notify/indicate (NOTIF lines)"));
  Serial.println(F("  bleunsub <svc_uuid> <char_uuid>                          unsubscribe"));
  Serial.println(F("  blemtu [new_mtu]                                         show or request a new MTU"));
  Serial.println(F("  reboot"));
  Serial.println(F("Transmitting / jamming RF may be regulated where you are - stay within the law."));
}

static void cliStatus() {
  bool sd_present = SD.cardType() != CARD_NONE;
  Serial.println(F("--- status ---"));
  Serial.print(F("firmware_ver : ")); Serial.println(EVILCROW_FW_VERSION);
  Serial.print(F("uptime_s     : ")); Serial.println(millis() / 1000);
  Serial.print(F("cpu_mhz      : ")); Serial.println(getCpuFrequencyMhz());
  Serial.print(F("temperature  : ")); Serial.println(temperatureRead());
  Serial.print(F("free_heap    : ")); Serial.println(ESP.getFreeHeap());
  Serial.print(F("total_heap   : ")); Serial.println(ESP.getHeapSize());
  Serial.print(F("sd_present   : ")); Serial.println(sd_present ? F("true") : F("false"));
  Serial.print(F("rx_active    : ")); Serial.println(raw_rx);
  Serial.print(F("jammer_active: ")); Serial.println(jammer_tx);
  Serial.print(F("serial_echo  : ")); Serial.println(serialRxEcho ? F("on") : F("off"));
}

// Mirrors the old /setrx handler.
static void cliRx(const String &line) {
  tmp_module = tok(line, 1);
  String f   = tok(line, 2);
  String m   = tok(line, 3);
  String bw  = tok(line, 4);
  String dev = tok(line, 5);
  String dr  = tok(line, 6);

  if (tmp_module == "" || f == "" || m == "" || bw == "" || dev == "" || dr == "") {
    Serial.println(F("ERR rx: usage rx <module> <freq> <mod> <rxbw> <deviation> <datarate>"));
    return;
  }
  if (tmp_module != "1" && tmp_module != "2") {
    Serial.println(F("ERR rx: module must be 1 or 2"));
    return;
  }

  frequency = f.toFloat();
  setrxbw   = bw.toFloat();
  mod       = m.toInt();
  deviation = dev.toFloat();
  datarate  = dr.toInt();

  if (tmp_module == "1") {
    ELECHOUSE_cc1101.setModul(0);
    ELECHOUSE_cc1101.Init();
  } else {
    ELECHOUSE_cc1101.setModul(1);
    ELECHOUSE_cc1101.Init();
  }

  if (mod == 2) {
    ELECHOUSE_cc1101.setDcFilterOff(0);
  } else if (mod == 0) {
    ELECHOUSE_cc1101.setDcFilterOff(1);
    ELECHOUSE_cc1101.setDeviation(deviation);
  }

  ELECHOUSE_cc1101.setModulation(mod);
  ELECHOUSE_cc1101.setMHZ(frequency);
  ELECHOUSE_cc1101.setSyncMode(0);
  ELECHOUSE_cc1101.setPktFormat(3);
  ELECHOUSE_cc1101.setRxBW(setrxbw);
  ELECHOUSE_cc1101.setDRate(datarate);
  enableReceive();
  raw_rx = "1";

  Serial.print(F("OK rx: module ")); Serial.print(tmp_module);
  Serial.print(F(" @ ")); Serial.print(frequency, 2);
  Serial.println(F(" MHz - listening (results stream when echo=on)"));
}

// Mirrors the old /stoprx handler.
static void cliStopRx() {
  ELECHOUSE_cc1101.setModul(0);
  ELECHOUSE_cc1101.setSidle();
  ELECHOUSE_cc1101.setModul(1);
  ELECHOUSE_cc1101.setSidle();
  raw_rx = "0";
  Serial.println(F("OK stoprx"));
}

// Mirrors the old /settx handler.
static void cliTx(const String &line) {
  tmp_module     = tok(line, 1);
  String f       = tok(line, 2);
  String m       = tok(line, 3);
  String dev     = tok(line, 4);
  String rawdata = restFrom(line, 5);

  if (tmp_module == "" || f == "" || m == "" || dev == "" || rawdata == "") {
    Serial.println(F("ERR tx: usage tx <module> <freq> <mod> <deviation> <raw_csv>"));
    return;
  }
  if (tmp_module != "1" && tmp_module != "2") {
    Serial.println(F("ERR tx: module must be 1 or 2"));
    return;
  }

  transmit  = rawdata;
  frequency = f.toFloat();
  deviation = dev.toFloat();
  mod       = m.toInt();

  int counter = 0;
  int pos = 0;
  for (int i = 0; i < transmit.length(); i++) {
    if (transmit.substring(i, i + 1) == ",") {
      if (counter >= 2000) break;
      data_to_send[counter++] = transmit.substring(pos, i).toInt();
      pos = i + 1;
    }
  }
  if (pos < transmit.length() && counter < 2000)
    data_to_send[counter++] = transmit.substring(pos).toInt();

  int tx_pin = (tmp_module == "1") ? tx_pin1 : tx_pin2;
  ELECHOUSE_cc1101.setModul((tmp_module == "1") ? 0 : 1);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setModulation(mod);
  ELECHOUSE_cc1101.setMHZ(frequency);
  ELECHOUSE_cc1101.setDeviation(deviation);
  ELECHOUSE_cc1101.SetTx();
  pinMode(tx_pin, OUTPUT);

  for (int i = 0; i + 1 < counter; i += 2) {
    digitalWrite(tx_pin, HIGH);
    delayMicroseconds(data_to_send[i]);
    digitalWrite(tx_pin, LOW);
    delayMicroseconds(data_to_send[i + 1]);
  }

  ELECHOUSE_cc1101.setSidle();
  Serial.print(F("OK tx: sent ")); Serial.print(counter);
  Serial.println(F(" edges"));
}

// Mirrors the old /setjammer handler.
static void cliJammer(const String &line) {
  tmp_module = tok(line, 1);
  String f   = tok(line, 2);
  String p   = tok(line, 3);

  if (tmp_module == "" || f == "" || p == "") {
    Serial.println(F("ERR jammer: usage jammer <module> <freq> <power>"));
    return;
  }
  if (tmp_module != "1" && tmp_module != "2") {
    Serial.println(F("ERR jammer: module must be 1 or 2"));
    return;
  }

  frequency = f.toFloat();
  int power_jammer = p.toInt();

  int moduleIndex = (tmp_module == "1") ? 0 : 1;
  int tx_pin = (tmp_module == "1") ? tx_pin1 : tx_pin2;

  pinMode(tx_pin, OUTPUT);
  ELECHOUSE_cc1101.setModul(moduleIndex);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setMHZ(frequency);
  ELECHOUSE_cc1101.setPA(power_jammer);
  ELECHOUSE_cc1101.SetTx();
  jammer_tx = "1";

  Serial.print(F("OK jammer: module ")); Serial.print(tmp_module);
  Serial.print(F(" @ ")); Serial.print(frequency, 2);
  Serial.println(F(" MHz - running (stopjammer to halt)"));
}

// Mirrors the old /stopjammer handler.
static void cliStopJammer() {
  ELECHOUSE_cc1101.setModul(0);
  ELECHOUSE_cc1101.setSidle();
  ELECHOUSE_cc1101.setModul(1);
  ELECHOUSE_cc1101.setSidle();
  jammer_tx = "0";
  Serial.println(F("OK stopjammer"));
}

static void cliEcho(const String &line) {
  String v = tok(line, 1);
  if (v == "on")  { serialRxEcho = true;  Serial.println(F("OK echo on")); }
  else if (v == "off") { serialRxEcho = false; Serial.println(F("OK echo off")); }
  else Serial.println(F("ERR echo: usage echo on|off"));
}

// ============================================================================
//  Extended features: CC1101 presets, RSSI scanner, Flipper .sub file I/O
// ============================================================================

static const Preset PRESETS[] = {
  {"AM270",  2, 270.83,  0.00, 5},
  {"AM650",  2, 650.00,  0.00, 5},
  {"FM238",  0, 270.83,  2.38, 5},
  {"FM4768", 0, 270.83, 47.61, 5},
};
static const int PRESET_COUNT = sizeof(PRESETS) / sizeof(PRESETS[0]);

// Standard sub-GHz frequencies to sweep (MHz).
static const float SCAN_FREQS[] = {
  300.00, 303.87, 304.25, 315.00, 318.00, 390.00, 418.00,
  433.07, 433.92, 434.42, 434.77, 438.90,
  868.30, 868.35, 868.86, 868.95, 915.00, 925.00,
};
static const int SCAN_FREQ_COUNT = sizeof(SCAN_FREQS) / sizeof(SCAN_FREQS[0]);

static const Preset *findPreset(const String &name) {
  for (int i = 0; i < PRESET_COUNT; i++)
    if (name.equalsIgnoreCase(PRESETS[i].name)) return &PRESETS[i];
  return nullptr;
}

// Flipper preset string for a .sub header, from modulation code.
static const char *subPresetName(int m) {
  return (m == 2) ? "FuriHalSubGhzPresetOok650Async"
                  : "FuriHalSubGhzPreset2FSKDev476Async";
}

static void cliPresets() {
  Serial.println(F("presets:"));
  for (int i = 0; i < PRESET_COUNT; i++) {
    Serial.print(F("  "));
    Serial.print(PRESETS[i].name);
    Serial.print(F("  mod=")); Serial.print(PRESETS[i].mod);
    Serial.print(F(" rxbw=")); Serial.print(PRESETS[i].rxbw);
    Serial.print(F(" dev="));  Serial.print(PRESETS[i].deviation);
    Serial.print(F(" dr="));   Serial.println(PRESETS[i].datarate);
  }
}

// rxp <module> <freq> <preset>
static void cliRxPreset(const String &line) {
  String mods = tok(line, 1);
  String f    = tok(line, 2);
  String pn   = tok(line, 3);
  const Preset *p = findPreset(pn);
  if (mods == "" || f == "" || p == nullptr) {
    Serial.println(F("ERR rxp: usage rxp <module> <freq> <preset>  (see 'presets')"));
    return;
  }
  // Build an equivalent rx command line and reuse the validated rx path.
  cliRx("rx " + mods + " " + f + " " + String(p->mod) + " " +
        String(p->rxbw) + " " + String(p->deviation) + " " + String(p->datarate));
}

// txp <module> <freq> <preset> <raw_csv>
static void cliTxPreset(const String &line) {
  String mods = tok(line, 1);
  String f    = tok(line, 2);
  String pn   = tok(line, 3);
  String raw  = restFrom(line, 4);
  const Preset *p = findPreset(pn);
  if (mods == "" || f == "" || p == nullptr || raw == "") {
    Serial.println(F("ERR txp: usage txp <module> <freq> <preset> <raw_csv>"));
    return;
  }
  cliTx("tx " + mods + " " + f + " " + String(p->mod) + " " +
        String(p->deviation) + " " + raw);
}

// scan [module] [rssi_threshold_dbm]   default module 1, threshold -75
static void cliScan(const String &line) {
  String ms = tok(line, 1);
  String ts = tok(line, 2);
  int module = (ms == "2") ? 1 : 0;       // ELECHOUSE module index
  int threshold = (ts != "") ? ts.toInt() : -75;

  // Stop any active RX so the scan owns the radio.
  ELECHOUSE_cc1101.setModul(0); ELECHOUSE_cc1101.setSidle();
  ELECHOUSE_cc1101.setModul(1); ELECHOUSE_cc1101.setSidle();
  raw_rx = "0";

  Serial.print(F("--- scan (module "));
  Serial.print(ms == "2" ? "2" : "1");
  Serial.print(F(", threshold ")); Serial.print(threshold);
  Serial.println(F(" dBm) ---"));

  ELECHOUSE_cc1101.setModul(module);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setModulation(2);      // OOK for energy detection
  ELECHOUSE_cc1101.setRxBW(650.0);

  for (int i = 0; i < SCAN_FREQ_COUNT; i++) {
    ELECHOUSE_cc1101.setMHZ(SCAN_FREQS[i]);
    ELECHOUSE_cc1101.SetRx();
    delay(15);
    int rssi = ELECHOUSE_cc1101.getRssi();
    Serial.print(F("  "));
    Serial.print(SCAN_FREQS[i], 2);
    Serial.print(F(" MHz  rssi="));
    Serial.print(rssi);
    if (rssi >= threshold) Serial.print(F("  *ACTIVE*"));
    Serial.println();
  }
  ELECHOUSE_cc1101.setSidle();
  Serial.println(F("OK scan"));
}

static void ensureSubghzDir() {
  if (!SD.exists("/SUBGHZ")) SD.mkdir("/SUBGHZ");
}

// subsave <name>   - write the last decoded RX frame as a Flipper .sub
static void cliSubSave(const String &line) {
  String name = tok(line, 1);
  if (name == "") { Serial.println(F("ERR subsave: usage subsave <name>")); return; }
  int n = lastIndex + 1;                  // corrected sample count
  if (n <= 0) { Serial.println(F("ERR subsave: no RX frame captured yet")); return; }

  ensureSubghzDir();
  String path = "/SUBGHZ/" + name + ".sub";
  if (SD.exists(path)) SD.remove(path);
  File fp = SD.open(path, FILE_WRITE);
  if (!fp) { Serial.println(F("ERR subsave: cannot open file (SD present?)")); return; }

  fp.println("Filetype: Flipper SubGhz RAW File");
  fp.println("Version: 1");
  fp.print("Frequency: "); fp.println((unsigned long)(frequency * 1000000.0));
  fp.print("Preset: ");    fp.println(subPresetName(mod));
  fp.println("Protocol: RAW");
  fp.print("RAW_Data: ");
  for (int i = 0; i < n; i++) {
    long v = (long)samplesmooth[i];
    if (i & 1) v = -v;                    // odd index = low (negative)
    fp.print(v);
    if (i < n - 1) fp.print(' ');
  }
  fp.println();
  fp.close();

  Serial.print(F("OK subsave: ")); Serial.print(path);
  Serial.print(F(" (")); Serial.print(n); Serial.println(F(" samples)"));
}

static void cliSubList() {
  ensureSubghzDir();
  File dir = SD.open("/SUBGHZ");
  if (!dir) { Serial.println(F("ERR sublist: cannot open /SUBGHZ")); return; }
  Serial.println(F("--- .sub files ---"));
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (!f.isDirectory()) {
      Serial.print(F("  "));
      Serial.print(f.name());
      Serial.print(F("  ("));
      Serial.print(f.size());
      Serial.println(F(" bytes)"));
    }
    f.close();
  }
  dir.close();
  Serial.println(F("OK sublist"));
}

static void cliSubDel(const String &line) {
  String name = tok(line, 1);
  if (name == "") { Serial.println(F("ERR subdel: usage subdel <name>")); return; }
  String path = "/SUBGHZ/" + name + ".sub";
  if (SD.remove(path)) Serial.println(F("OK subdel"));
  else Serial.println(F("ERR subdel: not found"));
}

// subsend <module> <name>  - load a .sub and transmit it
static void cliSubSend(const String &line) {
  String mods = tok(line, 1);
  String name = tok(line, 2);
  if (mods == "" || name == "") {
    Serial.println(F("ERR subsend: usage subsend <module> <name>"));
    return;
  }
  if (mods != "1" && mods != "2") { Serial.println(F("ERR subsend: module 1 or 2")); return; }

  String path = "/SUBGHZ/" + name + ".sub";
  File fp = SD.open(path, FILE_READ);
  if (!fp) { Serial.println(F("ERR subsend: file not found")); return; }

  float subFreq = frequency;
  int   subMod  = 2;
  int   counter = 0;

  while (fp.available()) {
    String l = fp.readStringUntil('\n');
    l.trim();
    if (l.startsWith("Frequency:")) {
      subFreq = l.substring(10).toFloat() / 1000000.0;
    } else if (l.startsWith("Preset:")) {
      if (l.indexOf("2FSK") >= 0 || l.indexOf("Dev") >= 0) subMod = 0; else subMod = 2;
    } else if (l.startsWith("RAW_Data:")) {
      String data = l.substring(9);
      data.trim();
      int start = 0;
      while (start < data.length() && counter < 2000) {
        int sp = data.indexOf(' ', start);
        String tokv = (sp < 0) ? data.substring(start) : data.substring(start, sp);
        tokv.trim();
        if (tokv.length()) {
          long v = tokv.toInt();
          data_to_send[counter++] = v < 0 ? -v : v;   // TX loop alternates H/L
        }
        if (sp < 0) break;
        start = sp + 1;
      }
    }
  }
  fp.close();

  if (counter == 0) { Serial.println(F("ERR subsend: no RAW_Data")); return; }

  tmp_module = mods;
  frequency  = subFreq;
  mod        = subMod;
  int tx_pin = (mods == "1") ? tx_pin1 : tx_pin2;
  ELECHOUSE_cc1101.setModul((mods == "1") ? 0 : 1);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setModulation(mod);
  ELECHOUSE_cc1101.setMHZ(frequency);
  ELECHOUSE_cc1101.setDeviation(deviation);
  ELECHOUSE_cc1101.SetTx();
  pinMode(tx_pin, OUTPUT);
  for (int i = 0; i + 1 < counter; i += 2) {
    digitalWrite(tx_pin, HIGH);
    delayMicroseconds(data_to_send[i]);
    digitalWrite(tx_pin, LOW);
    delayMicroseconds(data_to_send[i + 1]);
  }
  ELECHOUSE_cc1101.setSidle();

  Serial.print(F("OK subsend: ")); Serial.print(name);
  Serial.print(F(" @ ")); Serial.print(frequency, 2);
  Serial.print(F(" MHz, ")); Serial.print(counter); Serial.println(F(" edges"));
}

// txbin <module> <freq> <preset> <te_us> <bits>
//   Transmit an OOK binary symbol stream. Each character is one symbol of
//   te_us microseconds; '1' = carrier on, '0' = carrier off. The bitstream is
//   run-length encoded into the high/low duration pairs the TX path expects.
static void cliTxBin(const String &line) {
  String mods = tok(line, 1);
  String f    = tok(line, 2);
  String pn   = tok(line, 3);
  String tes  = tok(line, 4);
  String bits = tok(line, 5);
  const Preset *p = findPreset(pn);
  if (mods == "" || f == "" || p == nullptr || tes == "" || bits == "") {
    Serial.println(F("ERR txbin: usage txbin <module> <freq> <preset> <te_us> <bits>"));
    return;
  }
  if (mods != "1" && mods != "2") { Serial.println(F("ERR txbin: module 1 or 2")); return; }

  long te = tes.toInt();
  if (te <= 0) { Serial.println(F("ERR txbin: te_us must be > 0")); return; }

  // RLE the bitstream into data_to_send[] (durations, starting with a HIGH run).
  int counter = 0;
  if (bits.length() && bits[0] == '0' && counter < 2000)
    data_to_send[counter++] = 0;          // zero-length leading high run
  int run = 0;
  char cur = bits.length() ? bits[0] : '1';
  for (int i = 0; i < bits.length(); i++) {
    if (bits[i] != '0' && bits[i] != '1') continue;
    if (bits[i] == cur) {
      run++;
    } else {
      if (counter < 2000) data_to_send[counter++] = (long)run * te;
      cur = bits[i];
      run = 1;
    }
  }
  if (run > 0 && counter < 2000) data_to_send[counter++] = (long)run * te;

  tmp_module = mods;
  frequency  = f.toFloat();
  mod        = p->mod;
  deviation  = p->deviation;
  int tx_pin = (mods == "1") ? tx_pin1 : tx_pin2;
  ELECHOUSE_cc1101.setModul((mods == "1") ? 0 : 1);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setModulation(mod);
  ELECHOUSE_cc1101.setMHZ(frequency);
  ELECHOUSE_cc1101.setDeviation(deviation);
  ELECHOUSE_cc1101.SetTx();
  pinMode(tx_pin, OUTPUT);
  for (int i = 0; i + 1 < counter; i += 2) {
    digitalWrite(tx_pin, HIGH);
    delayMicroseconds(data_to_send[i]);
    digitalWrite(tx_pin, LOW);
    delayMicroseconds(data_to_send[i + 1]);
  }
  ELECHOUSE_cc1101.setSidle();

  Serial.print(F("OK txbin: ")); Serial.print(bits.length());
  Serial.print(F(" symbols, te=")); Serial.print(te); Serial.println(F("us"));
}

// lastbin [te_us]
//   Print the last decoded RX frame as an OOK binary symbol string. te_us is
//   the symbol period; if omitted, the smallest captured duration is used.
static void cliLastBin(const String &line) {
  int n = lastIndex + 1;
  if (n <= 0) { Serial.println(F("ERR lastbin: no RX frame captured yet")); return; }

  String tes = tok(line, 1);
  long te = tes.toInt();
  if (te <= 0) {
    unsigned long mn = 0xFFFFFFFF;
    for (int i = 0; i < n; i++) if (samplesmooth[i] > 0 && samplesmooth[i] < mn) mn = samplesmooth[i];
    te = (mn == 0xFFFFFFFF) ? 1 : (long)mn;
  }

  String out = "";
  int symbols = 0;
  for (int i = 0; i < n; i++) {
    int reps = (int)((samplesmooth[i] + te / 2) / te);   // round to nearest symbol
    char level = (i & 1) ? '0' : '1';                     // even=high, odd=low
    for (int r = 0; r < reps; r++) { out += level; symbols++; }
  }
  Serial.print(F("Binary (te=")); Serial.print(te); Serial.print(F("us): "));
  Serial.println(out);
  Serial.print(F("Symbol: ")); Serial.println(symbols);
  Serial.println(F("OK lastbin"));
}

// ============================================================================
//  nRF24 receive-only channel scanner (requires soldered nRF24L01+ module)
// ============================================================================

// Initialize the nRF24 in pure listener mode: no pipes opened for read, TX
// disabled, low-noise config. We only ever query carrier-detect / RPD.
static bool nrfInit() {
  if (nrfPresent) return true;
  if (!nrf.begin()) return false;
  if (!nrf.isChipConnected()) return false;
  nrf.setAutoAck(false);
  nrf.disableCRC();
  nrf.setAddressWidth(2);
  nrf.setPayloadSize(4);
  nrf.setDataRate(RF24_2MBPS);
  nrf.setPALevel(RF24_PA_MIN);    // RX-only; PA level affects only TX
  nrf.stopListening();             // make sure TX path is idle
  nrfPresent = true;
  return true;
}

static void appendNrfLog(uint8_t ch, int hits) {
  File fp = SD.open("/NRF/scan.log", FILE_APPEND);
  if (!fp) {
    SD.mkdir("/NRF");
    fp = SD.open("/NRF/scan.log", FILE_APPEND);
    if (!fp) return;
  }
  fp.print(millis()); fp.print(',');
  fp.print(2400 + ch); fp.print("MHz,ch=");
  fp.print(ch); fp.print(",hits=");
  fp.println(hits);
  fp.close();
}

// nrfscan [start] [end] [dwell_us] [passes]
//   Sweep nRF24 channels start..end, dwelling dwell_us on each per pass,
//   counting how many passes detected a carrier on each channel. Active
//   channels are appended to /NRF/scan.log on the SD card.
//   Defaults: 0..125, 200us, 40 passes.
static void cliNrfScan(const String &line) {
  if (!nrfInit()) {
    Serial.println(F("ERR nrfscan: nRF24 not detected (check wiring/power)"));
    return;
  }
  int chStart = tok(line, 1) != "" ? tok(line, 1).toInt() : 0;
  int chEnd   = tok(line, 2) != "" ? tok(line, 2).toInt() : 125;
  int dwell   = tok(line, 3) != "" ? tok(line, 3).toInt() : 200;
  int passes  = tok(line, 4) != "" ? tok(line, 4).toInt() : 40;
  if (chStart < 0) chStart = 0;
  if (chEnd > 125) chEnd = 125;
  if (chStart > chEnd) { Serial.println(F("ERR nrfscan: start > end")); return; }

  int width = chEnd - chStart + 1;
  static uint8_t hits[126];
  for (int i = 0; i < width; i++) hits[i] = 0;

  Serial.print(F("--- nrf scan ch ")); Serial.print(chStart);
  Serial.print(F("..")); Serial.print(chEnd);
  Serial.print(F(" dwell=")); Serial.print(dwell);
  Serial.print(F("us passes=")); Serial.print(passes); Serial.println(F(" ---"));

  for (int p = 0; p < passes; p++) {
    for (int c = chStart; c <= chEnd; c++) {
      nrf.setChannel(c);
      nrf.startListening();
      delayMicroseconds(dwell);
      bool carrier = nrf.testRPD();    // received power detect
      nrf.stopListening();
      if (carrier && hits[c - chStart] < 255) hits[c - chStart]++;
    }
    // keep the serial responsive between passes
    if ((p & 0x07) == 0) processSerial();
  }

  int active = 0;
  for (int c = chStart; c <= chEnd; c++) {
    uint8_t h = hits[c - chStart];
    if (h == 0) continue;
    active++;
    Serial.print(F("  ch=")); Serial.print(c);
    Serial.print(F(" (")); Serial.print(2400 + c); Serial.print(F(" MHz)  hits="));
    Serial.print(h); Serial.print(F("/")); Serial.println(passes);
    appendNrfLog(c, h);
  }
  Serial.print(F("OK nrfscan: ")); Serial.print(active); Serial.println(F(" active channel(s)"));
}

// nrflog                show the saved scan log
// nrflog clear          erase it
static void cliNrfLog(const String &line) {
  String arg = tok(line, 1);
  if (arg == "clear") {
    if (SD.exists("/NRF/scan.log")) SD.remove("/NRF/scan.log");
    Serial.println(F("OK nrflog: cleared"));
    return;
  }
  File fp = SD.open("/NRF/scan.log", FILE_READ);
  if (!fp) { Serial.println(F("(no log yet)")); Serial.println(F("OK nrflog")); return; }
  Serial.println(F("--- /NRF/scan.log ---"));
  while (fp.available()) Serial.write(fp.read());
  fp.close();
  Serial.println(F("OK nrflog"));
}

// ============================================================================
//  BLE scanner (built-in ESP32 radio, no extra hardware)
// ============================================================================

static bool bleReady   = false;

static void bleEnsureInit() {
  if (bleReady) return;
  // Central-only init: empty name and explicit advertising stop so the
  // device never broadcasts itself or accepts incoming connections.
  // (We only use the scan + client APIs - peripheral role stays dormant.)
  BLEDevice::init("");
  BLEDevice::getAdvertising()->stop();
  bleReady = true;
}

static void appendBleLog(const String &line) {
  if (!SD.exists("/BLE")) SD.mkdir("/BLE");
  File fp = SD.open("/BLE/scan.log", FILE_APPEND);
  if (!fp) return;
  fp.print(millis()); fp.print(',');
  fp.println(line);
  fp.close();
}

// blescan [seconds]   passive BLE advertisement scan, default 8s
static void cliBleScan(const String &line) {
  bleEnsureInit();
  int secs = tok(line, 1) != "" ? tok(line, 1).toInt() : 8;
  if (secs < 1) secs = 1;
  if (secs > 60) secs = 60;

  BLEScan *scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);

  Serial.print(F("--- blescan ")); Serial.print(secs); Serial.println(F("s ---"));
  BLEScanResults *results = scan->start(secs, false);
  int n = results->getCount();
  for (int i = 0; i < n; i++) {
    BLEAdvertisedDevice d = results->getDevice(i);
    String mac  = String(d.getAddress().toString().c_str());
    String name = d.haveName() ? String(d.getName().c_str()) : String("no_name");
    int    rssi = d.getRSSI();
    String svcs = "";
    if (d.haveServiceUUID()) {
      for (int s = 0; s < d.getServiceUUIDCount(); s++) {
        if (svcs.length()) svcs += "|";
        svcs += String(d.getServiceUUID(s).toString().c_str());
      }
    }
    Serial.print(F("  "));
    Serial.print(mac); Serial.print(F("  rssi=")); Serial.print(rssi);
    Serial.print(F("  name=")); Serial.print(name);
    if (svcs.length()) { Serial.print(F("  svcs=")); Serial.print(svcs); }
    Serial.println();

    String row = mac + ",rssi=" + String(rssi) + ",name=" + name;
    if (svcs.length()) row += ",svcs=" + svcs;
    appendBleLog(row);
  }
  scan->clearResults();
  Serial.print(F("OK blescan: ")); Serial.print(n); Serial.println(F(" device(s)"));
}

// blegatt <MAC>   connect once, list services + characteristics, then disconnect
static void cliBleGatt(const String &line) {
  bleEnsureInit();
  String mac = tok(line, 1);
  if (mac == "") { Serial.println(F("ERR blegatt: usage blegatt <MAC>")); return; }

  // If a persistent connection is open to this peer already, reuse it.
  // Otherwise open a transient client just for the enumeration.
  bool transient = false;
  BLEClient *client = nullptr;
  if (bleClient && bleClient->isConnected() && bleConnectedMac == mac) {
    client = bleClient;
  } else {
    client = BLEDevice::createClient();
    transient = true;
    Serial.print(F("--- gatt for ")); Serial.print(mac); Serial.println(F(" ---"));
    BLEAddress addr(mac.c_str());
    if (!client->connect(addr)) {
      Serial.println(F("ERR blegatt: connect failed"));
      delete client;
      return;
    }
    // Let the GATTC task finish discovery before we walk the service map.
    // Without this, iterating the std::map can race with population and
    // trip an internal xQueueGenericSend assert in the BLE stack.
    delay(800);
  }

  std::map<std::string, BLERemoteService*> *svcs = client->getServices();
  int sCount = 0, cCount = 0;
  if (svcs && !svcs->empty()) {
    for (auto &kv : *svcs) {
      if (!kv.second) continue;
      sCount++;
      Serial.print(F("  service ")); Serial.println(kv.first.c_str());

      std::map<std::string, BLERemoteCharacteristic*> *chars =
          kv.second->getCharacteristics();
      if (!chars || chars->empty()) continue;
      for (auto &ckv : *chars) {
        BLERemoteCharacteristic *rc = ckv.second;
        if (!rc) continue;
        cCount++;
        Serial.print(F("    char ")); Serial.print(ckv.first.c_str());
        Serial.print(F("  props="));
        if (rc->canRead())             Serial.print(F("R"));
        if (rc->canWrite())            Serial.print(F("W"));
        if (rc->canWriteNoResponse())  Serial.print(F("w"));
        if (rc->canNotify())           Serial.print(F("N"));
        if (rc->canIndicate())         Serial.print(F("I"));
        Serial.println();
      }
    }
  }

  if (transient) {
    // Disconnect cleanly, then let the BLE stack's housekeeping queues
    // drain (CCCD writes, descriptor cleanup, etc.) before destroying
    // the client. Skipping this trips an internal prvNotifyQueueSetContainer
    // assert on peers with many characteristics.
    if (client->isConnected()) client->disconnect();
    delay(500);
    delete client;
  }

  appendBleLog("gatt," + mac + ",services=" + String(sCount) + ",chars=" + String(cCount));
  Serial.print(F("OK blegatt: ")); Serial.print(sCount);
  Serial.print(F(" service(s), ")); Serial.print(cCount); Serial.println(F(" characteristic(s)"));
}

// blelog [clear]
static void cliBleLog(const String &line) {
  if (tok(line, 1) == "clear") {
    if (SD.exists("/BLE/scan.log")) SD.remove("/BLE/scan.log");
    Serial.println(F("OK blelog: cleared")); return;
  }
  File fp = SD.open("/BLE/scan.log", FILE_READ);
  if (!fp) { Serial.println(F("(no log yet)")); Serial.println(F("OK blelog")); return; }
  Serial.println(F("--- /BLE/scan.log ---"));
  while (fp.available()) Serial.write(fp.read());
  fp.close();
  Serial.println(F("OK blelog"));
}

// ============================================================================
//  BLE GATT client - persistent connection for protocol implementation
// ============================================================================

static void cliBleDisconnect() {
  if (bleClient) {
    if (bleClient->isConnected()) bleClient->disconnect();
    delete bleClient;
    bleClient = nullptr;
  }
  bleConnectedMac = "";
  bleNotifyStream = false;
  Serial.println(F("OK bledisconnect"));
}

// bleconnect <MAC>
static void cliBleConnect(const String &line) {
  bleEnsureInit();
  String mac = tok(line, 1);
  if (mac == "") { Serial.println(F("ERR bleconnect: usage bleconnect <MAC>")); return; }

  if (bleClient) {
    if (bleClient->isConnected()) bleClient->disconnect();
    delete bleClient;
    bleClient = nullptr;
  }
  bleClient = BLEDevice::createClient();
  BLEAddress addr(mac.c_str());
  if (!bleClient->connect(addr)) {
    Serial.println(F("ERR bleconnect: connect failed"));
    delete bleClient;
    bleClient = nullptr;
    return;
  }
  bleConnectedMac = mac;
  Serial.print(F("OK bleconnect: ")); Serial.print(mac);
  Serial.print(F("  mtu=")); Serial.println(bleClient->getMTU());
}

// Resolve service+characteristic UUIDs from the active connection. Returns
// nullptr and prints ERR if not found.
static BLERemoteCharacteristic* bleResolveChar(const String &svc, const String &chr) {
  if (!bleClient || !bleClient->isConnected()) {
    Serial.println(F("ERR not connected (use bleconnect first)"));
    return nullptr;
  }
  BLERemoteService *rs = bleClient->getService(BLEUUID(svc.c_str()));
  if (!rs) { Serial.println(F("ERR service not found")); return nullptr; }
  BLERemoteCharacteristic *rc = rs->getCharacteristic(BLEUUID(chr.c_str()));
  if (!rc) { Serial.println(F("ERR characteristic not found")); return nullptr; }
  return rc;
}

static void printHexLine(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (data[i] < 0x10) Serial.print('0');
    Serial.print(data[i], HEX);
  }
}

static int hexCharVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

// Parse hex string (with optional spaces / colons / 0x prefix) into bytes.
// Returns number of bytes written, or -1 on parse error.
static int parseHex(const String &s, uint8_t *out, size_t cap) {
  size_t n = 0;
  int hi = -1;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == ' ' || c == ':' || c == '-' || c == ',') continue;
    if (c == '0' && i + 1 < s.length() && (s[i+1] == 'x' || s[i+1] == 'X')) { i++; continue; }
    int v = hexCharVal(c);
    if (v < 0) return -1;
    if (hi < 0) { hi = v; }
    else {
      if (n >= cap) return -1;
      out[n++] = (uint8_t)((hi << 4) | v);
      hi = -1;
    }
  }
  if (hi >= 0) return -1;     // odd nibble count
  return (int)n;
}

// bleread <service_uuid> <char_uuid>
static void cliBleRead(const String &line) {
  String svc = tok(line, 1), chr = tok(line, 2);
  if (svc == "" || chr == "") {
    Serial.println(F("ERR bleread: usage bleread <service_uuid> <char_uuid>"));
    return;
  }
  BLERemoteCharacteristic *rc = bleResolveChar(svc, chr);
  if (!rc) return;
  if (!rc->canRead()) { Serial.println(F("ERR bleread: not readable")); return; }

  String value = rc->readValue();
  Serial.print(F("OK bleread: len=")); Serial.print(value.length());
  Serial.print(F(" hex="));
  printHexLine((const uint8_t*)value.c_str(), value.length());
  Serial.println();
}

// blewrite <service_uuid> <char_uuid> <hex_bytes>      (write with response)
// blewriten <service_uuid> <char_uuid> <hex_bytes>     (write without response)
static void cliBleWriteImpl(const String &line, bool withResponse) {
  String svc = tok(line, 1), chr = tok(line, 2);
  String hex = restFrom(line, 3);
  if (svc == "" || chr == "" || hex == "") {
    Serial.println(F("ERR blewrite: usage blewrite[n] <service_uuid> <char_uuid> <hex_bytes>"));
    return;
  }
  BLERemoteCharacteristic *rc = bleResolveChar(svc, chr);
  if (!rc) return;
  if (withResponse && !rc->canWrite()) { Serial.println(F("ERR blewrite: not writable")); return; }
  if (!withResponse && !rc->canWriteNoResponse()) { Serial.println(F("ERR blewriten: not writable without response")); return; }

  uint8_t buf[244];        // ATT_MTU - 3, max useful BLE payload
  int n = parseHex(hex, buf, sizeof(buf));
  if (n < 0) { Serial.println(F("ERR blewrite: bad hex (or too long)")); return; }

  rc->writeValue(buf, (size_t)n, withResponse);
  Serial.print(F("OK ")); Serial.print(withResponse ? F("blewrite") : F("blewriten"));
  Serial.print(F(": wrote ")); Serial.print(n); Serial.println(F(" byte(s)"));
}

static void cliBleWrite(const String &line)  { cliBleWriteImpl(line, true); }
static void cliBleWriteN(const String &line) { cliBleWriteImpl(line, false); }

// blesub <service_uuid> <char_uuid>      subscribe to notifications/indications
static void cliBleSub(const String &line) {
  String svc = tok(line, 1), chr = tok(line, 2);
  if (svc == "" || chr == "") {
    Serial.println(F("ERR blesub: usage blesub <service_uuid> <char_uuid>"));
    return;
  }
  BLERemoteCharacteristic *rc = bleResolveChar(svc, chr);
  if (!rc) return;
  if (!rc->canNotify() && !rc->canIndicate()) {
    Serial.println(F("ERR blesub: characteristic does not notify/indicate"));
    return;
  }
  rc->registerForNotify(bleNotifyCallback);
  bleNotifyStream = true;
  Serial.println(F("OK blesub: streaming notifications (NOTIF lines)"));
}

// bleunsub <service_uuid> <char_uuid>
static void cliBleUnsub(const String &line) {
  String svc = tok(line, 1), chr = tok(line, 2);
  if (svc == "" || chr == "") {
    Serial.println(F("ERR bleunsub: usage bleunsub <service_uuid> <char_uuid>"));
    return;
  }
  BLERemoteCharacteristic *rc = bleResolveChar(svc, chr);
  if (!rc) return;
  rc->registerForNotify(nullptr);
  Serial.println(F("OK bleunsub"));
}

// blemtu [new_mtu]   show or request a new MTU (server may cap it)
static void cliBleMtu(const String &line) {
  if (!bleClient || !bleClient->isConnected()) {
    Serial.println(F("ERR blemtu: not connected"));
    return;
  }
  String n = tok(line, 1);
  if (n != "") {
    uint16_t want = (uint16_t)n.toInt();
    BLEDevice::setMTU(want);
  }
  Serial.print(F("OK blemtu: ")); Serial.println(bleClient->getMTU());
}

// blestatus
static void cliBleStatus() {
  Serial.print(F("ble_connected: "));
  Serial.println((bleClient && bleClient->isConnected()) ? F("true") : F("false"));
  if (bleClient && bleClient->isConnected()) {
    Serial.print(F("ble_peer:      ")); Serial.println(bleConnectedMac);
    Serial.print(F("ble_mtu:       ")); Serial.println(bleClient->getMTU());
  }
  Serial.print(F("ble_stream:    ")); Serial.println(bleNotifyStream ? F("on") : F("off"));
}

// Drain any queued notifications to the serial line. Called from loop().
static void bleDrainNotifications() {
  while (bleNotifTail != bleNotifHead) {
    BleNotif *slot = (BleNotif*)&bleNotifQ[bleNotifTail];
    Serial.print(F("NOTIF t=")); Serial.print(slot->ts);
    Serial.print(F(" uuid=")); Serial.print(slot->uuid);
    Serial.print(F(" len=")); Serial.print((unsigned)slot->len);
    Serial.print(F(" hex="));
    printHexLine(slot->data, slot->len);
    Serial.println();
    bleNotifTail = (bleNotifTail + 1) % BLE_NOTIF_QUEUE;
  }
}

static void dispatch(String line) {
  line.trim();
  if (line.length() == 0) return;

  String cmd = tok(line, 0);
  cmd.toLowerCase();

  if      (cmd == "help" || cmd == "?") cliHelp();
  else if (cmd == "status")             cliStatus();
  else if (cmd == "rx")                 cliRx(line);
  else if (cmd == "stoprx")             cliStopRx();
  else if (cmd == "tx")                 cliTx(line);
  else if (cmd == "jammer")             cliJammer(line);
  else if (cmd == "stopjammer")         cliStopJammer();
  else if (cmd == "echo")               cliEcho(line);
  else if (cmd == "presets")            cliPresets();
  else if (cmd == "rxp")                cliRxPreset(line);
  else if (cmd == "txp")                cliTxPreset(line);
  else if (cmd == "scan")               cliScan(line);
  else if (cmd == "subsave")            cliSubSave(line);
  else if (cmd == "sublist")            cliSubList();
  else if (cmd == "subsend")            cliSubSend(line);
  else if (cmd == "subdel")             cliSubDel(line);
  else if (cmd == "txbin")              cliTxBin(line);
  else if (cmd == "lastbin")            cliLastBin(line);
  else if (cmd == "nrfscan")            cliNrfScan(line);
  else if (cmd == "nrflog")             cliNrfLog(line);
  else if (cmd == "blescan")            cliBleScan(line);
  else if (cmd == "blegatt")            cliBleGatt(line);
  else if (cmd == "blelog")             cliBleLog(line);
  else if (cmd == "bleconnect")         cliBleConnect(line);
  else if (cmd == "bledisconnect")      cliBleDisconnect();
  else if (cmd == "bleread")            cliBleRead(line);
  else if (cmd == "blewrite")           cliBleWrite(line);
  else if (cmd == "blewriten")          cliBleWriteN(line);
  else if (cmd == "blesub")             cliBleSub(line);
  else if (cmd == "bleunsub")           cliBleUnsub(line);
  else if (cmd == "blemtu")             cliBleMtu(line);
  else if (cmd == "blestatus")          cliBleStatus();
  else if (cmd == "reboot")             { Serial.println(F("OK reboot")); delay(150); ESP.restart(); }
  else { Serial.print(F("ERR unknown command: ")); Serial.println(cmd); Serial.println(F("type 'help'")); }
}

void processSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      dispatch(serialLine);
      serialLine = "";
    } else {
      serialLine += c;
      if (serialLine.length() > 16000) serialLine = "";
    }
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(300);

  // MicroSD (used for /logs.txt; gracefully no-ops if no card present)
  sdspi.begin(18, 19, 23, 22);
  SD.begin(22, sdspi);

  // Radios
  ELECHOUSE_cc1101.addSpiPin(sck_pin, miso_pin, mosi_pin, cs_pin1, 0);
  ELECHOUSE_cc1101.addSpiPin(sck_pin, miso_pin, mosi_pin, cs_pin2, 1);
  enableReceive();

  Serial.println();
  Serial.print(F("Evil Crow RF V2 v"));
  Serial.print(EVILCROW_FW_VERSION);
  Serial.println(F(" (serial mode). Type 'help'."));
}

void loop() {
  processSerial();
  bleDrainNotifications();

  if (raw_rx == "1") {
    if (checkReceived()) {
      printReceived();
      signalanalyse();
      if (serialRxEcho) {
        Serial.println(OutputLog);
        Serial.println(F("--- end frame ---"));
      }
      enableReceive();
      delay(700);
    }
  }

  if (jammer_tx == "1") {
    if (tmp_module == "1") {
      for (int i = 0; i + 1 < jammer_len; i += 2) {
        digitalWrite(tx_pin1, HIGH);
        delayMicroseconds(jammer[i]);
        digitalWrite(tx_pin1, LOW);
        delayMicroseconds(jammer[i + 1]);
      }
    } else if (tmp_module == "2") {
      for (int i = 0; i + 1 < jammer_len; i += 2) {
        digitalWrite(tx_pin2, HIGH);
        delayMicroseconds(jammer[i]);
        digitalWrite(tx_pin2, LOW);
        delayMicroseconds(jammer[i + 1]);
      }
    }
  }
}
