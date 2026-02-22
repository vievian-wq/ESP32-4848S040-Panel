/************************************************************
 * ESP32-S3 480x480 Smart Panel (LVGL)
 * Screens (5):
 *  1) HOME     - time/date + temp + condition icon animation
 *  2) LIGHT    - Living Room Light futuristic toggle (Tuya)
 *  3) WEATHER  - 3-day forecast + animated weather icons
 *  4) NEWS     - today's headlines
 *  5) SETTINGS - brightness + location Auto/London
 *
 * Notes:
 * - Requires LVGL (v8), Arduino_GFX_Library, ArduinoJson, TAMC_GT911
 * - Requires lv_conf.h properly installed (as discussed).
 * - Weather uses OpenWeatherMap (current + forecast).
 * - Location uses ip-api.com (HTTP).
 * - News uses NewsAPI.
 * - Tuya control is via Tuya Cloud API (requires signing).
 ************************************************************/

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <time.h>

#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <TAMC_GT911.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "This sketch targets ESP32-S3 smart panel hardware. Select an ESP32-S3 board (not ESP32-C3)."
#endif

// -------------------- USER CONFIG --------------------

// WiFi
static const char* WIFI_SSID = "YOUR_WIFI_SSID";
static const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// Time (UK: GMT offset 0; BST offset 3600 when needed)
static const char* NTP_SERVER_1 = "pool.ntp.org";
static const char* NTP_SERVER_2 = "time.google.com";
static const long  GMT_OFFSET_SEC = 0;
static const int   DAYLIGHT_OFFSET_SEC = 0;

// Weather (OpenWeatherMap)
static const char* OWM_API_KEY = "YOUR_OPENWEATHER_API_KEY";
static const char* OWM_UNITS   = "metric";  // "metric" or "imperial"

// News (NewsAPI)
static const char* NEWS_API_KEY = "YOUR_NEWSAPI_KEY";
static const char* NEWS_COUNTRY = "gb";     // gb / us / etc.

// Tuya Cloud (for Tuya Zigbee Hub -> Bulb)
static const bool  ENABLE_TUYA = false; // set true after you provide Tuya keys/device
static const char* TUYA_HOST   = "https://openapi.tuyaeu.com"; // EU example; change to your region
static const char* TUYA_CLIENT_ID     = "YOUR_TUYA_CLIENT_ID";
static const char* TUYA_CLIENT_SECRET = "YOUR_TUYA_CLIENT_SECRET";
static const char* TUYA_DEVICE_ID     = "YOUR_TUYA_DEVICE_ID";
// Switch code often "switch_led" but MUST be verified from device functions.
static const char* TUYA_SWITCH_CODE   = "switch_led";

// London fallback lat/lon
static const float LONDON_LAT = 51.5074f;
static const float LONDON_LON = -0.1278f;

// -------------------- Display Pins (from your board mapping) --------------------
static const int PIN_LCD_SCK   = 48;
static const int PIN_LCD_MOSI  = 47;
static const int PIN_LCD_CS    = 39;

static const int PIN_DE        = 18;
static const int PIN_HSYNC     = 16;
static const int PIN_VSYNC     = 17;
static const int PIN_PCLK      = 21;

static const int PIN_BL        = 38; // backlight PWM

static const int PIN_R0 = 11;
static const int PIN_R1 = 12;
static const int PIN_R2 = 13;
static const int PIN_R3 = 14;
static const int PIN_R4 = 0;

static const int PIN_G0 = 8;
static const int PIN_G1 = 20;
static const int PIN_G2 = 3;
static const int PIN_G3 = 46;
static const int PIN_G4 = 9;
static const int PIN_G5 = 10;

static const int PIN_B0 = 4;
static const int PIN_B1 = 5;
static const int PIN_B2 = 6;
static const int PIN_B3 = 7;
static const int PIN_B4 = 15;

// Touch GT911 I2C
static const int PIN_I2C_SDA = 19;
static const int PIN_I2C_SCL = 45;
static const int PIN_TOUCH_INT = -1;
static const int PIN_TOUCH_RST = -1;
static const uint16_t TOUCH_WIDTH = 480;
static const uint16_t TOUCH_HEIGHT = 480;

// Backlight PWM
static const int BL_PWM_CH = 0;
static const int BL_PWM_FREQ = 150;
static const int BL_PWM_RES = 10;

static void backlight_set_percent(uint8_t pct) {
  if (pct > 100) pct = 100;
  uint32_t duty = (uint32_t)pct * ((1 << BL_PWM_RES) - 1) / 100;
  ledcWrite(BL_PWM_CH, duty);
}

