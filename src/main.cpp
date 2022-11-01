#define TINY_GSM_MODEM_SIM7000
#define TINY_GSM_RX_BUFFER 1024 // Set RX buffer to 1Kb

#include <Arduino.h>
#include <stdio.h>
#include <SoftwareSerial.h>
#include <TinyGsmClient.h>
#include <esp_now.h>
#include <WiFi.h>
#include <StreamDebugger.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WebSerial.h>
#include <Preferences.h>

#define SerialAT Serial1
#define uS_TO_S_FACTOR      1000000ULL  // Conversion factor for micro seconds to seconds
#define TIME_TO_SLEEP       60          // Time ESP32 will go to sleep (in seconds)

#define UART_BAUD           115200
#define PIN_DTR             25
#define PIN_TX              27
#define PIN_RX              26
#define PWR_PIN             4

#define GSM_PIN ""

Preferences prefs;

char sentinels[][20] = {"00:00:00:00:00:00",    //A1
                        "00:00:00:00:00:01"};   //A2

String Ssentinels[] = {"00:00:00:00:00:00",
                       "00:00:00:00:00:01"};

char networking[14][20] = {"", "", "", "", "", "", "", "", "", "", "", "", "", ""};
char reqResponse[][10] =  {"", "", ""};

SoftwareSerial monitor(14, 15);

char ssid[16] = "HostAP-";
char psk[13] = "b4s4ltr0ck5";
StreamDebugger debugger(SerialAT, Serial);
StreamDebugger monitor_debug(Serial, monitor);
TinyGsm modem(debugger);
const char apn[]  = "m2mdirect";     //SET TO YOUR APN
const char gprsUser[] = "";
const char gprsPass[] = "";
String response = "";
String subResponse = "";
String ip_address = "";
String webinfo = "";
int ipnum = 1;
int netRetry = 0;
int requestNum = 0;
int reqReset = 0;

uint8_t address[][6] = {{0, 0, 0, 0, 0, 0},   //A1
                        {0, 0, 0, 0, 0, 1},   //A2
                        {0, 0, 0, 0, 0, 2}};  //SELF

String Saddress[] = {"00:00:00:00:00:00",
                     "00:00:00:00:00:01",
                     "00:00:00:00:00:02"};

typedef struct message {
  int x;
  int y;
} message;

message myData;

bool state = true;
int result = 0;
int retry = 3;
volatile bool sendStatus = true;

unsigned long timer = 0;
unsigned long reqTime = 0;
bool waiting = false;

volatile bool scanState = false;
volatile bool updateData = false;

int counter, lastIndex, numberOfPieces = 24;
String pieces[24], input;

esp_now_peer_info_t peerInfo;

AsyncWebServer server(80);

/* Function Prototypes*/
void modemPowerOn();
void modemPowerOff();
void modemRestart();

/*ESP-Now functions*/
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void ScanForSlave();

/*Modem functions*/
void modemConfigure();
int  modemConnect();
void modemDisconnect();

/*HTTP request functions*/
int  httpRequest();
void resetRelay();
void restartSelf();

void recvMsg(uint8_t *data, size_t len);

