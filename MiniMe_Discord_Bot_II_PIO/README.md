# MiniMe II - PlatformIO project (optional build system)

Second way to build the firmware that also lives in the Arduino IDE sketch next
to this folder. The Arduino route (`../MiniMe_Discord_Bot_II`, compiled by
upstream CI) remains the canonical source of truth for development; this project
exists so that users who prefer PlatformIO can build and flash the same firmware.
Nothing in this folder changes the sketch, `docs/` or `tools`.

| | Arduino IDE route | PlatformIO route (this folder) |
| --- | --- | --- |
| Sources | `../MiniMe_Discord_Bot_II/*` | `src/*` (content-identical copy; LF line endings) |
| Version | `VERSION` and `MINIME_VERSION` in `src/minime_config.h` | same |
| Board options | `esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=custom,PSRAM=opi,CDCOnBoot=cdc` | `board = esp32-s3-devkitc1-n16r8`, `board_build.partitions`, `build_flags` |
| Framework | Arduino-ESP32 core installed by Board Manager (3.3.12+) | pinned pioarduino platform 55.03.312-1 (Arduino-ESP32 3.3.12, IDF 5.5.5) |
| Credentials | `MiniMe_Discord_Bot_II/secrets.h` | `src/secrets.h` (gitignored) |

## Contents

- `src/` - content-identical copy of the sketch sources (same files, including
  `partitions.csv`); this copy uses LF line endings.
- `platformio.ini` - pinned platform, board options and libraries.
- `src/secrets.h` - not in git; you create it (see below).

## Requirements

- PlatformIO Core 6.1+ (`pio`) or the VS Code PlatformIO IDE extension.
- Network access on the first build: the pinned platform (Arduino-ESP32 3.3.12)
  and its prebuilt ESP-IDF libraries are downloaded once into `~/.platformio`.

## 1. Add your secrets

The firmware reads credentials from a gitignored `secrets.h`. This project looks
for its own copy under `src/`:

```
rem Windows cmd, from this folder
copy ..\MiniMe_Discord_Bot_II\secrets.example.h src\secrets.h
```

```
# Windows PowerShell, from this folder
Copy-Item ..\MiniMe_Discord_Bot_II\secrets.example.h src\secrets.h
```

Then fill in WiFi/bot/OTA values and remove the `MINIME_SECRETS_IS_EXAMPLE`
define - the template deliberately trips `#error` while that flag is present.
Upstream CI does the same with
`sed -i '/MINIME_SECRETS_IS_EXAMPLE/d' MiniMe_Discord_Bot_II/secrets.h`.

If you already have `../MiniMe_Discord_Bot_II/secrets.h`, copy that file instead
so both routes use the same credentials.

## 2. Build

```
pio run                      # default env: minime-ii
pio run -t clean             # wipe .pio/build
```

Expected size for v1.00.01: about 20 percent of the 8 MB app slot.

## 3. Flash over USB

Connect the board (it appears as USB VID:PID 303A:1001, i.e. the ESP32-S3 native
USB serial/JTAG) and upload:

```
pio device list                        # find the port, e.g. COM11
pio run -t upload --upload-port COM11
```

`pio run -t upload` alone also works when exactly one board is connected.

## 4. Flash over Wi-Fi (ArduinoOTA)

The firmware runs an ArduinoOTA server on port 3232 whose password is
`OTA_PASSWORD` from `secrets.h`. Use the `espota` tool that ships with the
Arduino-ESP32 platform:

```
# Windows; on Linux/macOS use espota.py from the same folder
~\.platformio\packages\framework-arduinoespressif32\tools\espota.exe ^
    -i BOARD_IP -p 3232 -a OTA_PASSWORD ^
    -f .pio\build\minime-ii\firmware.bin
```

Or add Wi-Fi upload to this environment in `platformio.ini`:

```
upload_protocol = espota
upload_port = BOARD_IP
```

and run `pio run -t upload`. Note that a `secrets.json` on the SD card overrides
the compiled secrets at runtime, so the OTA password may be the SD one.

## 5. Verify

- LCD header shows the running version (`MINIME_VERSION`, e.g. v1.00.01).
- LAN web UI: `http://<board-ip>/` (hostname comes from `OTA_HOSTNAME`); the
  status API is `/api/status` and needs the web UI password when
  `WEB_UI_PASSWORD` is set.
- `pio device monitor -b 115200` - most logging goes to the LAN web UI log page
  rather than Serial (see `src/serial_log.cpp` and `docs/`).

## Keeping src/ in step with the sketch

After pulling a new sketch revision, refresh this copy (`.pio/` and `secrets.h`
are left alone):

```
rem Windows cmd, from this folder
robocopy ..\MiniMe_Discord_Bot_II src /MIR /XF secrets.h /NFL /NDL /NJH /NJS
```

```
# Windows PowerShell, from this folder
Copy-Item ..\MiniMe_Discord_Bot_II\* .\src\ -Recurse -Force
```

`src/` and `../MiniMe_Discord_Bot_II/` must stay content-identical apart from
`secrets.h`, which is gitignored. The PIO copy uses LF line endings; an Arduino
checkout may use CRLF. Keep the Arduino tree as the source of truth for
development, and refresh `src/` from it after sketch changes. Check with
`git status --short` before committing.

On Windows checkouts, compare the folders end-of-line agnostically:

```
git diff --no-index --ignore-cr-at-eol ..\MiniMe_Discord_Bot_II src
```

## Troubleshooting

- `pio run -t upload` aborts with
  `UnicodeEncodeError: 'charmap' codec can't encode characters` when its output
  is piped or redirected (and builds print `Firmware metrics can not be shown.
  Set the terminal codepage to "utf-8" or "cp65001"`): PlatformIO echoes esptool
  progress characters that the Windows console codepage cannot encode. Use a
  UTF-8 console (`chcp 65001`), or in PowerShell set
  `$env:PYTHONUTF8 = 1` and `[Console]::OutputEncoding = [Text.Encoding]::UTF8`
  before running the command.
- An interrupted upload can leave a stale `esptool`/`pio` process holding
  `.pio\build\minime-ii\*.bin` (upload then fails with
  `The process cannot access the file`). Close the PlatformIO terminal, make
  sure no `esptool`/`python` process is left, then upload again.
- To check what is really in the app slot:

```
%USERPROFILE%\.platformio\penv\Scripts\esptool.exe --chip esp32s3 --port COMx ^
    verify-flash 0x10000 .pio\build\minime-ii\firmware.bin
```

## Host checks and CI

`.github/workflows/compile.yml` compiles the Arduino sketch, so the PlatformIO
route is not part of CI. The host checks (`python tools/ci_sanity.py`,
`python tools/ci_html.py`, `pytest tools/...`) inspect only the sketch folder,
but `ci_sanity.py` also scans untracked text files in the repository - and this
project's gitignored `.pio/` tree contains third-party sources. Run those checks
before the first build, or from a clean clone, if you want a local run to match
CI.
