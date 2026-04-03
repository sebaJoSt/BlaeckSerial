/*
  FakeMasterWithSlaves.ino

  Single-board Arduino sketch that emulates a BlaeckSerial master with
  two I2C slaves — no real slave hardware needed.  Speaks the raw
  BlaeckSerial D2 protocol (B0, B3, D2) so blaecktcpy hubs and Loggbok
  can't tell the difference.

  Device tree as seen by Loggbok / blaecktcpy:
    Fake Master       (master, SlaveID 0)  — MasterVoltage, Uptime
    ├── TempSensor    (slave,  SlaveID 8)  — Temperature, Humidity
    └── PressureSensor(slave,  SlaveID 42) — Pressure

  Upload to any Arduino and connect via Serial or blaecktcpy:
    hub.add_serial("COM3", 115200)

  Custom commands (send via blaecktcpy or Serial Monitor):
    <BLAECK.FAIL_SLAVE,8>     — simulate slave 8 I2C failure
    <BLAECK.RESTORE_SLAVE,8>  — bring slave 8 back online

  Requires: CRC32 library (by Rob Tillaart, Arduino Library Manager)
*/

#include <Arduino.h>
#include <CRC32.h>

// ── Protocol constants ──────────────────────────────────────────────
static const byte MSG_KEY_B0 = 0xB0; // Symbol list
static const byte MSG_KEY_B3 = 0xB3; // Device info
static const byte MSG_KEY_D2 = 0xD2; // Data frame (D2)
static const byte MSG_KEY_C0 = 0xC0; // Restarted

// ── Fake signal storage ─────────────────────────────────────────────
// Master signals (SlaveID 0)
float masterVoltage = 3.3;
unsigned long uptimeSeconds = 0;
// Slave 8 signals
float temperature = 22.0;
float humidity = 55.0;
// Slave 42 signals
float pressure = 1013.25;

// ── Slave failure simulation ────────────────────────────────────────
bool slave8Failed = false;
bool slave42Failed = false;

// ── Timed data ──────────────────────────────────────────────────────
bool timedActive = false;
unsigned long timedInterval = 1000;
unsigned long timedNextSend = 0;
bool firstTimedSend = true;

// ── Restart flag ────────────────────────────────────────────────────
bool sendRestartFlag = true;

// ── Schema hash (CRC16-CCITT, init=0, poly=0x1021) ─────────────────
// Hash covers ALL registered signals (local + slave), matching the
// add-only I2C scan design: slave failures are transient, reported via
// StatusByte=1, and do NOT change the schema.
uint16_t schemaHash = 0;

uint16_t crc16_ccitt_byte(uint16_t crc, byte b)
{
  crc ^= ((uint16_t)b << 8);
  for (byte k = 0; k < 8; k++)
  {
    if (crc & 0x8000)
      crc = (crc << 1) ^ 0x1021;
    else
      crc <<= 1;
  }
  return crc;
}

uint16_t hashSignal(uint16_t crc, const char *name, byte dtCode)
{
  while (*name)
    crc = crc16_ccitt_byte(crc, (byte)*name++);
  crc = crc16_ccitt_byte(crc, dtCode);
  return crc;
}

void computeSchemaHash()
{
  uint16_t h = 0;
  h = hashSignal(h, "MasterVoltage", 0x08);
  h = hashSignal(h, "Uptime", 0x07);
  h = hashSignal(h, "Temperature", 0x08);
  h = hashSignal(h, "Humidity", 0x08);
  h = hashSignal(h, "Pressure", 0x08);
  schemaHash = h;
}

// ── Command parser ──────────────────────────────────────────────────
static const int MAX_CMD = 64;
char cmdBuf[MAX_CMD];
int cmdIdx = 0;
bool cmdInProgress = false;

// ── CRC32 helper (frame CRC) ───────────────────────────────────────
CRC32 crc;

