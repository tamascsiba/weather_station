#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "DFRobot_RainfallSensor.h"
#include <HardwareSerial.h>
#include <ArduinoJson.h>

// Define the I2C pins for ESP32
#define SDA_PIN 21   // SDA pin connected to the sensor SDA (GPIO 21)
#define SCL_PIN 22   // SCL pin connected to the sensor SCL (GPIO 22)

#define SOIL_MOISTURE_PIN 32

// Wind speed sensor pin
#define WIND_SENSOR_PIN 33  // GPIO 33

// UV sensor pin
#define UV_SENSOR_PIN 34 // GPIO 34 pin for GUVA-S12SD


Adafruit_BME280 bme; // BME280 sensor
DFRobot_RainfallSensor_I2C rainfallSensor(&Wire); // Rainfall sensor

HardwareSerial simSerial(2);  // SIM module

// Global variables
float temperature;
float humidity;
float pressure;
float altitude;
float clearRawData;
float rainrawdata;
float rainfallLastHour;
float resetRainfallSensor;
float reseted_rainfallLastHour;
int uvIndex;
float maxWindSpeed = 100.0;
float maxVoltage = 2.0;
float windSpeed;
int moisturePercent;

// Variables for calculate the average wind speed per minute
const int maxMeasurements = 60; // Store measurements for 60 seconds
float windSpeedMeasurements[maxMeasurements]; // Array to store wind speed readings
int measurementCount = 0; // Counter to track the number of measurements

// Struct definition for DateTime
struct DateTime {
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
};

// Global time variables
DateTime currentTime;    // To store the current time
unsigned long prevMillis; // Store the last time when the clock was updated
unsigned long elapsedMillis = 0; // Total elapsed time in milliseconds

// Buffer to store JSON data for the entire hour
String hourlyDataBuffer[60]; // Store up to 60 minutes of data
int bufferIndex = 0; // Track the current index in the buffer

// Month days (non-leap year)
const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

void setup() {
  Serial.begin(115200);
  simSerial.begin(115200, SERIAL_8N1, 16, 17);

  // Initialize I2C communication
  Wire.begin(SDA_PIN, SCL_PIN);

  // Initialize the BME280 sensor
  if (!bme.begin(0x76)) {  // BME280's I2C address is 0x76
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1);
  }

  // Initialize the Rainfall sensor
  while (!rainfallSensor.begin()) {
    Serial.println("Rainfall Sensor init error!!!");
    delay(1000);
  }

  // Set the UV sensor and Wind sensor pins as input
  pinMode(UV_SENSOR_PIN, INPUT);
  pinMode(WIND_SENSOR_PIN, INPUT);

  //wakeUpSIM();
  sendATCommand("AT");

  // Set time for SIM A7670E inbuilt RTC from NTP server
  sendATCommand("AT+CNTP=\"pool.ntp.org\",4"); // UTC+1
  sendATCommand("AT+CNTP");
  
  String initialTime = sendATCommand("AT+CCLK?");// Get the current time from the SIM module
  currentTime = parseDateTime(initialTime); // Initialize the DateTime struct with the current time

  prevMillis = millis(); // Record the initial millis() value
}

void loop() {
  // Calculate the elapsed time since last update
  unsigned long currentMillis = millis();
  elapsedMillis += currentMillis - prevMillis; // Add elapsed time
  prevMillis = currentMillis;

  // Update the time every second
  if (elapsedMillis >= 1000) {
    updateTime();
    elapsedMillis = 0;  // Reset the elapsed time counter

    Serial.println("Date: " + String(currentTime.year) + "-" + String(currentTime.month) + "-" + String(currentTime.day));
    Serial.println("Time: " + formatTime(currentTime.hour, currentTime.minute, currentTime.second));
    Serial.println();

    // Measure wind speed every second
    readWindSpeed(); // Measure wind speed

    // Every minute, collect sensor data
    if (currentTime.second == 0) {
      float MaximumWindSpeed = calculateMaxWindSpeed(); // Calculate the maximum wind speed for this minute
      collectSensorData(MaximumWindSpeed); // Pass the maximum to the collect function

      // Reset the wind speed measurement count for the next minute
      measurementCount = 0;
    }

    // Every hour, send all the collected data
    if (currentTime.minute == 0 && currentTime.second == 0) {
      //wakeUpSIM();  // Wake up SIM before sending data
      sendAllSensorData();

      // Set time for SIM A7670E inbuilt RTC from NTP server
      sendATCommand("AT+CNTP=\"pool.ntp.org\",4"); // UTC+1
      sendATCommand("AT+CNTP");

      String initialTime = sendATCommand("AT+CCLK?"); // Get the current time from the SIM module
      currentTime = parseDateTime(initialTime); // Initialize the DateTime struct with the current time

      sleepSIM();  // Put SIM to sleep after sending data
    }
  }
}

