// Screen size = 480 x 320 (size ratio relative to 320x240 = 1.5 x 1.33
// Screen Name = ESP32 SCT01
// Drive Type  = ST7796
// Set board type ESP32 Dev Module
// https://tchapi.github.io/Adafruit-GFX-Font-Customiser/
////////////////////////////////////////////////////////////////////////////////////
String version_num = "Davis WLL v5";
#include <WiFi.h>
#include <Wire.h>
/*Make sure all the required fonts are loaded by editing the
  User_Setup.h file in the TFT_eSPI library folder.
  #########################################################################
  ###### DON"T FORGET TO UPDATE THE User_Setup.h FILE IN THE LIBRARY ######
  ######            TO SELECT THE FONTS YOU USE, SEE ABOVE           ######
  ######       Refer to WT32SC01 File for User_setup.h settings      ######
  #########################################################################
*/
#include <TFT_eSPI.h>  //https://github.com/ESDeveloperBR/TFT_eSPI_ES32Lab
#include <SPI.h>
#include <time.h>
#include <Adafruit_FT6206.h>
#include "credentials.h"  // Contains const char* ssid "YourSSID" and const char* password "YourPassword" or type the two lines below
#include "symbols.h"      // Weather symbols
TFT_eSPI tft = TFT_eSPI();
Adafruit_FT6206 ts = Adafruit_FT6206();  // Touch screen object

#include <HTTPClient.h>
#include <ArduinoJson.h>  // https://github.com/bblanchon/ArduinoJson

#define GFXFF 1

const char* ntpServer = "uk.pool.ntp.org";
const char* Timezone  = "GMT0BST,M3.5.0/01,M10.5.0/02"; 
const long gmtOffset_sec = 0;      // Set your offset in seconds from GMT e.g. if Europe then 3600
const int daylightOffset_sec = 0;  // Set your offset in seconds for Daylight saving

const char* host = "http://192.168.0.41";  // Local address of the Davis WLL 6100
// http://192.168.0.41/v1/current_conditions

//WiFiClient client;

// Assign human-readable names to common 16-bit color values:
#define BLACK 0x0000
#define RED 0xF800
#define GREEN 0x07E0
#define BLUE 0x001F
#define CYAN 0x07FF
#define MAGENTA 0xF81F
#define YELLOW 0xFFE0
#define WHITE 0xFFFF

enum alignment { LEFT,
                 RIGHT,
                 CENTER };

float Temperature, Humidity, Dewpoint, AirPressure, Trend, RainFall, windchill, windspeed, winddirection, RainRate;
int TimeStamp, solarradiation, z_month, message;
String UpdateTime, Icon, z_code, forecast;
uint32_t previousMillis = 0;
int loopDelay = 15 * 60000;  // 15-mins
bool Refresh;

String PressureToCode(int pressure, String trend);
String calc_zambretti(int zpressure, int month, float windDirection, float windSpeed, float Trend);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(__FILE__);
  delay(500);
  tft.init();
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, 128);
  Wire.begin(18, 19);
  ts.begin(40, &Wire);
  Serial.println();
  tft.setRotation(1);  // 0-3
  tft.setTextFont(4);  // 1 is the default, 2 is OK but lacks extended chars. 3 is the same as 1, 4 is
  clear_screen();      // Clear screen
  StartWiFi();         // Start the WiFi service
  StartTime();
  clear_screen();  // Clear screen before moving to next station display
  display_text(10, 20, String("Started WiFi"), GREEN, 2);
  delay(2000);
  previousMillis = -loopDelay;
  Refresh = false;
}

