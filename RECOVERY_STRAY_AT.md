# Fixing `Compilation error: stray '@' in program`

If Arduino IDE shows errors like:
- `stray '@' in program`
- `diff --git ... does not name a type`
- lines starting with `@@`, `+`, `-`

then your local `.ino` file contains pasted **Git diff text** instead of pure sketch code.

## Quick fix (Windows)

1. Close Arduino IDE.
2. Open your local sketch file:
   - `C:\Users\vievi\Desktop\4_0_LvglWidgetsHoly.ino\4_0_LvglWidgetsHoly\4_0_LvglWidgetsHoly.ino`
3. Delete all content.
4. Copy the clean sketch from this repository file:
   - `4_0_LvglWidgetsHoly.ino/4_0_LvglWidgetsHoly.ino`
5. Save the file.
6. Reopen Arduino IDE.

## Board selection (important)

This project is for **ESP32-S3 panel hardware**.
Do **not** compile as ESP32-C3.

Use an ESP32-S3 board profile in Arduino IDE before Verify/Upload.

## Libraries seen in your log

Your library versions are fine for this sketch:
- esp32 core `3.3.7`
- `lvgl` `8.3.11`
- `GFX Library for Arduino` `1.6.5`
- `TAMC_GT911` `1.0.2`
- `ArduinoJson` `7.4.2`

The current failure is caused by local file corruption, not by missing libraries.