void setup(){
  Serial.begin(115200);
  SerialAT.begin(UART_BAUD, SERIAL_8N1, PIN_RX, PIN_TX);
  monitor.begin(9600);

  WiFi.mode(WIFI_AP_STA);
  strcat(ssid, WiFi.getHostname()+6);
  WiFi.softAP(ssid, psk, 1, 0, 1);

  WebSerial.msgCallback(recvMsg);
  WebSerial.begin(&server);
  server.begin();

  WebSerial.println("Server started");

  monitor_debug.printf("\n\rStarting output.\n\n\r");

  monitor_debug.printf("AP:  %s\n\r",ssid);
  monitor_debug.printf("MAC: %s\n\n\r",WiFi.macAddress().c_str());


  if (esp_now_init() != ESP_OK) {
    monitor_debug.println("Error initializing ESP-NOW");
    WebSerial.println("Error initializing ESP-NOW");
    return;
  }

  myData.x = 1;
  myData.y = 10;

  esp_now_register_send_cb(OnDataSent);

  String stringTest;
  int bufferTest;

  prefs.begin("Data", false);
  stringTest = prefs.getString("Sen1", "test");
  if (stringTest.compareTo("test") == 0){
    prefs.putString("Sen1", Ssentinels[0]);
  }
  else {
    strcpy(sentinels[0], stringTest.c_str());
    Ssentinels[0] = stringTest;
  }

  stringTest = prefs.getString("Sen2", "test");
  if (stringTest.compareTo("test") == 0){
    prefs.putString("Sen2", Ssentinels[1]);
  }
  else {
    strcpy(sentinels[1], stringTest.c_str());
    Ssentinels[1] = stringTest;
  }

  bufferTest = prefs.getBytesLength("Rel1");
  monitor_debug.printf("\n\rRel1 buffer length: %d\n\r", bufferTest);
  if (bufferTest != 0){
    prefs.getBytes("Rel1", address[0], 6);
  }
  else {
    prefs.putBytes("Rel1", address[0], 6);
  }

  bufferTest = prefs.getBytesLength("Rel2");
  monitor_debug.printf("\n\rRel2 buffer length: %d\n\r", bufferTest);
  if (bufferTest != 0){
    prefs.getBytes("Rel2", address[1], 6);
  }
  else {
    prefs.putBytes("Rel2", address[1], 6);
  }

  prefs.end();

  String relTemp = "";
  char buffer[10] = "";

  for (int i = 0; i < 6; i++){
    itoa(address[0][i], buffer, 16);
    relTemp.concat(buffer);
    if (i < 5)
      relTemp.concat(":");
  }
  relTemp.toUpperCase();
  Saddress[0] = relTemp;

  relTemp = "";
  for (int i = 0; i < 6; i++){
    itoa(address[1][i], buffer, 16);
    relTemp.concat(buffer);
    if (i < 5)
      relTemp.concat(":");
  }
  relTemp.toUpperCase();
  Saddress[1] = relTemp;

  for (int i = 0; i < sizeof(address) / 6; i++){
    memcpy(peerInfo.peer_addr, address[i], 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      monitor_debug.printf("Peer %02X:%02X:%02X:%02X:%02X:%02X failed.\n\r", address[i][0], address[i][1], address[i][2], address[i][3], address[i][4], address[i][5]);
      WebSerial.println("Peer failed");
    }
    else {
      monitor_debug.printf("Peer %02X:%02X:%02X:%02X:%02X:%02X added successfully.\n\r", address[i][0], address[i][1], address[i][2], address[i][3], address[i][4], address[i][5]);
      WebSerial.println("Peer success");
    }
  }

  monitor_debug.println("\nTurning on modem.");
  WebSerial.println("Starting modem");
  modemPowerOn();

  monitor_debug.println("Initialising...");
  WebSerial.println("Initialising...");
  if (!modem.init()) {
    modemRestart();
    monitor_debug.println("Waiting for modem");
    WebSerial.println("Waiting for modem");
    delay(5000);
  }

  modemConfigure();
  
  ScanForSlave();

  while(state){
    if (modemConnect() == 1){
      netRetry = 1;
      break;
      //restartSelf();
    }
    else {
      monitor_debug.printf("Connection failed. Retry attempt %d\n\r", netRetry);
      WebSerial.println("Connection Failed. Retry attempt " + String(netRetry));
      netRetry++;
      continue;
    }
  }
  if (httpRequest() == 1){
    monitor_debug.println("Request successful.");
    WebSerial.println("Request successful");
  }
  else { 
    monitor_debug.println("Request failed.");
    WebSerial.println("Request failed");
  }
  resetRelay();
  timer = millis();
}