void crcReset()
{
  crc.setPolynome(0x04C11DB7);
  crc.setInitial(0xFFFFFFFF);
  crc.setXorOut(0xFFFFFFFF);
  crc.setReverseIn(true);
  crc.setReverseOut(true);
  crc.restart();
}

void serialWriteAndCRC(const byte *data, size_t len)
{
  Serial.write(data, len);
  crc.add(data, len);
}

void serialWriteAndCRC(byte b)
{
  Serial.write(b);
  crc.add(b);
}

// ── Frame header / footer ───────────────────────────────────────────
void writeHeader(byte msgKey, unsigned long msgId)
{
  Serial.write("<BLAECK:");
  Serial.write(msgKey);
  Serial.write(":");
  byte idBytes[4];
  idBytes[0] = msgId & 0xFF;
  idBytes[1] = (msgId >> 8) & 0xFF;
  idBytes[2] = (msgId >> 16) & 0xFF;
  idBytes[3] = (msgId >> 24) & 0xFF;
  Serial.write(idBytes, 4);
  Serial.write(":");
}

void writeFooter()
{
  Serial.write("/BLAECK>");
  Serial.write("\r\n");
  Serial.flush();
}

// ── Null-terminated string write ────────────────────────────────────
void writeString0(const char *s)
{
  Serial.print(s);
  Serial.print('\0');
}

// ── B0: Write Symbols ───────────────────────────────────────────────
void writeSymbols(unsigned long msgId)
{
  writeHeader(MSG_KEY_B0, msgId);

  // Master (MSC=1, SlaveID=0): MasterVoltage (float=0x08), Uptime (ulong=0x07)
  Serial.write((byte)1); Serial.write((byte)0);
  writeString0("MasterVoltage"); Serial.write((byte)0x08);
  Serial.write((byte)1); Serial.write((byte)0);
  writeString0("Uptime"); Serial.write((byte)0x07);

  // Slave 8 (MSC=2, SlaveID=8): Temperature (float), Humidity (float)
  Serial.write((byte)2); Serial.write((byte)8);
  writeString0("Temperature"); Serial.write((byte)0x08);
  Serial.write((byte)2); Serial.write((byte)8);
  writeString0("Humidity"); Serial.write((byte)0x08);

  // Slave 42 (MSC=2, SlaveID=42): Pressure (float)
  Serial.write((byte)2); Serial.write((byte)42);
  writeString0("Pressure"); Serial.write((byte)0x08);

  writeFooter();
}

// ── B3: Write Devices ───────────────────────────────────────────────
void writeDevice(byte msc, byte slaveId, const char *name,
                 const char *hw, const char *fw)
{
  Serial.write(msc);
  Serial.write(slaveId);
  writeString0(name);
  writeString0(hw);
  writeString0(fw);
  writeString0("6.0.0");        // Library version
  writeString0("BlaeckSerial"); // Library name
}

void writeDevices(unsigned long msgId)
{
  writeHeader(MSG_KEY_B3, msgId);
  writeDevice(1, 0, "Fake Master", "Arduino Mega 2560", "1.0");
  writeDevice(2, 8, "TempSensor", "Arduino Nano", "1.0");
  writeDevice(2, 42, "PressureSensor", "Arduino Nano", "1.0");
  writeFooter();
}

// ── C0: Write Restarted ─────────────────────────────────────────────
void writeRestarted(unsigned long msgId)
{
  writeHeader(MSG_KEY_C0, msgId);
  writeDevice(1, 0, "Fake Master", "Arduino Mega 2560", "1.0");
  writeFooter();
}

// ── D2: Write Data ──────────────────────────────────────────────────
void writeFloatData(uint16_t idx, float val)
{
  byte idxBytes[2] = {(byte)(idx & 0xFF), (byte)((idx >> 8) & 0xFF)};
  serialWriteAndCRC(idxBytes, 2);
  byte *fb = (byte *)&val;
  serialWriteAndCRC(fb, 4);
}

