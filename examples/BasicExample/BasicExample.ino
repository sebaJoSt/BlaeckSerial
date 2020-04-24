#include "Arduino.h"
#include "BlaeckSerial.h"

//Instantiate a new advanced serial object
BlaeckSerial AdvSerial;

unsigned long lastupdate = 0;
unsigned long lastSquareTransition = 0;

//signals
float sine_value = 0;
float cosine_value = 0;
float square_value = -5;

void setup() {
  //Initialize Serial port
  Serial.begin(9600);
  AdvSerial.begin(&Serial, 100);
  //Add signals to Advanced Serial which will be transmitted
  AdvSerial.addSignal("sine", &sine_value);
  AdvSerial.addSignal("cosine", &cosine_value);
  AdvSerial.addSignal("square", &square_value);
}

void loop() {

  AdvSerial.read();

  if ((millis() - lastSquareTransition) > 5000) {
    //Update the calculated square value;
    square_value = -1 * square_value;

    lastSquareTransition = millis();
  }

  if ((millis() - lastupdate) > 1000) {
    //Update the calculated cosine and sine values;
    sine_value = 5.0 * sin( 0.5 * 3.1415 * ((double)(millis())) / 10);
    cosine_value = 5.0 * cos( 0.5 * 3.1415 * ((double)(millis())) / 10);
    
    lastupdate = millis();

    AdvSerial.writeData(123);
  }

}
