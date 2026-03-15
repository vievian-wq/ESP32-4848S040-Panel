/*
   HOME PANEL - ESP32 4848S040
   Full standalone sketch
   - dark space background
   - twinkling stars
   - animated Earth (fake rotation)
   - weather icon in front of Earth
   - date / time / city / weather text

   Required:
   - lvgl
   - LovyanGFX
   - lgfx_conf.h
*/

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <LovyanGFX.hpp>
#include "lgfx_conf.h"

// =====================================================
// TOUCH GT911
// =====================================================
static const int TP_SDA = 19;
static const int TP_SCL = 45;
static const int TP_RST = 4;
static const int TP_INT = 5;
static const uint8_t GT911_ADDR = 0x5D;

static bool gt911_read(uint16_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;

  int got = Wire.requestFrom((int)GT911_ADDR, (int)len);
  if (got != (int)len) return false;

  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static bool gt911_write8(uint16_t reg, uint8_t val) {
  Wire.beginTransmission(GT911_ADDR);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(val);
  return Wire.endTransmission(true) == 0;
}

static void gt911_reset() {
  pinMode(TP_RST, OUTPUT);
  pinMode(TP_INT, OUTPUT);

  digitalWrite(TP_INT, LOW);
  digitalWrite(TP_RST, LOW);
  delay(20);

  digitalWrite(TP_RST, HIGH);
  delay(50);

  pinMode(TP_INT, INPUT_PULLUP);
}

static bool gt911_get_point(uint16_t &x, uint16_t &y, bool &pressed) {
  uint8_t status = 0;
  if (!gt911_read(0x814E, &status, 1)) return false;

  uint8_t points = status & 0x0F;
  if (points == 0) {
    pressed = false;
    return true;
  }

  uint8_t buf[8] = {0};
  if (!gt911_read(0x8150, buf, 8)) return false;

  x = ((uint16_t)buf[1] << 8) | buf[0];
  y = ((uint16_t)buf[3] << 8) | buf[2];
  pressed = true;

  gt911_write8(0x814E, 0x00);
  return true;
}

static void lv_touch_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  LV_UNUSED(indev_drv);

  uint16_t x, y;
  bool pressed = false;

  if (!gt911_get_point(x, y, pressed)) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  if (pressed) {
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// =====================================================
// DISPLAY
// =====================================================
static LGFX lcd;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;

static void lv_flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
  uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);

  lcd.startWrite();
  lcd.setAddrWindow(area->x1, area->y1, w, h);
  lcd.writePixels((lgfx::rgb565_t *)&color_p->full, w * h);
  lcd.endWrite();

  lv_disp_flush_ready(disp);
}

// =====================================================
// UI GLOBALS
// =====================================================
static lv_obj_t *label_date = nullptr;
static lv_obj_t *label_time = nullptr;
static lv_obj_t *label_city = nullptr;
static lv_obj_t *label_temp = nullptr;
static lv_obj_t *label_details = nullptr;

static const uint8_t STAR_COUNT = 18;
static lv_obj_t *stars[STAR_COUNT];

static lv_obj_t *earth_glow = nullptr;
static lv_obj_t *earth_core = nullptr;
static lv_obj_t *earth_shadow = nullptr;
static lv_obj_t *earth_highlight = nullptr;

static lv_obj_t *continent_1 = nullptr;
static lv_obj_t *continent_2 = nullptr;
static lv_obj_t *continent_3 = nullptr;
static lv_obj_t *continent_4 = nullptr;

static lv_obj_t *weather_sun = nullptr;
static lv_obj_t *weather_cloud_1 = nullptr;
static lv_obj_t *weather_cloud_2 = nullptr;
static lv_obj_t *weather_rain_1 = nullptr;
static lv_obj_t *weather_rain_2 = nullptr;
static lv_obj_t *weather_rain_3 = nullptr;

static lv_timer_t *home_anim_timer = nullptr;
static uint32_t anim_tick = 0;

