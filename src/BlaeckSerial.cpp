/*
        File: BlaeckSerial.cpp
        Author: Sebastian Strobl
*/

#include <Arduino.h>
#include "BlaeckSerial.h"


BlaeckSerial::BlaeckSerial()
{
}
BlaeckSerial::~BlaeckSerial()
{
  delete Signals;
}

void BlaeckSerial::begin(Stream *Ref, unsigned int size)
{
  StreamRef = (Stream *)Ref;
  Signals = new Signal[size];
}

void BlaeckSerial::addSignal(String signalName, bool *value)
{
  Signals[_signalIndex].SignalName = signalName;
  Signals[_signalIndex].DataType = Blaeck_bool;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}

void BlaeckSerial::addSignal(String signalName, byte *value)
{
  Signals[_signalIndex].SignalName = signalName;
  Signals[_signalIndex].DataType = Blaeck_byte;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}

void BlaeckSerial::addSignal(String signalName, short *value)
{
  Signals[_signalIndex].SignalName = signalName;
  Signals[_signalIndex].DataType = Blaeck_short;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}

void BlaeckSerial::addSignal(String signalName, unsigned short *value)
{
  Signals[_signalIndex].SignalName = signalName;
  Signals[_signalIndex].DataType = Blaeck_ushort;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}

void BlaeckSerial::addSignal(String signalName, int *value)
{
  Signals[_signalIndex].SignalName = signalName;
  Signals[_signalIndex].DataType = Blaeck_int;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}

void BlaeckSerial::addSignal(String signalName, unsigned int *value)
{
  Signals[_signalIndex].SignalName = signalName;
  Signals[_signalIndex].DataType = Blaeck_uint;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}

void BlaeckSerial::addSignal(String signalName, long *value)
{
  Signals[_signalIndex].SignalName = signalName;
  Signals[_signalIndex].DataType = Blaeck_long;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}

void BlaeckSerial::addSignal(String signalName, unsigned long *value)
{
  Signals[_signalIndex].SignalName = signalName;
  Signals[_signalIndex].DataType = Blaeck_ulong;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}

void BlaeckSerial::addSignal(String signalName, float *value)
{
  Signals[_signalIndex].SignalName = signalName;
  Signals[_signalIndex].DataType = Blaeck_float;
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}

void BlaeckSerial::addSignal(String signalName, double *value)
{
  Signals[_signalIndex].SignalName = signalName;
#ifdef __AVR__
  /*On the Uno and other ATMEGA based boards, the double implementation occupies 4 bytes
  and is exactly the same as the float, with no gain in precision.*/
  Signals[_signalIndex].DataType = Blaeck_float;
#else
  Signals[_signalIndex].DataType = Blaeck_double;
#endif
  Signals[_signalIndex].Address = value;
  _signalIndex++;
}

void BlaeckSerial::deleteSignals()
{
  _signalIndex = 0;
}

void BlaeckSerial::read()
{

  if (recvWithStartEndMarkers() == true)
  {
    parseData();
    StreamRef->print("<");
    StreamRef->print(receivedChars);
    StreamRef->println(">");

    if (strcmp(COMMAND, "BLAECK.WRITE_SYMBOLS") == 0)
    {
      unsigned long msg_id = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16) | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);

      this->writeSymbols(msg_id);
    }
    else if (strcmp(COMMAND, "BLAECK.WRITE_DATA") == 0)
    {
      unsigned long msg_id = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16) | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);

      this->writeData(msg_id);
    }
    else if (strcmp(COMMAND, "BLAECK.GET_DEVICES") == 0)
    {
      unsigned long msg_id = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16) | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);

      this->writeDevices(msg_id);
    }
    else if (strcmp(COMMAND, "BLAECK.ACTIVATE") == 0)
    {
      unsigned long timedInterval_ms = ((unsigned long)PARAMETER[3] << 24) | ((unsigned long)PARAMETER[2] << 16) | ((unsigned long)PARAMETER[1] << 8) | ((unsigned long)PARAMETER[0]);
      this->setTimedData(true, timedInterval_ms);
    }
    else if (strcmp(COMMAND, "BLAECK.DEACTIVATE") == 0)
    {
      this->setTimedData(false, _timedInterval_ms);
    }

    if (_readCallback != NULL)
      _readCallback(COMMAND, PARAMETER, STRING_01);
  }
}

