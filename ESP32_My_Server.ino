#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <LittleFS.h>
#include <TimeLib.h> // Include TimeLib library

// Data wire is connected to GPIO 23
#define ONE_WIRE_BUS 23

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature sensor
DallasTemperature sensors(&oneWire);

// Variables to store temperature values and timestamp
String lastMeasurementTime = "";
int deviceCount = 0;

// Timer variables
unsigned long lastTime = 0;
unsigned long timerDelay = 60000;  // 60 seconds

// NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600 * 1, 60000);

// Replace with your network credentials
const char* ssid = "your_SSID";
const char* password = "your_PASSWORD";

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// History structure and buffer size
struct SensorData {
  String address;
  float temperature;
  String time;
};

#define HISTORY_SIZE 180
SensorData history[HISTORY_SIZE];
int historyIndex = 0;

// Global variable to hold the latest JSON response
String latestJsonResponse = "";

// Function to convert device address to string
String getDeviceAddress(DeviceAddress deviceAddress) {
  String address = "";
  for (uint8_t i = 0; i < 8; i++) {
    if (deviceAddress[i] < 16) address += String(0, HEX);
    address += String(deviceAddress[i], HEX);
  }
  return address;
}

// Function to format time in ISO 8601
String formatTimeISO8601(unsigned long epochTime) {
  // Convert epoch time to time_t
  time_t rawTime = epochTime;
  struct tm* timeInfo = localtime(&rawTime);

  char buffer[30];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02dZ",
           timeInfo->tm_year + 1900, timeInfo->tm_mon + 1, timeInfo->tm_mday,
           timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec);
  return String(buffer);
}

// Function to read temperatures and generate JSON data
String getSensorDataJson() {
  sensors.requestTemperatures();
  String jsonResponse = "{\"sensorData\":[";

  for (int i = 0; i < deviceCount; i++) {
    DeviceAddress deviceAddress;
    if (sensors.getAddress(deviceAddress, i)) {
      float tempC = sensors.getTempC(deviceAddress);
      jsonResponse += "{\"address\":\"" + getDeviceAddress(deviceAddress) + "\", \"temperature\":" + String(tempC) + "}";
      if (i < deviceCount - 1) {
        jsonResponse += ",";
      }
      // Store the data in the history array
      history[historyIndex] = {getDeviceAddress(deviceAddress), tempC, lastMeasurementTime};
      historyIndex = (historyIndex + 1) % HISTORY_SIZE;
    }
  }

  jsonResponse += "], \"measurementTime\":\"" + lastMeasurementTime + "\", \"history\":[";

  bool firstEntry = true;
  for (int i = 0; i < HISTORY_SIZE; i++) {
    int index = (historyIndex + i) % HISTORY_SIZE; // Adjusted for chronological order
    if (history[index].address != "") {
      if (!firstEntry) {
        jsonResponse += ",";
      }
      jsonResponse += "{\"address\":\"" + history[index].address + "\", \"temperature\":" + String(history[index].temperature) + ", \"time\":\"" + history[index].time + "\"}";
      firstEntry = false;
    }
  }

  jsonResponse += "]}";
  return jsonResponse;
}

// Function to update the temperature data
void updateTemperatureData() {
  timeClient.update();
  
  if (timeClient.isTimeSet()) {
    lastMeasurementTime = formatTimeISO8601(timeClient.getEpochTime());
  } else {
    Serial.println("NTP Time not synchronized yet");
  }

  // Generate the latest JSON response and store it in the global variable
  latestJsonResponse = getSensorDataJson();
}

void setup() {
  // Serial port for debugging purposes
  Serial.begin(115200);
  Serial.println("Sensors server started");

  // Start up the DS18B20 library
  sensors.begin();

  // Count the number of devices found on the OneWire bus
  deviceCount = sensors.getDeviceCount();
  Serial.print("Found ");
  Serial.print(deviceCount);
  Serial.println(" devices.");

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("Connected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Initialize a connection to the NTP server
  timeClient.begin();

  // Start the LittleFS
  if (!LittleFS.begin()) {
    Serial.println("An error has occurred while mounting LittleFS");
    return;
  }

  // Serve the index.html from LittleFS
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", String(), false);
  });

  // Route to provide temperature data, measurement time, and history
  server.on("/temperatureData", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("Sending JSON Response: ");
    Serial.println(latestJsonResponse);
    request->send(200, "application/json", latestJsonResponse);
  });

  // Start server
  server.begin();
}

void loop() {
  if ((millis() - lastTime) > timerDelay) {
    updateTemperatureData();
    lastTime = millis();
  }
}