void loop() {
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    //Serial.printf("%d %d\n", p.x, p.y);
    if (p.x < 200 && p.y < 200) {  // If upper top left touched then signal rescan of weather data
      Serial.println("Refreshing data...");
      clear_screen();
      Refresh = true;
    }
  }
  if (millis() > previousMillis + loopDelay || Refresh) {
    Refresh = false;
    previousMillis = millis();
    clear_screen();  // Clear screen
    Get_Data();
    clear_screen();  // Clear screen
    drawString(110, 10, UpdateTime, CENTER, YELLOW, 2);
    display_text(10, 38, String(Temperature, 1) + char(247), GREEN, 3);  // char(247) is ° symbol
    display_text(125, 42, String(Humidity, 0) + "%", GREEN, 2);          // char(247) is ° symbol
    display_text(10, 80,  "Dew Point   = " + String(Dewpoint, 1) + char(247), CYAN, 2);
    display_text(10, 103, "Wind Chill  = " + String(windchill, 1) + char(247), CYAN, 2);
    display_text(10, 126, "Solar rad.  = " + String(solarradiation) + "W/m2", CYAN, 2);
    display_text(10, 149, "SolarkW Gen = " + String(solarradiation * 1500 / 135 / 1000.0, 2) + "kWh", CYAN, 2);
    display_text(10, 172, "Rainfall    = " + String(RainFall, 1) + "mm", CYAN, 2);
    display_text(10, 195, "Rain-Rate   = " + String(RainRate, 1) + "mm/hr", CYAN, 2);
    String PressureTrend_Str = "Steady";  // Either steady, climbing or falling
    if (Trend >  0.05) PressureTrend_Str = "Rising";
    if (Trend < -0.05) PressureTrend_Str = "Falling";
    display_text(10, 218, "Pressure    = " + String(AirPressure, 0) + "hPa", CYAN, 2);
    z_code = calc_zambretti(AirPressure, z_month, winddirection, windspeed, Trend);
    forecast = ZCode(z_code);
    Serial.println("Forecast         = " + forecast + " (" + z_code + ")");
    display_text(270, 223, "(" + PressureTrend_Str + ") " + String(Trend, 2) + ", " + z_code, CYAN, 1);
    display_text(10, 265, forecast, RED, 2);
    DisplayWindDirection(385, 120, winddirection, windspeed, 75, YELLOW, 2);
    if (WiFi.status() == WL_CONNECTED) display_text(5, 12, "Wi-Fi", RED, 1);
    else StartWiFi();
  }
}

//----------------------------------------------------------------------------------------------------
void Get_Data() {  //client function to send/receive GET request data.
  String uri = "/v1/current_conditions";
  String Response;
  Serial.println("Connected,\nRequesting data");
  display_text(10, 20, String("Getting Data..."), GREEN, 2);
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(host + uri);     // Specify the URL
    int httpCode = http.GET();  // Start connection and send HTTP header
    Serial.println(httpCode);
    if (httpCode > 0) {  // HTTP header has been sent and Server response header has been handled
      if (httpCode == HTTP_CODE_OK) Response = http.getString();
      http.end();
      Serial.println(Response);
    } else {
      http.end();
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
      Response = "Station off-air";
    }
    http.end();
  }
  if (Response != "") Decode_Response(Response);
}

void Decode_Response(String input) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, input);
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }
  JsonObject data = doc["data"];
  // Get all the Davis WLL 6100 data
  //  const char* data_did = data["did"];  // "001D0A71475C"
  long reading_time = data["ts"];      // 1665396684
  JsonArray data_conditions = data["conditions"];
  JsonObject data_conditions_0 = data_conditions[0];
//  long data_conditions_0_lsid = data_conditions_0["lsid"];                               // 414281
//  int data_conditions_0_data_structure_type = data_conditions_0["data_structure_type"];  // 1
//  int data_conditions_0_txid = data_conditions_0["txid"];                                // 1
  float data_conditions_0_temp = data_conditions_0["temp"];                              // 55.5
  float data_conditions_0_hum = data_conditions_0["hum"];                                // 73.2
  float data_conditions_0_dew_point = data_conditions_0["dew_point"];                    // 47.1
//  float data_conditions_0_wet_bulb = data_conditions_0["wet_bulb"];                      // 50.4
//  float data_conditions_0_heat_index = data_conditions_0["heat_index"];                  // 54.8
  float data_conditions_0_wind_chill = data_conditions_0["wind_chill"];                  // 55.1
//  float data_conditions_0_thw_index = data_conditions_0["thw_index"];                    // 54.4
//  float data_conditions_0_thsw_index = data_conditions_0["thsw_index"];                  // 54.2
  int data_conditions_0_wind_speed_last = data_conditions_0["wind_speed_last"];          // 6
  int data_conditions_0_wind_dir_last = data_conditions_0["wind_dir_last"];              // 360
