/*
This example will open a configuration portal when the reset button is pressed twice. 
This method works well on Wemos boards which have a single reset button on board. It avoids using a pin for launching the configuration portal.


List files: http://rainguage.local/list?dir=/
Upload files: for file in `ls -A1`; do curl -F "file=@$PWD/$file" rainguage.local/edit; done
Show page: http://rainguage.local/
Debug: http://rainguage.local/debug


How It Works
When the ESP8266 loses power all data in RAM is lost but when it is reset the contents of a small region of RAM is preserved. So when the device starts up it checks this region of ram for a flag to see if it has been recently reset. If so it launches a configuration portal, if not it sets the reset flag. After running for a while this flag is cleared so that it will only launch the configuration portal in response to closely spaced resets.

Settings
There are two values to be set in the sketch.

DRD_TIMEOUT - Number of seconds to wait for the second reset. Set to 10 in the example.
DRD_ADDRESS - The address in RTC RAM to store the flag. This memory must not be used for other purposes in the same sketch. Set to 0 in the example.

This example, contributed by DataCute needs the Double Reset Detector library from https://github.com/datacute/DoubleResetDetector .
*/
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>          //https://github.com/kentaylor/WiFiManager

#include <DoubleResetDetector.h>  //https://github.com/datacute/DoubleResetDetector
#include <FS.h>
#include <NTPClient.h>  
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include "ESPTemplateProcessor.h"


// Number of seconds after reset during which a 
// subseqent reset will be considered a double reset.
#define DRD_TIMEOUT 10

// RTC Memory Address for the DoubleResetDetector to use
#define DRD_ADDRESS 0

DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);

// Onboard LED I/O pin on NodeMCU board
const int PIN_LED = LED_BUILTIN; // D4 on NodeMCU and WeMos. Controls the onboard LED.

// Indicates whether ESP has WiFi credentials saved from previous session, or double reset detected
bool initialConfig = false;



// My Rainguage settings

#define BUTTON_PIN D4
#define LED_PIN LED_BUILTIN
#define DELAY 1000
#define RAINGAUGEDELAY 2000
#define TIPSIZE 0.2794


unsigned long delaytime;
const char myversion[] = "0.3";
String PROGRAM_VERSION = "demo_rainguage";
String bootup_time = "";
// String rainlog = "";
int d4pinval=0;
float rainmm=0;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP,"0.nz.pool.ntp.org", 43200, 60000);
ESP8266WebServer server(80);

//holds the current upload
File fsUploadFile;

String systemstatus() {
 String status = "";
  status += "Name: ";
  status += PROGRAM_VERSION;
  status += "\n";
  status += "Version: ";
  status += myversion; 
  status += "\n";
  status += "SRC: ssh://nelg@localhost:29418/arduino/chris_rainguage/\n";
  status += "Digital pins: D4, set to INPUT_PULLUP (rain guage)\n";
  status += "\n";  
  status += "Time: ";
  status += timeClient.getFormattedDate();
  status += "\n";  
  status += "Boot time: ";
  status += bootup_time;
  status += "\n"; 
  status += "URIs: /, /index, /debug, /test, /inline, /apt/rainmm, /rainlogclear, /rainlogclearepoch, /rainlog";
  return status;
}


void handleDebug() {
  String s = "<html>\r\n<body><h1>" 
  + PROGRAM_VERSION + " v" + myversion  + "</h1><h2>Debug info</h2>";
  s += "</p><p>Time: " + timeClient.getFormattedDate()
  + "</p><p>Boot time: " + bootup_time
  + "</p><p>Rain mm: " + String(rainmm);
  s += "<h5>System status</h5><pre>";
  s += systemstatus();
  s += "</pre>";
  s +="</body></html> ";
  server.send(200, "text/html",s); 
}

