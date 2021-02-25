// Project Name: spotClock
//
// Filename: spotClock.ino
// Author: Reuben Strangelove 
// Last Revision Date: 7/2016
// Revision: 1 - working, no known bugs
//
// Operational notes:
// Displays Au and Ag spot prices from internet data feed, see esp8266 module for URL
// JSON formated data packets
// UART command line interface, type 'help' for user commands
//
// Dev notes:
// note error delay triggers in 5 minutes of unchanged of curTime
// StaticJsonBuffer must be lowest value possible to avoid memory errors


#include "LedControl.h"    // https://github.com/wayoda/LedControl
#include <ArduinoJson.h>   // https://github.com/bblanchon/ArduinoJson
#include "Timer.h"         // http://github.com/JChristensen/Timer
#include <EEPROM.h>        // built-in

// disable printing information over serial besides actual data used in the sytem
#define PRINT_REPLIES_OVER_SERIAL false       // print helpful debugging info
#define EEPROM_PASSWORD_OFFSET 35             // where in EEPROM the data is stored offset from the zero location
#define EEPROM_AU_ALERT_DELTA_OFFSET 100      // where in EEPROM the data is stored offset from the zero location
#define EEPROM_AG_ALERT_DELTA_OFFSET 110      // where in EEPROM the data is stored offset from the zero location
#define HTTP_CODE_OK "200"                    // http code 200 is page "OK" reponse from webserver
#define ERROR_DATA_DELAY_TIMEOUT_VALUE 300    // time in seconds before showing data delay error when data is not updated
#define ERROR_HTTP_TIMEOUT_VALUE 20      // time in seconds before flagging http error and displaying errors


Timer t; // declare timer

/* DataIn, CLK, LOAD, Num Devices (4 display segments per device) */
LedControl lc = LedControl(7, 5, 6, 4);

// setup input/out pins
const int pinBrightnessAdjustButton = 2;
const int pinTimeDateButton = 3;
const int pinDeltaErrorButton = 4;

bool buttonBrightnessAdjustBuffer;
bool buttonTimeDateBuffer;
bool buttonDeltaErrorBuffer;


const int ledBluePin = 12;
const int ledRedPin = 10;
const int ledGreenPin = 11;
const int ledPowerPin = 9;

// states of uart commands
enum uartCommandStates {
  SET_NULL, SET_SSID, SET_PASSWORD, SET_AU_ALERT_DELTA, SET_AG_ALERT_DELTA
};
enum uartCommandStates serialCommand = SET_NULL;

String inData; // data received by serial
String ssid = "                                "; // max length per 802.11 standard is 32 char
String password = "                                "; // max length per 802.11 standard is 32 char
String wifiStatus = "startUp";
String curTime;
String curTimeBuffer;

float agSpot = 0;
float auSpot = 0;
float auDelta = 0;
float agDelta = 0;
float auAlertDelta;
float agAlertDelta;

bool displayStrobeTimerFlag = false;
bool errorDelayFlag = false;
bool errorHttpFlag = false;
bool configSentSuccessFlag = false; // flag to indicate esp8266 has successfully connected using provided ssid and password
//bool errorLedFlashFlag = false;



long dataDelayCounter = 0; // must belong to better count delay in milliseconds
long errorHttpElaspedTimeCounter = 0;

int configSentWaitingValue = 0;
int httpCode;
int displayBrightness = 8;

