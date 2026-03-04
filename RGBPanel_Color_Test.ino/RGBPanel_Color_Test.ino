#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#warning "This sketch targets ESP32-S3 RGB panel boards."
#endif

// Pins for ESP32-4848S040 panel (same mapping as main project)
static const int PIN_DE    = 18;
static const int PIN_HSYNC = 16;
static const int PIN_VSYNC = 17;
static const int PIN_PCLK  = 21;

static const int PIN_BL = 38;

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

// Backlight PWM
static const int BL_PWM_FREQ = 150;
static const int BL_PWM_RES = 10;

#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void backlight_pwm_init() {
  ledcAttach(PIN_BL, BL_PWM_FREQ, BL_PWM_RES);
}

static void backlight_pwm_write(uint32_t duty) {
  ledcWrite(PIN_BL, duty);
}
#else
static const int BL_PWM_CH = 0;
static void backlight_pwm_init() {
  ledcSetup(BL_PWM_CH, BL_PWM_FREQ, BL_PWM_RES);
  ledcAttachPin(PIN_BL, BL_PWM_CH);
}

static void backlight_pwm_write(uint32_t duty) {
  ledcWrite(BL_PWM_CH, duty);
}
#endif

static void backlight_set_percent(uint8_t pct) {
  if (pct > 100) pct = 100;
  uint32_t duty = (uint32_t)pct * ((1 << BL_PWM_RES) - 1) / 100;
  backlight_pwm_write(duty);
}

#if defined(CONFIG_IDF_TARGET_ESP32S3)
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
  PIN_DE, PIN_VSYNC, PIN_HSYNC, PIN_PCLK,
  PIN_R0, PIN_R1, PIN_R2, PIN_R3, PIN_R4,
  PIN_G0, PIN_G1, PIN_G2, PIN_G3, PIN_G4, PIN_G5,
  PIN_B0, PIN_B1, PIN_B2, PIN_B3, PIN_B4,
  /*hsync_polarity=*/0, /*vsync_polarity=*/0,
  /*pclk_active_neg=*/0, /*auto_flush=*/true
);

// NOTE:
// Full 480x480 RGB frame buffer may not fit when PSRAM is disabled in Arduino menu.
// For diagnostics we use a smaller logical canvas to avoid immediate OOM resets.
static const int TEST_W = 240;
static const int TEST_H = 240;

Arduino_GFX *gfx = new Arduino_ST7701_RGBPanel(
  rgbpanel,
  /*rst=*/-1, /*rotation=*/0,
  /*ips=*/false,
  /*width=*/TEST_W, /*height=*/TEST_H
);
#else
Arduino_GFX *gfx = nullptr;
#endif

static void show_color_bars() {
  if (!gfx) return;

  const uint16_t colors[] = {
    RED, GREEN, BLUE,
    CYAN, MAGENTA, YELLOW,
    WHITE, BLACK
  };

  const int count = sizeof(colors) / sizeof(colors[0]);
  int w = gfx->width() / count;
  for (int i = 0; i < count; i++) {
    gfx->fillRect(i * w, 0, (i == count - 1) ? (gfx->width() - i * w) : w, gfx->height(), colors[i]);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("RGB panel color test starting...");
  Serial.printf("PSRAM detected: %s\n", psramFound() ? "YES" : "NO");
  if (!psramFound()) {
    Serial.println("TIP: In Arduino IDE enable Tools -> PSRAM -> OPI PSRAM for full 480x480 tests.");
  }

  backlight_pwm_init();
  backlight_set_percent(100);

  if (!gfx) {
    Serial.println("ERROR: gfx is null (wrong target?)");
    while (1) delay(1000);
  }

  gfx->begin();
  gfx->fillScreen(BLACK);
  delay(500);

  // First sanity sequence: full-screen colors
  gfx->fillScreen(RED);    delay(800);
  gfx->fillScreen(GREEN);  delay(800);
  gfx->fillScreen(BLUE);   delay(800);
  gfx->fillScreen(WHITE);  delay(800);
  gfx->fillScreen(BLACK);  delay(400);

  // Then static bars for channel verification
  show_color_bars();
  Serial.println("Color bars displayed (240x240 test canvas). If only backlight is visible, check RGB timing/pins.");
}

void loop() {
  // Blink between bars and gray every 2s so you can confirm refreshing works too
  static uint32_t last = 0;
  static bool gray = false;
  if (millis() - last > 2000) {
    last = millis();
    gray = !gray;
    if (gray) {
      gfx->fillScreen(0x8410); // mid-gray in RGB565
    } else {
      show_color_bars();
    }
  }
}
