/*
  V1_touch_fixed_light_clean.ino
  ESP32-S3 480x480 smart panel

  - 4 panels
  - exFAT microSD via SdFat
  - Wi-Fi + NTP time
  - Live weather from Open-Meteo
  - Touch enabled
  - Faster LVGL tick
  - Cleaner Living Room Light layout
  - No auto page cycling

  SD card layout:
    /assets/HOME.bin
    /assets/LIGHT_ON.bin
    /assets/LIGHT_OFF.bin
    /assets/WEATHER3.bin
    /assets/SETTINGS.bin
    /assets/clear.bin
    /assets/partly.bin
    /assets/rain.bin
    /assets/snow.bin
    /assets/storm.bin
*/

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <math.h>
#include <esp_timer.h>

#include <SdFat.h>
#include <ArduinoJson.h>

#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <TAMC_GT911.h>
#include <esp_heap_caps.h>

// =========================================================
// WIFI
// =========================================================
static const char *WIFI_SSID     = "WPISZ_TUTAJ_SSID";
static const char *WIFI_PASSWORD = "WPISZ_TUTAJ_HASLO";

// =========================================================
// LOCATION / TIME
// =========================================================
static const float LAT_EAST_GRINSTEAD = 51.1280f;
static const float LON_EAST_GRINSTEAD = -0.0170f;
static const char *TZ_INFO = "GMT0BST,M3.5.0/1,M10.5.0/2";

// =========================================================
// REFRESH
// =========================================================
static const uint32_t WIFI_RETRY_MS          = 15000;
static const uint32_t CLOCK_REFRESH_MS       = 1000;
static const uint32_t WEATHER_REFRESH_OK_MS  = 10UL * 60UL * 1000UL;
static const uint32_t WEATHER_REFRESH_BAD_MS = 30000;

// =========================================================
// DISPLAY / BOARD
// =========================================================
#define TFT_WIDTH   480
#define TFT_HEIGHT  480
#define GFX_BL      38

// microSD
#define SD_CS    42
#define SD_MOSI  47
#define SD_SCK   48
#define SD_MISO  41

// touch
#define TOUCH_SDA   19
#define TOUCH_SCL   45
#define TOUCH_INT   -1
#define TOUCH_RST   -1

// keep this simple first
static bool TOUCH_SWAP_XY  = false;
static bool TOUCH_INVERT_X = true;
static bool TOUCH_INVERT_Y = true;

// =========================================================
// HOTSPOTS
// =========================================================
#define NAV_Y       432
#define NAV_H       36

#define HOME_X      16
#define HOME_W      94

#define LIGHT_X     127
#define LIGHT_W     96

#define WEATHER_X   239
#define WEATHER_W   122

#define SET_X       374
#define SET_W       82

// cleaner light control area at bottom
#define LIGHT_BTN_X   120
#define LIGHT_BTN_Y   334
#define LIGHT_BTN_W   240
#define LIGHT_BTN_H   56

// =========================================================
// GLOBALS
// =========================================================
SdFs sd;
TAMC_GT911 touch(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, TFT_WIDTH, TFT_HEIGHT);

// =========================================================
// DISPLAY OBJECTS
// IMPORTANT: Arduino_GFX 1.2.9
// =========================================================
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
  39 /* CS */, 48 /* SCK */, 47 /* SDA */,
  18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
  11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */, 0 /* R4 */,
  8  /* G0 */, 20 /* G1 */, 3  /* G2 */, 46 /* G3 */, 9 /* G4 */, 10 /* G5 */,
  4  /* B0 */, 5  /* B1 */, 6  /* B2 */, 7  /* B3 */, 15 /* B4 */,
  false
);

Arduino_ST7701_RGBPanel *gfx = new Arduino_ST7701_RGBPanel(
  rgbpanel,
  GFX_NOT_DEFINED,
  0,
  true,
  480,
  480,
  st7701_type1_init_operations,
  sizeof(st7701_type1_init_operations),
  true,
  10, 8, 50,
  10, 8, 20
);

// =========================================================
// LVGL
// =========================================================
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;
static esp_timer_handle_t lvgl_tick_timer = nullptr;

// =========================================================
// APP STATE
// =========================================================
enum AppScreen {
  SCREEN_HOME = 0,
  SCREEN_LIGHT = 1,
  SCREEN_WEATHER = 2,
  SCREEN_SETTINGS = 3
};

static AppScreen currentScreen = SCREEN_HOME;
static bool lightOn = true;

// =========================================================
// WEATHER DATA
// =========================================================
struct CurrentWeather {
  bool valid = false;
  float temp = NAN;
  float feels = NAN;
  float humidity = NAN;
  float wind = NAN;
  int weatherCode = -1;
  bool isDay = true;
};

struct DailyForecastDay {
  bool valid = false;
  char dateText[16] = "";
  float tempMax = NAN;
  float tempMin = NAN;
  int weatherCode = -1;
};

struct Forecast3Day {
  CurrentWeather current;
  DailyForecastDay day[3];
};

static Forecast3Day weatherEG;