void setup()
{

  // setup ledControl devices, 7-segment displats
  int devices = lc.getDeviceCount();
  for (int address = 0; address < devices; address++)
  {
    /*The MAX72XX is in power-saving mode on startup*/
    lc.shutdown(address, false);
    /* Set the brightness to a medium values */
    lc.setIntensity(address, 4);
    /* and clear the display */
    lc.clearDisplay(address);
  }

  Serial.begin(57600);

  //get ssid from EEPROM
  for (int i = 0; i < 32; i++) {
    ssid[i] = EEPROM.read(i);
    if (ssid[i]  == 0 || i == 31) // if no null terminator found, add one at the end
    {
      ssid = ssid.substring(0, i); // places the null terminator in the correct spot
      break; // end of string
    }
  }

  //get password from EEPROM
  for (int i = 0; i < 32; i++) {
    password[i] = EEPROM.read(i + EEPROM_PASSWORD_OFFSET);
    if (password[i]  == 0 || i == 31)
    {
      password = password.substring(0, i); // places the null terminator in the correct spot
      break; // end of string
    }
  }

  //get auAlertDelta from EEPROM, format xxx.xx
  auAlertDelta = 0;
  for (int i = 0; i < 5; i++)
  {
    int intTemp = EEPROM.read(i + EEPROM_AU_ALERT_DELTA_OFFSET);
    if (intTemp > 9) // indicates bad eeprom data
    {
      auAlertDelta = 25.00;
      break;
    }
    if (i == 0) auAlertDelta = auAlertDelta + intTemp * 100;
    if (i == 1) auAlertDelta = auAlertDelta + intTemp * 10;
    if (i == 2) auAlertDelta = auAlertDelta + intTemp;
    if (i == 3) auAlertDelta = auAlertDelta + (float)intTemp / 10.0;
    if (i == 4) auAlertDelta = auAlertDelta + (float)intTemp / 100.0;
  }

  //get agAlertDelta from EEPROM, format x.xx
  agAlertDelta = 0;
  for (int i = 0; i < 3; i++)
  {
    int intTemp = EEPROM.read(i + EEPROM_AG_ALERT_DELTA_OFFSET);
    if (intTemp > 9) // indicates bad eeprom data
    {
      agAlertDelta = 0.50;
      break;
    }
    if (i == 0) agAlertDelta = agAlertDelta + intTemp;
    if (i == 1) agAlertDelta = agAlertDelta + (float)intTemp / 10.0;
    if (i == 2) agAlertDelta = agAlertDelta + (float)intTemp / 100.0;
  }

  // display starting message
  /*
    Serial.println("spotClock");
    Serial.print("ssid: ");
    Serial.println(ssid);
    Serial.print("password: ");
    Serial.println(password);
    Serial.print("auAlertDelta: ");
    Serial.println(auAlertDelta);
    Serial.print("agAlertDelta: ");
    Serial.println(agAlertDelta);
  */

  // setup timer events to perform periodic tasks

  // strobe interval, flash rate of display strobe
  t.every(1000, handlerStrobeDisplay); //instantiate the timer object

  // interval to resend ssid and password data to esp8266 while waiting code 200 from esp8266
  t.every(2500, handlerSendWifiConnectionData); //instantiate the timer object

  // data delay check interval for data delay error checking
  t.every(1000, handleCheckDataDelay); //instantiate the timer object

  // refresh 7-segment display interval
  t.every(100, writeArduinoOn7Segment); //instantiate the timer object

  // http error time elapsed counter
  t.every(1000, handlerHttpElapsedTimeCounter); //instantiate the timer object


  // setup input buttons
  pinMode(pinBrightnessAdjustButton, INPUT);             // set pin to input
  digitalWrite(pinBrightnessAdjustButton, HIGH);         // turn on pullup resistors
  pinMode(pinDeltaErrorButton, INPUT);           // set pin to input
  digitalWrite(pinDeltaErrorButton, HIGH);       // turn on pullup resistors
  pinMode(pinTimeDateButton, INPUT);                      // set pin to input
  digitalWrite(pinTimeDateButton, HIGH);                  // turn on pullup resistors

  pinMode(ledPowerPin, OUTPUT);
  pinMode(ledBluePin, OUTPUT);
  pinMode(ledRedPin, OUTPUT);
  pinMode(ledGreenPin, OUTPUT);

  digitalWrite(ledPowerPin, LOW);  // ground out LED
  digitalWrite(ledRedPin, LOW);
  digitalWrite(ledBluePin, LOW);
  digitalWrite(ledGreenPin, LOW);

} // end setup()


// return fraction part of a float as integer values
int fPart(float Value)
{
  int iPart = (int)(Value);
  int fPart = 100 * (Value - iPart);
  return fPart;
}


void handlerHttpElapsedTimeCounter()
{
  if (wifiStatus != HTTP_CODE_OK)
  {
    errorHttpElaspedTimeCounter++;
    if (errorHttpElaspedTimeCounter > ERROR_HTTP_TIMEOUT_VALUE) errorHttpFlag = true;
  }
  else
  {
    errorHttpElaspedTimeCounter = 0;
    errorHttpFlag = false;
  }
}


