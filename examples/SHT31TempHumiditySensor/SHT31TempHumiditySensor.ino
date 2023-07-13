/*
  SHT31TempHumiditySensor.ino

  This is an example for the SHT31 Temperature & Humidity Sensor.
  The sensor uses I2C to communicate, 2 pins are required to interface.
  This example requires Adafruit SHT31 Libray to be installed.

  Copyright (c) by Sebastian Strobl,
  More information on: https://github.com/sebaJoSt/BlaeckSerial

  Setup:
    Upload the sketch to your Arduino.
    Open the Serial Monitor and set the baudrate to 9600 baud.
    Type the following commands and press enter:

    <BLAECK.GET_DEVICES>              Writes the device's information to the PC
    <BLAECK.WRITE_SYMBOLS>            Writes the symbol list to the PC
    <BLAECK.WRITE_DATA>               Writes the temperature and humidity from the sensor to the PC
    <BLAECK.ACTIVATE,60>              The data is written every 60s
                                      Minimum: 0[seconds] Maximum: 32767[seconds]
    <BLAECK.DEACTIVATE>               Stops writing the data every 60s
*/

#include "Arduino.h"
#include "Adafruit_SHT31.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

float temperature;
float humidity;

Adafruit_SHT31 sht31 = Adafruit_SHT31();

//Instantiate a new BlaeckSerial object
BlaeckSerial BlaeckSerial;

void setup() {
  Serial.begin(9600);

  sht31.begin(0x44);
  //sht31.heater(true);

  //Setup BlaeckSerial
  BlaeckSerial.begin(
    &Serial, //Serial reference
    2        //Maximal signal count used;
  );

  BlaeckSerial.DeviceName = "Temp Humidity Sensor";
  BlaeckSerial.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  BlaeckSerial.DeviceFWVersion = ExampleVersion;

  BlaeckSerial.addSignal("Temperature [°C]", &temperature);
  BlaeckSerial.addSignal("Humidity [%]", &humidity);
}


void loop() {
  temperature = sht31.readTemperature();
  humidity = sht31.readHumidity();

  BlaeckSerial.tick();
}