// =========================================================
// ASSET PATHS
// =========================================================
static const char *BG_HOME       = "S:/assets/HOME.bin";
static const char *BG_LIGHT_ON   = "S:/assets/LIGHT_ON.bin";
static const char *BG_LIGHT_OFF  = "S:/assets/LIGHT_OFF.bin";
static const char *BG_WEATHER3   = "S:/assets/WEATHER3.bin";
static const char *BG_SETTINGS   = "S:/assets/SETTINGS.bin";

static const char *RAW_BG_HOME       = "/assets/HOME.bin";
static const char *RAW_BG_LIGHT_ON   = "/assets/LIGHT_ON.bin";
static const char *RAW_BG_LIGHT_OFF  = "/assets/LIGHT_OFF.bin";
static const char *RAW_BG_WEATHER3   = "/assets/WEATHER3.bin";
static const char *RAW_BG_SETTINGS   = "/assets/SETTINGS.bin";

static const char *ICON_CLEAR  = "S:/assets/clear.bin";
static const char *ICON_PARTLY = "S:/assets/partly.bin";
static const char *ICON_CLOUD  = "S:/assets/partly.bin";
static const char *ICON_FOG    = "S:/assets/partly.bin";
static const char *ICON_RAIN   = "S:/assets/rain.bin";
static const char *ICON_SNOW   = "S:/assets/snow.bin";
static const char *ICON_STORM  = "S:/assets/storm.bin";

// =========================================================
// COLORS
// =========================================================
static lv_color_t C_WHITE()      { return lv_color_hex(0xFFFFFF); }
static lv_color_t C_SUBWHITE()   { return lv_color_hex(0xE6EDF8); }
static lv_color_t C_CYAN()       { return lv_color_hex(0x64F1FF); }
static lv_color_t C_RED()        { return lv_color_hex(0xFF8A8A); }
static lv_color_t C_PANEL()      { return lv_color_hex(0x162235); }
static lv_color_t C_PANEL2()     { return lv_color_hex(0x0F1A2A); }
static lv_color_t C_BORDER()     { return lv_color_hex(0x7FCBFF); }
static lv_color_t C_GOLD()       { return lv_color_hex(0xFFD76A); }
static lv_color_t C_DARKTEXT()   { return lv_color_hex(0x1E1E22); }

// =========================================================
// TIMERS
// =========================================================
static unsigned long lastClockMs = 0;
static unsigned long lastWifiRetryMs = 0;
static unsigned long lastWeatherAttemptMs = 0;
static unsigned long lastGoodWeatherMs = 0;

// =========================================================
// UI GLOBALS
// =========================================================
static lv_obj_t *root = nullptr;
static lv_obj_t *pages[4] = {nullptr, nullptr, nullptr, nullptr};
static lv_obj_t *pageBg[4] = {nullptr, nullptr, nullptr, nullptr};

// HOME
static lv_obj_t *homeDateLabel = nullptr;
static lv_obj_t *homeTimeLabel = nullptr;
static lv_obj_t *homeTempLabel = nullptr;
static lv_obj_t *homeMetaLabel = nullptr;
static lv_obj_t *homeIcon = nullptr;

// LIGHT
static lv_obj_t *lightBottomCard = nullptr;
static lv_obj_t *lightStatusLabel = nullptr;
static lv_obj_t *lightTapLabel = nullptr;

// WEATHER
static lv_obj_t *weatherTimeLabel = nullptr;
static lv_obj_t *forecastTitleLabel = nullptr;
static lv_obj_t *dayDateLabel[3] = {nullptr, nullptr, nullptr};
static lv_obj_t *dayTempLabel[3] = {nullptr, nullptr, nullptr};
static lv_obj_t *dayMinMaxLabel[3] = {nullptr, nullptr, nullptr};
static lv_obj_t *dayIcon[3] = {nullptr, nullptr, nullptr};

// SETTINGS
static lv_obj_t *settingsWifiLabel = nullptr;
static lv_obj_t *settingsSsidLabel = nullptr;
static lv_obj_t *settingsIpLabel = nullptr;
static lv_obj_t *settingsUpdatedLabel = nullptr;

// =========================================================
// HELPERS
// =========================================================
static void style_clear(lv_obj_t *obj) {
  lv_obj_remove_style_all(obj);
}

static void make_decorative(lv_obj_t *obj) {
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

static bool asset_exists(const char *path) {
  return sd.exists(path);
}

static const lv_font_t *font20() { return &lv_font_montserrat_20; }
static const lv_font_t *font18() { return &lv_font_montserrat_18; }
static const lv_font_t *font16() { return &lv_font_montserrat_16; }
static const lv_font_t *font12() { return &lv_font_montserrat_12; }

static lv_obj_t *create_label_xy(lv_obj_t *parent, int x, int y, const char *text,
                                 const lv_font_t *font, lv_color_t color) {
  lv_obj_t *lbl = lv_label_create(parent);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_font(lbl, font, 0);
  lv_obj_set_style_text_color(lbl, color, 0);
  lv_obj_set_pos(lbl, x, y);
  make_decorative(lbl);
  return lbl;
}

static lv_obj_t *create_label_align(lv_obj_t *parent, lv_align_t align, int x, int y,
                                    const char *text, const lv_font_t *font, lv_color_t color) {
  lv_obj_t *lbl = lv_label_create(parent);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_font(lbl, font, 0);
  lv_obj_set_style_text_color(lbl, color, 0);
  lv_obj_align(lbl, align, x, y);
  make_decorative(lbl);
  return lbl;
}

static lv_obj_t *create_page_with_bg(lv_obj_t *parent, const char *bgPath, lv_obj_t **bgOut) {
  lv_obj_t *page = lv_obj_create(parent);
  style_clear(page);
  lv_obj_set_size(page, 480, 480);
  lv_obj_set_pos(page, 0, 0);
  lv_obj_set_style_bg_color(page, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
  lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *img = lv_img_create(page);
  lv_img_set_src(img, bgPath);
  lv_obj_center(img);
  make_decorative(img);

  if (bgOut) *bgOut = img;
  return page;
}

static lv_obj_t *create_hotspot(lv_obj_t *parent, int x, int y, int w, int h,
                                lv_event_cb_t cb, void *user_data) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, w, h);
  lv_obj_set_pos(btn, x, y);

  lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);

  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
  return btn;
}