void loop(){
  if (millis()-timer > 60000){
    WebSerial.println("");
    monitor_debug.println();
    switch(modemConnect())
    {
      case 2:
        timer = 0;
        state = false;
        break;
      case 0:
        timer = millis();
        waiting = false;
        state = false;
        break;
      default:
        state = true;
        break;
    }
    retry = 3;
    while (state){
      switch (httpRequest())
      {
      case 0:
        monitor_debug.println("Request error");
        WebSerial.println("Request error");
        timer = millis();
        waiting = false;
        state = false;
        break;
      case 2:
        monitor_debug.println("HTTP request timed out");
        WebSerial.println("HTTP request timed out");
        timer = millis();
        waiting = false; 
        state = false;
        break;
      default:
        monitor_debug.println("Request successful\n\r");
        WebSerial.println("Request successful");
        resetRelay();
        timer = millis();
        waiting = false;
        state = false;
        break;
      }
    }
  }
  else {
    if (!waiting){
      monitor_debug.printf("Waiting");
      WebSerial.print("Waiting");
      waiting = true;
    }
    else {
      if (scanState){
        ScanForSlave();
        scanState = false;
      }
      else if (updateData){
        monitor_debug.println("\n\r------------------------------------------------------");
        monitor_debug.println("Use update command to set Sentinel and Relay addresses");
        monitor_debug.println("Command structure:\n\r\"senX XX:XX:XX:XX:XX:XX\"\n or \n\r\"relX XX:XX:XX:XX:XX:XX\"");
        monitor_debug.println("Use command \"done\" to finish");
        monitor_debug.println("------------------------------------------------------");

        WebSerial.println("\n------------------------------------------------------");
        WebSerial.println("Use update command to set Sentinel and Relay addresses");
        WebSerial.println("Command structure:\n\"senX XX:XX:XX:XX:XX:XX\"\n or \n\"relX XX:XX:XX:XX:XX:XX\"");
        WebSerial.println("Use command \"done\" to finish");
        WebSerial.println("------------------------------------------------------");
        while(updateData){
          if (scanState){
            ScanForSlave();
            scanState = false;
          }
        }
      }
      monitor_debug.printf(".");
      WebSerial.print(".");
      delay(2500);
    }
  }
}

void modemPowerOn(){
    pinMode(PWR_PIN, OUTPUT);
    digitalWrite(PWR_PIN, LOW);
    delay(1000);    //Datasheet T_on mintues = 1S
    digitalWrite(PWR_PIN, HIGH);
}

void modemPowerOff(){
    pinMode(PWR_PIN, OUTPUT);
    digitalWrite(PWR_PIN, LOW);
    delay(1500);    //Datasheet T_on mintues = 1.2S
    digitalWrite(PWR_PIN, HIGH);
}

void modemRestart(){
    modemPowerOff();
    delay(1000);
    modemPowerOn();
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status){
  monitor_debug.printf("Send status: ");
  WebSerial.print("Send status: ");
  if (status == ESP_NOW_SEND_SUCCESS){
    monitor_debug.println("Delivery success\n");
    WebSerial.println("Delivery success");
    sendStatus = true;
  }
  else {
    monitor_debug.println("Delivery failed");
    WebSerial.println("Delivery failed");
    sendStatus = false;
  }
}

void ScanForSlave(){
  int8_t scanResults = WiFi.scanNetworks();
  monitor_debug.println();
  for (int i = 0; i < scanResults; ++i){
    if (WiFi.SSID(i).indexOf("Relay") == 0){
      monitor_debug.println("--------------------------------");
      monitor_debug.printf("SSID: %s\n\r", WiFi.SSID(i).c_str());
      monitor_debug.printf("MAC:  %s\n\r", WiFi.BSSIDstr(i).c_str());
      monitor_debug.printf("RSSI: %d\n\r", WiFi.RSSI(i));

      WebSerial.println("--------------------------------");
      WebSerial.println("SSID: " + WiFi.SSID(i));
      WebSerial.println("MAC:  " + WiFi.BSSIDstr(i));
      WebSerial.println("RSSI: " + String(WiFi.RSSI(i)));
    }
  }
  monitor_debug.println("--------------------------------");
  WebSerial.println("--------------------------------");
}

