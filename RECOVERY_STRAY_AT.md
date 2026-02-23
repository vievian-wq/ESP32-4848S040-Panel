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

## Fast verification commands (Windows CMD)

Run these against your local sketch before pressing Verify:

```cmd
findstr /n /c:"@@" "C:\Users\vievi\Documents\Arduino\HOLY\sketch_feb23a\sketch_feb23a.ino"
findstr /n /c:"diff --git" "C:\Users\vievi\Documents\Arduino\HOLY\sketch_feb23a\sketch_feb23a.ino"
```

- If either command prints any line, your file is still a patch, not a sketch.
- A clean sketch should return no output for both commands.

## One-file repair checklist for your current folder

For `C:\Users\vievi\Documents\Arduino\HOLY\sketch_feb23a\sketch_feb23a.ino`:

1. Open the file and select all (`Ctrl+A`) then delete.
2. Paste only the raw source from `4_0_LvglWidgetsHoly.ino/4_0_LvglWidgetsHoly.ino`.
3. Save.
4. Re-run the two `findstr` commands above; they must print nothing.
5. Build again using an ESP32-S3 board profile.

## If GitHub looks stale (no new updates)

If GitHub still shows old content, usually local commits were created but not pushed from your machine.

Run in your local repo terminal:

```bash
git status
git branch --show-current
git log --oneline -n 5
git remote -v
git push origin <your-branch>
```

If you are editing in GitHub web UI, make sure you click **Commit changes** (or **Create pull request** then merge).

Quick check:
- `git status` should be clean after commit.
- `git log -n 1` should show your latest commit message.
- On GitHub, open the same branch name you pushed.