// Function to collect sensor data every minute
void collectSensorData(float MaximumWindSpeed) {
  printBME280Values();
  printRainfallSensorValues();
  readUVSensor();
  readWindSpeed();
  readSoilMoisturePercentage();

  // Prepare JSON data for the current minute
  String jsonString = prepareJSON(currentTime, MaximumWindSpeed);

  // Store JSON data in the buffer
  if (bufferIndex < 60) {
    hourlyDataBuffer[bufferIndex++] = jsonString;
  }

  Serial.println("Data collected for minute: " + String(currentTime.minute));
}

// Function to send all collected sensor data every hour
void sendAllSensorData() {
  // All the stored JSON data into a single payload
  String jsonArray = "[";
  for (int i = 0; i < bufferIndex; i++) {
    jsonArray += hourlyDataBuffer[i];
    if (i < bufferIndex - 1) {
      jsonArray += ",";
    }
  }
  jsonArray += "]";

  // Send the entire array as a single HTTP request
  sendATCommand("AT+HTTPINIT"); // Initiate HTTP request
  sendATCommand("AT+HTTPPARA=\"URL\",\"http://35.159.30.117:8080/WeatherReport/ReceiveData\""); // URL
  sendATCommand("AT+HTTPPARA=\"CONTENT\",\"application/json\"");  // Set Content-Type

  int jsonLength = jsonArray.length();
  sendATCommand("AT+HTTPDATA=" + String(jsonLength) + ",10000");
  delay(200);
  simSerial.print(jsonArray); // Send the actual JSON data
  delay(200); // Wait for the data to be sent
  sendATCommand("AT+HTTPACTION=1"); // Send HTTP request
  delay(200); // Wait for response
  sendATCommand("AT+HTTPTERM"); // Close HTTP connection

  // Clear the buffer after sending
  bufferIndex = 0;
  Serial.println("Hourly data sent.");
  resetRainfallSensor = rainfallSensor.getRawData();
}

// Function to update time
void updateTime() {
  currentTime.second++;
  
  if (currentTime.second >= 60) {
    currentTime.second = 0;
    currentTime.minute++;
  }
  
  if (currentTime.minute >= 60) {
    currentTime.minute = 0;
    currentTime.hour++;
  }

  if (currentTime.hour >= 24) {
    currentTime.hour = 0;
    currentTime.day++;
  }

  int daysThisMonth = daysInMonth[currentTime.month - 1];

  if (currentTime.month == 2 && isLeapYear(currentTime.year)) {
    daysThisMonth = 29; // February has 29 days in a leap year
  }

  if (currentTime.day > daysThisMonth) {
    currentTime.day = 1;
    currentTime.month++;
  }

  if (currentTime.month > 12) {
    currentTime.month = 1;
    currentTime.year++;
  }
}

bool isLeapYear(int year) {
  if (year % 4 == 0) {
    if (year % 100 == 0) {
      if (year % 400 == 0) {
        return true;
      } else {
        return false;
      }
    } else {
      return true;
    }
  } else {
    return false;
  }
}

String formatTime(int hour, int minute, int second) {
  String h = (hour < 10) ? "0" + String(hour) : String(hour);
  String m = (minute < 10) ? "0" + String(minute) : String(minute);
  String s = (second < 10) ? "0" + String(second) : String(second);
  return h + ":" + m + ":" + s;
}

// Function to put SIM module to sleep
void sleepSIM() {
  Serial.println("Putting SIM module to sleep...");
  sendATCommand("AT+CSCLK=2");  // Enter sleep mode
}

// Function to wake up SIM module
void wakeUpSIM() {
  Serial.println("Waking up SIM module...");
  sendATCommand("AT");
}

void printBME280Values() {
  temperature = bme.readTemperature();
  Serial.print("Temperature = ");
  Serial.print(temperature);
  Serial.println(" *C");

  pressure = bme.readPressure() / 100.0F;  // Convert to hPa
  Serial.print("Pressure = ");
  Serial.print(pressure);
  Serial.println(" hPa");

  humidity = bme.readHumidity();
  Serial.print("Humidity = ");
  Serial.print(humidity);
  Serial.println(" %");
}

void printRainfallSensorValues() {
  rainrawdata = rainfallSensor.getRawData();
  reseted_rainfallLastHour = rainrawdata - resetRainfallSensor;
  rainfallLastHour = (reseted_rainfallLastHour * 0.279091);
  Serial.print("Rainfall (Last 1 Hour):\t");
  Serial.print(rainfallLastHour);
  Serial.println(" mm");
}