void modemConfigure(){
  WebSerial.println("Configuring modem");
  modem.sendAT("+CFUN=0 ");
  if(modem.waitResponse(10000L) != 1){
    DBG(" +CFUN=0 FALSE");
  }
  delay(200);

  String res;
  res = modem.setNetworkMode(38); // LTE only
  if (res != "1"){
    DBG("Set Network Mode = 38 FALSE");
    return;
  }
  delay(200);

  res = modem.setPreferredMode(1); // CAT-M1 only
  if (res != "1"){
    DBG("Set Preffered Mode = 1 FALSE");
    return;
  }
  delay(200);

  modem.sendAT("+CBANDCFG=\"CAT-M\",3,28");
  if (modem.waitResponse(10000L) != 1){
    DBG(" +CBANDCFG=\"CAT-M1\" ");
  }
  delay(200);

  modem.sendAT("+CFUN=1 ");
  if (modem.waitResponse(10000L) != 1){
    DBG(" +CFUN=1  false ");
  }
  delay(200);

  SerialAT.println("AT+CGDCONT?");
  delay(500);
  if (SerialAT.available())
  {
    input = SerialAT.readString();
    for (int i = 0; i < input.length(); i++)
    {
      if (input.substring(i, i + 1) == "\n") {
          pieces[counter] = input.substring(lastIndex, i);
          lastIndex = i + 1;
          counter++;
      }
      if (i == input.length() - 1) {
          pieces[counter] = input.substring(lastIndex, i);
      }
    }
    // Reset for reuse
    input = "";
    counter = 0;
    lastIndex = 0;

    for ( int y = 0; y < numberOfPieces; y++) {
      for ( int x = 0; x < pieces[y].length(); x++) {
        char c = pieces[y][x];  //gets one byte from buffer
        if (c == ',') {
          if (input.indexOf(": ") >= 0) {
            String data = input.substring((input.indexOf(": ") + 1));
            if ( data.toInt() > 0 && data.toInt() < 25) {
              modem.sendAT("+CGDCONT=" + String(data.toInt()) + ",\"IP\",\"" + String(apn) + "\",\"0.0.0.0\",0,0,0,0");
              if(modem.waitResponse(10000L) != 1){
                monitor_debug.println("Error.");
                WebSerial.println("Error");
              }
            }
            input = "";
            break;
          }
          input = "";
        } else {
            input += c;
        }
      }
    }
    monitor_debug.println("Configuration success!");
    WebSerial.println("Configuration success!");
  } else {
      monitor_debug.println("Failed to get PDP!");
      WebSerial.println("Failed to get PDP!");
  }
}

