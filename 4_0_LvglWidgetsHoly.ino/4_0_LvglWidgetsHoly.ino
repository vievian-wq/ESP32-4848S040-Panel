/************************************************************
 * Holy Grail UI v1 (PLACEHOLDERS)
 * HW: ESP32-S3 + ST7701 RGB 480x480 + GT911 touch
 *
 * Screens (5):
 *  1) HOME      - Universe background + rotating Earth + date/time + compact condition icon
 *  2) LIVING    - Futuristic light toggle with glow ON/OFF
 *  3) WEATHER EG - 3-day forecast (East Grinstead) placeholder
 *  4) WEATHER LDN- 3-day forecast (London) placeholder
 *  5) SETTINGS  - Wi-Fi / Time / Brightness placeholders
 *
 * Notes:
 * - Requires: lvgl (v8), Arduino_GFX_Library, Touch_GT911
 * - lv_conf.h must exist (best: Documents/Arduino/libraries/lv_conf.h)
 * - Touch mapping uses your working orientation: SWAP=0, INVERT_X=1, INVERT_Y=1
 ************************************************************/

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <time.h>

#define LV_CONF_INCLUDE_SIMPLE 1
#include <lvgl.h>

#include <Arduino_GFX_Library.h>
#include <Touch_GT911.h>

// -------------------- PANEL / PINS (your working mapping) --------------------
#define GFX_BL 38

static const int PIN_LCD_SCK   = 48;
static const int PIN_LCD_MOSI  = 47;
static const int PIN_LCD_CS    = 39;

static const int PIN_DE        = 18;
static const int PIN_VSYNC     = 17;
static const int PIN_HSYNC     = 16;
static const int PIN_PCLK      = 21;

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

// -------------------- TOUCH (GT911) --------------------
#define TOUCH_SDA 19
#define TOUCH_SCL 45
#define TOUCH_INT -1
#define TOUCH_RST -1

Touch_GT911 ts(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, 480, 480);

// mapping (your final fixed behaviour)
static bool SWAP_XY  = false;
static bool INVERT_X = true;
static bool INVERT_Y = true;

// Raw auto-range (helps if GT911 gives non-0..479)
static int rawMinX =  99999, rawMaxX = -99999;
static int rawMinY =  99999, rawMaxY = -99999;

static inline void updateRawRange(int rx, int ry) {
  if (rx < rawMinX) rawMinX = rx;
  if (rx > rawMaxX) rawMaxX = rx;
  if (ry < rawMinY) rawMinY = ry;
  if (ry > rawMaxY) rawMaxY = ry;
}

static int scaleTo480(int v, int vmin, int vmax) {
  if (vmax <= vmin) return constrain(v, 0, 479);
  long out = (long)(v - vmin) * 479L / (long)(vmax - vmin);
  if (out < 0) out = 0;
  if (out > 479) out = 479;
  return (int)out;
}

static void mapTouch(int rx, int ry, int &x, int &y) {
  updateRawRange(rx, ry);

  int sx = scaleTo480(rx, rawMinX, rawMaxX);
  int sy = scaleTo480(ry, rawMinY, rawMaxY);

  if (SWAP_XY) { int t = sx; sx = sy; sy = t; }
  if (INVERT_X) sx = 479 - sx;
  if (INVERT_Y) sy = 479 - sy;

  x = constrain(sx, 0, 479);
  y = constrain(sy, 0, 479);
}

// -------------------- DISPLAY (Arduino_GFX ST7701 RGB) --------------------
Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
  PIN_LCD_CS, PIN_LCD_SCK, PIN_LCD_MOSI,
  PIN_DE, PIN_VSYNC, PIN_HSYNC, PIN_PCLK,
  PIN_R0, PIN_R1, PIN_R2, PIN_R3, PIN_R4,
  PIN_G0, PIN_G1, PIN_G2, PIN_G3, PIN_G4, PIN_G5,
  PIN_B0, PIN_B1, PIN_B2, PIN_B3, PIN_B4
);

Arduino_ST7701_RGBPanel *gfx = new Arduino_ST7701_RGBPanel(
  bus,
  GFX_NOT_DEFINED /* RST */,
  0 /* rotation */,
  true /* IPS */,
  480 /* width */, 480 /* height */,
  st7701_type1_init_operations, sizeof(st7701_type1_init_operations),
  true /* BGR */,
  10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
  10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */
);

