/*
        File: BlaeckSerial.cpp
        Author: Sebastian Strobl
*/

#include "BlaeckSerial.h"
#include "Arduino.h"

// static initializer for the static member.
BlaeckSerial* BlaeckSerial::_pSingletonInstance = 0;

BlaeckSerial::BlaeckSerial() {

}
BlaeckSerial::~BlaeckSerial() {
  delete Signals;
}

void BlaeckSerial::begin(HardwareSerial *Ref, unsigned int size) {
  SerialRef = Ref;
  Signals = new Signal[size];
  // Assign the static singleton used in the static handlers.
  BlaeckSerial::_pSingletonInstance = this;
}
void BlaeckSerial::beginMaster(HardwareSerial *Ref, unsigned int size, uint32_t WireClockFrequency) {
  _masterSlaveConfig = Master;
  Wire.setClock(WireClockFrequency);
  Wire.begin();


  begin(Ref, size);
}
void BlaeckSerial::beginSlave(HardwareSerial *Ref, unsigned int size, byte slaveID) {
  _masterSlaveConfig = Slave;
  _slaveID = slaveID;
  if (_slaveID > 127) _slaveID = 127;
  String s = "S";
  _slaveSymbolPrefix = s + _slaveID + "_";

  Wire.onReceive(OnSendHandler);
  Wire.onRequest(OnReceiveHandler);
  Wire.begin(_slaveID);

  begin(Ref, size);
}

void BlaeckSerial::addSignal(String symbolName, bool * value) {
  Signals[_signalIndex].SymbolName = symbolName;
  if (_masterSlaveConfig == Slave) {
    Signals[_signalIndex].SymbolName = _slaveSymbolPrefix + symbolName;
  }
  Signals[_signalIndex].DataType = Blaeck_bool;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}
void BlaeckSerial::addSignal(String symbolName, byte * value) {
  Signals[_signalIndex].SymbolName = symbolName;
  if (_masterSlaveConfig == Slave) {
    Signals[_signalIndex].SymbolName = _slaveSymbolPrefix + symbolName;
  }
  Signals[_signalIndex].DataType = Blaeck_byte;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}
void BlaeckSerial::addSignal(String symbolName, short * value) {
  Signals[_signalIndex].SymbolName = symbolName;
  if (_masterSlaveConfig == Slave) {
    Signals[_signalIndex].SymbolName = _slaveSymbolPrefix + symbolName;
  }
  Signals[_signalIndex].DataType = Blaeck_short;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}
void BlaeckSerial::addSignal(String symbolName, unsigned short * value) {
  Signals[_signalIndex].SymbolName = symbolName;
  if (_masterSlaveConfig == Slave) {
    Signals[_signalIndex].SymbolName = _slaveSymbolPrefix + symbolName;
  }
  Signals[_signalIndex].DataType = Blaeck_ushort;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}
void BlaeckSerial::addSignal(String symbolName, int * value) {
  Signals[_signalIndex].SymbolName = symbolName;
  if (_masterSlaveConfig == Slave) {
    Signals[_signalIndex].SymbolName = _slaveSymbolPrefix + symbolName;
  }
  Signals[_signalIndex].DataType = Blaeck_int;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}
void BlaeckSerial::addSignal(String symbolName, unsigned int * value) {
  Signals[_signalIndex].SymbolName = symbolName;
  if (_masterSlaveConfig == Slave) {
    Signals[_signalIndex].SymbolName = _slaveSymbolPrefix + symbolName;
  }
  Signals[_signalIndex].DataType = Blaeck_uint;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}
void BlaeckSerial::addSignal(String symbolName, long * value) {
  Signals[_signalIndex].SymbolName = symbolName;
  if (_masterSlaveConfig == Slave) {
    Signals[_signalIndex].SymbolName = _slaveSymbolPrefix + symbolName;
  }
  Signals[_signalIndex].DataType = Blaeck_long;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}