static lv_obj_t *create_glass_card(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *obj = lv_obj_create(parent);
  style_clear(obj);
  lv_obj_set_size(obj, w, h);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_style_radius(obj, 26, 0);
  lv_obj_set_style_bg_color(obj, C_PANEL(), 0);
  lv_obj_set_style_bg_opa(obj, 185, 0);
  lv_obj_set_style_border_width(obj, 2, 0);
  lv_obj_set_style_border_color(obj, C_BORDER(), 0);
  lv_obj_set_style_border_opa(obj, 80, 0);
  lv_obj_set_style_shadow_width(obj, 18, 0);
  lv_obj_set_style_shadow_opa(obj, 35, 0);
  lv_obj_set_style_shadow_color(obj, C_BORDER(), 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}

// =========================================================
// LVGL tick
// =========================================================
static void lv_tick_cb(void *arg) {
  (void)arg;
  lv_tick_inc(1);
}

// =========================================================
// TOUCH MAPPING
// =========================================================
static void mapTouchPoint(int rx, int ry, int &x, int &y) {
  int sx = rx;
  int sy = ry;

  if (TOUCH_SWAP_XY) {
    int t = sx;
    sx = sy;
    sy = t;
  }

  if (TOUCH_INVERT_X) sx = (TFT_WIDTH - 1) - sx;
  if (TOUCH_INVERT_Y) sy = (TFT_HEIGHT - 1) - sy;

  x = constrain(sx, 0, TFT_WIDTH - 1);
  y = constrain(sy, 0, TFT_HEIGHT - 1);
}

// =========================================================
// LVGL FS -> SdFat
// =========================================================
static void *sd_open_cb(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode) {
  (void)drv;
  if (!path) return nullptr;

  String p = path;
  if (!p.startsWith("/")) p = "/" + p;

  FsFile *f = new FsFile;
  uint8_t flags = (mode == LV_FS_MODE_WR) ? (O_RDWR | O_CREAT) : O_RDONLY;

  *f = sd.open(p.c_str(), flags);
  if (!(*f)) {
    delete f;
    return nullptr;
  }
  return f;
}

static lv_fs_res_t sd_close_cb(lv_fs_drv_t *drv, void *file_p) {
  (void)drv;
  if (!file_p) return LV_FS_RES_INV_PARAM;

  FsFile *f = (FsFile *)file_p;
  f->close();
  delete f;
  return LV_FS_RES_OK;
}

static lv_fs_res_t sd_read_cb(lv_fs_drv_t *drv, void *file_p, void *buf, uint32_t btr, uint32_t *br) {
  (void)drv;
  if (!file_p || !buf || !br) return LV_FS_RES_INV_PARAM;

  FsFile *f = (FsFile *)file_p;
  int32_t n = f->read((uint8_t *)buf, btr);
  if (n < 0) {
    *br = 0;
    return LV_FS_RES_FS_ERR;
  }

  *br = (uint32_t)n;
  return LV_FS_RES_OK;
}

static lv_fs_res_t sd_seek_cb(lv_fs_drv_t *drv, void *file_p, uint32_t pos, lv_fs_whence_t whence) {
  (void)drv;
  if (!file_p) return LV_FS_RES_INV_PARAM;

  FsFile *f = (FsFile *)file_p;
  bool ok = false;

  switch (whence) {
    case LV_FS_SEEK_SET: ok = f->seekSet((uint64_t)pos); break;
    case LV_FS_SEEK_CUR: ok = f->seekSet((uint64_t)f->curPosition() + pos); break;
    case LV_FS_SEEK_END: ok = f->seekSet((uint64_t)f->fileSize() + pos); break;
    default: return LV_FS_RES_INV_PARAM;
  }

  return ok ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

static lv_fs_res_t sd_tell_cb(lv_fs_drv_t *drv, void *file_p, uint32_t *pos_p) {
  (void)drv;
  if (!file_p || !pos_p) return LV_FS_RES_INV_PARAM;

  FsFile *f = (FsFile *)file_p;
  *pos_p = (uint32_t)f->curPosition();
  return LV_FS_RES_OK;
}

static void register_sd_lvgl_fs() {
  static lv_fs_drv_t fs_drv;
  lv_fs_drv_init(&fs_drv);
  fs_drv.letter   = 'S';
  fs_drv.open_cb  = sd_open_cb;
  fs_drv.close_cb = sd_close_cb;
  fs_drv.read_cb  = sd_read_cb;
  fs_drv.seek_cb  = sd_seek_cb;
  fs_drv.tell_cb  = sd_tell_cb;
  lv_fs_drv_register(&fs_drv);
}

// =========================================================
// DISPLAY / TOUCH CALLBACKS
// =========================================================
static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  const int32_t w = area->x2 - area->x1 + 1;
  const int32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)color_p, w, h);
  lv_disp_flush_ready(disp);
}