// handle check for data delay
void handleCheckDataDelay()
{  
  if (curTime == curTimeBuffer)  
  {
    dataDelayCounter++;
    if (dataDelayCounter > ERROR_DATA_DELAY_TIMEOUT_VALUE)  errorDelayFlag = true;
  }
  else
  {
    // time has been updated
    errorDelayFlag = false; // clear error flag
    dataDelayCounter = 0; // reset counter
  }
  curTimeBuffer = curTime;
  return;
}


// handle stobe timing control for 7-segment displays
void handlerStrobeDisplay()
{
  displayStrobeTimerFlag = !displayStrobeTimerFlag;
}


// handle sending ssid and password to esp8266
void handlerSendWifiConnectionData()
{
  configSentWaitingValue++; // used for display feature
  if (configSentWaitingValue > 8) configSentWaitingValue = 0;

  // send configuration to esp8266 until a success has been returned
  if (configSentSuccessFlag == false)
  {

    // send configuration data to esp8266
    // build JSON object
    StaticJsonBuffer<200> jsonBufferSend;
    JsonObject& dataToSend = jsonBufferSend.createObject();
    dataToSend["ssid"] = ssid;
    dataToSend["password"] = password;
    // print data to serial
    dataToSend.printTo(Serial);
    Serial.println();
  }
}


