#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <Touch_GT911.h>

// ================== DISPLAY (Twoje działające piny) ==================
#define GFX_BL 38

Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
  39 /* CS */, 48 /* SCK */, 47 /* SDA or MOSI */,
  18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
  11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */, 0  /* R4 */,
  8  /* G0 */, 20 /* G1 */, 3  /* G2 */, 46 /* G3 */, 9  /* G4 */, 10 /* G5 */,
  4  /* B0 */, 5  /* B1 */, 6  /* B2 */, 7  /* B3 */, 15 /* B4 */
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

// ================== TOUCH (GT911) ==================
#define TOUCH_SDA 19
#define TOUCH_SCL 45
#define TOUCH_INT -1
#define TOUCH_RST -1

Touch_GT911 ts(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, 480, 480);

// ---- Startowe ustawienia mapowania (wg Twoich objawów) ----
static bool SWAP_XY  = false;
static bool INVERT_X = true;
static bool INVERT_Y = true;

// ---- Auto-skalowanie surowych wartości (gdy zakres nie jest 0..479) ----
static int rawMinX =  99999, rawMaxX = -99999;
static int rawMinY =  99999, rawMaxY = -99999;

static void updateRawRange(int rx, int ry) {
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
  // 1) auto-range
  updateRawRange(rx, ry);

  int sx = scaleTo480(rx, rawMinX, rawMaxX);
  int sy = scaleTo480(ry, rawMinY, rawMaxY);

  // 2) swap/invert
  if (SWAP_XY) {
    int t = sx;
    sx = sy;
    sy = t;
  }
  if (INVERT_X) sx = 479 - sx;
  if (INVERT_Y) sy = 479 - sy;

  x = constrain(sx, 0, 479);
  y = constrain(sy, 0, 479);
}

static void printCfg() {
  Serial.printf("\nCFG: SWAP_XY=%s  INVERT_X=%s  INVERT_Y=%s\n",
                SWAP_XY ? "true" : "false",
                INVERT_X ? "true" : "false",
                INVERT_Y ? "true" : "false");
  Serial.printf("RAW RANGE: X[%d..%d] Y[%d..%d]\n", rawMinX, rawMaxX, rawMinY, rawMaxY);
  Serial.println("Keys: x=toggle invertX, y=toggle invertY, s=toggle swapXY, c=clear, p=print cfg\n");
}

static void draw_test_screen() {
  gfx->fillScreen(BLACK);

  int w = gfx->width();
  int barW = w / 6;
  uint16_t colors[6] = {RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA};
  for (int i = 0; i < 6; i++) {
    gfx->fillRect(i * barW, 0, barW, 60, colors[i]);
  }

  gfx->setCursor(10, 80);
  gfx->setTextColor(WHITE);
  gfx->setTextSize(2);
  gfx->println("ST7701 RGB TEST");

  gfx->setTextSize(1);
  gfx->setCursor(10, 110);
  gfx->println("Arduino_GFX v1.2.9 + Touch_GT911");
  gfx->setCursor(10, 130);
  gfx->println("Tap -> dot. Serial: x/y/s to fix mapping.");

  gfx->drawRect(0, 0, gfx->width(), gfx->height(), WHITE);
}

static void i2c_scan() {
  Serial.println("I2C scan:");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  found: 0x%02X\n", addr);
      found++;
    }
  }
  if (!found) Serial.println("  (nothing found)");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // Backlight
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);

  // DISPLAY
  gfx->begin();
  gfx->setRotation(0);
  draw_test_screen();
  Serial.println("Display init OK.");

  // TOUCH I2C: wymuszone piny (NAJWAŻNIEJSZE)
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(400000);
  delay(50);

  i2c_scan();

  ts.begin();
  ts.setRotation(ROTATION_NORMAL);

  printCfg();
  Serial.println("Touch the screen...");
}

void loop() {
  // Sterowanie z Serial
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == 'x' || ch == 'X') {
      INVERT_X = !INVERT_X;
      printCfg();
    }
    if (ch == 'y' || ch == 'Y') {
      INVERT_Y = !INVERT_Y;
      printCfg();
    }
    if (ch == 's' || ch == 'S') {
      SWAP_XY = !SWAP_XY;
      printCfg();
    }
    if (ch == 'c' || ch == 'C') {
      draw_test_screen();
      Serial.println("Cleared.");
    }
    if (ch == 'p' || ch == 'P') {
      printCfg();
    }
  }

  ts.read();

  if (ts.isTouched && ts.touches > 0) {
    int rx = ts.points[0].x;
    int ry = ts.points[0].y;

    int x, y;
    mapTouch(rx, ry, x, y);

    Serial.printf("RAW(%d,%d) -> XY(%d,%d)\n", rx, ry, x, y);
    gfx->fillCircle(x, y, 6, WHITE);
    delay(15);
  }

  delay(5);
}