// -------------------- LVGL buffer / flush --------------------
static lv_disp_draw_buf_t draw_buf;

// Using PSRAM is ideal; keep memory safe: 480 * 40 lines
static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;

static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  const int32_t w = (area->x2 - area->x1 + 1);
  const int32_t h = (area->y2 - area->y1 + 1);

  // LVGL gives RGB565 in lv_color_t, Arduino_GFX expects uint16_t
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)color_p, w, h);

  lv_disp_flush_ready(disp);
}

static void my_touch_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data) {
  ts.read();

  if (ts.isTouched && ts.touches > 0) {
    int rx = ts.points[0].x;
    int ry = ts.points[0].y;

    int x, y;
    mapTouch(rx, ry, x, y);

    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// LVGL tick
static void lv_tick_task(void *arg) {
  (void)arg;
  lv_tick_inc(5);
}

// -------------------- UI globals --------------------
static lv_obj_t *tabview = nullptr;

// Screen 1 objects
static lv_obj_t *earth = nullptr;
static lv_obj_t *lbl_time = nullptr;
static lv_obj_t *lbl_date = nullptr;
static lv_obj_t *lbl_city = nullptr;
static lv_obj_t *lbl_temp = nullptr;
static lv_obj_t *lbl_cond = nullptr;
static lv_obj_t *cond_icon = nullptr;

// Screen 2 objects
static lv_obj_t *sw_light = nullptr;
static lv_obj_t *glow_group = nullptr;
static lv_obj_t *lbl_light_state = nullptr;

// Screen 3/4 forecast labels
static lv_obj_t *eg_cards[3] = {nullptr, nullptr, nullptr};
static lv_obj_t *ldn_cards[3] = {nullptr, nullptr, nullptr};

// Screen 5 objects
static lv_obj_t *slider_bl = nullptr;
static lv_obj_t *lbl_bl = nullptr;
static lv_obj_t *lbl_wifi = nullptr;
static lv_obj_t *lbl_time_src = nullptr;

// -------------------- Styles (minimal + futuristic shading) --------------------
static lv_style_t st_bg;
static lv_style_t st_panel;
static lv_style_t st_title;
static lv_style_t st_small;
static lv_style_t st_chip;
static lv_style_t st_glow;

static void styles_init() {
  lv_style_init(&st_bg);
  lv_style_set_bg_color(&st_bg, lv_color_hex(0x05060A));
  lv_style_set_bg_opa(&st_bg, LV_OPA_COVER);

  lv_style_init(&st_panel);
  lv_style_set_radius(&st_panel, 18);
  lv_style_set_bg_color(&st_panel, lv_color_hex(0x0B1020));
  lv_style_set_bg_opa(&st_panel, LV_OPA_80);
  lv_style_set_border_width(&st_panel, 1);
  lv_style_set_border_color(&st_panel, lv_color_hex(0x1E2A4A));
  lv_style_set_pad_all(&st_panel, 14);

  lv_style_init(&st_title);
  lv_style_set_text_color(&st_title, lv_color_hex(0xE9F0FF));
  lv_style_set_text_font(&st_title, &lv_font_montserrat_20);

  lv_style_init(&st_small);
  lv_style_set_text_color(&st_small, lv_color_hex(0xA9B6D6));
  lv_style_set_text_font(&st_small, &lv_font_montserrat_14);

  lv_style_init(&st_chip);
  lv_style_set_radius(&st_chip, 999);
  lv_style_set_bg_color(&st_chip, lv_color_hex(0x0E1630));
  lv_style_set_bg_opa(&st_chip, LV_OPA_90);
  lv_style_set_border_width(&st_chip, 1);
  lv_style_set_border_color(&st_chip, lv_color_hex(0x25335A));
  lv_style_set_pad_ver(&st_chip, 6);
  lv_style_set_pad_hor(&st_chip, 10);

  lv_style_init(&st_glow);
  lv_style_set_radius(&st_glow, 999);
  lv_style_set_bg_color(&st_glow, lv_color_hex(0x2BD2FF));
  lv_style_set_bg_opa(&st_glow, LV_OPA_80);
}

// -------------------- Helpers --------------------
static void backlight_set(uint8_t pct) {
  if (pct > 100) pct = 100;
  // Simple digital (you can PWM later). For now: >0 ON, 0 OFF
  digitalWrite(GFX_BL, (pct > 0) ? HIGH : LOW);
}

// tiny "stars" canvas background
static lv_obj_t* make_stars_bg(lv_obj_t *parent) {
  static lv_color_t cbuf[480 * 80]; // 80 lines only, canvas will tile visually
  lv_obj_t *canvas = lv_canvas_create(parent);
  lv_canvas_set_buffer(canvas, cbuf, 480, 80, LV_IMG_CF_TRUE_COLOR);
  lv_canvas_fill_bg(canvas, lv_color_hex(0x05060A), LV_OPA_COVER);

  // deterministic star pattern
  for (int i = 0; i < 120; i++) {
    int x = (i * 37) % 480;
    int y = (i * 53) % 80;
    lv_canvas_set_px(canvas, x, y, lv_color_hex(0xFFFFFF));
    if ((i % 5) == 0 && x + 1 < 480) lv_canvas_set_px(canvas, x + 1, y, lv_color_hex(0x9FB3FF));
  }

  lv_obj_set_size(canvas, 480, 480);
  // tile by scaling the small canvas; it will stretch nicely as "space"
  lv_obj_set_style_img_recolor_opa(canvas, LV_OPA_0, 0);
  return canvas;
}

// Weather icon placeholder (small chip)
static lv_obj_t* make_condition_chip(lv_obj_t *parent, const char *text) {
  lv_obj_t *chip = lv_obj_create(parent);
  lv_obj_add_style(chip, &st_chip, 0);
  lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

  lv_obj_t *lbl = lv_label_create(chip);
  lv_label_set_text(lbl, text);
  lv_obj_add_style(lbl, &st_small, 0);

  return chip;
}

// -------------------- Screen 1: HOME (rotating Earth placeholder) --------------------
static void earth_anim_cb(void *obj, int32_t v) {
  // LVGL transform angle is 0.1 degree units
  lv_obj_set_style_transform_angle((lv_obj_t*)obj, v, 0);
}

static void build_screen_home(lv_obj_t *tab) {
  lv_obj_add_style(tab, &st_bg, 0);
  make_stars_bg(tab);

  // main globe
  earth = lv_obj_create(tab);
  lv_obj_add_style(earth, &st_panel, 0);
  lv_obj_set_size(earth, 280, 280);
  lv_obj_center(earth);
  lv_obj_set_style_radius(earth, 140, 0);
  lv_obj_set_style_bg_color(earth, lv_color_hex(0x081426), 0);
  lv_obj_set_style_border_color(earth, lv_color_hex(0x2A3D6C), 0);

  // "continents" ring (simple arc to suggest a globe)
  lv_obj_t *arc = lv_arc_create(earth);
  lv_obj_set_size(arc, 260, 260);
  lv_obj_center(arc);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_rotation(arc, 0);
  lv_arc_set_value(arc, 70);
  lv_obj_set_style_arc_width(arc, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, lv_color_hex(0x0F2A4A), LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_hex(0x2BD2FF), LV_PART_INDICATOR);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);

  // animate rotation of the earth container
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, earth);
  lv_anim_set_exec_cb(&a, earth_anim_cb);
  lv_anim_set_time(&a, 9000);
  lv_anim_set_values(&a, 0, 3600); // 0..360 degrees
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);

  // top time/date
  lbl_time = lv_label_create(tab);
  lv_obj_add_style(lbl_time, &st_title, 0);
  lv_label_set_text(lbl_time, "08:31");
  lv_obj_align(lbl_time, LV_ALIGN_TOP_LEFT, 18, 14);

  lbl_date = lv_label_create(tab);
  lv_obj_add_style(lbl_date, &st_small, 0);
  lv_label_set_text(lbl_date, "Tue, 05 Mar 2026");
  lv_obj_align_to(lbl_date, lbl_time, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

  // city / weather summary
  lbl_city = lv_label_create(tab);
  lv_obj_add_style(lbl_city, &st_small, 0);
  lv_label_set_text(lbl_city, "East Grinstead");
  lv_obj_align(lbl_city, LV_ALIGN_BOTTOM_LEFT, 18, -20);

  lbl_temp = lv_label_create(tab);
  lv_obj_add_style(lbl_temp, &st_title, 0);
  lv_label_set_text(lbl_temp, "7°C");
  lv_obj_align(lbl_temp, LV_ALIGN_BOTTOM_RIGHT, -18, -26);

  // condition icon chip (sun+cloud only, no extra sun behind globe)
  cond_icon = make_condition_chip(tab, "Partly Cloudy");
  lv_obj_align(cond_icon, LV_ALIGN_BOTTOM_MID, 0, -22);

  lbl_cond = lv_label_create(tab);
  lv_obj_add_style(lbl_cond, &st_small, 0);
  lv_label_set_text(lbl_cond, "Universe / Earth animation: placeholder");
  lv_obj_align(lbl_cond, LV_ALIGN_BOTTOM_MID, 0, -2);
}