void BlaeckSerial::attachRead(void (*readCallback)(char *command, int *parameter, char *string01))
{
  _readCallback = readCallback;
}

void BlaeckSerial::attachUpdate(void (*updateCallback)())
{
  _updateCallback = updateCallback;
}

bool BlaeckSerial::recvWithStartEndMarkers()
{
  bool newData = false;
  static boolean recvInProgress = false;
  static byte ndx = 0;
  char startMarker = '<';
  char endMarker = '>';
  char rc;

  while (StreamRef->available() > 0 && newData == false)
  {
    rc = StreamRef->read();
    if (recvInProgress == true)
    {
      if (rc != endMarker)
      {
        receivedChars[ndx] = rc;
        ndx++;
        if (ndx >= MAXIMUM_CHAR_COUNT)
        {
          ndx = MAXIMUM_CHAR_COUNT - 1;
        }
      }
      else
      {
        // terminate the string
        receivedChars[ndx] = '\0';
        recvInProgress = false;
        ndx = 0;
        newData = true;
      }
    }
    else if (rc == startMarker)
    {
      recvInProgress = true;
    }
  }

  return newData;
}

void BlaeckSerial::parseData()
{
  // split the data into its parts
  char tempChars[sizeof(receivedChars)];
  strcpy(tempChars, receivedChars);
  char *strtokIndx;
  strtokIndx = strtok(tempChars, ",");
  strcpy(COMMAND, strtokIndx);

  strtokIndx = strtok(NULL, ",");
  if (strtokIndx != NULL)
  {
    // PARAMETER 1 is stored in PARAMETER_01 & STRING_01 (if PARAMETER 1 is a string)

    // Only copy first 15 chars
    strncpy(STRING_01, strtokIndx, 15);
    // 16th Char = Null Terminator
    STRING_01[15] = '\0';
    PARAMETER[0] = atoi(strtokIndx);
  }
  else
  {
    STRING_01[0] = '\0';
    PARAMETER[0] = 0;
  }

  strtokIndx = strtok(NULL, ", ");
  if (strtokIndx != NULL)
  {
    PARAMETER[1] = atoi(strtokIndx);
  }
  else
  {
    PARAMETER[1] = 0;
  }

  strtokIndx = strtok(NULL, ", ");
  if (strtokIndx != NULL)
  {
    PARAMETER[2] = atoi(strtokIndx);
  }
  else
  {
    PARAMETER[2] = 0;
  }

  strtokIndx = strtok(NULL, ", ");
  if (strtokIndx != NULL)
  {
    PARAMETER[3] = atoi(strtokIndx);
  }
  else
  {
    PARAMETER[3] = 0;
  }

  strtokIndx = strtok(NULL, ", ");
  if (strtokIndx != NULL)
  {
    PARAMETER[4] = atoi(strtokIndx);
  }
  else
  {
    PARAMETER[4] = 0;
  }

  strtokIndx = strtok(NULL, ", ");
  if (strtokIndx != NULL)
  {
    PARAMETER[5] = atoi(strtokIndx);
  }
  else
  {
    PARAMETER[5] = 0;
  }

  strtokIndx = strtok(NULL, ", ");
  if (strtokIndx != NULL)
  {
    PARAMETER[6] = atoi(strtokIndx);
  }
  else
  {
    PARAMETER[6] = 0;
  }

  strtokIndx = strtok(NULL, ", ");
  if (strtokIndx != NULL)
  {
    PARAMETER[7] = atoi(strtokIndx);
  }
  else
  {
    PARAMETER[7] = 0;
  }

  strtokIndx = strtok(NULL, ", ");
  if (strtokIndx != NULL)
  {
    PARAMETER[8] = atoi(strtokIndx);
  }
  else
  {
    PARAMETER[8] = 0;
  }

  strtokIndx = strtok(NULL, ", ");
  if (strtokIndx != NULL)
  {
    PARAMETER[9] = atoi(strtokIndx);
  }
  else
  {
    PARAMETER[9] = 0;
  }
}

void BlaeckSerial::setTimedData(bool timedActivated, unsigned long timedInterval_ms)
{
  _timedActivated = timedActivated;

  if (_timedActivated)
  {
    _timedSetPoint_ms = timedInterval_ms;
    _timedInterval_ms = timedInterval_ms;
    _timedFirstTime = true;
  }
}

