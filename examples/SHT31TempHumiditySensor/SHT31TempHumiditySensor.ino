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

  if (!sht31.begin(0x44))
  {
    // Carry on regardless: the device still appears, reporting NaN for both values,
    // which says more to a host than never connecting at all.
    Serial.println(F("No SHT31 answered at 0x44"));
  }

  // Clears condensation off the sensor. It warms the element, so a reading taken
  // with it on is too high.
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
  ReadSensor();

  // Reads what has come in and writes the signals when the interval is up.
  Blaeck.tick();
}

// A measurement takes about 15 ms of blocking I2C, so it runs on its own schedule
// rather than on every pass of loop().
void ReadSensor()
{
  static unsigned long lastRead = 0;

  if (millis() - lastRead < 1000)
    return;
  lastRead = millis();

  // Both are NaN while the sensor is unreachable, which a host shows as unavailable.
  temperature = sht31.readTemperature();
  humidity = sht31.readHumidity();
}
