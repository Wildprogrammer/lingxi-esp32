# Wildprogrammer ESP32-S3 N16R8

Custom **Lingxi** device board definition for the generic dual-USB ESP32-S3
development board marked `ESP32-S3 N16R8` (16 MB flash, 8 MB octal PSRAM),
built on the upstream `xiaozhi-esp32` firmware.

## USB ports

- Use the CH343P USB-to-UART port for flashing and serial logs.
- Keep the native ESP32-S3 USB/OTG port for the planned USB HID function.
- GPIO19 (USB D-) and GPIO20 (USB D+) are intentionally not assigned here.

## Audio wiring

The hardware uses independent I2S controllers for the INMP441 microphone and
MAX98357A amplifier, matching the upstream `bread-compact-wifi` wiring.

| Signal | ESP32-S3 | Module pin |
| --- | --- | --- |
| Microphone word select | GPIO4 | INMP441 WS |
| Microphone bit clock | GPIO5 | INMP441 SCK |
| Microphone data in | GPIO6 | INMP441 SD |
| Speaker data out | GPIO7 | MAX98357A DIN |
| Speaker bit clock | GPIO15 | MAX98357A BCLK |
| Speaker word select | GPIO16 | MAX98357A LRC |
| Microphone power | 3V3 | INMP441 VDD |
| Amplifier power | 5V | MAX98357A VIN |
| Ground | GND | Both modules GND |

Connect the INMP441 `L/R` pin to GND so its samples use the left I2S slot.
Connect the speaker only to the amplifier output terminals, never directly to an
ESP32 GPIO. Check the exact MAX98357A breakout documentation before deciding
whether its `SD` pin may float or must be pulled high.

## OLED wiring

The board preset supports a four-pin SSD1306 128x32 I2C OLED. It probes address
`0x3C` first and then `0x3D`; if neither address responds, firmware continues
with `NoDisplay` and records the probe failure in the serial log.

| OLED pin | ESP32-S3 |
| --- | --- |
| GND | GND |
| VCC | 3V3 |
| SCL | GPIO42 |
| SDA | GPIO41 |

The screen orientation defaults to mirrored X/Y to match the photographed
module's connector orientation. Change the two mirror constants in `config.h`
if the image is physically upside down after wiring.

## On-board controls

- BOOT button: GPIO0; click to start/stop a conversation, or enter Wi-Fi setup
  while the firmware is starting.
- WS2812 status LED: GPIO48.

## Build

The local checkout includes a repeatable local PowerShell entrypoint that activates
the installed ESP-IDF 6.1 profile, forces UTF-8 Python mode, and uses the short
component-cache path needed on Windows:

```powershell
$env:IDF_POWERSHELL_PROFILE = "C:\path\to\Microsoft.PowerShell_profile.ps1"
& .\main\boards\wildprogrammer-esp32s3-n16r8\build.ps1
```

The equivalent command after manually activating ESP-IDF is:

```powershell
python scripts/build.py wildprogrammer-esp32s3-n16r8 --language zh-CN --wake-word "custom:ni hao ling xi=你好灵溪;xiao ling xi=小灵溪;ling xi ling xi=灵溪灵溪;ling xi ni hao=灵溪你好;ling xi zai ma=灵溪在吗;ting zhi=停止;bi zui=闭嘴;tui chu=退出"
```

The user-facing device name is **灵溪**. The PowerShell entrypoint enables the
local ESP-SR/MultiNet wake phrases “你好灵溪”, “小灵溪”, “灵溪灵溪”, “灵溪你好”,
and “灵溪在吗” by default, together with the local control phrases “停止”, “闭嘴”,
and “退出”.
The short two-syllable phrase “灵溪” is intentionally not enabled because it is
homophonous with the common word “灵犀” and is more prone to false activation.
The detector uses a board-specific 25% custom threshold; lower values are more
sensitive. This sits between the upstream 20% default, which produced repeated
ambient false activations on the assembled device, and the previous 30% setting,
which could miss normal speech. The short local controls “停止”, “闭嘴”, and
“退出” remain available to interrupt playback but are ignored while the device
is idle, so they cannot open a new conversation. Pass
`-WakeWord disabled` only for diagnostics that intentionally use the BOOT
button as the sole conversation trigger.

Wi-Fi provisioning and the router-visible DHCP hostname use the prefix
`Lingxi` (for example `Lingxi-7A3D`). Internal repository names and the
`/xiaozhi/` compatibility endpoint remain unchanged because they are protocol
and upstream implementation identifiers rather than the device's name.

Successful output includes `build\merged-binary.bin`. Flashing is intentionally
not part of this script and remains a separate, user-approved operation.

## Release artifact ownership

The generated `xiaozhi.bin`, `generated_assets.bin`, and `merged-binary.bin` are
firmware release artifacts, not Agent Session Gateway workflow resources. Do
not copy them into the workflow Git directory. A release store should keep the
binary bytes outside Git and pair them with an explicit manifest containing the
clean firmware source commit, upstream baseline, ESP-IDF/toolchain version,
board, build arguments, embedded OTA origin, sizes, and SHA-256 digests.

`xiaozhi.bin` is the application/OTA image. `generated_assets.bin` is the
assets-partition image and currently lacks an A/B rollback boundary.
`merged-binary.bin` is a physical factory/recovery image; writing it from offset
zero clears NVS and resets OTA selection, so it must never be presented as an
ordinary remote update. The build entrypoint records its explicit inputs, but
ESP-IDF component resolution and compile-time metadata do not yet guarantee
byte-for-byte reproducible output.

The tracked `config.json` keeps the upstream OTA default. For a self-hosted
Agent Session Gateway deployment, copy `config.local.json.example` to
`config.local.json`, replace `YOUR_SERVER_HOST` with a host reachable by the
device, and build with `-Config config.local.json`. Never commit a private LAN
address, public tunnel token, or production credential.

Wake-word and audio behavior require the external audio modules and must be
validated on the assembled device. Building successfully does not validate the
physical wiring.

The custom diagnostic codec uses the upstream simplex I2S implementation.
Bounded microphone RMS/peak diagnostics are available through the device MCP
tool; firmware does not automatically play a diagnostic tone.