// refesh 7-segment LED display
// change display based on button states
void writeArduinoOn7Segment()
{

  // check for state change on buttons to indicate to clear the display
  bool button0State = digitalRead(pinBrightnessAdjustButton);
  bool button1State = digitalRead(pinTimeDateButton);
  bool button2State = digitalRead(pinDeltaErrorButton);
  if (button1State != buttonTimeDateBuffer || button2State != buttonDeltaErrorBuffer)
  {
    lc.clearDisplay(0);
    lc.clearDisplay(1);
  }

  // check for input on brightness buttons
  // debounce not needed considering the subroutine is executed every 100 milliseconds
  // change of state and button is pressed
  if (button0State != buttonBrightnessAdjustBuffer && button0State == LOW)
  {
    displayBrightness = displayBrightness + 4;
    if (displayBrightness > 11) displayBrightness = 0;
    // set display to new brightness
    for (int address = 0; address < 4; address++)
    {
      lc.setIntensity(address, displayBrightness);
    }
  }

  // store button stats in buffer for later checking
  buttonBrightnessAdjustBuffer = button0State;
  buttonTimeDateBuffer = button1State;
  buttonDeltaErrorBuffer = button2State;


  // check if device is in start up
  // display startup sequence while waiting for OK from esp8266
  if (wifiStatus == "startUp")
  {
    if (configSentWaitingValue == 0)
    {
      lc.clearDisplay(0);
      lc.clearDisplay(1);
      configSentWaitingValue++;
    }
    for (int i = 0; i < configSentWaitingValue; i++)
    {
      lc.setChar(0, i, '-', false);
      lc.setChar(1, i, '-', false);
    }
    return;
  }

  // check for errors and button pressed
  // display errors and/or error code
  if (buttonDeltaErrorBuffer == LOW) {
    if ((wifiStatus != HTTP_CODE_OK || errorDelayFlag == true))
    {
      // display "Error"
      if (wifiStatus == "noWifi")
      {
        // display "no con"
        lc.setChar(0, 5, 'n', false);
        lc.setChar(0, 4, 'o', false);
        lc.setChar(0, 3, ' ', false);
        lc.setChar(0, 2, 'C', false);
        lc.setChar(0, 1, 'o', false);
        lc.setChar(0, 0, 'n', false);
      }
      else if (errorDelayFlag == true)
      {
        // display "DELAy"
        lc.setChar(0, 4, 'D', false);
        lc.setChar(0, 3, 'E', false);
        lc.setChar(0, 2, 'L', false);
        lc.setChar(0, 1, 'A', false);
        lc.setLed(0, 0, 2, true); // y
        lc.setLed(0, 0, 3, true); // y
        lc.setLed(0, 0, 4, true); // y
        lc.setLed(0, 0, 6, true); // y
        lc.setLed(0, 0, 7, true); // y
      }
      else
      {
        // display numberic error code
        signed int code = wifiStatus.toInt();
        if (code == -1)
        {
          lc.setChar(0, 2, ' ', false);
          lc.setChar(0, 1, '-', false);
          lc.setChar(0, 0, '1', false);
        }
        else
        {
          code = abs(code);
          int codeHundreds = code / 100;
          int codeTens = (code - codeHundreds * 100) / 10;
          int codeOnes = (code - codeHundreds * 100 - codeTens * 10);
          lc.setChar(0, 2, codeHundreds, false);
          lc.setChar(0, 1, codeTens, false);
          lc.setChar(0, 0, codeOnes, false);
        }
      }

long eTime;

      // choose which error time elasped counter to use
      if (errorDelayFlag == true) eTime = dataDelayCounter;
      else eTime = errorHttpElaspedTimeCounter;

      int eHours = eTime / 3600;
      int eMinutes = (eTime - eHours * 3600) / 60;
      int eSeconds = (eTime - eHours * 3600 - eMinutes * 60) % 60;
      lc.setDigit(1, 7, eHours / 10, false);
      lc.setDigit(1, 6, eHours % 10, false);
      lc.setChar(1, 5, '-', false);
      lc.setDigit(1, 4, eMinutes / 10, false);
      lc.setDigit(1, 3, eMinutes % 10, false);
      lc.setChar(1, 2, '-', false);
      lc.setDigit(1, 1, eSeconds / 10, false);
      lc.setDigit(1, 0, eSeconds % 10, false); 
            
      return;
    }
    else // no error, button pressed, show Au Ag delta alarms
    {
      // display Au alert delta
      int auHundreds = auAlertDelta / 100;
      int auTens = (auAlertDelta - auHundreds * 100) / 10;
      int auOnes = (auAlertDelta - auHundreds * 100 - auTens * 10);
      lc.setChar(0, 7, 'A', false);
      lc.setLed(0, 6, 3, true); // u
      lc.setLed(0, 6, 4, true); // u
      lc.setLed(0, 6, 5, true); // u
      if (auHundreds > 0) lc.setDigit(0, 4, auHundreds, false);
      if (auTens > 0) lc.setDigit(0, 3, auTens, false);
      lc.setDigit(0, 2, auOnes, true);
      lc.setDigit(0, 1, fPart(abs(auAlertDelta)) / 10, false);
      lc.setDigit(0, 0, fPart(abs(auAlertDelta)) % 10, false);

      // display Ag alert delta
      int agTens = agAlertDelta / 10;
      int agOnes = (agAlertDelta - agTens * 10);
      lc.setChar(1, 7, 'A', false);
      lc.setLed(1, 6, 1, true); // g
      lc.setLed(1, 6, 2, true); // g
      lc.setLed(1, 6, 3, true); // g
      lc.setLed(1, 6, 4, true); // g
      lc.setLed(1, 6, 6, true); // g
      lc.setLed(1, 6, 7, true); // g
      if (agTens > 0) lc.setDigit(1, 3, agTens, false);
      if (agOnes > 0) lc.setDigit(1, 2, agOnes, true);
      else lc.setChar(1, 2, ' ', true);
      lc.setDigit(1, 1, fPart(abs(agAlertDelta)) / 10, false);
      lc.setDigit(1, 0, fPart(abs(agAlertDelta)) % 10, false);
      return;
    }
  }

  // display time and data, or elasped time of error
  if (buttonTimeDateBuffer == LOW)
  {
    // display time
    lc.setDigit(1, 7, curTime.substring(11, 12).toInt(), false);
    lc.setDigit(1, 6, curTime.substring(12, 13).toInt(), false);
    lc.setChar(1, 5, '-', false);
    lc.setDigit(1, 4, curTime.substring(14, 15).toInt(), false);
    lc.setDigit(1, 3, curTime.substring(15, 16).toInt(), false);
    lc.setChar(1, 2, '-', false);
    lc.setDigit(1, 1, curTime.substring(17, 18).toInt(), false);
    lc.setDigit(1, 0, curTime.substring(18, 19).toInt(), false);
  
      // display date
      lc.setDigit(0, 7, curTime.substring(5, 6).toInt(), false);
      lc.setDigit(0, 6, curTime.substring(6, 7).toInt(), false);
      lc.setChar(0, 5, '-', false);
      lc.setDigit(0, 4, curTime.substring(8, 9).toInt(), false);
      lc.setDigit(0, 3, curTime.substring(9, 10).toInt(), false);
      lc.setChar(0, 2, '-', false);
      lc.setDigit(0, 1, curTime.substring(2, 3).toInt(), false);
      lc.setDigit(0, 0, curTime.substring(3, 4).toInt(), false);
      return;
    
  }


  // no errors flagged and button not pressed, move onto displaying prices

  // display spot prices on 7-segment displays
  // manually shift digits and decimal points on displays

  //  test for strobe conditions before display prices
  if (abs(auDelta) > auAlertDelta && displayStrobeTimerFlag)
  {
    lc.clearDisplay(0);
  }
  else
  {
    // Au
    int auThousands = auSpot / 1000;
    int auHundreds = (auSpot - auThousands * 1000) / 100;
    int auTens = (auSpot - auThousands * 1000 - auHundreds * 100) / 10;
    int auOnes = (auSpot - auThousands * 1000 - auHundreds * 100 - auTens * 10);

    lc.setDigit(0, 7, auThousands, false);
    lc.setDigit(0, 6, auHundreds, false);
    lc.setDigit(0, 5, auTens, false);
    lc.setDigit(0, 4, auOnes, false);

    auThousands = abs(auDelta) / 1000;
    auHundreds = (abs(auDelta) - auThousands * 1000) / 100;
    auTens = (abs(auDelta) - auThousands * 1000 - auHundreds * 100) / 10;
    auOnes = (abs(auDelta) - auThousands * 1000 - auHundreds * 100 - auTens * 10);

    if (auHundreds > 0)
    { // delta over $99.99
      if (auDelta < 0) lc.setChar(0, 3, '-', false);
      else lc.setChar(0, 3, ' ', false);
      lc.setDigit(0, 2, auHundreds, false);
      lc.setDigit(0, 1, auTens, false);
      lc.setDigit(0, 0, auOnes, true);
    }
    else
    {
      if (auTens != 0)
        // delta over 9.99
      {
        if (auDelta < 0) lc.setChar(0, 3, '-', false);
        else lc.setChar(0, 3, ' ', false);
        lc.setDigit(0, 2, auTens, false);
        lc.setDigit(0, 1, auTens, true);
        lc.setDigit(0, 0, fPart(abs(auDelta)) / 10, false);
      }
      else
      {
        lc.setChar(0, 3, ' ', false);
        if (auDelta < 0) lc.setChar(0, 2, '-', false);
        else lc.setChar(0, 2, ' ', false);
        lc.setDigit(0, 1, auOnes, true);
        lc.setDigit(0, 0, fPart(abs(auDelta)) / 10, false);
      }

    }
  }

  if (abs(agDelta) > agAlertDelta && displayStrobeTimerFlag)
  {
    lc.clearDisplay(1);
  }
  else
  {
    //Ag
    int agThousands = agSpot / 1000;
    int agHundreds = (agSpot - agThousands * 1000) / 100;
    int agTens = (agSpot - agThousands * 1000 - agHundreds * 100) / 10;
    int agOnes = (agSpot - agThousands * 1000 - agHundreds * 100 - agTens * 10);

    if ((int)agSpot >= 100)
    {
      lc.setDigit(1, 7, agHundreds, false);
      lc.setDigit(1, 6, agTens, false);
      lc.setDigit(1, 5, agOnes, true);
      lc.setDigit(1, 4, fPart(agSpot) / 10, false);
    }
    else
    {
      lc.setDigit(1, 7, agTens, false);
      lc.setDigit(1, 6, agOnes, true);
      lc.setDigit(1, 5, fPart(agSpot) / 10, false);
      lc.setDigit(1, 4, fPart(agSpot) % 10, false);
    }

    float agDeltaABS = abs(agDelta);

    if ((int)agDeltaABS >= 10)
    { // spot moved more than $10
      if (agDelta < 0) lc.setChar(1, 3, '-', false);
      else lc.setChar(1, 3, ' ', false);
      lc.setDigit(1, 2, (int)agDeltaABS / 10, false);
      lc.setDigit(1, 1, (int)agDeltaABS - ((int)agDeltaABS / 10) * 10, true);
      lc.setDigit(1, 0, fPart(agDeltaABS) / 10, false);
    }
    else
    {
      if ((int)agDeltaABS >= 1)
      { // spot moved more than $1
        if (agDelta < 0) lc.setChar(1, 3, '-', false);
        else lc.setChar(1, 3, ' ', false);
        lc.setDigit(1, 2, (int)agDeltaABS, true);
      }
      else
      {
        lc.setChar(1, 3, ' ', false);
        if (agDelta < 0) lc.setChar(1, 2, '-', true);
        else lc.setChar(1, 2, ' ', true);
      }
      lc.setDigit(1, 1, fPart(agDeltaABS) / 10, false);
      lc.setDigit(1, 0, fPart(agDeltaABS) % 10, false);
    }
  }
}