void BlaeckSerial::addSignal(String symbolName, unsigned long * value) {
  Signals[_signalIndex].SymbolName = symbolName;
  if (_masterSlaveConfig == Slave) {
    Signals[_signalIndex].SymbolName = _slaveSymbolPrefix + symbolName;
  }
  Signals[_signalIndex].DataType = Blaeck_ulong;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}
void BlaeckSerial::addSignal(String symbolName, float * value) {
  Signals[_signalIndex].SymbolName = symbolName;
  if (_masterSlaveConfig == Slave) {
    Signals[_signalIndex].SymbolName = _slaveSymbolPrefix + symbolName;
  }
  Signals[_signalIndex].DataType = Blaeck_float;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}
void BlaeckSerial::addSignal(String symbolName, double * value) {
  /*On the Uno and other ATMEGA based boards, the double implementation occupies 4 bytes
    and is exactly the same as the float, with no gain in precision.*/
  Signals[_signalIndex].SymbolName = symbolName;
  if (_masterSlaveConfig == Slave) {
    Signals[_signalIndex].SymbolName = _slaveSymbolPrefix + symbolName;
  }
  Signals[_signalIndex].DataType = Blaeck_float;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}

void BlaeckSerial::deleteSignals() {
  _signalIndex = 0;
}

void BlaeckSerial::read() {

  if (recvWithStartEndMarkers() == true) {
    parseData();
    Serial.print(F("<"));
    Serial.print(receivedChars);
    Serial.println(F(">"));

    if (strcmp(COMMAND, "BLAECK.WRITE_SYMBOLS") == 0)
    {
      unsigned long msg_id = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16)
                             | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);

      this->writeSymbols(msg_id);

    }
    else if (strcmp(COMMAND, "BLAECK.WRITE_DATA") == 0)
    {
      unsigned long msg_id = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16)
                             | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);

      this->writeData(msg_id);

    }
    else if (strcmp(COMMAND, "BLAECK.WRITE_VERSION") == 0)
    {
      unsigned long msg_id = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16)
                             | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);

      this->writeVersionNumber(msg_id, true);

    }
    else if (strcmp(COMMAND, "BLAECK.WRITE_DEVICE_NAME") == 0)
    {
      unsigned long msg_id = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16)
                             | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);

      this->writeDeviceName(msg_id, true);

    }
    else if (strcmp(COMMAND, "BLAECK.WRITE_DEVICE_HW_VERSION") == 0)
    {
      unsigned long msg_id = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16)
                             | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);

      this->writeDeviceHWVersion(msg_id, true);

    }
    else if (strcmp(COMMAND, "BLAECK.WRITE_DEVICE_FW_VERSION") == 0)
    {
      unsigned long msg_id = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16)
                             | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);

      this->writeDeviceFWVersion(msg_id, true);

    }
    else if (strcmp(COMMAND, "BLAECK.ACTIVATE") == 0)
    {
      unsigned long parameter = PARAMETER[0];
      if (parameter > 32767) parameter = 32767;
      unsigned long unit_multiplicator = 1000;
      unsigned long timedInterval_ms = parameter * unit_multiplicator;

      this->setTimedData(true, timedInterval_ms);

    }
    else if (strcmp(COMMAND, "BLAECK.DEACTIVATE") == 0)
    {
      this->setTimedData(false, _timedInterval_ms);
    }

    if (_readCallback != NULL) _readCallback(COMMAND, PARAMETER, STRING_01);
  }
}
void BlaeckSerial::attachRead(void (*readCallback) (char * command, int * parameter, char * string01)) {
  _readCallback = readCallback;
}

void BlaeckSerial::attachUpdate(void (*updateCallback)()) {
  _updateCallback = updateCallback;
}