// -------------------- Display (Arduino_GFX) --------------------
Arduino_DataBus *bus = new Arduino_SWSPI(
  /*dc=*/-1, /*cs=*/PIN_LCD_CS, /*sck=*/PIN_LCD_SCK, /*mosi=*/PIN_LCD_MOSI, /*miso=*/-1
);

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
  PIN_DE, PIN_VSYNC, PIN_HSYNC, PIN_PCLK,
  PIN_R0, PIN_R1, PIN_R2, PIN_R3, PIN_R4,
  PIN_G0, PIN_G1, PIN_G2, PIN_G3, PIN_G4, PIN_G5,
  PIN_B0, PIN_B1, PIN_B2, PIN_B3, PIN_B4,
  /*hsync_polarity=*/0, /*hsync_front_porch=*/10, /*hsync_pulse_width=*/8, /*hsync_back_porch=*/20,
  /*vsync_polarity=*/0, /*vsync_front_porch=*/10, /*vsync_pulse_width=*/8, /*vsync_back_porch=*/10,
  /*pclk_active_neg=*/0, /*prefer_speed=*/12000000, /*auto_flush=*/true
);

Arduino_GFX *gfx = new Arduino_ST7701_RGBPanel(
  bus, rgbpanel,
  /*rst=*/-1, /*rotation=*/0,
  /*ips=*/false,
  /*width=*/480, /*height=*/480
);

// -------------------- LVGL Glue --------------------
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = nullptr;

static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)color_p, w, h);
  lv_disp_flush_ready(disp);
}

// Touch
TAMC_GT911 tp(PIN_I2C_SDA, PIN_I2C_SCL, PIN_TOUCH_INT, PIN_TOUCH_RST, TOUCH_WIDTH, TOUCH_HEIGHT);
static void my_touch_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  (void)indev_drv;
  tp.read();
  if (tp.isTouched) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = tp.points[0].x;
    data->point.y = tp.points[0].y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// -------------------- Data Models --------------------
struct Location {
  bool ok = false;
  float lat = 0;
  float lon = 0;
  String city;
  String country;
};

enum WeatherMain {
  WM_UNKNOWN,
  WM_CLEAR,
  WM_RAIN,
  WM_CLOUDS,
  WM_SNOW,
  WM_THUNDER
};

struct WeatherNow {
  bool ok = false;
  float temp = 0;
  float feels = 0;
  int humidity = 0;
  float wind = 0;
  String desc;
  WeatherMain main = WM_UNKNOWN;
};

struct ForecastDay {
  String dayLabel; // "Today", "Tomorrow", or weekday
  float tMin = 0;
  float tMax = 0;
  WeatherMain main = WM_UNKNOWN;
  String desc;
};

static Location gLoc;
static bool gUseAutoLocation = true;
static bool gMetric = true;

static WeatherNow gNow;
static ForecastDay gDays[3];
static int gBrightness = 100;
static bool gWeatherRefreshRequested = false;

// -------------------- Networking Helpers --------------------
static bool wifi_connect(uint32_t timeout_ms = 15000) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeout_ms) {
    delay(200);
  }
  return WiFi.status() == WL_CONNECTED;
}

static void time_init_ntp() {
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2);
}

static bool get_time_strings(String &outTime, String &outDate) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 300)) return false;
  char tbuf[16];
  char dbuf[32];
  strftime(tbuf, sizeof(tbuf), "%H:%M", &timeinfo);
  strftime(dbuf, sizeof(dbuf), "%A, %d %B", &timeinfo);
  outTime = tbuf;
  outDate = dbuf;
  return true;
}

// Auto location via ip-api.com (HTTP)
static bool fetch_location_ipapi(Location &loc) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  http.begin("http://ip-api.com/json/");
  int code = http.GET();
  String payload = http.getString();
  http.end();
  if (code != 200) return false;

  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, payload)) return false;

  const char* status = doc["status"] | "fail";
  if (String(status) != "success") return false;

  loc.lat = doc["lat"] | 0.0;
  loc.lon = doc["lon"] | 0.0;
  const char* city = doc["city"] | "";
  const char* region = doc["regionName"] | "";
  const char* country = doc["country"] | "";
  loc.city = city;
  loc.country = (strlen(region) > 0) ? region : country;
  loc.ok = true;
  return true;
}

static WeatherMain map_weather_main(const String &m) {
  if (m == "Clear") return WM_CLEAR;
  if (m == "Rain" || m == "Drizzle") return WM_RAIN;
  if (m == "Clouds") return WM_CLOUDS;
  if (m == "Snow") return WM_SNOW;
  if (m == "Thunderstorm") return WM_THUNDER;
  return WM_UNKNOWN;
}

// OpenWeather current by lat/lon
static bool fetch_weather_now(float lat, float lon, WeatherNow &now) {
  if (WiFi.status() != WL_CONNECTED) return false;

  String units = gMetric ? "metric" : "imperial";
  String url = String("https://api.openweathermap.org/data/2.5/weather?lat=") +
               String(lat, 6) + "&lon=" + String(lon, 6) +
               "&appid=" + OWM_API_KEY + "&units=" + units;

  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  String payload = http.getString();
  http.end();
  if (code != 200) return false;

  StaticJsonDocument<2048> doc;
  if (deserializeJson(doc, payload)) return false;

  now.temp     = doc["main"]["temp"] | 0.0;
  now.feels    = doc["main"]["feels_like"] | 0.0;
  now.humidity = doc["main"]["humidity"] | 0;
  now.wind     = doc["wind"]["speed"] | 0.0;

  const char* desc = doc["weather"][0]["description"] | "--";
  const char* main = doc["weather"][0]["main"] | "--";
  now.desc = desc;
  now.main = map_weather_main(String(main));
  now.ok = true;
  return true;
}