//  float data_conditions_0_wind_speed_avg_last_1_min = data_conditions_0["wind_speed_avg_last_1_min"];
//  int data_conditions_0_wind_dir_scalar_avg_last_1_min = data_conditions_0["wind_dir_scalar_avg_last_1_min"];
//  float data_conditions_0_wind_speed_avg_last_2_min = data_conditions_0["wind_speed_avg_last_2_min"];
//  int data_conditions_0_wind_dir_scalar_avg_last_2_min = data_conditions_0["wind_dir_scalar_avg_last_2_min"];
//  int data_conditions_0_wind_speed_hi_last_2_min = data_conditions_0["wind_speed_hi_last_2_min"];  // 10
//  int data_conditions_0_wind_dir_at_hi_speed_last_2_min = data_conditions_0["wind_dir_at_hi_speed_last_2_min"];
//  float data_conditions_0_wind_speed_avg_last_10_min = data_conditions_0["wind_speed_avg_last_10_min"];
//  data_conditions_0["wind_dir_scalar_avg_last_10_min"] is null
//  int data_conditions_0_wind_speed_hi_last_10_min = data_conditions_0["wind_speed_hi_last_10_min"];  // 12
//  int data_conditions_0_wind_dir_at_hi_speed_last_10_min = data_conditions_0["wind_dir_at_hi_speed_last_10_min"];
//  int data_conditions_0_rain_size = data_conditions_0["rain_size"];                                // 2
  int data_conditions_0_rain_rate_last = data_conditions_0["rain_rate_last"];                      // 0
//  int data_conditions_0_rain_rate_hi = data_conditions_0["rain_rate_hi"];                          // 0
//  int data_conditions_0_rainfall_last_15_min = data_conditions_0["rainfall_last_15_min"];          // 0
//  int data_conditions_0_rain_rate_hi_last_15_min = data_conditions_0["rain_rate_hi_last_15_min"];  // 0
//  int data_conditions_0_rainfall_last_60_min = data_conditions_0["rainfall_last_60_min"];          // 0
//  int data_conditions_0_rainfall_last_24_hr = data_conditions_0["rainfall_last_24_hr"];            // 13
//  int data_conditions_0_rain_storm = data_conditions_0["rain_storm"];                              // 13
//  long data_conditions_0_rain_storm_start_at = data_conditions_0["rain_storm_start_at"];           // 1665371400
  int data_conditions_0_solar_rad = data_conditions_0["solar_rad"];                                // 91
//  float data_conditions_0_uv_index = data_conditions_0["uv_index"];
//  int data_conditions_0_rx_state = data_conditions_0["rx_state"];                      // 0
//  int data_conditions_0_trans_battery_flag = data_conditions_0["trans_battery_flag"];  // 0
  int data_conditions_0_rainfall_daily = data_conditions_0["rainfall_daily"];          // 13
//  int data_conditions_0_rainfall_monthly = data_conditions_0["rainfall_monthly"];      // 72
//  int data_conditions_0_rainfall_year = data_conditions_0["rainfall_year"];            // 1673
//  int data_conditions_0_rain_storm_last = data_conditions_0["rain_storm_last"];        // 13
//  long data_conditions_0_rain_storm_last_start_at = data_conditions_0["rain_storm_last_start_at"];
//  long data_conditions_0_rain_storm_last_end_at = data_conditions_0["rain_storm_last_end_at"];
//  JsonObject data_conditions_1 = data_conditions[1];
//  long data_conditions_1_lsid = data_conditions_1["lsid"];                               // 414278
//  int data_conditions_1_data_structure_type = data_conditions_1["data_structure_type"];  // 4
//  float data_conditions_1_temp_in = data_conditions_1["temp_in"];                        // 70.8
//  float data_conditions_1_hum_in = data_conditions_1["hum_in"];                          // 49.9
//  float data_conditions_1_dew_point_in = data_conditions_1["dew_point_in"];              // 51.2
//  float data_conditions_1_heat_index_in = data_conditions_1["heat_index_in"];            // 69.2
  JsonObject data_conditions_2 = data_conditions[2];
 // long data_conditions_2_lsid = data_conditions_2["lsid"];                               // 414277
//  int data_conditions_2_data_structure_type = data_conditions_2["data_structure_type"];  // 3
  float data_conditions_2_bar_sea_level = data_conditions_2["bar_sea_level"];            // 30.106
  float data_conditions_2_bar_trend = data_conditions_2["bar_trend"];                    // 0.106