// =====================================================
// HELPERS
// =====================================================
static lv_obj_t* make_circle(lv_obj_t *parent, int x, int y, int size, lv_color_t color, lv_opa_t opa) {
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_size(obj, size, size);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(obj, color, 0);
  lv_obj_set_style_bg_opa(obj, opa, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  return obj;
}

static lv_obj_t* make_rect(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t color, lv_opa_t opa, int radius) {
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_size(obj, w, h);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_style_radius(obj, radius, 0);
  lv_obj_set_style_bg_color(obj, color, 0);
  lv_obj_set_style_bg_opa(obj, opa, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  return obj;
}

static void add_soft_shadow(lv_obj_t *obj, lv_color_t color, int width, lv_opa_t opa) {
  lv_obj_set_style_shadow_color(obj, color, 0);
  lv_obj_set_style_shadow_width(obj, width, 0);
  lv_obj_set_style_shadow_opa(obj, opa, 0);
  lv_obj_set_style_shadow_spread(obj, 0, 0);
}

static void set_weather_partly_cloudy() {
  if (weather_sun) lv_obj_clear_flag(weather_sun, LV_OBJ_FLAG_HIDDEN);
  if (weather_cloud_1) lv_obj_clear_flag(weather_cloud_1, LV_OBJ_FLAG_HIDDEN);
  if (weather_cloud_2) lv_obj_clear_flag(weather_cloud_2, LV_OBJ_FLAG_HIDDEN);

  if (weather_rain_1) lv_obj_add_flag(weather_rain_1, LV_OBJ_FLAG_HIDDEN);
  if (weather_rain_2) lv_obj_add_flag(weather_rain_2, LV_OBJ_FLAG_HIDDEN);
  if (weather_rain_3) lv_obj_add_flag(weather_rain_3, LV_OBJ_FLAG_HIDDEN);
}

// =====================================================
// BUILD HOME UI
// =====================================================
static void build_home_ui() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_remove_style_all(scr);
  lv_obj_set_size(scr, 480, 480);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x040816), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // Background glow blobs
  lv_obj_t *nebula_1 = make_circle(scr, -60, 10, 280, lv_color_hex(0x1C5FFF), LV_OPA_20);
  lv_obj_t *nebula_2 = make_circle(scr, 250, 20, 180, lv_color_hex(0x53A8FF), LV_OPA_20);
  lv_obj_t *nebula_3 = make_circle(scr, 140, 300, 220, lv_color_hex(0x102C88), LV_OPA_25);
  lv_obj_t *nebula_4 = make_circle(scr, 320, 250, 130, lv_color_hex(0x4F87FF), LV_OPA_15);

  add_soft_shadow(nebula_1, lv_color_hex(0x1C5FFF), 30, LV_OPA_20);
  add_soft_shadow(nebula_2, lv_color_hex(0x53A8FF), 26, LV_OPA_20);
  add_soft_shadow(nebula_3, lv_color_hex(0x102C88), 28, LV_OPA_20);
  add_soft_shadow(nebula_4, lv_color_hex(0x4F87FF), 18, LV_OPA_15);

  // Date
  label_date = lv_label_create(scr);
  lv_label_set_text(label_date, "Wednesday, 6 March");
  lv_obj_set_style_text_color(label_date, lv_color_hex(0xF4F7FF), 0);
  lv_obj_set_style_text_font(label_date, &lv_font_montserrat_18, 0);
  lv_obj_align(label_date, LV_ALIGN_TOP_MID, 0, 26);

  // top line
  lv_obj_t *line1 = make_rect(scr, 78, 63, 324, 2, lv_color_hex(0xB7D4FF), LV_OPA_60, 1);
  LV_UNUSED(line1);

  // Time
  label_time = lv_label_create(scr);
  lv_label_set_text(label_time, "08:24");
  lv_obj_set_style_text_color(label_time, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(label_time, &lv_font_montserrat_18, 0);
  lv_obj_align(label_time, LV_ALIGN_TOP_MID, 0, 78);

  // Stars
  const int star_xy[STAR_COUNT][2] = {
    {36, 52}, {78, 88}, {120, 56}, {392, 52}, {428, 96}, {396, 146},
    {60, 158}, {430, 196}, {72, 238}, {410, 250}, {52, 320}, {436, 330},
    {88, 386}, {392, 388}, {210, 112}, {270, 140}, {182, 350}, {296, 364}
  };

  for (uint8_t i = 0; i < STAR_COUNT; i++) {
    int sz = (i % 3 == 0) ? 5 : ((i % 3 == 1) ? 3 : 2);
    stars[i] = make_circle(scr, star_xy[i][0], star_xy[i][1], sz, lv_color_hex(0xFFFBEF), LV_OPA_80);
    add_soft_shadow(stars[i], lv_color_hex(0xFFFFFF), 6, LV_OPA_50);
  }

  // Earth glow
  earth_glow = make_circle(scr, 111, 112, 258, lv_color_hex(0x2E78FF), LV_OPA_10);
  add_soft_shadow(earth_glow, lv_color_hex(0x67A7FF), 26, LV_OPA_50);

  // Earth core
  earth_core = make_circle(scr, 125, 126, 230, lv_color_hex(0x143DAB), LV_OPA_COVER);
  add_soft_shadow(earth_core, lv_color_hex(0x5E9CFF), 12, LV_OPA_40);

  // atmospheric ring
  lv_obj_t *earth_ring = lv_obj_create(scr);
  lv_obj_remove_style_all(earth_ring);
  lv_obj_set_size(earth_ring, 238, 238);
  lv_obj_set_pos(earth_ring, 121, 122);
  lv_obj_set_style_radius(earth_ring, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(earth_ring, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(earth_ring, 3, 0);
  lv_obj_set_style_border_color(earth_ring, lv_color_hex(0xDFF0FF), 0);
  lv_obj_set_style_border_opa(earth_ring, LV_OPA_80, 0);

  // night side
  earth_shadow = make_circle(earth_core, -30, 10, 190, lv_color_hex(0x07112D), LV_OPA_70);

  // highlight
  earth_highlight = make_circle(earth_core, 82, 18, 120, lv_color_hex(0xA8DDFF), LV_OPA_25);
  add_soft_shadow(earth_highlight, lv_color_hex(0xA8DDFF), 20, LV_OPA_20);

  // continents
  continent_1 = make_rect(earth_core, 88, 42, 62, 28, lv_color_hex(0x70A6D6), LV_OPA_60, 16);
  continent_2 = make_rect(earth_core, 118, 86, 48, 22, lv_color_hex(0x78AFDD), LV_OPA_55, 14);
  continent_3 = make_rect(earth_core, 132, 132, 36, 18, lv_color_hex(0x81B6E2), LV_OPA_55, 12);
  continent_4 = make_rect(earth_core, 102, 156, 28, 14, lv_color_hex(0x8DBEE8), LV_OPA_45, 10);

  // Weather icon in front of Earth
  weather_sun = make_circle(scr, 235, 192, 40, lv_color_hex(0xFFD043), LV_OPA_COVER);
  add_soft_shadow(weather_sun, lv_color_hex(0xFFD043), 18, LV_OPA_60);

  weather_cloud_1 = make_circle(scr, 205, 212, 44, lv_color_hex(0xF4F7FD), LV_OPA_COVER);
  weather_cloud_2 = make_circle(scr, 232, 222, 34, lv_color_hex(0xE8EEF8), LV_OPA_COVER);
  lv_obj_t *cloud_base = make_rect(scr, 196, 224, 78, 24, lv_color_hex(0xEEF3FB), LV_OPA_COVER, 12);
  LV_UNUSED(cloud_base);

  weather_rain_1 = make_rect(scr, 216, 248, 6, 18, lv_color_hex(0x66B8FF), LV_OPA_COVER, 3);
  weather_rain_2 = make_rect(scr, 232, 248, 6, 18, lv_color_hex(0x66B8FF), LV_OPA_COVER, 3);
  weather_rain_3 = make_rect(scr, 248, 248, 6, 18, lv_color_hex(0x66B8FF), LV_OPA_COVER, 3);

  set_weather_partly_cloudy();

  // City
  label_city = lv_label_create(scr);
  lv_label_set_text(label_city, "East Grinstead");
  lv_obj_set_style_text_color(label_city, lv_color_hex(0xF6F8FF), 0);
  lv_obj_set_style_text_font(label_city, &lv_font_montserrat_18, 0);
  lv_obj_align(label_city, LV_ALIGN_BOTTOM_MID, 0, -82);

  // bottom line
  lv_obj_t *line2 = make_rect(scr, 70, 404, 340, 2, lv_color_hex(0xAECDFD), LV_OPA_45, 1);
  LV_UNUSED(line2);

  // Temperature
  label_temp = lv_label_create(scr);
  lv_label_set_text(label_temp, "12°C | Feels like 10°C");
  lv_obj_set_style_text_color(label_temp, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(label_temp, &lv_font_montserrat_18, 0);
  lv_obj_align(label_temp, LV_ALIGN_BOTTOM_MID, 0, -44);

  // Details
  label_details = lv_label_create(scr);
  lv_label_set_text(label_details, "Humidity 63% • Wind 8 mph");
  lv_obj_set_style_text_color(label_details, lv_color_hex(0xD5E3F7), 0);
  lv_obj_set_style_text_font(label_details, &lv_font_montserrat_14, 0);
  lv_obj_align(label_details, LV_ALIGN_BOTTOM_MID, 0, -18);
}

// =====================================================
// ANIMATION
// =====================================================
static void home_anim_cb(lv_timer_t *timer) {
  LV_UNUSED(timer);

  anim_tick++;

  // stars twinkle
  for (uint8_t i = 0; i < STAR_COUNT; i++) {
    uint8_t phase = (anim_tick + i * 7) % 40;
    lv_opa_t opa;

    if (phase < 10) opa = LV_OPA_30;
    else if (phase < 20) opa = LV_OPA_50;
    else if (phase < 30) opa = LV_OPA_80;
    else opa = LV_OPA_60;

    lv_obj_set_style_bg_opa(stars[i], opa, 0);
  }

  // fake earth rotation
  int shift = (int)(anim_tick % 36) - 18;

  lv_obj_set_pos(continent_1, 88 + shift / 3, 42);
  lv_obj_set_pos(continent_2, 118 + shift / 4, 86);
  lv_obj_set_pos(continent_3, 132 + shift / 5, 132);
  lv_obj_set_pos(continent_4, 102 + shift / 4, 156);

  lv_obj_set_pos(earth_highlight, 82 + shift / 4, 18);

  // weather wobble
  int wobble = (int)(anim_tick % 12) - 6;
  lv_obj_set_pos(weather_sun,     235 + wobble / 3, 192 + wobble / 6);
  lv_obj_set_pos(weather_cloud_1, 205 + wobble / 4, 212);
  lv_obj_set_pos(weather_cloud_2, 232 + wobble / 5, 222);
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  // LCD
  lcd.init();
  lcd.setRotation(0);
  lcd.fillScreen(TFT_BLACK);

  // LVGL init
  lv_init();

  const uint16_t screenWidth  = 480;
  const uint16_t screenHeight = 480;

  const uint32_t buf_pixels = (uint32_t)screenWidth * 40;

  buf1 = (lv_color_t *)heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  buf2 = (lv_color_t *)heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  if (!buf1 || !buf2) {
    Serial.println("LVGL buffer allocation failed");
    while (true) {
      delay(1000);
    }
  }

  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buf_pixels);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = lv_flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  // Touch init
  Wire.begin(TP_SDA, TP_SCL);
  gt911_reset();

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = lv_touch_read;
  lv_indev_drv_register(&indev_drv);

  // UI
  build_home_ui();

  // Animation timer
  home_anim_timer = lv_timer_create(home_anim_cb, 140, nullptr);
}

// =====================================================
// LOOP
// =====================================================
void loop() {
  static uint32_t last_tick = millis();
  uint32_t now = millis();

  lv_tick_inc(now - last_tick);
  last_tick = now;

  lv_timer_handler();
  delay(5);
}
