
#include "Adafruit_ThinkInk.h"
#include "time.h"
#include "Arduino.h"
#include <ArduinoJson.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMono18pt7b.h>
#include <Fonts/FreeMono24pt7b.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <UrlEncode.h>



#define EPD_DC      7
#define EPD_CS      8
#define EPD_BUSY    -1
#define SRAM_CS     -1
#define EPD_RESET   6

ThinkInk_290_Grayscale4_T5 display(EPD_DC, EPD_RESET, EPD_CS, SRAM_CS, EPD_BUSY);

WiFiMulti wifiMulti;

String graf_token = "grafana api token here";
String wifi_name  = "wifi ssid";
String wifi_pass  = "wifi password";

#define BUTTON_DOWN 12
#define BUTTON_UP 14
#define LIGHT_SENSOR 3
#define LIGHT_SENSOR_ENABLE 21


char sensors[4][3][32] = {
    { "Aaron's Bedroom",    "env_aaron_bedroom_temperature",      "env_aaron_bedroom_humidity"   },
    { "Kitchen",            "env_kitchen_temperature",            "env_kitchen_humidity"         },
    { "Lounge",             "env_lounge_temperature",             "env_lounge_humidity"          },
    { "Bathroom",           "env_bathroom_temperature",           "env_bathroom_humidity"        }
};

String urlencode(String str)
{
    String encodedString="";
    char c;
    char code0;
    char code1;
    char code2;
    for (int i =0; i < str.length(); i++){
      c=str.charAt(i);
      if (c == ' '){
        encodedString+= '+';
      } else if (isalnum(c)){
        encodedString+=c;
      } else{
        code1=(c & 0xf)+'0';
        if ((c & 0xf) >9){
            code1=(c & 0xf) - 10 + 'A';
        }
        c=(c>>4)&0xf;
        code0=c+'0';
        if (c > 9){
            code0=c - 10 + 'A';
        }
        code2='\0';
        encodedString+='%';
        encodedString+=code0;
        encodedString+=code1;
        //encodedString+=code2;
      }
      yield();
    }
    return encodedString;

}


void setup() {
  Serial.begin(115200);

  display.begin(THINKINK_GRAYSCALE4);
  display.setRotation(0);

  pinMode(BUTTON_DOWN, INPUT);
  pinMode(BUTTON_UP, INPUT);

  pinMode(LIGHT_SENSOR_ENABLE, OUTPUT);
  digitalWrite(LIGHT_SENSOR_ENABLE, LOW);

  pinMode(LIGHT_SENSOR, INPUT);

  wifiMulti.addAP(wifi_name, wifi_pass);
  wifiMulti.run();

  display.clearBuffer();
}

bool timeSet = false;
uint8_t frame[296*128];

int mins = 0;
bool forceRedraw = false;
int sensorChoice = 0;

void loop() {


 if((wifiMulti.run() != WL_CONNECTED)) return;

  if (!timeSet) {
    delay(5000);
    configTime(0, 3600, "uk.pool.ntp.org");
    timeSet = true;
  }

  if (digitalRead(BUTTON_UP) == LOW) {
    forceRedraw = true;
    if (++sensorChoice >= (sizeof(sensors)/sizeof(*sensors))) sensorChoice = 0;
  }

  struct tm timeinfo;
  getLocalTime(&timeinfo);

  if (timeinfo.tm_min == mins && !forceRedraw) return;
  mins = timeinfo.tm_min;
  forceRedraw = false;

  display.clearBuffer();
  drawClock();
  notes();
  power();
  temperature();

   display.display();
}


void notes() {
  WiFiClient client;
  HTTPClient http;

  http.begin(client, "http://vinci.rhwyd.co.uk/clock.txt");
  http.addHeader("Authorization", "Bearer " + hos_token);
  int httpCode = http.GET();

  GFXcanvas1 noteCanvas(190, 59);
  //noteCanvas.setFont(&FreeMono9pt7b);
  noteCanvas.setCursor(0, 8);
  noteCanvas.println(http.getString());

  display.drawBitmap(4, 40,  noteCanvas.getBuffer(), 190, 59, 1, 0);

}