static void my_touch_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  (void)indev_drv;
  touch.read();

  if (touch.isTouched && touch.touches > 0) {
    int rx = touch.points[0].x;
    int ry = touch.points[0].y;

    int x, y;
    mapTouchPoint(rx, ry, x, y);

    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// =========================================================
// WIFI / TIME
// =========================================================
static bool connectWiFi(uint32_t timeoutMs = 15000) {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  if (WiFi.status() == WL_CONNECTED) return true;

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
  }

  return WiFi.status() == WL_CONNECTED;
}

static void initTimeIfPossible() {
  if (WiFi.status() == WL_CONNECTED) {
    configTzTime(TZ_INFO, "pool.ntp.org", "time.cloudflare.com", "time.google.com");
  }
}

static bool getLocalTm(struct tm &t) {
  time_t now = time(nullptr);
  if (now < 100000) return false;
  localtime_r(&now, &t);
  return true;
}

static void waitForTimeSync(uint32_t timeoutMs = 8000) {
  unsigned long start = millis();
  struct tm t;
  while (millis() - start < timeoutMs) {
    if (getLocalTm(t)) return;
    delay(200);
  }
}

// =========================================================
// HTTPS GET
// =========================================================
static bool httpsGET(const char *host, const String &path, String &bodyOut) {
  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect(host, 443)) return false;

  client.print(String("GET ") + path + " HTTP/1.1\r\n");
  client.print(String("Host: ") + host + "\r\n");
  client.print("User-Agent: ESP32\r\n");
  client.print("Connection: close\r\n\r\n");

  unsigned long start = millis();
  while (!client.available() && client.connected()) {
    if (millis() - start > 10000) {
      client.stop();
      return false;
    }
    delay(10);
  }

  String statusLine = client.readStringUntil('\n');
  statusLine.trim();
  if (statusLine.indexOf("200") < 0) {
    client.stop();
    return false;
  }

  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r" || line.length() == 0) break;
  }

  bodyOut = "";
  while (client.available()) {
    bodyOut += client.readString();
  }

  client.stop();
  return bodyOut.length() > 0;
}

// =========================================================
// WEATHER HELPERS
// =========================================================
static const char *icon_from_code(int code, bool isDay) {
  (void)isDay;

  if (code == 0) return ICON_CLEAR;
  if (code == 1 || code == 2) return ICON_PARTLY;
  if (code == 3) return ICON_CLOUD;
  if (code == 45 || code == 48) return ICON_FOG;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return ICON_RAIN;
  if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) return ICON_SNOW;
  if (code >= 95 && code <= 99) return ICON_STORM;

  return ICON_CLOUD;
}

static bool format_iso_date_to_short(const char *iso, char *out, size_t outSize) {
  int y, m, d;
  if (sscanf(iso, "%d-%d-%d", &y, &m, &d) != 3) return false;

  struct tm tmv = {};
  tmv.tm_year = y - 1900;
  tmv.tm_mon  = m - 1;
  tmv.tm_mday = d;
  tmv.tm_hour = 12;
  mktime(&tmv);

  static const char *wdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

  snprintf(out, outSize, "%s %d %s", wdays[tmv.tm_wday], d, months[m - 1]);
  return true;
}

// =========================================================
// WEATHER FETCH
// =========================================================
static bool fetchEastGrinsteadWeather(Forecast3Day &out) {
  if (WiFi.status() != WL_CONNECTED) return false;

  String path = "/v1/forecast?";
  path += "latitude=" + String(LAT_EAST_GRINSTEAD, 4);
  path += "&longitude=" + String(LON_EAST_GRINSTEAD, 4);
  path += "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,is_day";
  path += "&daily=weather_code,temperature_2m_max,temperature_2m_min";
  path += "&temperature_unit=celsius";
  path += "&wind_speed_unit=mph";
  path += "&timezone=Europe%2FLondon";
  path += "&forecast_days=3";

  String payload;
  if (!httpsGET("api.open-meteo.com", path, payload)) return false;

  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) return false;

  JsonObject current = doc["current"];
  JsonObject daily = doc["daily"];

  if (current.isNull() || daily.isNull()) return false;

  out.current.valid = true;
  out.current.temp = current["temperature_2m"] | NAN;
  out.current.feels = current["apparent_temperature"] | NAN;
  out.current.humidity = current["relative_humidity_2m"] | NAN;
  out.current.wind = current["wind_speed_10m"] | NAN;
  out.current.weatherCode = current["weather_code"] | -1;
  out.current.isDay = (current["is_day"] | 1) == 1;

  JsonArray timeArr = daily["time"].as<JsonArray>();
  JsonArray codeArr = daily["weather_code"].as<JsonArray>();
  JsonArray maxArr  = daily["temperature_2m_max"].as<JsonArray>();
  JsonArray minArr  = daily["temperature_2m_min"].as<JsonArray>();

  if (timeArr.isNull() || codeArr.isNull() || maxArr.isNull() || minArr.isNull()) return false;
  if (timeArr.size() < 3 || codeArr.size() < 3 || maxArr.size() < 3 || minArr.size() < 3) return false;

  for (int i = 0; i < 3; i++) {
    out.day[i].valid = true;
    out.day[i].tempMax = maxArr[i] | NAN;
    out.day[i].tempMin = minArr[i] | NAN;
    out.day[i].weatherCode = codeArr[i] | -1;

    const char *iso = timeArr[i] | "";
    if (!format_iso_date_to_short(iso, out.day[i].dateText, sizeof(out.day[i].dateText))) {
      snprintf(out.day[i].dateText, sizeof(out.day[i].dateText), "Day %d", i + 1);
    }
  }

  return true;
}