void loop() {

  t.update();  //required for timer events
  // timer events:
  // strobe cycle flag
  // data delay check
  // send config data to esp8266
  // refresh 7-segment display
  // count elapsed time of http error


  // LED control
  if (errorHttpFlag == true || errorDelayFlag == true || wifiStatus == "startUp")
  {
    // error present
    digitalWrite(ledRedPin, HIGH);
    digitalWrite(ledBluePin, LOW);
    digitalWrite(ledGreenPin, LOW);
  }
  else
  {
    // check for delta alerts
    if (abs(auDelta) > auAlertDelta || abs(agDelta) > agAlertDelta)
    {
      // delta alert
      digitalWrite(ledRedPin, LOW);
      digitalWrite(ledBluePin, HIGH);
      digitalWrite(ledGreenPin, LOW);
    }
    else
    {
      // no errors, no delta alerts
      digitalWrite(ledRedPin, LOW);
      digitalWrite(ledBluePin, LOW);
      digitalWrite(ledGreenPin, HIGH);
    }
  }


  //check for available serial data
  while (Serial.available() > 0)
  {
    char received = Serial.read();
    inData += received;

    //process message when new line character is received
    if (received == '\n' || received == '\n')
    {

      // process commands
      if (PRINT_REPLIES_OVER_SERIAL)  {
        Serial.print("Data Received: ");
        Serial.println(inData);
      }

      // check for JSON object
      StaticJsonBuffer<200> jsonBuffer;
      JsonObject& root = jsonBuffer.parseObject(inData);
      // Test if parsing succeeds.
      if (!root.success())
      {
        if (PRINT_REPLIES_OVER_SERIAL) Serial.println("JSON parseObject() failed");
      } else {

        // check if json object has specific keys and update associated data
        // note: data is sent in several json blocks due to serial errors with large blocks
        if (root.containsKey("wifiStatus"))
        {
          const char* tempWifiStatus = root["wifiStatus"];
          wifiStatus = tempWifiStatus;
        }
        if (root.containsKey("time"))
        {
          const char* tempCurTime = root["time"];
          curTime = tempCurTime;
        }
        if (root.containsKey("au")) auSpot = root["au"];
        if (root.containsKey("ag")) agSpot = root["ag"];
        if (root.containsKey("auDelta")) auDelta = root["auDelta"];
        if (root.containsKey("agDelta")) agDelta = root["agDelta"];

        if ((String)wifiStatus != "startUp" && (String)wifiStatus != "noWifi") configSentSuccessFlag = true; // halt sending ssid/password asap

        if (PRINT_REPLIES_OVER_SERIAL) Serial.println("JSON parseObject() success");
      }


      if (serialCommand == SET_SSID)
      {
        //save ssid to eeprom
        for (int i = 0; i < 32; i++)
        {
          if (inData[i] == '\n')
          {
            EEPROM.write(i, 0); // write null terminator
            ssid = inData.substring(0, i);
            break; // end of string
          }
          EEPROM.write(i, inData[i]);
        }
        Serial.print("New SSID: ");
        Serial.println(ssid);
      }

      if (serialCommand == SET_PASSWORD) {
        //save password to eeprom
        for (int i = 0; i < 32; i++)
        {
          if (inData[i] == '\n')
          {
            EEPROM.write(i + EEPROM_PASSWORD_OFFSET, 0); // write null terminator
            password = inData.substring(0, i);
            break; // end of string
          }
          EEPROM.write(i + EEPROM_PASSWORD_OFFSET, inData[i]);
        }
        Serial.print("New Password: ");
        Serial.println(password);
      }

      // save new auAlertDelta to eeprom
      if (serialCommand == SET_AU_ALERT_DELTA)
      {
        //save AuAlertDelta to eeprom
        float auAlertDeltaInput = inData.toFloat();
        int auHundreds = (int)auAlertDeltaInput / 100;
        int auTens = ((int)auAlertDeltaInput - auHundreds * 100) / 10;
        int auOnes = ((int)auAlertDeltaInput - auHundreds * 100 - auTens * 10);
        for (int i = 0; i < 5; i++) {
          if (i == 0) EEPROM.write(i + EEPROM_AU_ALERT_DELTA_OFFSET, auHundreds);
          if (i == 1) EEPROM.write(i + EEPROM_AU_ALERT_DELTA_OFFSET, auTens);
          if (i == 2) EEPROM.write(i + EEPROM_AU_ALERT_DELTA_OFFSET, auOnes);
          if (i == 3) EEPROM.write(i + EEPROM_AU_ALERT_DELTA_OFFSET, fPart(auAlertDeltaInput) / 10);
          if (i == 4) EEPROM.write(i + EEPROM_AU_ALERT_DELTA_OFFSET, fPart(auAlertDeltaInput) % 10);
        }
        auAlertDelta = auAlertDeltaInput;
        Serial.print("New Au Alert Delta: ");
        Serial.println(auAlertDelta);
      }

      // save new agAlertDelta to eeprom
      if (serialCommand == SET_AG_ALERT_DELTA)
      {
        //save agAlertDelta to eeprom
        float agAlertDeltaInput = inData.toFloat();
        for (int i = 0; i < 3; i++) {
          if (i == 0) EEPROM.write(i + EEPROM_AG_ALERT_DELTA_OFFSET, (int)agAlertDeltaInput);
          if (i == 1) EEPROM.write(i + EEPROM_AG_ALERT_DELTA_OFFSET, fPart(agAlertDeltaInput) / 10);
          if (i == 2) EEPROM.write(i + EEPROM_AG_ALERT_DELTA_OFFSET, fPart(agAlertDeltaInput) % 10);
        }
        agAlertDelta = agAlertDeltaInput;
        Serial.print("New Ag Alert Delta: ");
        Serial.println(agAlertDelta);
      }

      serialCommand = SET_NULL;

      if (inData == "?\n" || inData == "help\n" || inData == "\n")
      {
        Serial.println("Commands:");
        Serial.println("show ssid");
        Serial.println("show password");
        Serial.println("set ssid");
        Serial.println("set password");
        Serial.println("show au alert delta");
        Serial.println("show ag alert delta");
        Serial.println("set au alert delta");
        Serial.println("set ag alert delta");
      }

      if (inData == "show ssid\n")
      {
        Serial.print("ssid: ");
        Serial.println(ssid);
      }

      if (inData == "show password\n")
      {
        Serial.print("password: ");
        Serial.println(password);
      }

      if (inData == "show au alert delta\n")
      {
        Serial.print("auAlertDelta: ");
        Serial.println(auAlertDelta);
      }

      if (inData == "show ag alert delta\n")
      {
        Serial.print("agAlertDelta: ");
        Serial.println(agAlertDelta);
      }

      if (inData == "set ssid\n")
      {
        Serial.println("Enter new ssid: ");
        serialCommand = SET_SSID;
      }

      if (inData == "set password\n")
      {
        Serial.println("Enter new password: ");
        serialCommand = SET_PASSWORD;
      }

      if (inData == "set au alert delta\n")
      {
        Serial.println("Enter Au alter delta (xxx.xx): ");
        serialCommand = SET_AU_ALERT_DELTA;
      }

      if (inData == "set ag alert delta\n")
      {
        Serial.println("Enter Ag alter delta (x.xx): ");
        serialCommand = SET_AG_ALERT_DELTA;
      }

      inData = ""; //clear recieved buffer

    }

  } // end while
} // end loop()