// OpenWeather forecast: we compute min/max for next 3 days
// Endpoint returns 3-hour steps. We'll bucket by date and compute min/max + choose most common "main".
static bool fetch_weather_3day(float lat, float lon, ForecastDay outDays[3]) {
  if (WiFi.status() != WL_CONNECTED) return false;

  String units = gMetric ? "metric" : "imperial";
  String url = String("https://api.openweathermap.org/data/2.5/forecast?lat=") +
               String(lat, 6) + "&lon=" + String(lon, 6) +
               "&appid=" + OWM_API_KEY + "&units=" + units;

  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  String payload = http.getString();
  http.end();
  if (code != 200) return false;

  StaticJsonDocument<15000> doc;
  if (deserializeJson(doc, payload)) return false;

  JsonArray list = doc["list"].as<JsonArray>();
  if (list.isNull()) return false;

  // We'll store up to 3 distinct dates
  String dates[3];
  bool have[3] = {false,false,false};

  // per day temp min/max
  float tmin[3] = {9999,9999,9999};
  float tmax[3] = {-9999,-9999,-9999};

  // per day main counts
  int clearC[3]={0,0,0}, rainC[3]={0,0,0}, cloudC[3]={0,0,0}, snowC[3]={0,0,0}, thC[3]={0,0,0}, unkC[3]={0,0,0};

  auto add_main = [&](int idx, WeatherMain wm){
    switch(wm){
      case WM_CLEAR: clearC[idx]++; break;
      case WM_RAIN: rainC[idx]++; break;
      case WM_CLOUDS: cloudC[idx]++; break;
      case WM_SNOW: snowC[idx]++; break;
      case WM_THUNDER: thC[idx]++; break;
      default: unkC[idx]++; break;
    }
  };

  for (JsonObject it : list) {
    const char* dt_txt = it["dt_txt"] | "";
    if (strlen(dt_txt) < 10) continue;
    String d = String(dt_txt).substring(0, 10); // YYYY-MM-DD

    int idx = -1;
    for (int i=0;i<3;i++){
      if (have[i] && dates[i] == d) { idx = i; break; }
    }
    if (idx == -1) {
      for (int i=0;i<3;i++){
        if (!have[i]) { have[i]=true; dates[i]=d; idx=i; break; }
      }
    }
    if (idx == -1) break; // already have 3 days

    float t = it["main"]["temp"] | 0.0;
    if (t < tmin[idx]) tmin[idx] = t;
    if (t > tmax[idx]) tmax[idx] = t;

    const char* main = it["weather"][0]["main"] | "--";
    add_main(idx, map_weather_main(String(main)));
  }

  // Build output labels
  // We'll label day0 "Today", day1 "Tomorrow", day2 weekday from NTP if possible.
  outDays[0].dayLabel = "Today";
  outDays[1].dayLabel = "Tomorrow";
  outDays[2].dayLabel = "Day 3";

  // If time is available, get weekday for +2 days
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 200)) {
    // Create a copy and add 2 days
    time_t raw = mktime(&timeinfo);
    raw += 2 * 24 * 3600;
    struct tm *t2 = localtime(&raw);
    char wbuf[16];
    strftime(wbuf, sizeof(wbuf), "%A", t2);
    outDays[2].dayLabel = wbuf;
  }

  for (int i=0;i<3;i++){
    outDays[i].tMin = (have[i] ? tmin[i] : 0);
    outDays[i].tMax = (have[i] ? tmax[i] : 0);

    // choose main by max count
    int best = unkC[i]; WeatherMain wm = WM_UNKNOWN;
    if (clearC[i] > best) { best=clearC[i]; wm=WM_CLEAR; }
    if (rainC[i]  > best) { best=rainC[i];  wm=WM_RAIN; }
    if (cloudC[i] > best) { best=cloudC[i]; wm=WM_CLOUDS; }
    if (snowC[i]  > best) { best=snowC[i];  wm=WM_SNOW; }
    if (thC[i]    > best) { best=thC[i];    wm=WM_THUNDER; }
    outDays[i].main = wm;

    // short description for cards
    switch(wm){
      case WM_CLEAR: outDays[i].desc="Clear"; break;
      case WM_RAIN: outDays[i].desc="Rain"; break;
      case WM_CLOUDS: outDays[i].desc="Clouds"; break;
      case WM_SNOW: outDays[i].desc="Snow"; break;
      case WM_THUNDER: outDays[i].desc="Thunder"; break;
      default: outDays[i].desc="--"; break;
    }
  }

  return true;
}

// -------------------- Tuya Cloud (skeleton) --------------------
static String tuya_access_token;
static unsigned long tuya_token_expire_ms = 0;

// TODO: Implement Tuya signing properly.
// For now, Tuya functions return false until signing is completed.
static bool tuya_ready() {
  if (!ENABLE_TUYA) return false;
  // when you provide your Tuya region + verify signature, this becomes true.
  return false;
}