bool BlaeckSerial::recvWithStartEndMarkers() {
  bool newData = false;
  static boolean recvInProgress = false;
  static byte ndx = 0;
  char startMarker = '<';
  char endMarker = '>';
  char rc;

  while (SerialRef->available() > 0 && newData == false) {
    rc = SerialRef->read();
    if (recvInProgress == true) {
      if (rc != endMarker) {
        receivedChars[ndx] = rc;
        ndx++;
        if (ndx >= MAXIMUM_CHAR_COUNT) {
          ndx = MAXIMUM_CHAR_COUNT - 1;
        }
      }
      else {
        // terminate the string
        receivedChars[ndx] = '\0';
        recvInProgress = false;
        ndx = 0;
        newData = true;
      }
    }
    else if (rc == startMarker) {
      recvInProgress = true;
    }
  }

  return newData;
}
void BlaeckSerial::parseData() {
  // split the data into its parts
  char tempChars[sizeof(receivedChars)];
  strcpy(tempChars, receivedChars);
  char * strtokIndx;
  strtokIndx = strtok(tempChars, ",");
  strcpy(COMMAND, strtokIndx);
  strtokIndx = strtok(NULL, ",");
  //PARAMETER 1 is stored in PARAMETER_01 & STRING_01 (if PARAMETER 1 is a string)
  //Only copy first 15 chars
  strncpy(STRING_01, strtokIndx, 15);
  //16th Char = Null Terminator
  STRING_01[15] = '\0';
  PARAMETER[0] = atoi(strtokIndx);
  strtokIndx = strtok(NULL, ", ");
  PARAMETER[1] = atoi(strtokIndx);
  strtokIndx = strtok(NULL, ", ");
  PARAMETER[2] = atoi(strtokIndx);
  strtokIndx = strtok(NULL, ", ");
  PARAMETER[3] = atoi(strtokIndx);
  strtokIndx = strtok(NULL, ", ");
  PARAMETER[4] = atoi(strtokIndx);
  strtokIndx = strtok(NULL, ", ");
  PARAMETER[5] = atoi(strtokIndx);
  strtokIndx = strtok(NULL, ", ");
  PARAMETER[6] = atoi(strtokIndx);
  strtokIndx = strtok(NULL, ", ");
  PARAMETER[7] = atoi(strtokIndx);
  strtokIndx = strtok(NULL, ", ");
  PARAMETER[8] = atoi(strtokIndx);
  strtokIndx = strtok(NULL, ", ");
  PARAMETER[9] = atoi(strtokIndx);
}

void BlaeckSerial::setTimedData(bool timedActivated, unsigned long timedInterval_ms) {
  _timedActivated = timedActivated;

  if (_timedActivated)
  {
    if (timedInterval_ms == 0)
    {
      _timedSetPoint_ms = 100;
      _timedInterval_ms = 100;
    }
    else if (timedInterval_ms > 32767000)
    {
      _timedSetPoint_ms = 32767000;
      _timedInterval_ms = 32767000;
    }
    else
    {
      _timedSetPoint_ms = timedInterval_ms;
      _timedInterval_ms = timedInterval_ms;
    }
    _timedFirstTime = true;
  }
}

void BlaeckSerial::writeSymbols() {
  this->writeSymbols(1);
}
void BlaeckSerial::writeSymbols(unsigned long msg_id) {
  if (_masterSlaveConfig == Single || _masterSlaveConfig == Slave)
  {
    this->writeLocalSymbols(msg_id, true);
  }
  else if (_masterSlaveConfig == Master)
  {
    this->writeLocalSymbols(msg_id, false);
    this->writeSlaveSymbols(true);
  }
}

void BlaeckSerial::writeData() {
  this->writeData(1);
}
void BlaeckSerial::writeData(unsigned long msg_id) {
  if (_masterSlaveConfig == Single)
  {
    if (_updateCallback != NULL) _updateCallback();
    this->writeLocalData(msg_id, true);
  }
  else if (_masterSlaveConfig == Slave)
  {
    //updateCallback is called in BlaeckSerial::wireSlaveReceive()
    this->writeLocalData(msg_id, true);
  }
  else if (_masterSlaveConfig == Master)
  {
    if (_updateCallback != NULL) _updateCallback();
    this->writeLocalData(msg_id, false);
    this->writeSlaveData(true);
  }
}