// =========================================================
// STAR BLINKING
// =========================================================
static void star_anim_cb(void *obj, int32_t v) {
  lv_obj_set_style_bg_opa((lv_obj_t *)obj, v, 0);
  lv_obj_set_style_shadow_opa((lv_obj_t *)obj, v, 0);
}

static void add_star(lv_obj_t *parent, int x, int y, int size, int delayMs, int timeMs) {
  lv_obj_t *s = lv_obj_create(parent);
  lv_obj_remove_style_all(s);
  lv_obj_set_size(s, size, size);
  lv_obj_set_pos(s, x, y);
  lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(s, lv_color_hex(0xF7FBFF), 0);
  lv_obj_set_style_bg_opa(s, 80, 0);
  lv_obj_set_style_shadow_color(s, lv_color_hex(0xD6F8FF), 0);
  lv_obj_set_style_shadow_width(s, 10, 0);
  lv_obj_set_style_shadow_opa(s, 80, 0);
  make_decorative(s);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s);
  lv_anim_set_exec_cb(&a, star_anim_cb);
  lv_anim_set_values(&a, 40, 255);
  lv_anim_set_time(&a, timeMs);
  lv_anim_set_playback_time(&a, timeMs);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_delay(&a, delayMs);
  lv_anim_start(&a);
}

static void add_blinking_stars(lv_obj_t *page) {
  add_star(page, 34, 78, 3,   0, 900);
  add_star(page, 76, 116, 2, 180, 700);
  add_star(page, 396, 88, 3, 250, 1100);
  add_star(page, 424, 132, 2, 420, 800);
  add_star(page, 58, 274, 3, 120, 1000);
  add_star(page, 402, 246, 2, 310, 900);
  add_star(page, 356, 326, 3, 520, 750);
  add_star(page, 104, 344, 2, 650, 1200);
  add_star(page, 448, 366, 3, 80, 950);
  add_star(page, 26, 392, 2, 460, 820);
}

// =========================================================
// UI UPDATE
// =========================================================
static void updateClockUi() {
  struct tm t;
  if (!getLocalTm(t)) {
    if (homeDateLabel) lv_label_set_text(homeDateLabel, "No time sync");
    if (homeTimeLabel) lv_label_set_text(homeTimeLabel, "--:--");
    if (weatherTimeLabel) lv_label_set_text(weatherTimeLabel, "--:--");
    return;
  }

  static const char *wdays[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
  };
  static const char *months[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
  };

  char dateBuf[40];
  snprintf(dateBuf, sizeof(dateBuf), "%s, %d %s",
           wdays[t.tm_wday], t.tm_mday, months[t.tm_mon]);

  char timeBuf[8];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", t.tm_hour, t.tm_min);

  if (homeDateLabel) lv_label_set_text(homeDateLabel, dateBuf);
  if (homeTimeLabel) lv_label_set_text(homeTimeLabel, timeBuf);
  if (weatherTimeLabel) lv_label_set_text(weatherTimeLabel, timeBuf);
}

static void updateHomeWeatherUi() {
  if (!weatherEG.current.valid) {
    if (homeTempLabel) lv_label_set_text(homeTempLabel, "--C | Feels like --C");
    if (homeMetaLabel) lv_label_set_text(homeMetaLabel, "Humidity --% | Wind -- mph");
    return;
  }

  char tempBuf[64];
  snprintf(tempBuf, sizeof(tempBuf), "%.0fC | Feels like %.0fC",
           weatherEG.current.temp, weatherEG.current.feels);

  char metaBuf[64];
  snprintf(metaBuf, sizeof(metaBuf), "Humidity %.0f%% | Wind %.0f mph",
           weatherEG.current.humidity, weatherEG.current.wind);

  if (homeTempLabel) lv_label_set_text(homeTempLabel, tempBuf);
  if (homeMetaLabel) lv_label_set_text(homeMetaLabel, metaBuf);
  if (homeIcon) lv_img_set_src(homeIcon, icon_from_code(weatherEG.current.weatherCode, weatherEG.current.isDay));
}