static bool tuya_set_livingroom(bool on) {
  if (!tuya_ready()) return false;
  (void)on;
  // TODO:
  // 1) get token GET /v1.0/token?grant_type=1
  // 2) POST /v1.0/iot-03/devices/{device_id}/commands with {"commands":[{"code":"switch_led","value":true}]}
  // 3) add headers client_id, t, sign_method, access_token, sign
  return false;
}

// -------------------- UI: 5 screens (no scaling; fixed 480x480) --------------------
static lv_obj_t *scr_home, *scr_light, *scr_weather, *scr_news, *scr_settings;
static lv_obj_t *navbar;

// Home labels
static lv_obj_t *lbl_time, *lbl_date, *lbl_temp, *lbl_cond, *lbl_loc_home;

// Light screen
static lv_obj_t *lbl_light_title, *lbl_light_status;
static lv_obj_t *btn_light_toggle;
static lv_obj_t *light_glow;

// Weather screen
static lv_obj_t *lbl_w_title, *lbl_w_loc, *lbl_w_now;
static lv_obj_t *day_card[3];
static lv_obj_t *day_lbl_name[3], *day_lbl_mm[3], *day_icon_box[3];

// News screen
static lv_obj_t *lbl_n_title;
static lv_obj_t *news_list;

// Settings screen
static lv_obj_t *lbl_s_title;
static lv_obj_t *slider_bright;
static lv_obj_t *lbl_bright;
static lv_obj_t *sw_auto_loc;
static lv_obj_t *lbl_loc_mode;

// Animation objects (simple LVGL shapes)
static lv_obj_t *home_icon_container;
static lv_obj_t *sun_glow, *sun_disc;

static WeatherMain gIconModeHome = WM_UNKNOWN;
static WeatherMain gIconModeWeather = WM_UNKNOWN;
static WeatherMain gDayIconMode[3] = {WM_UNKNOWN, WM_UNKNOWN, WM_UNKNOWN};

// -------------------- Style Helpers --------------------
static void set_space_background(lv_obj_t *obj) {
  lv_obj_set_style_bg_color(obj, lv_color_hex(0x050714), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
}

static void set_title_style(lv_obj_t *lbl) {
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
}

static void set_body_style(lv_obj_t *lbl) {
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
}

// -------------------- Weather Icon Animations --------------------
static void clear_icon_objects(lv_obj_t *parent) {
  lv_obj_clean(parent);
}

static void sun_glow_anim_cb(void* obj, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
}

static void sun_scale_anim_cb(void* obj, int32_t v) {
  lv_obj_set_style_transform_zoom((lv_obj_t*)obj, v, 0);
}

static void create_sun_icon(lv_obj_t *parent) {
  clear_icon_objects(parent);

  sun_glow = lv_obj_create(parent);
  lv_obj_set_size(sun_glow, 120, 120);
  lv_obj_set_style_radius(sun_glow, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(sun_glow, lv_color_hex(0xFFD45A), 0);
  lv_obj_set_style_bg_opa(sun_glow, LV_OPA_30, 0);
  lv_obj_set_style_border_width(sun_glow, 0, 0);
  lv_obj_center(sun_glow);

  sun_disc = lv_obj_create(parent);
  lv_obj_set_size(sun_disc, 80, 80);
  lv_obj_set_style_radius(sun_disc, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(sun_disc, lv_color_hex(0xFFB300), 0);
  lv_obj_set_style_bg_opa(sun_disc, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(sun_disc, 0, 0);
  lv_obj_center(sun_disc);

  lv_anim_t a1;
  lv_anim_init(&a1);
  lv_anim_set_var(&a1, sun_glow);
  lv_anim_set_exec_cb(&a1, sun_glow_anim_cb);
  lv_anim_set_values(&a1, 40, 120);
  lv_anim_set_time(&a1, 1400);
  lv_anim_set_playback_time(&a1, 1400);
  lv_anim_set_repeat_count(&a1, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a1);

  lv_anim_t a2;
  lv_anim_init(&a2);
  lv_anim_set_var(&a2, sun_disc);
  lv_anim_set_exec_cb(&a2, sun_scale_anim_cb);
  lv_anim_set_values(&a2, 240, 272);
  lv_anim_set_time(&a2, 1400);
  lv_anim_set_playback_time(&a2, 1400);
  lv_anim_set_repeat_count(&a2, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a2);
}

static void create_rain_icon(lv_obj_t *parent) {
  clear_icon_objects(parent);

  // cloud
  lv_obj_t* cloud = lv_obj_create(parent);
  lv_obj_set_size(cloud, 140, 60);
  lv_obj_set_style_radius(cloud, 25, 0);
  lv_obj_set_style_bg_color(cloud, lv_color_hex(0xBFD3FF), 0);
  lv_obj_set_style_bg_opa(cloud, LV_OPA_70, 0);
  lv_obj_set_style_border_width(cloud, 0, 0);
  lv_obj_align(cloud, LV_ALIGN_TOP_MID, 0, 10);

  // drops
  for (int i=0;i<10;i++) {
    lv_obj_t* drop = lv_obj_create(parent);
    lv_obj_set_size(drop, 6, 18);
    lv_obj_set_style_radius(drop, 3, 0);
    lv_obj_set_style_bg_color(drop, lv_color_hex(0x4DA6FF), 0);
    lv_obj_set_style_bg_opa(drop, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(drop, 0, 0);

    int rain_x = 10 + (esp_random() % 110);
    int rain_y = 50 + (esp_random() % 70);
    lv_obj_set_pos(drop, rain_x, rain_y);
  }
}

static void create_clouds_icon(lv_obj_t *parent) {
  clear_icon_objects(parent);
  // simple static clouds (lightweight)
  for (int i=0;i<3;i++){
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_set_size(c, 120 - i*20, 45 - i*5);
    lv_obj_set_style_radius(c, 22, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(0xD6E4FF), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_70, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_align(c, LV_ALIGN_TOP_MID, 0, 15 + i*20);
  }
}

static void set_weather_icon(lv_obj_t *container, WeatherMain wm) {
  switch(wm){
    case WM_CLEAR: create_sun_icon(container); break;
    case WM_RAIN: create_rain_icon(container); break;
    case WM_CLOUDS: create_clouds_icon(container); break;
    default: create_clouds_icon(container); break;
  }
}

static void set_weather_icon_if_changed(lv_obj_t *container, WeatherMain desired, WeatherMain &state) {
  if (desired == state) return;
  state = desired;
  set_weather_icon(container, desired);
}

// -------------------- Navigation (bottom bar) --------------------
static void go_screen(lv_obj_t *scr) {
  lv_scr_load(scr);
}

static void nav_btn_event(lv_event_t *e) {
  const char* id = (const char*)lv_event_get_user_data(e);
  if (!id) return;
  if (strcmp(id,"home")==0) go_screen(scr_home);
  else if (strcmp(id,"light")==0) go_screen(scr_light);
  else if (strcmp(id,"weather")==0) go_screen(scr_weather);
  else if (strcmp(id,"news")==0) go_screen(scr_news);
  else if (strcmp(id,"settings")==0) go_screen(scr_settings);
}

static lv_obj_t* build_navbar(lv_obj_t *parent, const char* active) {
  lv_obj_t *bar = lv_obj_create(parent);
  lv_obj_set_size(bar, 480, 60);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x0B0D1F), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_80, 0);
  lv_obj_set_style_border_width(bar, 0, 0);

  auto mkbtn = [&](const char* label, int x, const char* id){
    lv_obj_t *btn = lv_btn_create(bar);
    lv_obj_set_size(btn, 88, 44);
    lv_obj_align(btn, LV_ALIGN_LEFT_MID, x, 0);
    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, label);
    lv_obj_center(l);

    if (strcmp(active, id)==0) {
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x1B2A6B), 0);
      lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0);
    } else {
      lv_obj_set_style_bg_opa(btn, LV_OPA_30, 0);
    }

    lv_obj_add_event_cb(btn, nav_btn_event, LV_EVENT_CLICKED, (void*)id);
  };

  mkbtn("HOME",     10,  "home");
  mkbtn("LIGHT",    106, "light");
  mkbtn("WEATH",    202, "weather");
  mkbtn("NEWS",     298, "news");
  mkbtn("SET",      394, "settings");
  return bar;
}