void BlaeckSerial::timedWriteData() {
  this->timedWriteData(1);
}
void BlaeckSerial::timedWriteData(unsigned long msg_id) {

  if (_timedFirstTime == true) _timedFirstTimeDone_ms = millis();
  unsigned long _timedElapsedTime_ms = (millis() - _timedFirstTimeDone_ms);

  if (((_timedElapsedTime_ms >= _timedSetPoint_ms) || _timedFirstTime == true) && _timedActivated == true) {
    if (_timedFirstTime == false) _timedSetPoint_ms += _timedInterval_ms;
    _timedFirstTime = false;


    this->writeData(msg_id);

  }
}

void BlaeckSerial::writeVersionNumber(unsigned long msg_id, bool send_eol) {
  SerialRef->write("<BLAECK:");
  byte msg_key = 0xB2;
  SerialRef->write(msg_key);
  SerialRef->write(":");
  ulngCvt.val = msg_id;
  SerialRef->write(ulngCvt.bval, 4);
  SerialRef->write(":");
  SerialRef->write(BLAECKSERIAL_VERSION);

  if (send_eol) {
    SerialRef->write("/BLAECK>");
    SerialRef->write("\r\n");
    SerialRef->flush();
  }
}

void BlaeckSerial::writeDeviceName(unsigned long msg_id, bool send_eol) {
  SerialRef->write("<BLAECK:");
  byte msg_key = 0xB3;
  SerialRef->write(msg_key);
  SerialRef->write(":");
  ulngCvt.val = msg_id;
  SerialRef->write(ulngCvt.bval, 4);
  SerialRef->write(":");
  SerialRef->print(DeviceName);

  if (send_eol) {
    SerialRef->write("/BLAECK>");
    SerialRef->write("\r\n");
    SerialRef->flush();
  }
}

void BlaeckSerial::writeDeviceHWVersion(unsigned long msg_id, bool send_eol) {
  SerialRef->write("<BLAECK:");
  byte msg_key = 0xB4;
  SerialRef->write(msg_key);
  SerialRef->write(":");
  ulngCvt.val = msg_id;
  SerialRef->write(ulngCvt.bval, 4);
  SerialRef->write(":");
  SerialRef->print(DeviceHWVersion);

  if (send_eol) {
    SerialRef->write("/BLAECK>");
    SerialRef->write("\r\n");
    SerialRef->flush();
  }
}

void BlaeckSerial::writeDeviceFWVersion(unsigned long msg_id, bool send_eol) {
  SerialRef->write("<BLAECK:");
  byte msg_key = 0xB5;
  SerialRef->write(msg_key);
  SerialRef->write(":");
  ulngCvt.val = msg_id;
  SerialRef->write(ulngCvt.bval, 4);
  SerialRef->write(":");
  SerialRef->print(DeviceFWVersion);

  if (send_eol) {
    SerialRef->write("/BLAECK>");
    SerialRef->write("\r\n");
    SerialRef->flush();
  }
}