void writeULongData(uint16_t idx, unsigned long val)
{
  byte idxBytes[2] = {(byte)(idx & 0xFF), (byte)((idx >> 8) & 0xFF)};
  serialWriteAndCRC(idxBytes, 2);
  byte vb[4];
  vb[0] = val & 0xFF;
  vb[1] = (val >> 8) & 0xFF;
  vb[2] = (val >> 16) & 0xFF;
  vb[3] = (val >> 24) & 0xFF;
  serialWriteAndCRC(vb, 4);
}

void writeData(unsigned long msgId)
{
  Serial.write("<BLAECK:");

  crcReset();

  // Message key
  serialWriteAndCRC(MSG_KEY_D2);

  // ":"
  serialWriteAndCRC(':');

  // Message ID (4 bytes LE)
  byte idBytes[4];
  idBytes[0] = msgId & 0xFF;
  idBytes[1] = (msgId >> 8) & 0xFF;
  idBytes[2] = (msgId >> 16) & 0xFF;
  idBytes[3] = (msgId >> 24) & 0xFF;
  serialWriteAndCRC(idBytes, 4);

  // ":"
  serialWriteAndCRC(':');

  // Restart flag
  byte rf = sendRestartFlag ? 1 : 0;
  serialWriteAndCRC(rf);
  sendRestartFlag = false;

  // ":"
  serialWriteAndCRC(':');

  // Schema hash (2 bytes LE, CRC16-CCITT) — always the full hash
  byte hashBytes[2] = {(byte)(schemaHash & 0xFF), (byte)((schemaHash >> 8) & 0xFF)};
  serialWriteAndCRC(hashBytes, 2);

  // ":"
  serialWriteAndCRC(':');

  // Timestamp mode = 0 (no timestamp)
  serialWriteAndCRC((byte)0);

  // ":"
  serialWriteAndCRC(':');

  // Signal data — skip signals belonging to failed slaves
  // idx 0: MasterVoltage (master)
  writeFloatData(0, masterVoltage);
  // idx 1: Uptime (master)
  writeULongData(1, uptimeSeconds);

  if (!slave8Failed)
  {
    // idx 2: Temperature (slave 8)
    writeFloatData(2, temperature);
    // idx 3: Humidity (slave 8)
    writeFloatData(3, humidity);
  }

  if (!slave42Failed)
  {
    // idx 4: Pressure (slave 42)
    writeFloatData(4, pressure);
  }

  // D2 tail: StatusByte + StatusPayload(4)
  byte statusByte = 0;
  byte statusPayload[4] = {0, 0, 0, 0};

  byte skippedCount = 0;
  byte firstSkippedId = 0;
  byte firstSkipReason = 0;

  if (slave8Failed)
  {
    skippedCount++;
    if (firstSkippedId == 0)
    {
      firstSkippedId = 8;
      firstSkipReason = 0x01; // preflight no response
    }
  }
  if (slave42Failed)
  {
    skippedCount++;
    if (firstSkippedId == 0)
    {
      firstSkippedId = 42;
      firstSkipReason = 0x01;
    }
  }

  if (skippedCount > 0)
  {
    statusByte = 1;
    statusPayload[0] = skippedCount;
    statusPayload[1] = firstSkippedId;
    statusPayload[2] = firstSkipReason;
    statusPayload[3] = 0;
  }

  serialWriteAndCRC(statusByte);
  serialWriteAndCRC(statusPayload, 4);

  // CRC32 (not included in CRC itself)
  uint32_t crcVal = crc.calc();
  Serial.write((byte *)&crcVal, 4);

  writeFooter();
}

// ── Command parsing ─────────────────────────────────────────────────
unsigned long parseMsgId(const char *params)
{
  int p[4] = {0, 0, 0, 0};
  int n = 0;
  char tmp[MAX_CMD];
  strncpy(tmp, params, MAX_CMD - 1);
  tmp[MAX_CMD - 1] = '\0';
  char *tok = strtok(tmp, ",");
  while (tok && n < 4)
  {
    p[n++] = atoi(tok);
    tok = strtok(NULL, ",");
  }
  return ((unsigned long)p[3] << 24) | ((unsigned long)p[2] << 16) |
         ((unsigned long)p[1] << 8) | (unsigned long)p[0];
}