// -------------------- Screen Builders --------------------
static void build_home() {
  scr_home = lv_obj_create(NULL);
  set_space_background(scr_home);

  lbl_time = lv_label_create(scr_home);
  lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_72, 0);
  lv_obj_set_style_text_color(lbl_time, lv_color_white(), 0);
  lv_label_set_text(lbl_time, "--:--");
  lv_obj_align(lbl_time, LV_ALIGN_TOP_LEFT, 24, 30);

  lbl_date = lv_label_create(scr_home);
  set_body_style(lbl_date);
  lv_label_set_text(lbl_date, "--");
  lv_obj_align(lbl_date, LV_ALIGN_TOP_LEFT, 24, 120);

  lbl_loc_home = lv_label_create(scr_home);
  lv_obj_set_style_text_font(lbl_loc_home, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(lbl_loc_home, lv_color_hex(0xC7D2FF), 0);
  lv_label_set_text(lbl_loc_home, "Loc: --");
  lv_obj_align(lbl_loc_home, LV_ALIGN_TOP_LEFT, 24, 155);

  lbl_temp = lv_label_create(scr_home);
  lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(lbl_temp, lv_color_white(), 0);
  lv_label_set_text(lbl_temp, "--°");
  lv_obj_align(lbl_temp, LV_ALIGN_BOTTOM_RIGHT, -24, -90);

  lbl_cond = lv_label_create(scr_home);
  set_body_style(lbl_cond);
  lv_label_set_text(lbl_cond, "--");
  lv_obj_align(lbl_cond, LV_ALIGN_BOTTOM_RIGHT, -24, -55);

  // icon container (animated sun/rain/clouds)
  home_icon_container = lv_obj_create(scr_home);
  lv_obj_set_size(home_icon_container, 160, 160);
  lv_obj_set_style_bg_opa(home_icon_container, LV_OPA_0, 0);
  lv_obj_set_style_border_width(home_icon_container, 0, 0);
  lv_obj_align(home_icon_container, LV_ALIGN_CENTER, 0, 40);

  // Navbar
  navbar = build_navbar(scr_home, "home");

  // initial icon
  set_weather_icon_if_changed(home_icon_container, WM_CLOUDS, gIconModeHome);
}