int modemConnect(){
  monitor_debug.println("\n\nWaiting for network...");
  WebSerial.println("Waiting for network...");
  for (int i = 3; i > 0; i--){
    if (!modem.waitForNetwork()){
      if(i == 1){
        monitor_debug.printf("Failed to connect, restarting ESP\n\r", i-1);
        WebSerial.println("Failed to connect, restarting modem");
        netRetry++;
        modemRestart();
        delay(10000);
        if (!modem.init())
          ESP.restart();
        modemConfigure();
        return 2;
      }
      modem.sendAT("+CGREG?");
      if (modem.waitResponse(10000L, response) != 1){
        response = "";
        WebSerial.println("Connection refused, restarting modem");
        netRetry++;
        modemRestart();
        delay(10000);
        if (!modem.init())
          ESP.restart();
        modemConfigure();
        return 2;
      }
      else {
        response.replace(GSM_OK, "");
        response.replace(GSM_NL, "");
        if (strcmp(response.c_str(), "+CGREG: 0,2") != 0){
          WebSerial.println("Connection refused, restarting modem");
          netRetry++;
          modemRestart();
          delay(10000);
          if (!modem.init())
            ESP.restart();
          modemConfigure();
          return 2;
        }
        else {
          response.replace(GSM_OK, "");
          response.replace(GSM_NL, "");
          WebSerial.println(response);
          modem.sendAT("+CPSI?");
          if(modem.waitResponse(1000L, response) != 1){
            WebSerial.println("Connection refused, restarting modem");
            netRetry++;
            modemRestart();
            delay(10000);
            if (!modem.init())
              ESP.restart();
            modemConfigure();
            return 2;
          }
          else {
            response.replace(GSM_OK, "");
            response.replace(GSM_NL, "");
            WebSerial.println(response);
          }
          modem.sendAT("+COPS?");
          if(modem.waitResponse(1000L, response) != 1){
            WebSerial.println("Connection refused, restarting modem");
            netRetry++;
            modemRestart();
            delay(10000);
            if (!modem.init())
              ESP.restart();
            modemConfigure();
            return 2;
          }
          else {
            response.replace(GSM_OK, "");
            response.replace(GSM_NL, "");
            WebSerial.println(response);
          }
        }
      }
      monitor_debug.printf("Failed to connect, retrying %d times\n\r", i-1);
      WebSerial.print("Failed to connect, retrying ");
      WebSerial.print(i-1);
      WebSerial.println(" times");
      delay(2000);
      continue;
    }
    else
      break;
  }

  if (modem.isNetworkConnected()){
    monitor_debug.println("Network connected");
    WebSerial.println("Network connected");
  }
  else{
    modem.sendAT();
    if (modem.waitResponse(10000L) != 1){
      restartSelf();
    }
    else
      return 0;
  }
  
  monitor_debug.print("GPRS status: ");
  WebSerial.print("GPRS status: ");
  if (modem.isGprsConnected()) {
      monitor_debug.println("connected");
      WebSerial.println("connected");
  } else {
      monitor_debug.println("not connected");
      WebSerial.println("not connected");
      monitor_debug.println("Connecting to: " + String(apn));
      WebSerial.print("Connecting to: ");
      WebSerial.println(apn);
      if (!modem.gprsConnect(apn, gprsUser, gprsPass)){
        monitor_debug.println("Failed to connect to GPRS.");
        WebSerial.println("Failed to connect to GPRS");
      }
  }

  response = "";

  String ccid = modem.getSimCCID();
  monitor_debug.println("CCID: " + ccid);
  WebSerial.print("CCID: ");
  WebSerial.println(ccid);

  String imei = modem.getIMEI();
  monitor_debug.println("IMEI: " + imei);
  WebSerial.print("IMEI: ");
  WebSerial.println(imei);

  String cop = modem.getOperator();
  monitor_debug.println("Operator: " + cop);
  WebSerial.print("Operator: ");
  WebSerial.println(cop);

  IPAddress local = modem.localIP();
  monitor_debug.println("Local IP: " + local.toString());
  WebSerial.print("Local IP: ");
  WebSerial.println(local.toString());

  if (ip_address == "")
    ip_address = local.toString();

  if (ip_address != local.toString()){
    ipnum++;
    ip_address = local.toString();
  }

  monitor_debug.println("IP's:     " + String(ipnum));

  int csq = modem.getSignalQuality();
  monitor_debug.println("Signal quality: " + String(csq));
  WebSerial.print("Signal quality: ");
  WebSerial.println(csq);

  char networkInfo[100] = "";
  modem.sendAT("+CPSI?");
  if (modem.waitResponse(10000L, response) != 1){
    monitor_debug.println("Failed to get network info");
    WebSerial.println("Failed to get network info");
  }
  response.replace(GSM_OK, "");
  response.replace(GSM_NL, "");
  strcpy(networkInfo, response.substring(7).c_str());
  monitor_debug.printf("Network info:\n\r");
  WebSerial.println("Network info:");
  char* token = strtok(networkInfo, ",");
  int tokenCount = 0;
  while (token != NULL){
    strcpy(networking[tokenCount], token);
    token = strtok(NULL, ",");
    tokenCount++;
  }
  if (tokenCount < 5){
    monitor_debug.println(response);
    WebSerial.println(response);
  }
  else {
    monitor_debug.printf("-------------------------\n\r");
    monitor_debug.printf("  Mode:   %s\n\r", networking[0]);
    monitor_debug.printf("  Status: %s\n\r", networking[1]);
    monitor_debug.printf("  Band:   %s\n\r", networking[6]);
    monitor_debug.printf("  RSSI:   %s\n\r", networking[12]);
    monitor_debug.printf("-------------------------\n\n\r");

    WebSerial.println("-------------------------");
    WebSerial.println("  Mode:  " + String(networking[0]));
    WebSerial.println("  Status:  " + String(networking[1]));
    WebSerial.println("  Band:   " + String(networking[6]));
    WebSerial.println("  RSSI:    " + String(networking[12]));
    WebSerial.println("-------------------------");
  }
  return 1;
}

void modemDisconnect(){
  modem.gprsDisconnect();
  if (!modem.isGprsConnected()) {
      monitor_debug.println("GPRS disconnected");
      WebSerial.println("GPRS disconnected");
  } else {
      monitor_debug.println("GPRS disconnect: Failed.");
      WebSerial.println("GPRS disconnect: Failed");
  }
}

