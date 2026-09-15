[README.md](https://github.com/user-attachments/files/32242760/README.md)
# ESP32-S3 N16R8 MoonModules Super Controller

Performance-focused MoonModules WLED build for an ESP32-S3 N16R8 with an INMP441 I2S microphone and a combined Bluepad32 + BLE MIDI controller sender over UART.

## Compiled pin plan

- LED data: GPIO16
- GameController UART RX: GPIO18 at 115200 baud
- INMP441 SD: GPIO14
- INMP441 SCK/BCLK: GPIO12
- INMP441 WS/LRCLK: GPIO13
- INMP441 L/R: GND
- INMP441 VDD: 3.3V
- INMP441 GND: GND

## Performance features

- Audio Reactive with INMP441
- ANIMartRIX
- Full 2D support
- PixArt
- PixelForge
- GIF support inherited from MoonModules speed feature set
- WLED FASTPATH
- E1.31 / Art-Net style realtime network functionality remains available in WLED
- Wi-Fi, Web UI, WebSockets, OTA, presets/playlists, filesystem
- Smart-home integrations removed: Alexa, Hue Sync, MQTT, Loxone, IR, Adalight/serial protocols

## GameControllerUART

Protocol for the full-UART controller:

- `P:n` preset
- `PAL:n` palette on all active segments
- `C1:n`, `C2:n`, `C3:n`, `IX:n`, `SX:n` absolute values
- `C1D:n`, `C2D:n`, `C3D:n`, `IXD:n`, `SXD:n` relative values
- `STB:1/0` momentary strobe
- `MAP:-1/1` previous/next 1D→2D mapping
- `RND:1` random effect/palette/live parameters
- `RSTL:1`, `RSTR:1` reset left/right stick parameters to the active effect defaults

Preset transition policy:

- Presets 9-12: 0 ms hard cut
- Every other preset: 700 ms one-shot transition

## Build

Open GitHub Actions, choose **Build MoonModules WLED - S3 Super Controller**, and select **Run workflow**.


Both the Switch-style game controller and SMC-PAD Pocket BLE MIDI controller feed the same UART link. MIDI Note On requires velocity > 5.