//  float data_conditions_2_bar_absolute = data_conditions_2["bar_absolute"];              // 29.961

  // doc["error"] is null
  TimeStamp = reading_time;
  Temperature = FtoC(data_conditions_0_temp);
  Humidity = data_conditions_0_hum;
  Dewpoint = FtoC(data_conditions_0_dew_point);
  AirPressure = InchesToHPA(data_conditions_2_bar_sea_level);
  Trend = InchesToHPA(data_conditions_2_bar_trend); // From inch to hpa
  windchill = FtoC(data_conditions_0_wind_chill);
  windspeed = data_conditions_0_wind_speed_last;
  winddirection = data_conditions_0_wind_dir_last;
  solarradiation = data_conditions_0_solar_rad;
  RainFall = data_conditions_0_rainfall_daily * 0.2;  // Units are 0.2mm
  RainRate = data_conditions_0_rain_rate_last * 0.2;  // Units are 0.2mm
  GetTimeDate();
  Serial.println("Time Stamp       = " + String(TimeStamp));
  Serial.println("Update Time      = " + String(UpdateTime));
  Serial.println("Temperature      = " + String(Temperature));
  Serial.println("Humidity         = " + String(Humidity));
  Serial.println("Dewpoint         = " + String(Dewpoint));
  Serial.println("Air Pressure     = " + String(AirPressure));
  Serial.println("Air Pres. Trend  = " + String(Trend));
  Serial.println("Windchill        = " + String(windchill));
  Serial.println("Windspeed        = " + String(windspeed));
  Serial.println("Wind Direction   = " + String(winddirection));
  Serial.println("Ordinal Wind Dir = " + WindDegToOrdDirection(winddirection));
  Serial.println("Solar Rad        = " + String(solarradiation));
  Serial.println("Rainfall         = " + String(RainFall));
  Serial.println("Rain Rate        = " + String(RainRate));
}
void DisplayWindDirection(int x, int y, float angle, float windspeed, int Cradius, int color, int Size) {
  arrow(x, y, Cradius - 21, angle, 15, 25, color);  // Show wind direction on outer circle of width and length
  int dxo, dyo, dxi, dyi;
  tft.drawCircle(x, y, Cradius, color);         // Draw compass circle
  tft.drawCircle(x, y, Cradius + 1, color);     // Draw compass circle
  tft.drawCircle(x, y, Cradius * 0.75, color);  // Draw compass inner circle
  for (float a = 0; a < 360; a = a + 22.5) {
    dxo = Cradius * cos((a - 90) * PI / 180);
    dyo = Cradius * sin((a - 90) * PI / 180);
    if (a == 45) drawString(dxo + x + 15, dyo + y - 10, "NE", CENTER, color, Size);
    if (a == 135) drawString(dxo + x + 10, dyo + y + 5, "SE", CENTER, color, Size);
    if (a == 225) drawString(dxo + x - 18, dyo + y + 5, "SW", CENTER, color, Size);
    if (a == 315) drawString(dxo + x - 20, dyo + y - 10, "NW", CENTER, color, Size);
    dxi = dxo * 0.9;
    dyi = dyo * 0.9;
    tft.drawLine(dxo + x, dyo + y, dxi + x, dyi + y, color);
    dxo = dxo * 0.75;
    dyo = dyo * 0.75;
    dxi = dxo * 0.9;
    dyi = dyo * 0.9;
    tft.drawLine(dxo + x, dyo + y, dxi + x, dyi + y, color);
  }
  drawString(x - 0, y - Cradius - 18, "N", CENTER, color, Size);
  drawString(x - 0, y + Cradius + 5, "S", CENTER, color, Size);
  drawString(x - Cradius - 12, y - 8, "W", CENTER, color, Size);
  drawString(x + Cradius + 10, y - 8, "E", CENTER, color, Size);
  drawString(x - 2, y - 40, WindDegToOrdDirection(angle), CENTER, color, Size);
  drawString(x - 5, y - 15, String(windspeed, 1), CENTER, color, Size);
  drawString(x - 4, y + 5, "mph", CENTER, color, Size);
  drawString(x - 7, y + 30, String(angle, 0) + char(247), CENTER, color, Size);
}
//#########################################################################################
String WindDegToOrdDirection(float winddirection) {
  int dir = int((winddirection / 22.5) + 0.5);
  String Ord_direction[16] = { "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW" };
  return Ord_direction[(dir % 16)];
}