void BlaeckSerial::writeSymbols()
{
  this->writeSymbols(1);
}
void BlaeckSerial::writeSymbols(unsigned long msg_id)
{
  StreamRef->write("<BLAECK:");
  byte msg_key = 0xB0;
  StreamRef->write(msg_key);
  StreamRef->write(":");
  ulngCvt.val = msg_id;
  StreamRef->write(ulngCvt.bval, 4);
  StreamRef->write(":");

  for (int i = 0; i < _signalIndex; i++)
  {
    StreamRef->write(_masterSlaveConfig);
    StreamRef->write(_slaveID);

    Signal signal = Signals[i];

    StreamRef->print(signal.SignalName);
    StreamRef->print('\0');

    switch (signal.DataType)
    {
    case (Blaeck_bool):
    {
      StreamRef->write((byte)0x0);
      break;
    }
    case (Blaeck_byte):
    {
      StreamRef->write(0x1);
      break;
    }
    case (Blaeck_short):
    {
      StreamRef->write(0x2);
      break;
    }
    case (Blaeck_ushort):
    {
      StreamRef->write(0x3);
      break;
    }
    case (Blaeck_int):
    {
      StreamRef->write(0x4);
      break;
    }
    case (Blaeck_uint):
    {
      StreamRef->write(0x5);
      break;
    }
    case (Blaeck_long):
    {
      StreamRef->write(0x6);
      break;
    }
    case (Blaeck_ulong):
    {
      StreamRef->write(0x7);
      break;
    }
    case (Blaeck_float):
    {
      StreamRef->write(0x8);
      break;
    }
    case (Blaeck_double):
    {
      StreamRef->write(0x9);
      break;
    }
    }
  }
 
  StreamRef->write("/BLAECK>");
  StreamRef->write("\r\n");
  StreamRef->flush();
}

void BlaeckSerial::writeData()
{
  this->writeData(1);
}
void BlaeckSerial::writeData(unsigned long msg_id)
{
  if (_updateCallback != NULL)
    _updateCallback();

  _crc.setPolynome(0x04C11DB7);
  _crc.setInitial(0xFFFFFFFF);
  _crc.setXorOut(0xFFFFFFFF);
  _crc.setReverseIn(true);
  _crc.setReverseOut(true);
  _crc.restart();

  StreamRef->write("<BLAECK:");
  byte msg_key = 0xB1;
  StreamRef->write(msg_key);
  StreamRef->write(":");
  ulngCvt.val = msg_id;
  StreamRef->write(ulngCvt.bval, 4);
  StreamRef->write(":");

  _crc.add(msg_key);
  _crc.add(':');
  _crc.add(ulngCvt.bval, 4);
  _crc.add(':');

  for (int i = 0; i < _signalIndex; i++)
  {
    intCvt.val = i;
    StreamRef->write(intCvt.bval, 2);
    _crc.add(intCvt.bval, 2);

    Signal signal = Signals[i];
    switch (signal.DataType)
    {
    case (Blaeck_bool):
    {
      boolCvt.val = *((bool *)signal.Address);
      StreamRef->write(boolCvt.bval, 1);
      _crc.add(boolCvt.bval, 1);
    }
    break;
    case (Blaeck_byte):
    {
      StreamRef->write(*((byte *)signal.Address));
      _crc.add(*((byte *)signal.Address));
    }
    break;
    case (Blaeck_short):
    {
      shortCvt.val = *((short *)signal.Address);
      StreamRef->write(shortCvt.bval, 2);
      _crc.add(shortCvt.bval, 2);
    }
    break;
    case (Blaeck_ushort):
    {
      ushortCvt.val = *((unsigned short *)signal.Address);
      StreamRef->write(ushortCvt.bval, 2);
      _crc.add(ushortCvt.bval, 2);
    }
    break;
    case (Blaeck_int):
    {
      intCvt.val = *((int *)signal.Address);
      StreamRef->write(intCvt.bval, 2);
      _crc.add(intCvt.bval, 2);
    }
    break;
    case (Blaeck_uint):
    {
      uintCvt.val = *((unsigned int *)signal.Address);
      StreamRef->write(uintCvt.bval, 2);
      _crc.add(uintCvt.bval, 2);
    }
    break;
    case (Blaeck_long):
    {
      lngCvt.val = *((long *)signal.Address);
      StreamRef->write(lngCvt.bval, 4);
      _crc.add(lngCvt.bval, 4);
    }
    break;
    case (Blaeck_ulong):
    {
      ulngCvt.val = *((unsigned long *)signal.Address);
      StreamRef->write(ulngCvt.bval, 4);
      _crc.add(ulngCvt.bval, 4);
    }
    break;
    case (Blaeck_float):
    {
      fltCvt.val = *((float *)signal.Address);
      StreamRef->write(fltCvt.bval, 4);
      _crc.add(fltCvt.bval, 4);
    }
    break;
    case (Blaeck_double):
    {
      dblCvt.val = *((double *)signal.Address);
      StreamRef->write(dblCvt.bval, 8);
      _crc.add(dblCvt.bval, 8);
    }
    break;
    }
  }

    // StatusByte 0: Normal transmission
    // StatusByte + CRC First Byte + CRC Second Byte + CRC Third Byte + CRC Fourth Byte
    StreamRef->write((byte)0);

    uint32_t crc_value = _crc.calc();
    StreamRef->write((byte *)&crc_value, 4);

    StreamRef->write("/BLAECK>");
    StreamRef->write("\r\n");
    StreamRef->flush();
}

