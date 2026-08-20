/*
  SHT31TempHumiditySensor.ino

  Reads temperature and humidity from an Adafruit SHT31 sensor over I2C,
  then sends both values to Loggbok.

  Requires the Adafruit SHT31 library.

  Author: Sebastian Strobl,
  More information on: https://github.com/sebaJoSt/BlaeckSerial

*/

#include "Arduino.h"
#include "Adafruit_SHT31.h"
#include "BlaeckSerial.h"

#define ExampleVersion "1.0"

float temperature;
float humidity;

Adafruit_SHT31 sht31 = Adafruit_SHT31();

// Instantiate a new BlaeckSerial object
BlaeckSerial Blaeck;

void setup()
{
  Serial.begin(115200);

  sht31.begin(0x44);
  // sht31.heater(true);

  // Setup BlaeckSerial
  Blaeck.begin(
      &Serial, // Serial reference
      2        // Maximal signal count used;
  );

  Blaeck.DeviceName = "Temp Humidity Sensor";
  Blaeck.DeviceHWVersion = "Arduino Mega 2560 Rev3";
  Blaeck.DeviceFWVersion = ExampleVersion;

  Blaeck.addSignal(F("Temperature [°C]"), &temperature);
  Blaeck.addSignal(F("Humidity [%]"), &humidity);
}

void loop()
{
  temperature = sht31.readTemperature();
  humidity = sht31.readHumidity();

  Blaeck.tick();
}