void BlaeckSerial::writeLocalData(unsigned long msg_id, bool send_eol) {
  SerialRef->write("<BLAECK:");
  byte msg_key = 0xB1;
  SerialRef->write(msg_key);
  SerialRef->write(":");
  ulngCvt.val = msg_id;
  SerialRef->write(ulngCvt.bval, 4);
  SerialRef->write(":");

  for (int i = 0; i < _signalIndex; i++) {
    intCvt.val = i;
    SerialRef->write(intCvt.bval, 2);
    Signal signal = Signals[i];
    switch (signal.DataType) {
      case (Blaeck_bool): {
          boolCvt.val = *((bool*)signal.Address);
          SerialRef->write(boolCvt.bval, 1);
        } break;
      case (Blaeck_byte): {
          SerialRef->write(*((byte*)signal.Address));
        } break;
      case (Blaeck_short): {
          shortCvt.val = *((short*)signal.Address);
          SerialRef->write(shortCvt.bval, 2);
        } break;
      case (Blaeck_ushort): {
          ushortCvt.val = *((unsigned short*)signal.Address);
          SerialRef->write(ushortCvt.bval, 2);
        } break;
      case (Blaeck_int): {
          intCvt.val = *((int*)signal.Address);
          SerialRef->write(intCvt.bval, 2);
        } break;
      case (Blaeck_uint): {
          uintCvt.val = *((unsigned int*)signal.Address);
          SerialRef->write(uintCvt.bval, 2);
        } break;
      case (Blaeck_long): {
          lngCvt.val = *((long*)signal.Address);
          SerialRef->write(lngCvt.bval, 4);
        } break;
      case (Blaeck_ulong): {
          ulngCvt.val = *((unsigned long*)signal.Address);
          SerialRef->write(ulngCvt.bval, 4);
        } break;
      case (Blaeck_float): {
          fltCvt.val = *((float*)signal.Address);
          SerialRef->write(fltCvt.bval, 4);
        } break;
    }
  }

  if (send_eol) {
    SerialRef->write("/BLAECK>");
    SerialRef->write("\r\n");
    SerialRef->flush();
  }
}

void BlaeckSerial::writeSlaveData(bool send_eol) {

  int signalCount = 0;

  for (int slaveindex = 0; slaveindex <= 127; slaveindex++) { //Cycle through slaves
    if (slaveFound(slaveindex)) {
      byte transmissionIsSuccess = false;

      for (byte retries = 0; retries < 4; retries++) {
        Wire.beginTransmission(slaveindex);
        Wire.write(1);
        transmissionIsSuccess = Wire.endTransmission();
        //0: success
        if (transmissionIsSuccess == 0) break;
      }

      if (transmissionIsSuccess == 0) {
        bool eolist_found = false;
        for (int slaveSignal = 0; slaveSignal < 1000; slaveSignal++) {
          // request 32 bytes from slave device
          byte receivedBytes = Wire.requestFrom(slaveindex, 32);
          //try again
          if (receivedBytes < 2) continue;


          //Signal Key
          Serial.write(lowByte(_signalIndex + signalCount));
          Serial.write(highByte(_signalIndex + signalCount));

          signalCount += 1;
          // slave may send less than requested
          for (int symbolchar = 0; symbolchar <= 31; symbolchar++) {
            //first receive number of bytes to expect
            char bytecount = Wire.read();
            char c;
            for (int i = 0; i < bytecount; i++) {
              //then read the data bytes
              c = Wire.read();
              Serial.print(c);
            }
            char c_before = char(0x7F);
            byte endoflist_count = 0;
            //empty buffer
            while (Wire.available()) {
              c = Wire.read();
              if (c == char(0x7F) && c_before == char(0x7F)) endoflist_count += 1;
              c_before = c;
            }
            //8 times 0x7F -> All Wire data received from slave
            if (endoflist_count == 8) eolist_found = true;
          }
          if (eolist_found) break;
        }
      }

    }
  }
  if (send_eol) {
    SerialRef->write("/BLAECK>");
    SerialRef->write("\r\n");
    SerialRef->flush();
  }
}

