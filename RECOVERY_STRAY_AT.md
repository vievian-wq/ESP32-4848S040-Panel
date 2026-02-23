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

## Do NOT copy from Diff view

If you copy from a PR/"Diff" tab, you may accidentally copy lines like:
- `@@ -1,54 +1,62 @@`
- `+...` / `-...`

Always copy from the **raw file view** of `4_0_LvglWidgetsHoly.ino/4_0_LvglWidgetsHoly.ino`.

## Quick self-check before compile

Open your local `.ino` and confirm:
- line 1 should start with the sketch header comment banner (the first line in the repo file)
- there is **no** line containing `@@`
- there is **no** line starting with `diff --git`

If any of those appear, the file is still a patch, not a sketch.

## If you get `unterminated comment`

This usually means markdown/checklist text was pasted into the `.ino` (for example lines starting with `+-` or backticked snippets).
Delete all local `.ino` content and paste only the raw sketch file contents again.