static void light_toggle_event(lv_event_t *e) {
  (void)e;

  // toggle local UI state
  static bool isOn = false;
  isOn = !isOn;

  // Visual: glow intensity
  lv_obj_set_style_bg_opa(light_glow, isOn ? LV_OPA_60 : LV_OPA_10, 0);
  lv_label_set_text(lbl_light_status, isOn ? "Status: ON" : "Status: OFF");

  // Tuya command (if enabled)
  if (ENABLE_TUYA) {
    bool ok = tuya_set_livingroom(isOn);
    if (!ok) {
      // revert if failed
      isOn = !isOn;
      lv_obj_set_style_bg_opa(light_glow, isOn ? LV_OPA_60 : LV_OPA_10, 0);
      lv_label_set_text(lbl_light_status, isOn ? "Status: ON" : "Status: OFF");
    }
  }
}

static void build_light() {
  scr_light = lv_obj_create(NULL);
  set_space_background(scr_light);

  lbl_light_title = lv_label_create(scr_light);
  set_title_style(lbl_light_title);
  lv_label_set_text(lbl_light_title, "Living Room");
  lv_obj_align(lbl_light_title, LV_ALIGN_TOP_MID, 0, 25);

  // glow platform
  light_glow = lv_obj_create(scr_light);
  lv_obj_set_size(light_glow, 360, 180);
  lv_obj_set_style_radius(light_glow, 24, 0);
  lv_obj_set_style_bg_color(light_glow, lv_color_hex(0x00A8FF), 0);
  lv_obj_set_style_bg_opa(light_glow, LV_OPA_10, 0);
  lv_obj_set_style_border_width(light_glow, 0, 0);
  lv_obj_align(light_glow, LV_ALIGN_CENTER, 0, -10);

  // toggle button (futuristic)
  btn_light_toggle = lv_btn_create(scr_light);
  lv_obj_set_size(btn_light_toggle, 320, 80);
  lv_obj_align(btn_light_toggle, LV_ALIGN_CENTER, 0, 70);
  lv_obj_set_style_radius(btn_light_toggle, 40, 0);
  lv_obj_set_style_bg_color(btn_light_toggle, lv_color_hex(0x1B2A6B), 0);
  lv_obj_set_style_bg_opa(btn_light_toggle, LV_OPA_80, 0);
  lv_obj_add_event_cb(btn_light_toggle, light_toggle_event, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lbl = lv_label_create(btn_light_toggle);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
  lv_label_set_text(lbl, "Living Room Light");
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -12);

  lbl_light_status = lv_label_create(btn_light_toggle);
  lv_obj_set_style_text_font(lbl_light_status, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lbl_light_status, lv_color_hex(0xC7D2FF), 0);
  lv_label_set_text(lbl_light_status, "Status: OFF");
  lv_obj_align(lbl_light_status, LV_ALIGN_CENTER, 0, 22);

  navbar = build_navbar(scr_light, "light");
}