// -------------------- Screen 2: Living Room light --------------------
static void set_glow(bool on) {
  if (!glow_group) return;
  uint8_t opa = on ? LV_OPA_90 : LV_OPA_20;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(glow_group); i++) {
    lv_obj_t *c = lv_obj_get_child(glow_group, i);
    lv_obj_set_style_bg_opa(c, opa, 0);
  }
  lv_label_set_text(lbl_light_state, on ? "Light: ON" : "Light: OFF");
}

static void sw_event_cb(lv_event_t *e) {
  lv_obj_t *sw = lv_event_get_target(e);
  bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
  set_glow(on);
}

static void build_screen_living(lv_obj_t *tab) {
  lv_obj_add_style(tab, &st_bg, 0);

  // A "cockpit-like" placeholder background: dark + orange/blue dots
  lv_obj_t *bg = lv_obj_create(tab);
  lv_obj_set_size(bg, 480, 480);
  lv_obj_center(bg);
  lv_obj_set_style_bg_color(bg, lv_color_hex(0x05060A), 0);
  lv_obj_set_style_border_width(bg, 0, 0);
  lv_obj_set_style_radius(bg, 0, 0);

  // glow dots group
  glow_group = lv_obj_create(tab);
  lv_obj_set_size(glow_group, 480, 480);
  lv_obj_set_style_bg_opa(glow_group, LV_OPA_0, 0);
  lv_obj_set_style_border_width(glow_group, 0, 0);

  for (int i = 0; i < 26; i++) {
    lv_obj_t *dot = lv_obj_create(glow_group);
    lv_obj_add_style(dot, &st_glow, 0);
    lv_obj_set_size(dot, (i % 3) ? 10 : 14, (i % 3) ? 10 : 14);
    int x = (i * 67) % 460;
    int y = (i * 41) % 460;
    lv_obj_set_pos(dot, x, y);
  }

  // panel
  lv_obj_t *panel = lv_obj_create(tab);
  lv_obj_add_style(panel, &st_panel, 0);
  lv_obj_set_size(panel, 440, 220);
  lv_obj_align(panel, LV_ALIGN_BOTTOM_MID, 0, -18);

  lv_obj_t *title = lv_label_create(panel);
  lv_obj_add_style(title, &st_title, 0);
  lv_label_set_text(title, "Living Room");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

  lbl_light_state = lv_label_create(panel);
  lv_obj_add_style(lbl_light_state, &st_small, 0);
  lv_label_set_text(lbl_light_state, "Light: OFF");
  lv_obj_align(lbl_light_state, LV_ALIGN_TOP_LEFT, 0, 36);

  // switch
  sw_light = lv_switch_create(panel);
  lv_obj_align(sw_light, LV_ALIGN_RIGHT_MID, -10, 0);
  lv_obj_add_event_cb(sw_light, sw_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // initial OFF
  set_glow(false);

  lv_obj_t *hint = lv_label_create(panel);
  lv_obj_add_style(hint, &st_small, 0);
  lv_label_set_text(hint, "Placeholder: later we bind this to your real relay / Tuya / MQTT.");
  lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

// -------------------- Forecast card builder --------------------
static lv_obj_t* make_forecast_card(lv_obj_t *parent, const char *day, const char *t, const char *cond) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_add_style(card, &st_panel, 0);
  lv_obj_set_size(card, 440, 110);

  lv_obj_t *d = lv_label_create(card);
  lv_obj_add_style(d, &st_title, 0);
  lv_label_set_text(d, day);
  lv_obj_align(d, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *temp = lv_label_create(card);
  lv_obj_add_style(temp, &st_title, 0);
  lv_label_set_text(temp, t);
  lv_obj_align(temp, LV_ALIGN_TOP_RIGHT, 0, 0);

  lv_obj_t *c = lv_label_create(card);
  lv_obj_add_style(c, &st_small, 0);
  lv_label_set_text(c, cond);
  lv_obj_align(c, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  return card;
}

static void build_screen_weather_city(lv_obj_t *tab, const char *city, lv_obj_t **cards_out) {
  lv_obj_add_style(tab, &st_bg, 0);
  make_stars_bg(tab);

  lv_obj_t *header = lv_obj_create(tab);
  lv_obj_add_style(header, &st_panel, 0);
  lv_obj_set_size(header, 440, 90);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 18);

  lv_obj_t *t = lv_label_create(header);
  lv_obj_add_style(t, &st_title, 0);
  lv_label_set_text_fmt(t, "%s Forecast", city);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *sub = lv_label_create(header);
  lv_obj_add_style(sub, &st_small, 0);
  lv_label_set_text(sub, "3-day outlook (placeholder data)");
  lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  // cards
  lv_obj_t *c1 = make_forecast_card(tab, "Today",     "7°C / 3°C", "Partly Cloudy");
  lv_obj_align(c1, LV_ALIGN_TOP_MID, 0, 125);
  lv_obj_t *c2 = make_forecast_card(tab, "Tomorrow",  "8°C / 2°C", "Light Rain");
  lv_obj_align(c2, LV_ALIGN_TOP_MID, 0, 245);
  lv_obj_t *c3 = make_forecast_card(tab, "Day +2",    "6°C / 1°C", "Clear");
  lv_obj_align(c3, LV_ALIGN_TOP_MID, 0, 365);

  cards_out[0] = c1; cards_out[1] = c2; cards_out[2] = c3;
}

// -------------------- Screen 5: Settings --------------------
static void slider_event_cb(lv_event_t *e) {
  int v = lv_slider_get_value(slider_bl);
  lv_label_set_text_fmt(lbl_bl, "Brightness: %d%%", v);
  backlight_set((uint8_t)v);
}

static void build_screen_settings(lv_obj_t *tab) {
  lv_obj_add_style(tab, &st_bg, 0);

  lv_obj_t *panel = lv_obj_create(tab);
  lv_obj_add_style(panel, &st_panel, 0);
  lv_obj_set_size(panel, 440, 440);
  lv_obj_center(panel);

  lv_obj_t *title = lv_label_create(panel);
  lv_obj_add_style(title, &st_title, 0);
  lv_label_set_text(title, "Settings");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

  // Wi-Fi placeholder
  lbl_wifi = lv_label_create(panel);
  lv_obj_add_style(lbl_wifi, &st_small, 0);
  lv_label_set_text(lbl_wifi, "Wi-Fi: Not connected (placeholder)");
  lv_obj_align(lbl_wifi, LV_ALIGN_TOP_LEFT, 0, 46);

  // Time source placeholder
  lbl_time_src = lv_label_create(panel);
  lv_obj_add_style(lbl_time_src, &st_small, 0);
  lv_label_set_text(lbl_time_src, "Time: NTP (preferred) / RTC (fallback) - placeholder");
  lv_obj_align(lbl_time_src, LV_ALIGN_TOP_LEFT, 0, 70);

  // Brightness
  lbl_bl = lv_label_create(panel);
  lv_obj_add_style(lbl_bl, &st_small, 0);
  lv_label_set_text(lbl_bl, "Brightness: 100%");
  lv_obj_align(lbl_bl, LV_ALIGN_TOP_LEFT, 0, 120);

  slider_bl = lv_slider_create(panel);
  lv_obj_set_width(slider_bl, 400);
  lv_obj_align(slider_bl, LV_ALIGN_TOP_LEFT, 0, 150);
  lv_slider_set_range(slider_bl, 0, 100);
  lv_slider_set_value(slider_bl, 100, LV_ANIM_OFF);
  lv_obj_add_event_cb(slider_bl, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // Network section placeholders
  lv_obj_t *net = lv_obj_create(panel);
  lv_obj_add_style(net, &st_panel, 0);
  lv_obj_set_size(net, 400, 180);
  lv_obj_align(net, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_obj_t *n1 = lv_label_create(net);
  lv_obj_add_style(n1, &st_small, 0);
  lv_label_set_text(n1, "Wi-Fi setup: SSID list + password entry (placeholder)");
  lv_obj_align(n1, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *n2 = lv_label_create(net);
  lv_obj_add_style(n2, &st_small, 0);
  lv_label_set_text(n2, "NTP servers: pool.ntp.org / time.google.com (placeholder)");
  lv_obj_align(n2, LV_ALIGN_TOP_LEFT, 0, 30);

  lv_obj_t *n3 = lv_label_create(net);
  lv_obj_add_style(n3, &st_small, 0);
  lv_label_set_text(n3, "Device info: build, heap, PSRAM (placeholder)");
  lv_obj_align(n3, LV_ALIGN_TOP_LEFT, 0, 60);
}

// -------------------- Build UI (TabView with 5 screens) --------------------
static void ui_build() {
  styles_init();

  tabview = lv_tabview_create(lv_scr_act(), LV_DIR_BOTTOM, 58);
  lv_obj_set_size(tabview, 480, 480);

  // style tab buttons a bit
  lv_obj_t *tab_btns = lv_tabview_get_tab_btns(tabview);
  lv_obj_set_style_bg_color(tab_btns, lv_color_hex(0x070A14), 0);
  lv_obj_set_style_border_width(tab_btns, 0, 0);

  lv_obj_t *t1 = lv_tabview_add_tab(tabview, "Home");
  lv_obj_t *t2 = lv_tabview_add_tab(tabview, "Living");
  lv_obj_t *t3 = lv_tabview_add_tab(tabview, "E.Grin");
  lv_obj_t *t4 = lv_tabview_add_tab(tabview, "London");
  lv_obj_t *t5 = lv_tabview_add_tab(tabview, "Settings");

  build_screen_home(t1);
  build_screen_living(t2);
  build_screen_weather_city(t3, "East Grinstead", eg_cards);
  build_screen_weather_city(t4, "London", ldn_cards);
  build_screen_settings(t5);
}

// -------------------- Time placeholders (later: NTP/RTC) --------------------
static uint32_t last_time_update = 0;

static void ui_time_tick() {
  if (!lbl_time || !lbl_date) return;
  if (millis() - last_time_update < 1000) return;
  last_time_update = millis();

  // Placeholder “running clock” without NTP, based on millis()
  static int hh = 8, mm = 31, ss = 0;
  ss++;
  if (ss >= 60) { ss = 0; mm++; }
  if (mm >= 60) { mm = 0; hh++; }
  if (hh >= 24) hh = 0;

  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
  lv_label_set_text(lbl_time, buf);

  // Date placeholder
  lv_label_set_text(lbl_date, "Tue, 05 Mar 2026");
}

// -------------------- Arduino setup/loop --------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  // Backlight
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);

  // Display init
  gfx->begin();
  gfx->setRotation(0);

  // Touch I2C
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(400000);
  delay(50);

  ts.begin();
  ts.setRotation(ROTATION_NORMAL);

  // LVGL init
  lv_init();

  // Allocate LVGL buffers (PSRAM if possible)
  const size_t buf_pixels = 480 * 40; // 40 lines
  buf1 = (lv_color_t*)heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  buf2 = (lv_color_t*)heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf1 || !buf2) {
    // fallback to internal RAM
    buf1 = (lv_color_t*)malloc(buf_pixels * sizeof(lv_color_t));
    buf2 = (lv_color_t*)malloc(buf_pixels * sizeof(lv_color_t));
  }

  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buf_pixels);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 480;
  disp_drv.ver_res = 480;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touch_read;
  lv_indev_drv_register(&indev_drv);

  // LVGL tick timer every 5ms
  const esp_timer_create_args_t periodic_timer_args = {
    .callback = &lv_tick_task,
    .arg = nullptr,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "lv_tick"
  };
  esp_timer_handle_t periodic_timer;
  esp_timer_create(&periodic_timer_args, &periodic_timer);
  esp_timer_start_periodic(periodic_timer, 5000);

  ui_build();

  Serial.println("HolyGrail UI v1 ready.");
  Serial.println("Tabs: Home / Living / E.Grin / London / Settings");
}

void loop() {
  lv_timer_handler();
  ui_time_tick();
  delay(5);
}
