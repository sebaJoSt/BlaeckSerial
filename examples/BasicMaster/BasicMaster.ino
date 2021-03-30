/*
  BasicMaster.ino

  This is a sample sketch to show how to use the BlaeckSerial library to transmit data
  from multiple Arduino boards in a master/slave configuration to your PC.
  Upload this sketch to the master Arduino.

  Copyright (c) by Sebastian Strobl,
  More information on: https://github.com/sebaJoSt/BlaeckSerial

  I2C interface:
  The two wires, or lines of the IC2 are called Serial Clock (or SCL) and Serial Data (or SDA). 
  The SCL line is the clock signal which synchronize the data transfer between the devices on 
  the I2C bus and it’s generated by the master device. 
  The other line is the SDA line which carries the data. As a reference the table below shows 
  where I2C pins are located on various Arduino boards.

  Board             I2C pins
  Uno               A4 (SDA), A5 (SCL)
  Mega2560          20 (SDA), 21 (SCL)

  Setup a test circuit:
  Connect the SCL pin and SDA pin on the master board to their counterparts on the slave boards. 
  Make sure that all boards share a common ground. If powering the boards independently is an 
  issue, connect the 5V output of the Master to the VIN pin on the slaves.
  
  Upload the sketches:
  * Upload this sketch to the master arduino.
  * Upload the sketch BasicSlave.ino to the slave arduinos.

  Usage:
  After uploading all the sketches, the master board must be connected to your computer via USB.
  Then open the Serial Monitor and set the baudrate to 9600 baud. The commands are the same as
  in single device configuration (see Basic.Ino) but the symbol list and data includes all 
  the slave signals.

    <BLAECK.WRITE_SYMBOLS>      Write the symbol list to the PC
    <BLAECK.WRITE_DATA>         Write the data to the PC
    <BLAECK.ACTIVATE,60>        The data is written every 60s to the PC
                                Minimum: 1[seconds] Maximum: 32767[seconds]
    <BLAECK.DEACTIVATE>         Stops writing the data every 60s
*/

#include "Arduino.h"
#include "BlaeckSerial.h"

//Instantiate a new BlaeckSerial object
BlaeckSerial BlaeckSerial;

//Signals
float randomSmallNumber;
float randomSmallNumber2;

void setup()
{
  // Initialize Serial port
  Serial.begin(9600);

/*Setup BlaeckSerial master
 * First parameter: Serial reference
 * Second parameter: Maxmimal signal count used;
 * Third parameter: Clock frequency: the value (in Hertz) of desired communication clock. 
     Accepted values are 100000L (standard mode) and 400000L (fast mode). 
     Some processors also support 
       * 10000 (low speed mode)
       * 1000000 (fast mode plus)
       * 3400000 (high speed mode)
     Please refer to the specific processor documentation to make sure the desired mode is supported.
*/ 
  BlaeckSerial.beginMaster(&Serial, 2, 400000L);

  // Add signals to BlaeckSerial master
  BlaeckSerial.addSignal("Small Number", &randomSmallNumber);
  BlaeckSerial.addSignal("Small Number 2", &randomSmallNumber2);

  /*Uncomment this function for initial settings
    first parameter: timedActivated
    second parameter: timedInterval_ms */
  // BlaeckSerial.setTimedData(true, 60000);
}

void loop()
{
  UpdateRandomNumbers();

  /*Keeps watching for serial input (Serial.read) and
    transmits the data at the user-set interval (Serial.write)*/
  BlaeckSerial.tick();
}

void UpdateRandomNumbers()
{
  // Random small number from 0.00 to 9.99
  randomSmallNumber = random(1000) / 100.0;

    // Random small number from 10.00 to 20.00
  randomSmallNumber2 = random(1000, 2001) / 100.0;
}