//#########################################################################################
void arrow(int x, int y, int asize, float aangle, int pwidth, int plength, int color) {
  float dx = (asize + 28) * cos((aangle - 90) * PI / 180) + x;  // calculate X position
  float dy = (asize + 28) * sin((aangle - 90) * PI / 180) + y;  // calculate Y position
  float x1 = 0;
  float y1 = plength;
  float x2 = pwidth / 2;
  float y2 = pwidth / 2;
  float x3 = -pwidth / 2;
  float y3 = pwidth / 2;
  float angle = aangle * PI / 180;
  float xx1 = x1 * cos(angle) - y1 * sin(angle) + dx;
  float yy1 = y1 * cos(angle) + x1 * sin(angle) + dy;
  float xx2 = x2 * cos(angle) - y2 * sin(angle) + dx;
  float yy2 = y2 * cos(angle) + x2 * sin(angle) + dy;
  float xx3 = x3 * cos(angle) - y3 * sin(angle) + dx;
  float yy3 = y3 * cos(angle) + x3 * sin(angle) + dy;
  tft.fillTriangle(xx1, yy1, xx3, yy3, xx2, yy2, color);
}

float FtoC(float Value) {
  return (Value - 32) * 5.0 / 9.0;
}

float InchesToHPA(float Value) {
  return Value * 33.863886666667;
}

String ConvertUnixTime(int unix_time) {
  // http://www.cplusplus.com/reference/ctime/strftime/
  time_t tm = unix_time;
  struct tm* now_tm = gmtime(&tm);
  char output[40];
  strftime(output, sizeof(output), "%m", now_tm);
  z_month = String(output).toInt();
  strftime(output, sizeof(output), "%H:%M   %d/%m/%y", now_tm);
  return output;
}

void clear_screen() {
  tft.fillScreen(BLACK);
}

void display_text(int x, int y, String text_string, int txt_colour, int txt_size) {
  tft.setTextColor(txt_colour, TFT_BLACK);
  tft.setTextSize(txt_size);
  tft.drawString(text_string, x, y, GFXFF);
}

void drawString(int x, int y, String text_string, alignment align, int text_colour, int text_size) {
  int w = 2;  // The width of the font spacing
  tft.setTextWrap(false);
  tft.setTextColor(text_colour);
  tft.setTextSize(text_size);
  if (text_size == 1) w = 4  * text_string.length();
  if (text_size == 2) w = 8  * text_string.length();
  if (text_size == 3) w = 12 * text_string.length();
  if (text_size == 4) w = 16 * text_string.length();
  if (text_size == 5) w = 20 * text_string.length();
  if (align == RIGHT) x = x - w;
  if (align == CENTER) x = x - (w / 2);
  tft.drawString(text_string, x, y, GFXFF);
  tft.setTextSize(1);  // Back to default text size
}

void StartWiFi() {
  display_text(10, 20, String("Starting WiFi..."), GREEN, 2);
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);  // switch off AP
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected at: " + WiFi.localIP().toString());
}

void StartTime() {
  //init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer, "time.nist.gov"); //(gmtOffset_sec, daylightOffset_sec, ntpServer)
  setenv("TZ", Timezone, 1);  //setenv()adds the "TZ" variable to the environment with a value TimeZone, only used if set to 1, 0 means no change
  tzset(); // Set the TZ environment variable
  delay(100);
}

void GetTimeDate() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 15000)) {
    Serial.println("Failed to obtain time");
  }
  char output[40];
  strftime(output, sizeof(output), "%m", &timeinfo);
  z_month = String(output).toInt();
  strftime(output, sizeof(output), "%H:%M  %d/%m/%y", &timeinfo);
  UpdateTime = output;
}

//##############################################
int CorrectForWind(int zpressure, String windDirection, float windSpeed) {
  if (windSpeed > 0) {
    if (windDirection == "WNW") return zpressure - 0.5;
    if (windDirection == "E")   return zpressure - 0.5;
    if (windDirection == "ESE") return zpressure - 2;
    if (windDirection == "W")   return zpressure - 3;
    if (windDirection == "WSW") return zpressure - 4.5;
    if (windDirection == "SE")  return zpressure - 5;
    if (windDirection == "SW")  return zpressure - 6;
    if (windDirection == "SSE") return zpressure - 8.5;
    if (windDirection == "SSW") return zpressure - 10;
    if (windDirection == "S")   return zpressure - 12;
    if (windDirection == "NW")  return zpressure + 1;
    if (windDirection == "ENE") return zpressure + 2;
    if (windDirection == "NNW") return zpressure + 3;
    if (windDirection == "NE")  return zpressure + 5;
    if (windDirection == "NNE") return zpressure + 5;
    if (windDirection == "N")   return zpressure + 6;
  }
  return zpressure;
}