void handleTest() {
  if (ESPTemplateProcessor(server).send(String("/index.html"), indexProcessor)) {
      Serial.println("SUCCESS serving file /index.html");
    } else {
      Serial.println("FAIL");
      server.send(200, "text/plain", "page not found.");
  }
}
void handleRoot() {
  if (ESPTemplateProcessor(server).send(String("/graph.html"), indexProcessor)) {
      Serial.println("SUCCESS serving file /graph.html");
    } else {
      Serial.println("FAIL");
      server.send(200, "text/plain", "page not found.");
  }
}
String indexProcessor(const String& key) {
  Serial.println(String("KEY IS ") + key);
  if (key == "TITLE") return "Rain guage @ " + timeClient.getFormattedDate();
  else if (key == "BODY") return "It works!";
  else if (key == "DAILY") return readrainloghours();
  else if (key == "WEEKLY") return readrainlogweekly();
  else if (key == "MONTHLY") return "[ 1,2,3,4,5,6,7,8,9,10,11,12 ],[ 5,4,6,2,6,6,6,2,5,4,2,7 ]";
  else
  return "oops";
}

//format bytes
String formatBytes(size_t bytes){
  if (bytes < 1024){
    return String(bytes)+"B";
  } else if(bytes < (1024 * 1024)){
    return String(bytes/1024.0)+"KB";
  } else if(bytes < (1024 * 1024 * 1024)){
    return String(bytes/1024.0/1024.0)+"MB";
  } else {
    return String(bytes/1024.0/1024.0/1024.0)+"GB";
  }
}

String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path){
  Serial.println("handleFileRead: " + path);
  if(path.endsWith("/")) path += "index.htm";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void handleFileUpload(){
  if(server.uri() != "/edit") return;
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    if(!filename.startsWith("/")) filename = "/"+filename;
    Serial.print("handleFileUpload Name: "); Serial.println(filename);
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if(upload.status == UPLOAD_FILE_WRITE){
    //Serial.print("handleFileUpload Data: "); Serial.println(upload.currentSize);
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile)
      fsUploadFile.close();
    Serial.print("handleFileUpload Size: "); Serial.println(upload.totalSize);
  }
}