static void updateWeatherUi() {
  if (forecastTitleLabel) lv_label_set_text(forecastTitleLabel, "East Grinstead");

  for (int i = 0; i < 3; i++) {
    if (!weatherEG.day[i].valid) {
      if (dayDateLabel[i]) lv_label_set_text(dayDateLabel[i], "--");
      if (dayTempLabel[i]) lv_label_set_text(dayTempLabel[i], "--C");
      if (dayMinMaxLabel[i]) lv_label_set_text(dayMinMaxLabel[i], "-- | --");
      continue;
    }

    char tempBuf[16];
    char mmBuf[24];
    snprintf(tempBuf, sizeof(tempBuf), "%.0fC", weatherEG.day[i].tempMax);
    snprintf(mmBuf, sizeof(mmBuf), "%.0f | %.0f", weatherEG.day[i].tempMax, weatherEG.day[i].tempMin);

    if (dayDateLabel[i]) lv_label_set_text(dayDateLabel[i], weatherEG.day[i].dateText);
    if (dayTempLabel[i]) lv_label_set_text(dayTempLabel[i], tempBuf);
    if (dayMinMaxLabel[i]) lv_label_set_text(dayMinMaxLabel[i], mmBuf);
    if (dayIcon[i]) lv_img_set_src(dayIcon[i], icon_from_code(weatherEG.day[i].weatherCode, true));
  }
}

static void updateLightUi() {
  if (pageBg[SCREEN_LIGHT]) {
    lv_img_set_src(pageBg[SCREEN_LIGHT], lightOn ? BG_LIGHT_ON : BG_LIGHT_OFF);
  }

  if (lightBottomCard) {
    lv_obj_set_style_bg_color(lightBottomCard, lightOn ? C_GOLD() : C_PANEL2(), 0);
    lv_obj_set_style_bg_opa(lightBottomCard, lightOn ? 230 : 205, 0);
    lv_obj_set_style_border_color(lightBottomCard, lightOn ? lv_color_hex(0xFFF2C0) : C_BORDER(), 0);
  }

  if (lightStatusLabel) {
    lv_label_set_text(lightStatusLabel, lightOn ? "Living Room Light ON" : "Living Room Light OFF");
    lv_obj_set_style_text_color(lightStatusLabel, lightOn ? C_DARKTEXT() : C_WHITE(), 0);
  }

  if (lightTapLabel) {
    lv_label_set_text(lightTapLabel, lightOn ? "Tap to switch off" : "Tap to switch on");
    lv_obj_set_style_text_color(lightTapLabel, lightOn ? lv_color_hex(0x4A3A10) : C_SUBWHITE(), 0);
  }
}

static void updateSettingsUi() {
  if (settingsWifiLabel) {
    lv_label_set_text(settingsWifiLabel,
                      WiFi.status() == WL_CONNECTED ? "Wi-Fi Connected" : "Wi-Fi Offline");
    lv_obj_set_style_text_color(settingsWifiLabel,
                                WiFi.status() == WL_CONNECTED ? C_CYAN() : C_RED(), 0);
  }

  if (settingsSsidLabel) {
    char buf[96];
    snprintf(buf, sizeof(buf), "SSID: %s", WIFI_SSID);
    lv_label_set_text(settingsSsidLabel, buf);
  }

  if (settingsIpLabel) {
    char buf[64];
    if (WiFi.status() == WL_CONNECTED) {
      snprintf(buf, sizeof(buf), "local IP: %s", WiFi.localIP().toString().c_str());
    } else {
      snprintf(buf, sizeof(buf), "local IP: --");
    }
    lv_label_set_text(settingsIpLabel, buf);
  }

  if (settingsUpdatedLabel) {
    if (lastGoodWeatherMs == 0) {
      lv_label_set_text(settingsUpdatedLabel, "Weather updated: --");
    } else {
      unsigned long mins = (millis() - lastGoodWeatherMs) / 60000UL;
      char buf[64];
      snprintf(buf, sizeof(buf), "Weather updated: %lu min ago", mins);
      lv_label_set_text(settingsUpdatedLabel, buf);
    }
  }
}

static void refreshAllUi() {
  updateClockUi();
  updateHomeWeatherUi();
  updateWeatherUi();
  updateLightUi();
  updateSettingsUi();
}

// =========================================================
// EVENTS
// =========================================================
static void show_screen(AppScreen s) {
  currentScreen = s;
  for (int i = 0; i < 4; i++) {
    if (pages[i]) lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_clear_flag(pages[(int)s], LV_OBJ_FLAG_HIDDEN);
}

static void nav_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  intptr_t idx = (intptr_t)lv_event_get_user_data(e);
  show_screen((AppScreen)idx);
}

static void light_toggle_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (currentScreen != SCREEN_LIGHT) return;

  lightOn = !lightOn;
  updateLightUi();
}

// =========================================================
// BUILD OVERLAYS
// =========================================================
static void build_home_overlay() {
  lv_obj_t *page = pages[SCREEN_HOME];

  homeDateLabel = create_label_align(page, LV_ALIGN_TOP_MID, 0, 18, "Wednesday, 6 March", font20(), C_WHITE());

  homeIcon = lv_img_create(page);
  lv_img_set_src(homeIcon, ICON_PARTLY);
  lv_img_set_zoom(homeIcon, 1180);
  lv_obj_set_pos(homeIcon, 82, 0);
  make_decorative(homeIcon);

  homeTimeLabel = create_label_align(page, LV_ALIGN_TOP_MID, 0, 300, "08:24", font20(), C_WHITE());
  homeTempLabel = create_label_align(page, LV_ALIGN_TOP_MID, 0, 344, "12C | Feels like 10C", font16(), C_WHITE());
  homeMetaLabel = create_label_align(page, LV_ALIGN_TOP_MID, 0, 376, "Humidity 63% | Wind 8 mph", font12(), C_SUBWHITE());

  add_blinking_stars(page);
}