void power() {
  WiFiClient client;
  HTTPClient http;

  int16_t  x1, y1;
  uint16_t w, h;

  GFXcanvas8 powerCanvas(100, 56);

  const char query[] = "SELECT mean(value) FROM \"sensor.total_power\" WHERE time >= now() - 15m and time < now() GROUP BY time(20s) fill(none)";

  char url[4096];
  sprintf(url, "http://influx.nat.rhwyd.co.uk:3000/api/datasources/proxy/1/query?db=hal&q=%s&epoch=ms", urlencode(query).c_str());

  Serial.println(url);

  http.useHTTP10(true);
  http.begin(client, url);
  http.addHeader("Authorization", "Bearer " + graf_token);
  http.addHeader("Accept-Encoding", "identity");
  http.GET();

  StaticJsonDocument<256> filter;
  filter["results"][0]["series"][0]["values"][0] = true;
  filter["results"][0]["series"][0]["values"][1] = true;

  DynamicJsonDocument doc(1024*40);
  DeserializationError err = deserializeJson(doc, client, DeserializationOption::Filter(filter));
  if (err) Serial.println(err.c_str());

  int dataPoints = doc["results"][0]["series"][0]["values"].size();

  JsonArray points = doc["results"][0]["series"][0]["values"].as<JsonArray>();

  float segmentLength = floor(100 / (dataPoints - 1));

  float dataMin = 10000;
  float dataMax = 0;
  float dataLast = 0;
  for (int i=0; i < dataPoints; i++) {
    float c = points[i][1].as<float>();
    if (c == 0) continue;
    if (c < dataMin) dataMin = c;
    if (c > dataMax) dataMax = c;
    dataLast = c;
  }

  float dataVar = dataMax - dataMin;

  float verticalScale = 30 / dataVar ;

  for (int i=1; i < dataPoints; i++) {
    powerCanvas.drawLine(segmentLength * (i-1) + (i-1), 40 - (points[i-1][1].as<float>() - dataMin) * verticalScale,
                         segmentLength * i + i ,         40 - (points[i][1].as<float>()   - dataMin) * verticalScale  ,  3);
    for (int y=1; y < 60; y++) {
      powerCanvas.drawLine(segmentLength * (i-1) + (i-1), 40 + y - (points[i-1][1].as<float>() - dataMin) * verticalScale,
                           segmentLength * i + i,         40 + y - (points[i][1].as<float>()   - dataMin) * verticalScale  ,  2);
    }
  }

  char watts[10];
  sprintf(watts, "%.0f", dataLast);

  powerCanvas.setTextColor(1);

  powerCanvas.setFont(&FreeMono18pt7b);
  powerCanvas.getTextBounds(watts, 0, 30, &x1, &y1, &w, &h);
  powerCanvas.setCursor(96/2 - w/2, 30);
  powerCanvas.print(watts);

  powerCanvas.setFont(&FreeMono9pt7b);
  powerCanvas.getTextBounds("WATTS", 0, 45, &x1, &y1, &w, &h);
  powerCanvas.setCursor(96/2 - w/2, 45);
  powerCanvas.print("WATTS");

  display.drawGrayscaleBitmap(196, 40,  powerCanvas.getBuffer(), 100, 56);
  display.drawLine(195, 37, 195, 96, 2);
}

void drawClock() {
  struct tm timeinfo;
  getLocalTime(&timeinfo);

  GFXcanvas1 clockCanvas(140, 35);
  GFXcanvas1 dayCanvas(100, 35);

  clockCanvas.setFont(&FreeMono24pt7b);
  clockCanvas.setCursor(0, 30);
  clockCanvas.println(&timeinfo, "%H:%M");

  dayCanvas.setFont(&FreeMono9pt7b);
  dayCanvas.setCursor(0, 12);
  dayCanvas.println(&timeinfo, "%A");
  dayCanvas.setCursor(0, 30);
  dayCanvas.println(&timeinfo, "%d %B");

  display.drawBitmap(0, 0,  clockCanvas.getBuffer(), 140, 35, 1, 0);
  display.drawBitmap(150, 0,  dayCanvas.getBuffer(), 100, 35, 1, 0);
  display.drawLine(145, 0, 145, 37, 2);
  display.drawLine(0, 37, 296, 37, 2);
}