String OrdinalWindDir(int dir) {
  int val = int((dir / 22.5) + 0.5);
  String arr[] = { "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
  return arr[(val % 16)];
}

//                      1050 1045 1040 1035 1030 1025 1020 1015 1010 1005 1000 995  990  985  980  975  970  965  960  955
String risingCode[20]  = { "A", "A", "A", "A", "A", "B", "B", "B", "C", "F", "G", "I", "J", "L", "M", "Q", "Q", "T", "Y", "Y" };
String fallingCode[20] = { "A", "A", "B", "B", "B", "D", "H", "O", "R", "R", "U", "V", "X", "X", "X", "Z", "Z", "Z", "Z", "Z" };
String steadyCode[20]  = { "A", "A", "A", "A", "B", "B", "B", "E", "K", "N", "P", "S", "W", "W", "W", "X", "X", "Z", "Z", "Z" };

String PressureToCode(int pressure, String trend) {
  if (pressure >= 1045) {
    if (trend == "Rising") return risingCode[0];
    if (trend == "Falling") return fallingCode[0];
    if (trend == "Steady") return steadyCode[0];
  }
  if (pressure >= 1040 && pressure < 1045) {
    if (trend == "Rising") return risingCode[1];
    if (trend == "Falling") return fallingCode[1];
    if (trend == "Steady") return steadyCode[1];
  }
  if (pressure >= 1035 && pressure < 1040) {
    if (trend == "Rising") return risingCode[2];
    if (trend == "Falling") return fallingCode[2];
    if (trend == "Steady") return steadyCode[2];
  }
  if (pressure >= 1030 && pressure < 1035) {
    if (trend == "Rising") return risingCode[3];
    if (trend == "Falling") return fallingCode[3];
    if (trend == "Steady") return steadyCode[3];
  }
  if (pressure >= 1025 && pressure < 1030) {
    if (trend == "Rising") return risingCode[4];
    if (trend == "Falling") return fallingCode[4];
    if (trend == "Steady") return steadyCode[4];
  }
  if (pressure >= 1020 && pressure < 1025) {
    if (trend == "Rising") return risingCode[5];
    if (trend == "Falling") return fallingCode[5];
    if (trend == "Steady") return steadyCode[5];
  }
  if (pressure >= 1015 && pressure < 1020) {
    if (trend == "Rising") return risingCode[6];
    if (trend == "Falling") return fallingCode[6];
    if (trend == "Steady") return steadyCode[6];
  }
  if (pressure >= 1010 && pressure < 1015) {
    if (trend == "Rising") return risingCode[7];
    if (trend == "Falling") return fallingCode[7];
    if (trend == "Steady") return steadyCode[7];
  }
  if (pressure >= 1005 && pressure < 1010) {
    if (trend == "Rising") return risingCode[8];
    if (trend == "Falling") return fallingCode[8];
    if (trend == "Steady") return steadyCode[8];
  }
  if (pressure >= 1000 && pressure < 1005) {
    if (trend == "Rising") return risingCode[9];
    if (trend == "Falling") return fallingCode[9];
    if (trend == "Steady") return steadyCode[9];
  }
  if (pressure >= 995 && pressure < 1000) {
    if (trend == "Rising") return risingCode[10];
    if (trend == "Falling") return fallingCode[10];
    if (trend == "Steady") return steadyCode[10];
  }
  if (pressure >= 990 && pressure < 995) {
    if (trend == "Rising") return risingCode[11];
    if (trend == "Falling") return fallingCode[11];
    if (trend == "Steady") return steadyCode[11];
  }
  if (pressure >= 985 && pressure < 990) {
    if (trend == "Rising") return risingCode[12];
    if (trend == "Falling") return fallingCode[12];
    if (trend == "Steady") return steadyCode[12];
  }
  if (pressure >= 980 && pressure < 985) {
    if (trend == "Rising") return risingCode[13];
    if (trend == "Falling") return fallingCode[13];
    if (trend == "Steady") return steadyCode[13];
  }
  if (pressure >= 975 && pressure < 980) {
    if (trend == "Rising") return risingCode[14];
    if (trend == "Falling") return fallingCode[14];
    if (trend == "Steady") return steadyCode[14];
  }
  if (pressure >= 970 && pressure < 975) {
    if (trend == "Rising") return risingCode[15];
    if (trend == "Falling") return fallingCode[15];
    if (trend == "Steady") return steadyCode[15];
  }
  if (pressure >= 965 && pressure < 970) {
    if (trend == "Rising") return risingCode[16];
    if (trend == "Falling") return fallingCode[16];
    if (trend == "Steady") return steadyCode[16];
  }
  if (pressure >= 960 && pressure < 965) {
    if (trend == "Rising") return risingCode[17];
    if (trend == "Falling") return fallingCode[17];
    if (trend == "Steady") return steadyCode[17];
  }
  if (pressure >= 955 && pressure < 960) {
    if (trend == "Rising") return risingCode[18];
    if (trend == "Falling") return fallingCode[18];
    if (trend == "Steady") return steadyCode[18];
  }
  if (pressure < 955) {
    if (trend == "Rising") return risingCode[19];
    if (trend == "Falling") return fallingCode[19];
    if (trend == "Steady") return steadyCode[19];
  }
  return "";
}
//####################################
//# A Winter falling generally results in a Z value lower by 1 unit compared with a Summer falling pressure.
//# Similarly a Summer rising, improves the prospects by 1 unit over a Winter rising.
//#          1050                           950
//#          |                               |
//# Rising   A A A A B B C F G I J L M Q T Y Y
//# Falling  A A A A B B D H O R U V X X Z Z Z
//# Steady   A A A A B B E K N P S W X X Z Z Z

String calc_zambretti(int zpressure, int month, float windDirection, float windSpeed, float Trend) {
  // Correct pressure for time of year
  zpressure = CorrectForWind(zpressure, OrdinalWindDir(windDirection), windSpeed);
  if (Trend <= -0.05) {              // FALLING
    if (month <= 3 || month >= 9) {  // Adjust for Winter
      zpressure -= 3.0;
    }
    return PressureToCode(zpressure, "Falling");
  }
  if (Trend >= 0.05) {               // RISING
    if (month <= 3 || month >= 9) {  // Adjust for Winter
      zpressure -= 3.0;
    }
    return PressureToCode(zpressure, "Rising");
  }
  if (Trend > -0.05 && Trend < 0.05) {  // STEADY
    return PressureToCode(zpressure, "Steady");
  }
  return "";
}

String ZCode(String msg) {
  Serial.println("MSG = "+ msg);
  tft.setSwapBytes(true);
  int x_pos = 400;
  int y_pos = 235;
  int icon_x_size = 80;
  int icon_y_size = 80;
  String message = "";
  if (msg == "A") {
    //Icon = "https://openweathermap.org/img/wn/01d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_01d_2x);
    message = "Settled fine weather";
  }
  if (msg == "B") {
    //Icon = "https://openweathermap.org/img/wn/01d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_01d_2x);
    message = "Fine weather";
  }
  if (msg == "C") {
    //Icon = "https://openweathermap.org/img/wn/02d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_02d_2x);
    message = "Becoming fine";
  }
  if (msg == "D") {
    //Icon = "https://openweathermap.org/img/wn/02d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_02d_2x);
    message = "Fine, becoming less settled";
  }
  if (msg == "E") {
    //Icon = "https://openweathermap.org/img/wn/10d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_10d_2x);
    message = "Fine, possible showers";
  }
  if (msg == "F") {
    //Icon = "https://openweathermap.org/img/wn/02d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_02d_2x);
    message = "Fairly fine, improving";
  }
  if (msg == "G") {
    //Icon = "https://openweathermap.org/img/wn/10d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_10d_2x);
    message = "Fairly fine, showers early";
  }
  if (msg == "H") {
    //Icon = "https://openweathermap.org/img/wn/10d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_10d_2x);
    message = "Fairly fine, showery later";
  }
  if (msg == "I") {
    //Icon = "https://openweathermap.org/img/wn/10d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_10d_2x);
    message = "Showery early, improving";
  }
  if (msg == "J") {
    //Icon = "https://openweathermap.org/img/wn/10d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_02d_2x);
    message = "Changeable, improving";
  }
  if (msg == "K") {
    //Icon = "https://openweathermap.org/img/wn/10d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_10d_2x);
    message = "Fairly fine, showers likely";
  }
  if (msg == "L") {
    //Icon = "https://openweathermap.org/img/wn/04d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_04d_2x);
    message = "Rather unsettled, clear later";
  }
  if (msg == "M") {
    //Icon = "https://openweathermap.org/img/wn/04d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_04d_2x);
    message = "Unsettled, probably improving";
  }
  if (msg == "N") {
    //Icon = "https://openweathermap.org/img/wn/10d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_10d_2x);
    message = "Showery, bright intervals";
  }
  if (msg == "O") {
    //Icon = "https://openweathermap.org/img/wn/09d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_09d_2x);
    message = "Showery, becoming unsettled";
  }
  if (msg == "P") {
    //Icon = "https://openweathermap.org/img/wn/09d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_09d_2x);
    message = "Changeable, some rain";
  }
  if (msg == "Q") {
    //Icon = "https://openweathermap.org/img/wn/09d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_09d_2x);
    message = "Unsettled, fine intervals";
  }
  if (msg == "R") {
    //Icon = "https://openweathermap.org/img/wn/09d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_09d_2x);
    message = "Unsettled, rain later";
  }
  if (msg == "S") {
    //Icon = "https://openweathermap.org/img/wn/09d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_09d_2x);
    message = "Unsettled, rain at times";
  }
  if (msg == "T") {
    //Icon = "https://openweathermap.org/img/wn/04d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_04d_2x);
    message = "Very unsettled, improving";
  }
  if (msg == "U") {
    //Icon = "https://openweathermap.org/img/wn/09d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_09d_2x);
    message = "Rain at times, worst later";
  }
  if (msg == "V") {
    //Icon = "https://openweathermap.org/img/wn/09d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_09d_2x);
    message = "Rain, becoming very unsettled";
  }
  if (msg == "W") {
    //Icon = "https://openweathermap.org/img/wn/09d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_09d_2x);
    message = "Rain at frequent intervals";
  }
  if (msg == "X") {
    //Icon = "https://openweathermap.org/img/wn/09d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_09d_2x);
    message = "Very unsettled, rain";
  }
  if (msg == "Y") {
    //Icon = "https://openweathermap.org/img/wn/11d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_09d_2x);
    message = "Stormy, may improve";
  }
  if (msg == "Z") {
    //Icon = "https://openweathermap.org/img/wn/09d@2x.png";
    tft.pushImage(x_pos, y_pos, icon_x_size, icon_y_size, Img_09d_2x);
    message = "Stormy, much rain";
  }
  return message;
}

// Total of 699 lines

/* {"data":{"did":"001D0A71475C","ts":1665417350,
    "conditions":[{"lsid":414281,
    "data_structure_type":1,"txid":1,"temp": 64.7,"hum":50.0,"dew_point": 45.6,"wet_bulb": 51.8,"heat_index": 62.7,
    "wind_chill": 64.7,"thw_index": 62.7,"thsw_index": 69.6,
    "wind_speed_last":3.00,"wind_dir_last":344,"wind_speed_avg_last_1_min":3.75,"wind_dir_scalar_avg_last_1_min":341,"wind_speed_avg_last_2_min":4.00,
    "wind_dir_scalar_avg_last_2_min":338,"wind_speed_hi_last_2_min":9.00,"wind_dir_at_hi_speed_last_2_min":313,"wind_speed_avg_last_10_min":3.68,
    "wind_dir_scalar_avg_last_10_min":340,"wind_speed_hi_last_10_min":12.00,"wind_dir_at_hi_speed_last_10_min":352,
    "rain_size":2,"rain_rate_last":0,"rain_rate_hi":0,"rainfall_last_15_min":0,"rain_rate_hi_last_15_min":0,"rainfall_last_60_min":0,
    "rainfall_last_24_hr":13,"rain_storm":13,"rain_storm_start_at":1665371400,
    "solar_rad":199,"uv_index":null,"rx_state":0,"trans_battery_flag":0,
    "rainfall_daily":13,"rainfall_monthly":72,"rainfall_year":1673,"rain_storm_last":13,"rain_storm_last_start_at":1664965981,"rain_storm_last_end_at":1665082861},
    {"lsid":414278,
    "data_structure_type":4,"temp_in": 77.5,"hum_in":42.6,"dew_point_in": 53.0,"heat_index_in": 76.8},
    {"lsid":414277,"data_structure_type":3,"bar_sea_level":30.192,"bar_trend": 0.038,"bar_absolute":30.046}]},"error":null}
*/