static void build_light_overlay() {
  lv_obj_t *page = pages[SCREEN_LIGHT];

  lightBottomCard = create_glass_card(page, 110, 326, 260, 64);
  lv_obj_add_event_cb(lightBottomCard, light_toggle_cb, LV_EVENT_CLICKED, nullptr);

  lightStatusLabel = create_label_align(lightBottomCard, LV_ALIGN_TOP_MID, 0, 8, "Living Room Light ON", font16(), C_DARKTEXT());
  lightTapLabel    = create_label_align(lightBottomCard, LV_ALIGN_BOTTOM_MID, 0, -8, "Tap to switch off", font12(), lv_color_hex(0x4A3A10));
}

static void build_weather_overlay() {
  lv_obj_t *page = pages[SCREEN_WEATHER];

  forecastTitleLabel = create_label_xy(page, 55, 140, "East Grinstead", font20(), C_WHITE());

  dayDateLabel[0] = create_label_xy(page, 44, 198, "Day 1", font16(), C_WHITE());
  dayTempLabel[0] = create_label_xy(page, 48, 236, "--C", font16(), C_WHITE());
  dayIcon[0] = lv_img_create(page);
  lv_img_set_src(dayIcon[0], ICON_PARTLY);
  lv_img_set_zoom(dayIcon[0], 330);
  lv_obj_set_pos(dayIcon[0], 38, 278);
  make_decorative(dayIcon[0]);
  dayMinMaxLabel[0] = create_label_xy(page, 54, 356, "-- | --", font12(), C_SUBWHITE());

  dayDateLabel[1] = create_label_xy(page, 175, 198, "Day 2", font16(), C_WHITE());
  dayTempLabel[1] = create_label_xy(page, 179, 236, "--C", font16(), C_WHITE());
  dayIcon[1] = lv_img_create(page);
  lv_img_set_src(dayIcon[1], ICON_PARTLY);
  lv_img_set_zoom(dayIcon[1], 330);
  lv_obj_set_pos(dayIcon[1], 169, 278);
  make_decorative(dayIcon[1]);
  dayMinMaxLabel[1] = create_label_xy(page, 185, 356, "-- | --", font12(), C_SUBWHITE());

  dayDateLabel[2] = create_label_xy(page, 309, 198, "Day 3", font16(), C_WHITE());
  dayTempLabel[2] = create_label_xy(page, 313, 236, "--C", font16(), C_WHITE());
  dayIcon[2] = lv_img_create(page);
  lv_img_set_src(dayIcon[2], ICON_CLOUD);
  lv_img_set_zoom(dayIcon[2], 330);
  lv_obj_set_pos(dayIcon[2], 303, 278);
  make_decorative(dayIcon[2]);
  dayMinMaxLabel[2] = create_label_xy(page, 319, 356, "-- | --", font12(), C_SUBWHITE());

  weatherTimeLabel = create_label_align(page, LV_ALIGN_TOP_MID, 0, 392, "--:--", font20(), C_WHITE());

  add_blinking_stars(page);
}

static void build_settings_overlay() {
  lv_obj_t *page = pages[SCREEN_SETTINGS];

  settingsWifiLabel = create_label_xy(page, 50, 126, "Wi-Fi Offline", font20(), C_RED());
  settingsSsidLabel = create_label_xy(page, 50, 174, "SSID: --", font16(), C_WHITE());
  settingsIpLabel = create_label_xy(page, 50, 206, "local IP: --", font16(), C_WHITE());
  settingsUpdatedLabel = create_label_xy(page, 50, 394, "Weather updated: --", font12(), C_SUBWHITE());

  add_blinking_stars(page);
}

static void build_hotspots() {
  create_hotspot(root, HOME_X,    NAV_Y, HOME_W,    NAV_H, nav_event_cb, (void *)(intptr_t)SCREEN_HOME);
  create_hotspot(root, LIGHT_X,   NAV_Y, LIGHT_W,   NAV_H, nav_event_cb, (void *)(intptr_t)SCREEN_LIGHT);
  create_hotspot(root, WEATHER_X, NAV_Y, WEATHER_W, NAV_H, nav_event_cb, (void *)(intptr_t)SCREEN_WEATHER);
  create_hotspot(root, SET_X,     NAV_Y, SET_W,     NAV_H, nav_event_cb, (void *)(intptr_t)SCREEN_SETTINGS);

  create_hotspot(pages[SCREEN_LIGHT], LIGHT_BTN_X, LIGHT_BTN_Y, LIGHT_BTN_W, LIGHT_BTN_H,
                 light_toggle_cb, nullptr);
}