void BlaeckSerial::writeLocalSymbols(unsigned long msg_id, bool send_eol) {
  SerialRef->write("<BLAECK:");
  byte msg_key = 0xB0;
  SerialRef->write(msg_key);
  SerialRef->write(":");
  ulngCvt.val = msg_id;
  SerialRef->write(ulngCvt.bval, 4);
  SerialRef->write(":");

  for (int i = 0; i < _signalIndex; i++) {
    intCvt.val = i;
    SerialRef->write(intCvt.bval, 2);
    Signal signal = Signals[i];
    SerialRef->print(signal.SymbolName);
    SerialRef->write('\0');

    switch (signal.DataType) {
      case (Blaeck_bool): {
          SerialRef->write(0x0);
          break;
        }
      case (Blaeck_byte): {
          SerialRef->write(0x1);
          break;
        }
      case (Blaeck_short): {
          SerialRef->write(0x2);
          break;
        }
      case (Blaeck_ushort): {
          SerialRef->write(0x3);
          break;
        }
      case (Blaeck_int): {
          SerialRef->write(0x4);
          break;
        }
      case (Blaeck_uint): {
          SerialRef->write(0x5);
          break;
        }
      case (Blaeck_long): {
          SerialRef->write(0x6);
          break;
        }
      case (Blaeck_ulong): {
          SerialRef->write(0x7);
          break;
        }
      case (Blaeck_float): {
          SerialRef->write(0x8);
          break;
        }
    }
  }
  if (send_eol) {
    SerialRef->write("/BLAECK>");
    SerialRef->write("\r\n");
    SerialRef->flush();
  }
}
void BlaeckSerial::writeSlaveSymbols(bool send_eol) {

  int signalCount = 0;

  //Cycle through slaves
  for (int slaveindex = 0; slaveindex <= 127; slaveindex++) {
    byte transmissionIsSuccess = false;

    for (byte retries = 0; retries < 4; retries++) {
      Wire.beginTransmission(slaveindex);
      Wire.write(0);
      transmissionIsSuccess = Wire.endTransmission();
      //0: success
      if (transmissionIsSuccess == 0) break;
    }

    if (transmissionIsSuccess == 0) {

      storeSlave(slaveindex, false);
      bool eolist_found = false;

      for (int i = 0; i < 1000; i++) {
        // request 32 bytes from slave device
        byte receivedBytes = Wire.requestFrom(slaveindex, 32);
        if (receivedBytes < 2) continue; //try again

        bool eosignal_found = false;
        if (i == 0) {
          //Expecting response 0xAA from slave -> Slave found
          //Receive a byte as character
          char c = Wire.read();

          if (c == char(0xAA)) {
            storeSlave(slaveindex, true);
          } else {
            storeSlave(slaveindex, false);
            //exit loop
            break;
          }
        }

        if (slaveFound(slaveindex)) {
          //Signal Key
          Serial.write(lowByte(_signalIndex + signalCount));
          Serial.write(highByte(_signalIndex + signalCount));

          int charsToRead = 32;
          if (i == 0) charsToRead = 31;
          for (int symbolchar = 0; symbolchar <= charsToRead - 1; symbolchar++) {
            //Slave may send less than requested
            //SymbolName + \0 + DataType
            // receive a byte as character
            char c = Wire.read();
            //'\r'
            if (c == char(0x0D)) {
              eosignal_found = true;
              signalCount += 1;
            }
            //'\n'
            if (c == char(0x0A)) eolist_found = true;
            if (eosignal_found != true && eolist_found != true) Serial.print(c);
          }
          if (eolist_found) break;
        }
      }
    }
  }
  if (send_eol) {
    SerialRef->write("/BLAECK>");
    SerialRef->write("\r\n");
    SerialRef->flush();
  }


}

void BlaeckSerial::tick() {
  this->read();
  this->timedWriteData();
}