void BlaeckSerial::timedWriteData()
{
  this->timedWriteData(185273099);
}

void BlaeckSerial::timedWriteData(unsigned long msg_id)
{

  if (_timedFirstTime == true)
    _timedFirstTimeDone_ms = millis();
  unsigned long _timedElapsedTime_ms = (millis() - _timedFirstTimeDone_ms);

  if (((_timedElapsedTime_ms >= _timedSetPoint_ms) || _timedFirstTime == true) && _timedActivated == true)
  {
    if (_timedFirstTime == false)
      _timedSetPoint_ms += _timedInterval_ms;
    _timedFirstTime = false;

    this->writeData(msg_id);
  }
}

void BlaeckSerial::writeRestarted()
{
  this->writeRestarted(1);
}

void BlaeckSerial::writeRestarted(unsigned long msg_id)
{
  if (!_writeRestartedAlreadyDone)
  {
    _writeRestartedAlreadyDone = true;
    StreamRef->write("<BLAECK:");
    byte msg_key = 0xC0;
    StreamRef->write(msg_key);
    StreamRef->write(":");
    ulngCvt.val = msg_id;
    StreamRef->write(ulngCvt.bval, 4);
    StreamRef->write(":");

    StreamRef->write((byte)0);
    StreamRef->write((byte)0);
    StreamRef->print(DeviceName);
    StreamRef->print('\0');
    StreamRef->print(DeviceHWVersion);
    StreamRef->print('\0');
    StreamRef->print(DeviceFWVersion);
    StreamRef->print('\0');
    StreamRef->print(LIBRARY_VERSION);
    StreamRef->print('\0');
    StreamRef->print(LIBRARY_NAME);
    StreamRef->print('\0');

    StreamRef->write("/BLAECK>");
    StreamRef->write("\r\n");
    StreamRef->flush();
  }
}

void BlaeckSerial::writeDevices()
{
  this->writeDevices(1);
}

void BlaeckSerial::writeDevices(unsigned long msg_id)
{
  StreamRef->write("<BLAECK:");
  byte msg_key = 0xB3;
  StreamRef->write(msg_key);
  StreamRef->write(":");
  ulngCvt.val = msg_id;
  StreamRef->write(ulngCvt.bval, 4);
  StreamRef->write(":");

  StreamRef->write((byte)0));
  StreamRef->write((byte)0));
  StreamRef->print(DeviceName);
  StreamRef->print('\0');
  StreamRef->print(DeviceHWVersion);
  StreamRef->print('\0');
  StreamRef->print(DeviceFWVersion);
  StreamRef->print('\0');
  StreamRef->print(LIBRARY_VERSION);
  StreamRef->print('\0');
  StreamRef->print(LIBRARY_NAME);
  StreamRef->print('\0');

  StreamRef->write("/BLAECK>");
  StreamRef->write("\r\n");
  StreamRef->flush();
}

void BlaeckSerial::tick()
{
  this->tick(185273099);
}

void BlaeckSerial::tick(unsigned long msg_id)
{
  this->read();
  this->writeRestarted(msg_id);
  this->timedWriteData(msg_id);
}f