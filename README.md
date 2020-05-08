BlaeckSerial
===

This Arduino library extends Serial functionality to transmit binary data. It supports Master/Slave configuration to include data from additional slave boards connected to the master Arduino over I2C. Also included is a message parser which reads input in the syntax of `<HelloWorld, 12, 47>`. The parsed command `HelloWorld` and its parameters are available in your own sketch by attaching a callback function.

BlaeckSerial is heavily inspired by Nick Dodd's [AdvancedSerial Library](https://github.com/Nick1787/AdvancedSerial/).
The message parser uses code from Robin2's Arduino forum thread [Serial Basic Input](https://forum.arduino.cc/index.php?topic=396450.0).

## Getting Started

Clone this repository into `Arduino/Libraries` or use the built-in Arduino IDE Library manager to install
a copy of this library. You can find more detail about installing libraries 
[here, on Arduino's website](https://www.arduino.cc/en/guide/libraries).

(Open Basic Example under `File -> Examples -> BlaeckSerial` for reference)

```CPP
#include <Arduino.h>
#include <BlaeckSerial.h>
```
### Instantiate BlaeckSerial
```CPP
BlaeckSerial BlaeckSerial;
```
### Initialize Serial & BlaeckSerial
```CPP
void setup()
{
  Serial.begin(9600);

  BlaeckSerial.begin(
    &Serial,   //Serial reference
    2          //Maxmimal signal count used;
  );
}
```

### Add signals
```CPP
 BlaeckSerial.addSignal("Test Signal 1", &someGlobalVariable);
 BlaeckSerial.addSignal("Test Signal 2", &anotherGlobalVariable);
```

### Update your variables and don't forget to `tick()`!
```CPP
void loop()
{
  UpdateYourVariables();

  /*Keeps watching for serial input (Serial.read) and
    transmits the data at the user-set interval (Serial.write)*/
  BlaeckSerial.tick();
}
```

## BlaeckSerial commands

Here's a full list of serial commands handled by this library:

| Command                      | Description                                                                      |
| ---------------------------- | -------------------------------------------------------------------------------- |
| `<BLAECK.WRITE_SYMBOLS> `    | Writes symbol list including datatype information.                               |
| `<BLAECK.WRITE_DATA> `       | Writes the binary data.                                                          |
| `<BLAECK.ACTIVATE, interval>`| Activates writing the binary data in user-set interval [s] (Min: 1s Max: 32767s) |
| `<BLAECK.DEACTIVATE> `       | Deactivates writing in intervals.                                                |

## The Symbol List and Data Codec

The Symbol List and Data is in the following format:
````
|--  Header       --||--       Data         --||-- EOT  --|
<BLAECK:MSGKEY:MSGID:........................../BLAECK>\r\n
````

Type| MSGKEY | DATA# | DATA:| DESCRIPTION:
----|--------| ------|---------------------------------|---------------------------------------------
Symbol List | B0 | n | `<SymbolID><SymbolName><DTYPE>` | Up to n Items. Response to request for available symbols.
Data | B1 | n | `<SymbolID><DATA>` | Up to n Items. Response to request for data.
  

 Element|Type    |  DESCRIPTION:
 -------|--------| ---------------------------------------------------------------------------
 `MSGKEY`| byte |  Message Key, A unique key for the type of message being sent
 `MSGID` | ulong|  Message ID,  A unique message ID which echoes back to transmitter to indicate a response to a message (0 to 4294967295)
 `DATA`  | (varying)| Message Data, varying data types and length depending on message
 `SymbolID` | uint | Symbol ID number
 `SymbolName` |String0 | Symbol Name - Null Terminated String
 `DTYPE` | byte | DataType  0=bool, 1=byte, 2=short, 3=ushort, 4=int, 5=uint, 6=long, 7=ulong, 8=float
 
 MSGID is supported by `<BLAECK.WRITE_SYMBOLS>` and `<BLAECK.WRITE_DATA>`:
 ````
 <BLAECK.WRITE_SYMBOLS, firstByteMSGID, secondByteMSGID, thirdByteMSGID, fourthByteMSGID>
 <BLAECK.WRITE_DATA   , firstByteMSGID, secondByteMSGID, thirdByteMSGID, fourthByteMSGID>
 ````
 
 ### Decoding Examples
 
 #### Symbol List Decoding
 Example from `Basic.ino`:
 `<BLAECK.WRITE_SYMBOLS, 0, 255, 0, 0>`:
 ````
ASCII: <  B  L  A  E  C  K  :  °  :  °  °  °  °  :  °  °  S  m  a  l  l     N  u  m  b
HEX:   3C 42 4C 41 45 43 4B 3A B0 3A 00 FF 00 00 3A 00 00 53 6D 61 6C 6C 20 4E 75 6D 62
Byte:  0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26
-----------------------------------------------------------------------------------
ASCII: e  r  °  °  °  °  B  i  g     N  u  m  b  e  r  °  °  /  B  L  A  E  C  K  >
HEX:   65 72 00 08 01 00 42 69 67 20 4E 75 6D 62 65 72 00 06 2F 42 4C 41 45 43 4B 3E
Byte:  27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 56 47 48 49 50 51 52
 ````
 
 Byte | DESCRIPTION:
----|---------------------------------------------
8   | `MSGKEY`: B0 -> Symbol List
10-13| `MSGID`: Hex: 00 FF 00 00 -> Decimal: 65280
15-16| `SymbolID`: Hex: 00 00 -> Decimal: 0
17-29| `SymbolName`: ASCII: Small Number + Null Termination '\0'
30   | `DTYPE`: Hex: 08 -> Float
31-32| `SymbolID`: Hex: 01 00 -> Decimal: 1
33-43| `SymbolName`: ASCII: Big Number + Null Termination '\0'
44   | `DTYPE`: Hex: 06 -> Long

 #### Data Decoding
 Example from `Basic.ino`:
 `<BLAECK.WRITE_DATA, 255, 255, 255, 255>`:
 ````
ASCII: <  B  L  A  E  C  K  :  °  :  °  °  °  °  :  °  °  °  °  °  °  °  °  °  °  °  °  /  B  L  A  E  C  K  >
HEX:   3C 42 4C 41 45 43 4B 3A B1 3A FF FF FF FF 3A 00 00 B8 1E FD 40 01 00 D8 E6 32 7C 2F 42 4C 41 45 43 4B 3E
Byte:  0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34
 ````
 
 Byte | DESCRIPTION:
----|---------------------------------------------
8   | `MSGKEY`: B1 -> Data
10-13| `MSGID`: Hex: FF FF FF FF -> Decimal: 4294967295
15-16| `SymbolID`: Hex: 00 00 -> Decimal: 0
17-20| `DATA`: Float -> 4 Bytes; Hex: B8 1E FD 40 -> Float: 7.91
21-22| `SymbolID`: Hex: 01 00 -> Decimal: 1
23-26| `DATA`: Long  -> 4 Bytes; Hex: D8 E6 32 7C -> Long: 2083710680

### Datatypes

`DTYPE` | Datatype | Bytes
-- |----|---------------------------------------------
0| bool | 1
1|byte | 1
2|short| 2
3|unsigned short| 2
4|int| 2
5|unsigned int | 2
6|long | 4
7|unsigned long | 4
8|float | 4

On the Uno and other ATMEGA based boards, the double implementation occupies 4 byte and is exactly the same as the float, with no gain in precision. Therefore if you add a double signal with `addSignal(&doubleVariable)` the symbol list will return the `DTYPE` 8 (float).