int readUVSensor() {
  int uvAnalogValue = analogRead(34); 
  float UV_voltage = uvAnalogValue * (3.3 / 4095.0); // Convert the analog value to voltage (ESP32 uses 12-bit ADC)
  float uvIntensity = UV_voltage * 1000.0; // Convert voltage to millivolts (mV)
  
  // UV intensity to the UV Index
  if (uvIntensity < 50) {
    uvIndex = 0;
  } else if (uvIntensity < 227) {
    uvIndex = 1;
  } else if (uvIntensity < 318) {
    uvIndex = 2;
  } else if (uvIntensity < 408) {
    uvIndex = 3;
  } else if (uvIntensity < 503) {
    uvIndex = 4;
  } else if (uvIntensity < 606) {
    uvIndex = 5;
  } else if (uvIntensity < 696) {
    uvIndex = 6;
  } else if (uvIntensity < 795) {
    uvIndex = 7;
  } else if (uvIntensity < 881) {
    uvIndex = 8;
  } else if (uvIntensity < 976) {
    uvIndex = 9;
  } else if (uvIntensity < 1079) {
    uvIndex = 10;
  } else {
    uvIndex = 11;
  }

  return uvIndex;
}

void readWindSpeed() {
    int windAnalogValue = analogRead(WIND_SENSOR_PIN);
    
    // Voltage calculation
    float wind_voltage = (windAnalogValue / 4095.0) * maxVoltage;

    // Calculate wind speed based on the sensor's scale (maxWindSpeed = 100 km/h)
    float currentWindSpeed = (wind_voltage / maxVoltage) * maxWindSpeed;

    // Store the current wind speed in the array
    if (measurementCount < maxMeasurements) {
        windSpeedMeasurements[measurementCount++] = currentWindSpeed; // Store current speed and increment counter
    }
}

float calculateMaxWindSpeed() {
    if (measurementCount == 0) return 0.0; // Return 0 if no measurements are available
    
    float maxWindSpeed = windSpeedMeasurements[0]; // Initialize with the first element
    for (int i = 1; i < measurementCount; i++) {
        if (windSpeedMeasurements[i] > maxWindSpeed) {
            maxWindSpeed = windSpeedMeasurements[i]; // Update max if current is greater
        }
    }
    return maxWindSpeed; // Return the maximum wind speed
}

int readSoilMoisturePercentage() {
  int moistureLevel = analogRead(SOIL_MOISTURE_PIN);   
  moisturePercent = map(moistureLevel, 0, 4095, 100, 0); 
  return moisturePercent;                    
}

String prepareJSON(DateTime dt, float MaximumWindSpeed) {
  String jsonString = 
    "{\"Date\": \"" + String(dt.year) + "-" + String(dt.month) + "-" + String(dt.day) + "\"" +
    ", \"Time\": \"" + formatTime(dt.hour, dt.minute, dt.second) + "\"" +
    ", \"temperature\": " + String(temperature, 2) +
    ", \"humidity\": " + String(humidity, 2) +
    ", \"pressure\": " + String(pressure, 2) +
    ", \"rainfallLastHour\": " + String(rainfallLastHour, 2) +
    ", \"UVintensity\": " + String(uvIndex) +
    ", \"WindSpeed\": " + String(MaximumWindSpeed, 2) +
    ", \"SoilMoisture\": " + String(moisturePercent) +
    "}";
  return jsonString;
}

// Function to send AT commands
String sendATCommand(String command) {
  simSerial.println(command);
  String response = "";
  long int time = millis();
  while((time + 1000) > millis()) {
    while(simSerial.available()) {
      char c = simSerial.read();
      response += c;
    }
  }
  Serial.println(response);
  return response;
}

// Function to parse date and time from the SIM module response
DateTime parseDateTime(String response) {
  DateTime dt;

  // Example response: +CCLK: "24/10/12,14:04:12+00"
  int start = response.indexOf("\"");
  int end = response.indexOf("\"", start + 1);
  String dateTime = response.substring(start + 1, end);

  // Split date and time
  int commaIndex = dateTime.indexOf(",");
  String date = dateTime.substring(0, commaIndex);
  String time = dateTime.substring(commaIndex + 1);

  // Further split date and time into components
  dt.day = date.substring(6, 8).toInt();
  dt.month = date.substring(3, 5).toInt();
  dt.year = 2000 + date.substring(0, 2).toInt();

  dt.hour = time.substring(0, 2).toInt();
  dt.minute = time.substring(3, 5).toInt();
  dt.second = time.substring(6, 8).toInt();

  return dt;
}