void BlaeckSerial::wireSlaveTransmitSingleSymbol() {

  if (_wireSignalIndex == 0) Wire.write(0xAA);

  Signal signal = Signals[_wireSignalIndex];

  char little_s_string[32] = "";
  signal.SymbolName.toCharArray(little_s_string, 32);
  Wire.write(little_s_string);

  Wire.write('\0');

  switch (signal.DataType) {
    case (Blaeck_bool): {
        Wire.write(0x0);
        break;
      }
    case (Blaeck_byte): {
        Wire.write(0x1);
        break;
      }
    case (Blaeck_short): {
        Wire.write(0x2);
        break;
      }
    case (Blaeck_ushort): {
        Wire.write(0x3);
        break;
      }
    case (Blaeck_int): {
        Wire.write(0x4);
        break;
      }
    case (Blaeck_uint): {
        Wire.write(0x5);
        break;
      }
    case (Blaeck_long): {
        Wire.write(0x6);
        break;
      }
    case (Blaeck_ulong): {
        Wire.write(0x7);
        break;
      }
    case (Blaeck_float): {
        Wire.write(0x8);
        break;
      }
  }

  Wire.write(0x0D);

  _wireSignalIndex += 1;
  if (_wireSignalIndex >= _signalIndex) {
    _wireSignalIndex = 0;
    Wire.write(0x0A);
  }
}
void BlaeckSerial::wireSlaveTransmitSingleDataPoint() {

  Signal signal = Signals[_wireSignalIndex];

  switch (signal.DataType) {
    case (Blaeck_bool): {
        boolCvt.val = *((bool*)signal.Address);
        Wire.write(1);
        Wire.write(boolCvt.bval, 1);
      } break;
    case (Blaeck_byte): {
        Wire.write(1);
        Wire.write(*((byte*)signal.Address));
      } break;
    case (Blaeck_short): {
        shortCvt.val = *((short*)signal.Address);
        Wire.write(2);
        Wire.write(shortCvt.bval, 2);
      } break;
    case (Blaeck_ushort): {
        ushortCvt.val = *((unsigned short*)signal.Address);
        Wire.write(2);
        Wire.write(ushortCvt.bval, 2);
      } break;
    case (Blaeck_int): {
        intCvt.val = *((int*)signal.Address);
        Wire.write(2);
        Wire.write(intCvt.bval, 2);
      } break;
    case (Blaeck_uint): {
        uintCvt.val = *((unsigned int*)signal.Address);
        Wire.write(2);
        Wire.write(uintCvt.bval, 2);
      } break;
    case (Blaeck_long): {
        lngCvt.val = *((long*)signal.Address);
        Wire.write(4);
        Wire.write(lngCvt.bval, 4);
      } break;
    case (Blaeck_ulong): {
        ulngCvt.val = *((unsigned long*)signal.Address);
        Wire.write(4);
        Wire.write(ulngCvt.bval, 4);
      } break;
    case (Blaeck_float): {
        fltCvt.val = *((float*)signal.Address);
        Wire.write(4);
        Wire.write(fltCvt.bval, 4);
      } break;
  }

  _wireSignalIndex += 1;

  if (_wireSignalIndex >= _signalIndex) {
    _wireSignalIndex = 0;
    Wire.write(0x7F);
    Wire.write(0x7F);
    Wire.write(0x7F);
    Wire.write(0x7F);
    Wire.write(0x7F);
    Wire.write(0x7F);
    Wire.write(0x7F);
    Wire.write(0x7F);
  }
}
void BlaeckSerial::wireSlaveReceive() {
  _wireMode = Wire.read();
  _wireSignalIndex = 0;
  if (_masterSlaveConfig == Slave && _wireMode == 1) {
    if (_updateCallback != NULL) _updateCallback();
  };
}
void BlaeckSerial::wireSlaveTransmitToMaster() {
  if (_wireMode == 0) this->wireSlaveTransmitSingleSymbol();
  if (_wireMode == 1) this->wireSlaveTransmitSingleDataPoint();
}

bool BlaeckSerial::slaveFound(const unsigned int index) {
  if (index > 127)
    return false;
  return (boolean) bitRead(_slaveFound[index / 8], index % 8);
}

void BlaeckSerial::storeSlave(const unsigned int index, const boolean value) {
  if (index > 127)
    return;
  bitWrite(_slaveFound[index / 8], index % 8, value);
}