void temperature() {
  WiFiClient client;
  HTTPClient http;


  char query[1024];
  sprintf(query, "SELECT mean(value) FROM \"sensor.%s\" WHERE time >= now() - 1d and time < now() GROUP BY time(30m) fill(none);"
    "SELECT mean(value) FROM \"sensor.%s\" WHERE time >= now() - 1d and time < now() GROUP BY time(30m) fill(none)", sensors[sensorChoice][1],  sensors[sensorChoice][2]);

  char url[4096];
  sprintf(url, "http://influx.nat.rhwyd.co.uk:3000/api/datasources/proxy/1/query?db=hal&q=%s&epoch=ms", urlencode(query).c_str());

  Serial.println(url);

  http.useHTTP10(true);
  http.begin(client, url);
  http.addHeader("Authorization", "Bearer " + graf_token);
  int httpCode = http.GET();

  StaticJsonDocument<128> filter;
  filter["results"][0]["series"][0]["values"][0] = true;
  filter["results"][0]["series"][0]["values"][1] = true;
  filter["results"][1]["series"][0]["values"][0] = true;
  filter["results"][1]["series"][0]["values"][1] = true;

  DynamicJsonDocument doc(1024*20);
  DeserializationError err = deserializeJson(doc, client, DeserializationOption::Filter(filter));

  if (err) Serial.println(err.c_str());

  int dataPoints = doc["results"][0]["series"][0]["values"].size();

  JsonArray points = doc["results"][0]["series"][0]["values"].as<JsonArray>();

  float segmentLength = floor(296 / (dataPoints - 1));

  float dataMin = 10000;
  float dataMax = 0;
  float dataLast = 0;
  for (int i=0; i < dataPoints; i++) {
    float c = points[i][1].as<float>();
    if (c == 0) continue;
    if (c < dataMin) dataMin = c;
    if (c > dataMax) dataMax = c;
    dataLast = c;
  }


  float dataVar = dataMax - dataMin;

  float verticalScale = 26 / dataVar ;

  display.drawLine(0, 96, 296, 96, 2);

  for (int i=1; i < dataPoints; i++) {
    display.drawLine(segmentLength * (i-1) + (i-1), 128 - ((points[i-1][1].as<float>() - dataMin) * verticalScale),
                     segmentLength * i + i,         128 - ((points[i][1].as<float>()   - dataMin) * verticalScale)  ,  3);
    for (int y=1; y < 32; y++) {
      display.drawLine(segmentLength * (i-1) + (i-1), 128 + y - ((points[i-1][1].as<float>() - dataMin) * verticalScale),
                       segmentLength * i + i,         128 + y - ((points[i][1].as<float>()   - dataMin) * verticalScale)  ,  2);
    }
  }

  float temperature = dataLast;

// humidity
  int dataPoints2 = doc["results"][1]["series"][0]["values"].size();
  Serial.println(dataPoints2);

  JsonArray points2 = doc["results"][1]["series"][0]["values"].as<JsonArray>();

  segmentLength = floor(296 / (dataPoints2 - 1));
Serial.println(segmentLength);

  dataMin = 10000;
  dataMax = 0;
  float humidity = 0;
  for (int i=0; i < dataPoints2; i++) {
    float c = points2[i][1].as<float>();
    if (c == 0) continue;
    if (c < dataMin) dataMin = c;
    if (c > dataMax) dataMax = c;
    humidity = c;
  }


  dataVar = dataMax - dataMin;

  verticalScale = 26 / dataVar ;

  for (int i=1; i < dataPoints2; i++) {
    display.drawLine(segmentLength * (i-1) + (i-1), 128 - ((points2[i-1][1].as<float>() - dataMin) * verticalScale),
                     segmentLength * i + i,         128 - ((points2[i][1].as<float>()   - dataMin) * verticalScale)  ,  3);
  }

  display.setCursor(3, 119);
  display.setTextColor(1);
  display.print("Temperature: ");
  display.print(temperature);
  display.print(" c");
  display.print("         ");
  display.print("Humidity: ");
  display.print(humidity);
  display.print(" %");
  display.setCursor(3, 108);
  display.print(sensors[sensorChoice][0]);
}
