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

#define SERIAL_BAUD 115200

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

// CC1101 preset descriptor (defined here, before the first function, so the
// Arduino IDE's auto-generated prototypes can see the type).
struct Preset {
  const char *name;
  int   mod;        // 2 = ASK/OOK, 0 = 2-FSK
  float rxbw;       // kHz
  float deviation;  // kHz
  int   datarate;   // kBaud
};

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
  Serial.println(F("  reboot"));
  Serial.println(F("Transmitting / jamming RF may be regulated where you are - stay within the law."));
}

static void cliStatus() {
  bool sd_present = SD.cardType() != CARD_NONE;
  Serial.println(F("--- status ---"));
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
  int power_jammer = p.toInt(); //Set TxPower. The following settings are possible depending on the frequency band.  (-30  -20  -15  -10  -6    0    5    7    10   11   12)
  
  if (power_jammer != -30 && \
     power_jammer != -20 && \
     power_jammer != -15 && \
     power_jammer != -6 && \
     power_jammer != 0 && \
     power_jammer != 5 &&
     power_jammer != 7 && \
     power_jammer != 10 && \
     power_jammer != 11 && \
     power_jammer != 12) {
    Serial.println(F("ERR jammer: power outside of possible values : -30, -20, -15, -10, -6, 0, 5, 7, 10, 11, 12"));
    return;     
  }

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
  Serial.println(F("Evil Crow RF V2 ready (serial mode). Type 'help'."));
}

void loop() {
  processSerial();

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