int httpRequest(){
  modem.sendAT("+HTTPINIT");
  if (modem.waitResponse(10000L) == 2){
    monitor_debug.println("Failed to initialise connection... Attempting to terminate");
    WebSerial.println("Failed to initialise connection... Attempting to terminate");
    modem.sendAT("+HTTPTERM");
    if (modem.waitResponse(10000L) == 1){
      monitor_debug.println("Connection succesfully terminated.\n\rStarting new connection");
      WebSerial.println("Connection succesfully terminated.\n\rStarting new connection");
      modem.sendAT("+HTTPINIT");
      if (modem.waitResponse(10000L) != 1){
        monitor_debug.println("Failed to initialise connection\n");
        WebSerial.println("Failed to initialise connection\n");
        return 0;
      }
      else {
        monitor_debug.println("HTTP connection initialised");
        WebSerial.println("HTTP connection initialised");
      }
    }
    else {
      monitor_debug.println("Catastrophic failure");
      WebSerial.println("Catastrophic failure");
      return 0;
    }
  }
  else {
    monitor_debug.println("HTTP connection initialised");
    WebSerial.println("HTTP connection initialised");
  }
  delay(200);

  modem.sendAT("+HTTPPARA=\"CID\",1");
  if (modem.waitResponse(10000L) != 1)
    return 0;
  monitor_debug.println("Initialise GET request");
  WebSerial.println("Initialise GET request");
  delay(200);

  modem.sendAT("+HTTPPARA=\"URL\",\"http://13.236.92.153/reset.list\"");
  if (modem.waitResponse(10000L) != 1)
    return 0;
  monitor_debug.println("URL set");
  WebSerial.println("URL set");
  delay(200);

  modem.sendAT("+HTTPACTION=0");
  if (modem.waitResponse(10000L) != 1)
    return 0;
  monitor_debug.println("Start GET request");
  WebSerial.println("Start GET request");
  delay(200);

  int count = 0;
  while (true){
    if (SerialAT.available()){
      response = SerialAT.readString();
      if (strstr(response.c_str(), "+HTTPACTION") != NULL){
        String responseTemp = response;
        responseTemp.replace(GSM_NL, "");
        responseTemp = responseTemp.substring(13);
        char responseInfo[50] = "";
        strcpy(responseInfo, responseTemp.c_str());
        char* resToken = strtok(responseInfo, ",");
        int resTokenCount = 0;
        while (resToken != NULL){
          strcpy(reqResponse[resTokenCount], resToken);
          resToken = strtok(NULL, ",");
          resTokenCount++;
        }
        if (strcmp(reqResponse[1], "200") == 0){
          if (strcmp(reqResponse[2], "1") != 0)
            reqReset++;
          requestNum++;
          reqTime = millis();
        }
        monitor_debug.println("GET request successful");
        WebSerial.println("GET request successful");
        monitor_debug.println(response);
        WebSerial.println(response);
        response = "";
        break;
      }
    }
    else if (count >= 60){
      monitor_debug.println("GET request timeout");
      WebSerial.println("GET request timeout");
      return 2;
    }
    else {
      count++;
      delay(1000);
    }
  }

  modem.sendAT("+HTTPREAD");
  if (modem.waitResponse(10000L, response) != 1)
    return 0;
  monitor_debug.println("Data Read");
  WebSerial.println("Data Read");
  delay(200);

  modem.sendAT("+HTTPTERM");
  if (modem.waitResponse(10000L) != 1)
    return 0;
  monitor_debug.println("Terminate HTTP connection\n");
  WebSerial.println("Terminate HTTP connection\n");
  delay(200);

  return 1;
}