void handleFileDelete(){
  if(server.args() == 0) return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  Serial.println("handleFileDelete: " + path);
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(!SPIFFS.exists(path))
    return server.send(404, "text/plain", "FileNotFound");
  SPIFFS.remove(path);
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileCreate(){
  if(server.args() == 0)
    return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  Serial.println("handleFileCreate: " + path);
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(SPIFFS.exists(path))
    return server.send(500, "text/plain", "FILE EXISTS");
  File file = SPIFFS.open(path, "w");
  if(file)
    file.close();
  else
    return server.send(500, "text/plain", "CREATE FAILED");
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileList() {
  if(!server.hasArg("dir")) {server.send(500, "text/plain", "BAD ARGS"); return;}
  
  String path = server.arg("dir");
  Serial.println("handleFileList: " + path);
  Dir dir = SPIFFS.openDir(path);
  path = String();

  String output = "[";
  while(dir.next()){
    File entry = dir.openFile("r");
    if (output != "[") output += ',';
    bool isDir = false;
    output += "{\"type\":\"";
    output += (isDir)?"dir":"file";
    output += "\",\"name\":\"";
    output += String(entry.name()).substring(1);
    output += "\"}";
    entry.close();
  }
  
  output += "]";
  server.send(200, "text/json", output);
}




void setup() {
  // put your setup code here, to run once:
  // initialize the LED digital pin as an output.
  pinMode(PIN_LED, OUTPUT);
  Serial.begin(115200);
  Serial.println("\n Starting");
  WiFi.printDiag(Serial); //Remove this line if you do not want to see WiFi password printed
  if (WiFi.SSID()==""){
    Serial.println("We haven't got any access point credentials, so get them now");   
    initialConfig = true;
  }
  if (drd.detectDoubleReset()) {
    Serial.println("Double Reset Detected");
    initialConfig = true;
  }
  if (initialConfig) {
    Serial.println("Starting configuration portal.");
    digitalWrite(PIN_LED, LOW); // turn the LED on by making the voltage LOW to tell us we are in configuration mode.
    //Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    //sets timeout in seconds until configuration portal gets turned off.
    //If not specified device will remain in configuration mode until
    //switched off via webserver or device is restarted.
    //wifiManager.setConfigPortalTimeout(600);

    //it starts an access point 
    //and goes into a blocking loop awaiting configuration
    if (!wifiManager.startConfigPortal()) {
      Serial.println("Not connected to WiFi but continuing anyway.");
    } else {
      //if you get here you have connected to the WiFi
      Serial.println("connected...yeey :)");
    }
    digitalWrite(PIN_LED, HIGH); // Turn led off as we are not in configuration mode.
    ESP.reset(); // This is a bit crude. For some unknown reason webserver can only be started once per boot up 
    // so resetting the device allows to go back into config mode again when it reboots.
    delay(5000);
  }

  digitalWrite(PIN_LED, HIGH); // Turn led off as we are not in configuration mode.
  WiFi.mode(WIFI_STA); // Force to station mode because if device was switched off while in access point mode it will start up next time in access point mode.
  unsigned long startedAt = millis();
  Serial.print("After waiting ");
  int connRes = WiFi.waitForConnectResult();
  float waited = (millis()- startedAt);
  Serial.print(waited/1000);
  Serial.print(" secs in setup() connection result is ");
  Serial.println(connRes);
  if (WiFi.status()!=WL_CONNECTED){
    Serial.println("failed to connect, finishing setup anyway");
  } else{
    Serial.print("local ip: ");
    Serial.println(WiFi.localIP());
  }

  // My Raingage setup
  Serial.println();
  Serial.println("Rain guage chris program: V0.1"); 
  Serial.println("Delay: " + DELAY); 
  Serial.println("Reading pin 2 as pull up"); 
  Serial.println("Serial speed: 115200"); 
  Serial.println("Hardware: Wemos D1 R2 and Mini"); 
  Serial.println("Anduino: Bub - 1.8.5"); 
  
  // Setup the button with an internal pull-up :
  pinMode(BUTTON_PIN,INPUT_PULLUP);

  

  //Setup the LED :
  pinMode(LED_PIN,OUTPUT);
 
  delaytime = -RAINGAUGEDELAY;
  timeClient.begin();  
  timeClient.update();
  server.on("/", handleRoot);
  server.on("/index", handleTest);
  server.on("/debug", handleDebug);
  server.on("/inline", [](){
    server.send(200, "text/plain", "Testing inline");
    
  });
  server.on("/rainlog", [](){
    handleFileRead("/rainlog.txt");
   });
  
  server.on("/rainlogclear", [](){
    handleFileRead("/rainlog.txt");
    SPIFFS.remove("/rainlog.txt");
  });

   
  server.on("/rainlogepochclear", [](){
    handleFileRead("/rainlogepoch.txt");
    SPIFFS.remove("/rainlogepoch.txt");
  });
  
  server.on("/api/rainmm", [](){
    server.send(200, "text/plain", String(rainmm));
  });
  
  server.on("/list", HTTP_GET, handleFileList);
  //load editor
  server.on("/edit", HTTP_GET, [](){
    if(!handleFileRead("/edit.htm")) server.send(404, "text/plain", "FileNotFound");
  });
  //create file
  server.on("/edit", HTTP_PUT, handleFileCreate);
  //delete file
  server.on("/edit", HTTP_DELETE, handleFileDelete);
  //first callback is called after the request has ended with all parsed arguments
  //second callback handles file uploads at that location
  server.on("/edit", HTTP_POST, [](){ server.send(200, "text/plain", ""); }, handleFileUpload);
  
  //called when the url is not defined here
  //use it to load content from SPIFFS
  server.onNotFound([](){
    if(!handleFileRead(server.uri()))
      server.send(404, "text/plain", "FileNotFound");
  });

  //get heap status, analog input value and all GPIO statuses in one json call
  server.on("/all", HTTP_GET, [](){
    String json = "{";
    json += "\"heap\":"+String(ESP.getFreeHeap());
    json += ", \"analog\":"+String(analogRead(A0));
    json += ", \"gpio\":"+String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
    json += "}";
    server.send(200, "text/json", json);
    json = String();
  });
  
   server.begin();

   if (!MDNS.begin("rainguage")) {
    Serial.println("Error setting up MDNS responder rainguage.local !");
    while (1) {
      delay(1000);
    }
   }
   Serial.println("mDNS responder started");
   
  // Add service to MDNS-SD
   MDNS.addService("http", "tcp", 80);

  Serial.println("SPIFFS Begin");
  SPIFFS.begin();
  Serial.println("SPIFFS Begin completed");
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {    
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      Serial.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
    }
    Serial.printf("\n");
  }
  




   bootup_time = timeClient.getFormattedDate();
}


void loop() {
  // Call the double reset detector loop method every so often,
  // so that it can recognise when the timeout expires.
  // You can also call drd.stop() when you wish to no longer
  // consider the next reset as a double reset.
  drd.loop();
  timeClient.update();
  // put your main code here, to run repeatedly:
  // My raingauge loop
  server.handleClient();

  if ( millis() > delaytime + RAINGAUGEDELAY ) {
    int value = digitalRead(D4);
    if ( value == LOW && d4pinval != value ) {
      d4pinval = value;
      delaytime = millis();
      Serial.print("Rain guage tick: ");
      Serial.println(delaytime);
      writerainlog();
   //   rainlog += timeClient.getFormattedDate();
   //   rainlog += "\n";
      rainmm += TIPSIZE;
     }
     else {
      d4pinval = value;
     }
     
  }
  
}

void writerainlog() {
 File f = SPIFFS.open("/rainlog.txt", "a");
  if (!f) {
    Serial.println("Failed to open /rainlog.txt");
    return;
  }
  if (!f.println(timeClient.getFormattedDate())) {
      Serial.println("Failed to write to /rainlog.txt");
      return;
  }
  f.close();
  File fe = SPIFFS.open("/rainlogepoch.txt", "a");
  if (!fe) {
    Serial.println("Failed to open /rainlogepoch.txt");
    return;
  }
  if (!fe.println(timeClient.getEpochTime())) {
      Serial.println("Failed to write to /rainlogepoch.txt");
      return;
  }
  fe.close();
}


String readrainloghours() {
 File f = SPIFFS.open("/rainlogepoch.txt", "r");
  if (!f) {
    Serial.println("Failed to open /rainlogepoch.txt");
    return "failed";
  }
  int currenttime = timeClient.getEpochTime();  
  int midnight = (currenttime / 86400) * 86400;
  String output = "";
  for (int day=0; day < 7; day++) {
    int daymidnight = midnight - (day * 86400);
  
    float hours[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    f.seek(0);
    while(f.available()){
      String line = f.readStringUntil('\n');
      int lineepoch = line.toInt();
      Serial.print("Rain record: ");
      Serial.println(line);
      for (int i=0; i <= 23; i++) {
             
         if (lineepoch > (daymidnight + (i * 3600)) and lineepoch <= (daymidnight + ((i+1) * 3600))) {
            Serial.println("adding 1");
            hours[i] += TIPSIZE;
         }
      }
     }
     output += "[";
     for (int i=0; i <= 23; i++) {
       output += hours[i];
     if (i < 23) { output += ","; }
     else { output += "]"; }
     }
     if (day < 6) { output += ",\n"; }
     
  }
  
  f.close();  
  return output;
}

String readrainlogweekly() {
 File f = SPIFFS.open("/rainlogepoch.txt", "r");
  if (!f) {
    Serial.println("Failed to open /rainlogepoch.txt");
    return "failed";
  }
  int currenttime = timeClient.getEpochTime();  
  int midnight = (currenttime / 86400) * 86400;
  String output = "";
  float days[7];
  for (int week=0; week < 6; week++) {
  
  for (int day=0; day < 7; day++) {
    int daymidnight = midnight - (day * 86400) - ((86400*7) * week);

    days[day] = 0;
    f.seek(0);
    while(f.available()){
      String line = f.readStringUntil('\n');
      int lineepoch = line.toInt();
                
         if (lineepoch > (daymidnight) and lineepoch <= (daymidnight + ( 24 * 3600))) {
            Serial.println("adding 1 for day");
            days[day] += TIPSIZE;
         }
      
     }
  
     }
     output += "[";
     for (int i=0; i < 7; i++) {
       output += days[i];
     if (i < 6) { output += ","; }
     else { output += "]"; }
     }
     if (week < 5) { output += ",\n"; }
     
  
  }
  f.close();  
  return output;
}
