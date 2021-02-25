// Project Name: spotClock
//
// Filename: esp8266HttpClient.ino
// Author: Reuben Strangelove adastra@vt.edu
// Last Revision Date: 7/2016
// Revision: 1
//
// Operational notes:
//  Revision 1: Fully working, grabs Ag and Au spot prices from artofmystate.com/spot/getSpot.php
//  blocking delays used, no need for realtime

#include <Arduino.h>
#include <ArduinoJson.h>        // https://github.com/bblanchon/ArduinoJson
#include <ESP8266WiFi.h>        // https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WiFi
#include <ESP8266WiFiMulti.h>   // https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WiFi
#include <ESP8266HTTPClient.h>  // https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WiFi

#define USE_SERIAL Serial

// disable printing information over serial besides actual data used in the sytem
#define PRINT_REPLIES_OVER_SERIAL false


// time delay between wifi connect attempts
#define DATA_FINDING_CONNECTION_UPDATE_INTERVAL 1000 // in milliseconds

// time delay between data requests to http sever
#define DATA_CONNECTED_UPDATE_INTERVAL 5000 // in milliseconds

ESP8266WiFiMulti WiFiMulti;

String wifiStatus;
int httpCode;
String inData; // data received by serial
const char* ssid;
const char* password;

void setup() {

  USE_SERIAL.begin(57600);


  // ssid = "SkynetG";
  //  password = "56T34s78P12x";
  ssid = "xxxx";
  password = "xxxx";

  if (PRINT_REPLIES_OVER_SERIAL) Serial.println("esp8266 start up");

  WiFiMulti.addAP(ssid, password);

  // loop until ssid and password received from spotClock controller
  // opens a wifi connection
  while ((WiFiMulti.run() != WL_CONNECTED)) {

    delay(DATA_FINDING_CONNECTION_UPDATE_INTERVAL);

    //check for available serial data
    while (Serial.available() > 0)
    {
      char received = Serial.read();
      inData += received;

      //process message when new line character is received
      if (received == '\n')
      {

        // process commands
        if (PRINT_REPLIES_OVER_SERIAL) {
          Serial.print("Data Received: ");
          Serial.println(inData);
        }

        // check for JSON object
        StaticJsonBuffer<1000> jsonBuffer;
        JsonObject& rootReceived = jsonBuffer.parseObject(inData);
        // Test if parsing succeeds.
        if (!rootReceived.success()) {
          if (PRINT_REPLIES_OVER_SERIAL) Serial.println("parseObject() failed");
        } else {
          if (rootReceived.containsKey("ssid")) ssid = rootReceived["ssid"];
          if (rootReceived.containsKey("password")) password = rootReceived["password"];
          if (PRINT_REPLIES_OVER_SERIAL) 
          {    }
            Serial.print("ssid: ");
            Serial.println(ssid);
            Serial.print("password: ");
            Serial.println(password);
        
          
          WiFiMulti.addAP(ssid, password);

          if (PRINT_REPLIES_OVER_SERIAL) Serial.println("parseObject() success");
        }
      }
    
    }

 // print nowifi status until connection is found
      StaticJsonBuffer<200> jsonBufferSend;
      JsonObject& dataToSend = jsonBufferSend.createObject();
      dataToSend["wifiStatus"] = "noWifi";
      dataToSend.printTo(Serial);
      Serial.println();
    
  }
}

void loop() {

  // check for WiFi connection
  if ((WiFiMulti.run() == WL_CONNECTED))
  {

    HTTPClient http;
    http.begin("http://artofmystate.com/spot/getSpot.php");
    httpCode = http.GET();  // start connection and send HTTP header

    // httpCode will be negative on error
    if (httpCode > 0)
    {
      // file found at server
      if (httpCode == HTTP_CODE_OK)
      {
        String payload = http.getString();
        //USE_SERIAL.println(payload);

        // parse JSON object
        StaticJsonBuffer<1000> jsonBuffer;
        JsonObject& root = jsonBuffer.parseObject(payload);

        // send data to spotClock master controller
        // build JSON object

        // break the data into several objects
        // there's a flow error, maybe a buffer problem, that fails with receiving long strings
        for (int iData = 0; iData < 3; iData++)
        {
          StaticJsonBuffer<500> jsonBufferSend;
          JsonObject& dataToSend = jsonBufferSend.createObject();

          if (iData == 0)
          {
            dataToSend["wifiStatus"] = (String)httpCode;
            dataToSend["time"] = root["time"];;
          }
          if (iData == 1)
          {
            dataToSend["au"] = root["au"];
            dataToSend["auDelta"] = root["auDelta"];
          }
          if (iData == 2)
          {
            dataToSend["ag"] = root["ag"];
            dataToSend["agDelta"] = root["agDelta"];
          }

          // print data to serial
          dataToSend.printTo(Serial);
          Serial.println();

          delay(100); // blocking delay OK, this allows receiving device time to proccess UART buffer

        }
      }

      http.end();

    }
  }
  else
  {
    wifiStatus = "noWifi";
  }

  // print out status if wifi connection is not found or http not OK
  if (wifiStatus == "noWifi" || httpCode != HTTP_CODE_OK)
  {
    StaticJsonBuffer<500> jsonBufferSend;
    JsonObject& dataToSend = jsonBufferSend.createObject();
    if (wifiStatus == "noWifi")  dataToSend["wifiStatus"] = "noWifi";
    else  dataToSend["wifiStatus"] = httpCode;
    dataToSend.printTo(Serial);
    Serial.println();
  }

  delay(DATA_CONNECTED_UPDATE_INTERVAL);


}