void resetRelay(){
  const char* cResponse = response.c_str();
  for (int i = 0; i < sizeof(sentinels)/sizeof(sentinels[0]); ++i){
    if (strstr(cResponse, sentinels[i]) !=NULL){
      monitor_debug.printf("Sentinel %s offline, attempting to restart.\n\r", sentinels[i]);
      WebSerial.println("Sentinel "+String(sentinels[i])+" offline, attempting to restart.");
      monitor_debug.printf("Sending packet to %02X:%02X:%02X:%02X:%02X:%02X\n\r", address[i][0], address[i][1], address[i][2], address[i][3], address[i][4], address[i][5]);
      WebSerial.println("Sending packet to "+ Saddress[i]);
      for (int j = 5; j > 0; j--){
        if (esp_now_send(address[i], (uint8_t *)&myData, sizeof(myData)) != ESP_OK){
          monitor_debug.printf("Failed. Retrying %d\n\r", j);
          WebSerial.println("Failed. Retrying " + j);
          delay(200);
          continue;
        }
        else {
          delay(100);
          if (sendStatus){
            break;
          }
          else {
            continue;
          }
        }
      }
      delay(1000);
    }
  }
  response = "";
}

void restartSelf(){
  monitor_debug.printf("Modem not responding, hard restarting.\n\r");
  for (int j = 5; j > 0; j--){
    if (esp_now_send(address[2], (uint8_t *)&myData, sizeof(myData)) != ESP_OK){
      monitor_debug.printf("Failed. Retrying %d\n\r", j);
      delay(200);
      continue;
    }
    else {
      if (sendStatus){
        break;
      }
      else {
        continue;
      }
    }
  }
}