static void build_weather() {
  scr_weather = lv_obj_create(NULL);
  set_space_background(scr_weather);

  lbl_w_title = lv_label_create(scr_weather);
  set_title_style(lbl_w_title);
  lv_label_set_text(lbl_w_title, "Weather Details");
  lv_obj_align(lbl_w_title, LV_ALIGN_TOP_MID, 0, 20);

  lbl_w_loc = lv_label_create(scr_weather);
  lv_obj_set_style_text_font(lbl_w_loc, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(lbl_w_loc, lv_color_hex(0xC7D2FF), 0);
  lv_label_set_text(lbl_w_loc, "--");
  lv_obj_align(lbl_w_loc, LV_ALIGN_TOP_MID, 0, 60);

  lbl_w_now = lv_label_create(scr_weather);
  lv_obj_set_style_text_font(lbl_w_now, &lv_font_montserrat_44, 0);
  lv_obj_set_style_text_color(lbl_w_now, lv_color_white(), 0);
  lv_label_set_text(lbl_w_now, "--°");
  lv_obj_align(lbl_w_now, LV_ALIGN_TOP_LEFT, 24, 95);

  // 3-day cards
  int startY = 170;
  int cardW = 140, cardH = 170;
  for (int i=0;i<3;i++){
    day_card[i] = lv_obj_create(scr_weather);
    lv_obj_set_size(day_card[i], cardW, cardH);
    lv_obj_set_style_radius(day_card[i], 18, 0);
    lv_obj_set_style_bg_color(day_card[i], lv_color_hex(0x0B0D1F), 0);
    lv_obj_set_style_bg_opa(day_card[i], LV_OPA_60, 0);
    lv_obj_set_style_border_width(day_card[i], 0, 0);
    lv_obj_align(day_card[i], LV_ALIGN_TOP_LEFT, 20 + i*(cardW+10), startY);

    day_lbl_name[i] = lv_label_create(day_card[i]);
    lv_obj_set_style_text_font(day_lbl_name[i], &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(day_lbl_name[i], lv_color_white(), 0);
    lv_label_set_text(day_lbl_name[i], "Day");
    lv_obj_align(day_lbl_name[i], LV_ALIGN_TOP_MID, 0, 10);

    day_icon_box[i] = lv_obj_create(day_card[i]);
    lv_obj_set_size(day_icon_box[i], 120, 120);
    lv_obj_set_style_bg_opa(day_icon_box[i], LV_OPA_0, 0);
    lv_obj_set_style_border_width(day_icon_box[i], 0, 0);
    lv_obj_align(day_icon_box[i], LV_ALIGN_CENTER, 0, -5);

    day_lbl_mm[i] = lv_label_create(day_card[i]);
    lv_obj_set_style_text_font(day_lbl_mm[i], &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(day_lbl_mm[i], lv_color_hex(0xC7D2FF), 0);
    lv_label_set_text(day_lbl_mm[i], "-- / --");
    lv_obj_align(day_lbl_mm[i], LV_ALIGN_BOTTOM_MID, 0, -10);

    // initial icon
    set_weather_icon_if_changed(day_icon_box[i], WM_CLOUDS, gDayIconMode[i]);
  }

  navbar = build_navbar(scr_weather, "weather");
}

static void build_news() {
  scr_news = lv_obj_create(NULL);
  set_space_background(scr_news);

  lbl_n_title = lv_label_create(scr_news);
  set_title_style(lbl_n_title);
  lv_label_set_text(lbl_n_title, "Today's News");
  lv_obj_align(lbl_n_title, LV_ALIGN_TOP_MID, 0, 20);

  news_list = lv_list_create(scr_news);
  lv_obj_set_size(news_list, 440, 330);
  lv_obj_align(news_list, LV_ALIGN_TOP_MID, 0, 80);
  lv_obj_set_style_bg_opa(news_list, LV_OPA_40, 0);

  navbar = build_navbar(scr_news, "news");
}

static void build_settings() {
  scr_settings = lv_obj_create(NULL);
  set_space_background(scr_settings);

  lbl_s_title = lv_label_create(scr_settings);
  set_title_style(lbl_s_title);
  lv_label_set_text(lbl_s_title, "Settings");
  lv_obj_align(lbl_s_title, LV_ALIGN_TOP_MID, 0, 20);

  // Brightness
  lbl_bright = lv_label_create(scr_settings);
  set_body_style(lbl_bright);
  lv_label_set_text(lbl_bright, "Brightness: 100%");
  lv_obj_align(lbl_bright, LV_ALIGN_TOP_LEFT, 24, 80);

  slider_bright = lv_slider_create(scr_settings);
  lv_obj_set_size(slider_bright, 380, 18);
  lv_slider_set_range(slider_bright, 10, 100);
  lv_slider_set_value(slider_bright, gBrightness, LV_ANIM_OFF);
  lv_obj_align(slider_bright, LV_ALIGN_TOP_LEFT, 24, 120);

  // Location mode
  lbl_loc_mode = lv_label_create(scr_settings);
  set_body_style(lbl_loc_mode);
  lv_label_set_text(lbl_loc_mode, "Location: Auto");
  lv_obj_align(lbl_loc_mode, LV_ALIGN_TOP_LEFT, 24, 170);

  sw_auto_loc = lv_switch_create(scr_settings);
  lv_obj_align(sw_auto_loc, LV_ALIGN_TOP_LEFT, 250, 168);
  // Switch ON = London (manual)
  if (!gUseAutoLocation) lv_obj_add_state(sw_auto_loc, LV_STATE_CHECKED);

  navbar = build_navbar(scr_settings, "settings");
}

// -------------------- News Fetch --------------------
static bool fetch_news() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (String(NEWS_API_KEY).startsWith("YOUR_")) return false;

  String url = String("https://newsapi.org/v2/top-headlines?country=") +
               NEWS_COUNTRY + "&pageSize=10&apiKey=" + NEWS_API_KEY;

  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  String payload = http.getString();
  http.end();
  if (code != 200) return false;

  StaticJsonDocument<10000> doc;
  if (deserializeJson(doc, payload)) return false;

  JsonArray articles = doc["articles"].as<JsonArray>();
  if (articles.isNull()) return false;

  lv_obj_clean(news_list);

  int i=0;
  for (JsonObject a : articles) {
    const char* title = a["title"] | "(no title)";
    if (i++ >= 10) break;
    lv_list_add_btn(news_list, LV_SYMBOL_FILE, title);
  }
  return true;
}

// -------------------- UI Update --------------------
static void update_home_ui() {
  String t, d;
  if (get_time_strings(t, d)) {
    lv_label_set_text(lbl_time, t.c_str());
    lv_label_set_text(lbl_date, d.c_str());
  }

  if (gLoc.ok) {
    String loc = "Loc: " + gLoc.city + ", " + gLoc.country;
    lv_label_set_text(lbl_loc_home, loc.c_str());
  }

  if (gNow.ok) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f°", gNow.temp);
    lv_label_set_text(lbl_temp, buf);
    lv_label_set_text(lbl_cond, gNow.desc.c_str());

    set_weather_icon_if_changed(home_icon_container, gNow.main, gIconModeHome);
  }
}

static void update_weather_ui() {
  if (gLoc.ok) {
    String loc = gLoc.city + ", " + gLoc.country;
    lv_label_set_text(lbl_w_loc, loc.c_str());
  }
  if (gNow.ok) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f°", gNow.temp);
    lv_label_set_text(lbl_w_now, buf);
  }

  for (int i=0;i<3;i++){
    lv_label_set_text(day_lbl_name[i], gDays[i].dayLabel.c_str());

    char mm[48];
    snprintf(mm, sizeof(mm), "%.0f° / %.0f°", gDays[i].tMin, gDays[i].tMax);
    lv_label_set_text(day_lbl_mm[i], mm);

    set_weather_icon_if_changed(day_icon_box[i], gDays[i].main, gDayIconMode[i]);
  }
}