int parseSlaveId(const char *params)
{
  if (!params || *params == '\0')
    return -1;
  return atoi(params);
}

void handleCommand(const char *cmd)
{
  // Echo back
  Serial.print("<");
  Serial.print(cmd);
  Serial.println(">");

  const char *comma = strchr(cmd, ',');
  char command[MAX_CMD];
  char params[MAX_CMD] = "";

  if (comma)
  {
    int cmdLen = comma - cmd;
    strncpy(command, cmd, cmdLen);
    command[cmdLen] = '\0';
    strncpy(params, comma + 1, MAX_CMD - 1);
    params[MAX_CMD - 1] = '\0';
  }
  else
  {
    strncpy(command, cmd, MAX_CMD - 1);
    command[MAX_CMD - 1] = '\0';
  }

  char *c = command;
  while (*c == ' ')
    c++;

  if (strcmp(c, "BLAECK.WRITE_SYMBOLS") == 0)
  {
    writeSymbols(parseMsgId(params));
  }
  else if (strcmp(c, "BLAECK.WRITE_DATA") == 0)
  {
    writeData(parseMsgId(params));
  }
  else if (strcmp(c, "BLAECK.GET_DEVICES") == 0)
  {
    writeDevices(parseMsgId(params));
  }
  else if (strcmp(c, "BLAECK.ACTIVATE") == 0)
  {
    unsigned long interval = parseMsgId(params);
    timedActive = true;
    timedInterval = interval;
    firstTimedSend = true;
  }
  else if (strcmp(c, "BLAECK.DEACTIVATE") == 0)
  {
    timedActive = false;
  }
  else if (strcmp(c, "BLAECK.FAIL_SLAVE") == 0)
  {
    int sid = parseSlaveId(params);
    if (sid == 8)
      slave8Failed = true;
    else if (sid == 42)
      slave42Failed = true;
  }
  else if (strcmp(c, "BLAECK.RESTORE_SLAVE") == 0)
  {
    int sid = parseSlaveId(params);
    if (sid == 8)
      slave8Failed = false;
    else if (sid == 42)
      slave42Failed = false;
  }
}

// ── Main ────────────────────────────────────────────────────────────
void setup()
{
  Serial.begin(115200);
  computeSchemaHash();
}

void loop()
{
  // Update fake sensor data
  float t = millis() / 1000.0;
  masterVoltage = 3.3 + 0.1 * sin(t * 0.2);
  uptimeSeconds = millis() / 1000;
  temperature = 22.0 + 3.0 * sin(t * 0.3);
  humidity = 55.0 + 10.0 * sin(t * 0.15);
  pressure = 1013.25 + 5.0 * sin(t * 0.1);

  // Read serial commands
  while (Serial.available() > 0)
  {
    char rc = Serial.read();
    if (rc == '<')
    {
      cmdInProgress = true;
      cmdIdx = 0;
    }
    else if (rc == '>' && cmdInProgress)
    {
      cmdBuf[cmdIdx] = '\0';
      cmdInProgress = false;
      handleCommand(cmdBuf);
    }
    else if (cmdInProgress)
    {
      if (cmdIdx < MAX_CMD - 1)
        cmdBuf[cmdIdx++] = rc;
    }
  }

  // Send C0 (restarted) once
  static bool restartedSent = false;
  if (!restartedSent)
  {
    restartedSent = true;
    writeRestarted(1);
  }

  // Timed data
  if (timedActive)
  {
    unsigned long now = millis();
    if (firstTimedSend || now >= timedNextSend)
    {
      if (firstTimedSend)
        timedNextSend = now + timedInterval;
      else
        timedNextSend += timedInterval;
      firstTimedSend = false;

      writeData(185273099);
    }
  }
}