void recvMsg(uint8_t *data, size_t len){
  String d = "";
  for(int i=0; i < len; i++){
    d += char(data[i]);
  }
  d.toLowerCase();
  if(updateData){
    if (strstr(d.c_str(), "scan")){
      WebSerial.println("Scanning for ");
      scanState = true;
    }
    else if (strstr(d.c_str(), "clear")){
      monitor_debug.println("Removing all key-value pairs");
      WebSerial.println("Removing all key-value pairs");
      prefs.clear();
    }
    else if (strstr(d.c_str(), "check")){

    }
    else if (strstr(d.c_str(), "done")){
      monitor_debug.println("------------------------------------------------------");
      WebSerial.println("------------------------------------------------------");
      prefs.end();
      updateData = false;
    }  
    else {
      d.toUpperCase();
      char dChar[50] = "";
      if (d.length() > 5){
        strcpy(dChar, d.c_str()+5);
        if (strstr(d.c_str(), "SEN1")){
          strcpy(sentinels[0], dChar);
          Ssentinels[0] = d.substring(5);
          prefs.putString("Sen1", Ssentinels[0]);
          monitor_debug.println("Sentinel 1 updated to: " + Ssentinels[0]);
          WebSerial.println("Sentinel 1 updated to:" + Ssentinels[0]);
        }
        else if (strstr(d.c_str(), "SEN2")){
          strcpy(sentinels[1], dChar);
          Ssentinels[1] = d.substring(5);
          prefs.putString("Sen2", Ssentinels[1]);
          monitor_debug.println("Sentinel 2 updated to: " + Ssentinels[1]);
          WebSerial.println("Sentinel 2 updated to:" + Ssentinels[1]);
        }
        else if (strstr(d.c_str(), "REL1")){
          Saddress[0] = d.substring(5);
          char *updateToken = strtok(dChar, ":");
          int updateTokenCount = 0;
          while(updateToken != NULL){
            address[0][updateTokenCount] = (int)strtol(updateToken, NULL, 16);
            updateToken = strtok(NULL, ":");
            updateTokenCount++;
          }
          monitor_debug.println("Relay 1 updated to: " + Saddress[0]);
          WebSerial.println("Relay 1 updated to: " + Saddress[0]);
          if (prefs.putBytes("Rel1", address[0], 6) == 6){
            monitor_debug.println("Data stored succesfuly");
            WebSerial.println("Data stored succesfuly");
          }
          memcpy(peerInfo.peer_addr, address[0], 6);
          peerInfo.channel = 0;  
          peerInfo.encrypt = false;
          if (esp_now_add_peer(&peerInfo) == ESP_OK) {
            monitor_debug.println("Successfuly added as a Peer");
            WebSerial.println("Successfuly added as a Peer");
          }
          else {
            monitor_debug.println("Failed to add as a Peer");
            WebSerial.println("Failed to add as a Peer");
          }
        }
        else if (strstr(d.c_str(), "REL2")){
          Saddress[1] = d.substring(5);
          char *updateToken = strtok(dChar, ":");
          int updateTokenCount = 0;
          while(updateToken != NULL){
            address[1][updateTokenCount] = (int)strtol(updateToken, NULL, 16);
            updateToken = strtok(NULL, ":");
            updateTokenCount++;
          }
          monitor_debug.println("Relay 2 updated to: " + Saddress[1]);
          WebSerial.println("Relay 2 updated to: " + Saddress[1]);
          if (prefs.putBytes("Rel2", address[1], 6) == 6){
            monitor_debug.println("Data stored succesfuly");
            WebSerial.println("Data stored succesfuly");
          }
          memcpy(peerInfo.peer_addr, address[1], 6);
          peerInfo.channel = 0;  
          peerInfo.encrypt = false;
          if (esp_now_add_peer(&peerInfo) == ESP_OK) {
            monitor_debug.println("Successfuly added as a Peer");
            WebSerial.println("Successfuly added as a Peer");
          }
          else {
            monitor_debug.println("Failed to add as a Peer");
            WebSerial.println("Failed to add as a Peer");
          }
        }
        else {
          WebSerial.println("Not a valid command\nUse senX XX:XX:XX:XX:XX:XX\nor  relX XX:XX:XX:XX:XX");
          WebSerial.println(d.c_str());
        }
      }
      else {
        WebSerial.println("Not a valid command\nUse senX XX:XX:XX:XX:XX:XX\nor  relX XX:XX:XX:XX:XX");
        WebSerial.println(d.c_str());
      }
    }
  }
  else {
    if (strstr(d.c_str(), "test 1") != NULL){
      WebSerial.println("\rTesting Relay 1: " + Saddress[0]);
      if(esp_now_send(address[0], (uint8_t *)&myData, sizeof(myData)) != ESP_OK)
        WebSerial.println("Failed to reach: " + Saddress[0]);
    }
    else if (strstr(d.c_str(), "test 2") != NULL){
      WebSerial.println("\n\rTesting Relay 2: " + Saddress[1]);
      if(esp_now_send(address[1], (uint8_t *)&myData, sizeof(myData)) != ESP_OK)
        WebSerial.println("Failed to reach: " + Saddress[1]);
    }
    else if (strstr(d.c_str(), "stats") != NULL) {
      unsigned long uptime = millis();
      unsigned long requestTimer = uptime - reqTime;
      char *reqTimer = (char*)malloc(13 * sizeof(char)); // DDD:HH:MM:SS
      char *upTimer  = (char*)malloc(13 * sizeof(char)); // DDD:HH:MM:SS
      sprintf(reqTimer, "%03d:%02d:%02d:%02d", requestTimer / 86400000, (requestTimer / 3600000) % 24, (requestTimer / 60000) % 60, (requestTimer / 1000) % 60);
      sprintf(upTimer, "%03d:%02d:%02d:%02d", uptime / 86400000, (uptime / 3600000) % 24, (uptime / 60000) % 60, (uptime / 1000) % 60);
      WebSerial.println("\r\n--------------------------------");
      WebSerial.println("Relay 1:   " + Saddress[0]);
      WebSerial.println("Sentinel: " + Ssentinels[0]);
      WebSerial.println("--------------------------------");
      WebSerial.println("Relay 2:  " + Saddress[1]);
      WebSerial.println("Sentinel: " + Ssentinels[1]);
      WebSerial.println("--------------------------------");
      WebSerial.println("Number of IP's:   " + String(ipnum));
      WebSerial.println("Reconnects:        " + String(netRetry));
      WebSerial.println("Total Requests:    " + String(requestNum));
      WebSerial.println("Request Resets:   " + String(reqReset));
      WebSerial.println("Req Time:           " + String(reqTimer));
      WebSerial.println("Uptime:              " + String(upTimer));
      WebSerial.println("--------------------------------");
      free(reqTimer);
      free(upTimer);
    }
    else if (strstr(d.c_str(), "scan")){
      monitor_debug.println("\n\rScanning for slaves on the next loop");
      WebSerial.println("\nScanning for slaves on next loop");
      scanState = true;
    }
    else if (strstr(d.c_str(), "update")){
      updateData = true;
      prefs.begin("Data", false);
    }
    else {
      WebSerial.println("Not a valid command");
      WebSerial.println(d.c_str());
    }
  }
}