static void settings_poll() {
  // Brightness
  int b = lv_slider_get_value(slider_bright);
  if (b != gBrightness) {
    gBrightness = b;
    backlight_set_percent((uint8_t)gBrightness);

    char buf[32];
    snprintf(buf, sizeof(buf), "Brightness: %d%%", gBrightness);
    lv_label_set_text(lbl_bright, buf);
  }

  // Location mode: switch ON = London (manual)
  bool london = lv_obj_has_state(sw_auto_loc, LV_STATE_CHECKED);
  bool newAuto = !london;
  if (newAuto != gUseAutoLocation) {
    gUseAutoLocation = newAuto;
    lv_label_set_text(lbl_loc_mode, gUseAutoLocation ? "Location: Auto" : "Location: London");
    gWeatherRefreshRequested = true;
  }
}

// -------------------- Periodic Timers --------------------
static void refresh_location_and_weather();

static void timer_1s_cb(lv_timer_t *t) {
  (void)t;
  settings_poll();

  if (gWeatherRefreshRequested) {
    gWeatherRefreshRequested = false;
    refresh_location_and_weather();
    update_weather_ui();
  }

  update_home_ui();
}

static void refresh_location_and_weather() {
  // location
  if (gUseAutoLocation) {
    Location loc;
    if (fetch_location_ipapi(loc)) gLoc = loc;
  } else {
    gLoc.ok = true;
    gLoc.lat = LONDON_LAT;
    gLoc.lon = LONDON_LON;
    gLoc.city = "London";
    gLoc.country = "GB";
  }

  // weather
  if (gLoc.ok && !String(OWM_API_KEY).startsWith("YOUR_")) {
    WeatherNow now;
    if (fetch_weather_now(gLoc.lat, gLoc.lon, now)) gNow = now;

    ForecastDay days[3];
    if (fetch_weather_3day(gLoc.lat, gLoc.lon, days)) {
      for (int i=0;i<3;i++) gDays[i] = days[i];
    }
  }
}

static void timer_weather_cb(lv_timer_t *t) {
  (void)t;
  refresh_location_and_weather();
  update_home_ui();
  update_weather_ui();
}

static void timer_news_cb(lv_timer_t *t) {
  (void)t;
  fetch_news();
}

// -------------------- Setup / Loop --------------------
void setup() {
  Serial.begin(115200);
  delay(300);

  // Backlight
  ledcSetup(BL_PWM_CH, BL_PWM_FREQ, BL_PWM_RES);
  ledcAttachPin(PIN_BL, BL_PWM_CH);
  backlight_set_percent((uint8_t)gBrightness);

  // I2C touch
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  // Display init
  if (!gfx->begin()) {
    Serial.println("Display init failed.");
    while (1) delay(100);
  }
  gfx->fillScreen(BLACK);

  // LVGL init
  lv_init();

  // LVGL buffer (40 lines)
  size_t buf_pixels = 480 * 40;
  buf1 = (lv_color_t *)ps_malloc(buf_pixels * sizeof(lv_color_t));
  if (!buf1) buf1 = (lv_color_t *)malloc(buf_pixels * sizeof(lv_color_t));
  if (!buf1) {
    Serial.println("LVGL buffer alloc failed.");
    while (1) delay(100);
  }
  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, buf_pixels);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 480;
  disp_drv.ver_res = 480;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  // Touch init
  tp.begin();
  tp.setRotation(0);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touch_read;
  lv_indev_drv_register(&indev_drv);

  // Build screens
  build_home();
  build_light();
  build_weather();
  build_news();
  build_settings();

  // Start on Home
  lv_scr_load(scr_home);

  // WiFi + time
  bool ok = wifi_connect();
  if (ok) {
    time_init_ntp();
    refresh_location_and_weather();
    fetch_news();
  } else {
    // still run UI, but show placeholders
    gLoc.ok = true; gLoc.city="No WiFi"; gLoc.country="";
    gNow.ok = true; gNow.temp = 0; gNow.desc="Offline";
  }

  // Update UI with initial data
  update_home_ui();
  update_weather_ui();

  // Timers
  lv_timer_create(timer_1s_cb, 1000, NULL);
  lv_timer_create(timer_weather_cb, 10UL * 60UL * 1000UL, NULL); // every 10 minutes
  lv_timer_create(timer_news_cb, 30UL * 60UL * 1000UL, NULL);    // every 30 minutes

  Serial.println("Panel ready.");
}

void loop() {
  lv_timer_handler();
  delay(5);
}