static void build_ui() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);

  root = lv_obj_create(scr);
  style_clear(root);
  lv_obj_set_size(root, 480, 480);
  lv_obj_center(root);
  lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  pages[SCREEN_HOME]     = create_page_with_bg(root, BG_HOME, &pageBg[SCREEN_HOME]);
  pages[SCREEN_LIGHT]    = create_page_with_bg(root, BG_LIGHT_ON, &pageBg[SCREEN_LIGHT]);
  pages[SCREEN_WEATHER]  = create_page_with_bg(root, BG_WEATHER3, &pageBg[SCREEN_WEATHER]);
  pages[SCREEN_SETTINGS] = create_page_with_bg(root, BG_SETTINGS, &pageBg[SCREEN_SETTINGS]);

  build_home_overlay();
  build_light_overlay();
  build_weather_overlay();
  build_settings_overlay();
  build_hotspots();

  show_screen(SCREEN_HOME);
}

// =========================================================
// PERIODIC TASKS
// =========================================================
static void periodicWifiCheck() {
  if (WiFi.status() == WL_CONNECTED) return;

  if (millis() - lastWifiRetryMs >= WIFI_RETRY_MS) {
    lastWifiRetryMs = millis();
    connectWiFi(8000);
    if (WiFi.status() == WL_CONNECTED) {
      initTimeIfPossible();
      waitForTimeSync(5000);
    }
    updateSettingsUi();
  }
}

static void periodicClockUpdate() {
  if (millis() - lastClockMs >= CLOCK_REFRESH_MS) {
    lastClockMs = millis();
    updateClockUi();
  }
}

static void periodicWeatherUpdate() {
  uint32_t interval = weatherEG.current.valid ? WEATHER_REFRESH_OK_MS : WEATHER_REFRESH_BAD_MS;

  if (millis() - lastWeatherAttemptMs >= interval) {
    lastWeatherAttemptMs = millis();

    if (WiFi.status() == WL_CONNECTED) {
      if (fetchEastGrinsteadWeather(weatherEG)) {
        lastGoodWeatherMs = millis();
      }
      updateHomeWeatherUi();
      updateWeatherUi();
      updateSettingsUi();
    }
  }
}

// =========================================================
// SETUP / LOOP
// =========================================================
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);

  Serial.println("A: before gfx->begin()");
  gfx->begin();
  gfx->setRotation(0);
  Serial.println("B: after gfx->begin()");

  // LVGL init
  lv_init();
  Serial.println("C: after lv_init()");

  // LVGL tick timer
  const esp_timer_create_args_t lvgl_tick_timer_args = {
    .callback = &lv_tick_cb,
    .arg = nullptr,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "lvgl_tick"
  };
  esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer);
  esp_timer_start_periodic(lvgl_tick_timer, 1000);

  // buffers
  const size_t buf_pixels = TFT_WIDTH * 40;

  buf1 = (lv_color_t *)heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  buf2 = (lv_color_t *)heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  if (!buf1 || !buf2) {
    buf1 = (lv_color_t *)malloc(buf_pixels * sizeof(lv_color_t));
    buf2 = (lv_color_t *)malloc(buf_pixels * sizeof(lv_color_t));
  }

  if (!buf1 || !buf2) {
    Serial.println("LVGL buffer allocation failed");
    while (true) delay(1000);
  }

  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buf_pixels);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = TFT_WIDTH;
  disp_drv.ver_res = TFT_HEIGHT;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  Serial.println("D: display driver registered");

  // Touch
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(400000);
  delay(50);

  Serial.println("E: before touch.begin()");
  touch.begin();
  touch.setRotation(ROTATION_NORMAL);
  Serial.println("F: after touch.begin()");

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touch_read;
  lv_indev_drv_register(&indev_drv);

  // SD
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  SdSpiConfig spiConfig(SD_CS, SHARED_SPI, SD_SCK_MHZ(18), &SPI);

  if (!sd.begin(spiConfig)) {
    Serial.println("SD/exFAT mount failed");

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x200000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "SD mount failed");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(label);
    return;
  }

  register_sd_lvgl_fs();

  Serial.println(asset_exists(RAW_BG_HOME)      ? "HOME.bin OK"      : "HOME.bin MISSING");
  Serial.println(asset_exists(RAW_BG_LIGHT_ON)  ? "LIGHT_ON.bin OK"  : "LIGHT_ON.bin MISSING");
  Serial.println(asset_exists(RAW_BG_LIGHT_OFF) ? "LIGHT_OFF.bin OK" : "LIGHT_OFF.bin MISSING");
  Serial.println(asset_exists(RAW_BG_WEATHER3)  ? "WEATHER3.bin OK"  : "WEATHER3.bin MISSING");
  Serial.println(asset_exists(RAW_BG_SETTINGS)  ? "SETTINGS.bin OK"  : "SETTINGS.bin MISSING");

  build_ui();

  connectWiFi(15000);
  initTimeIfPossible();
  waitForTimeSync(5000);

  if (WiFi.status() == WL_CONNECTED) {
    if (fetchEastGrinsteadWeather(weatherEG)) {
      lastGoodWeatherMs = millis();
    }
  }

  refreshAllUi();

  lastClockMs = millis();
  lastWeatherAttemptMs = millis();

  Serial.println("V1_touch_fixed_light_clean ready");
}

void loop() {
  lv_timer_handler();
  periodicWifiCheck();
  periodicClockUpdate();
  periodicWeatherUpdate();
}
