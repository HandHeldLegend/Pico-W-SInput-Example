# Pico W SINPUT gamepad example

Reference firmware for **SINPUT-LIB-HID** (see `external/SINPUT-LIB-HID`) on **Raspberry Pi Pico W**. It shows how to:

- Initialize device capabilities with `sinput_api_init()`
- Fill HID input reports by **overriding the weak hooks** in `main.c`
- Forward host output reports with `sinput_api_output_tunnel()`
- Run the same stack over **USB (TinyUSB)** or **Bluetooth Classic HID**, selected at boot

The library (vendored under `external/SINPUT-LIB-HID`) owns report layouts and protocol framing. **Your board logic lives in the hooks** — GPIO, ADC, IMU, motors, and LEDs.

## What this demo implements

| Hook | Behavior in this project |
|------|---------------------------|
| `sinput_api_hook_get_input` | Buttons on **GP14** (south / “A”) and **GP15** (east / “B”), centered sticks and triggers (one `sinput_input_s` snapshot) |
| `sinput_api_hook_get_power` | Static “full battery” baseline |
| `sinput_api_hook_get_motion` | Synthetic gyro sweep (no IMU wired) |
| `sinput_api_hook_get_touchpads` | Not used (`false`; touchpads disabled in cfg) |
| **Setters** (`sinput_api_hook_set_*`) | Stores last rumble, haptics, player LED index, and RGB in **static `volatile`** shadows so you can inspect them under a debugger or extend to real drivers |

Production firmware would replace the demo motion source with an IMU, add ADC for sticks/triggers, and drive actuators from the setter hooks instead of only logging state.

## Requirements

- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) (this repo’s CMake hints expect **2.2.0** when using the VS Code Pico extension)
- **ARM GNU Toolchain** (e.g. 14.2 Rel1 as referenced in `CMakeLists.txt`)
- Pico W (CYW43439 is used for Bluetooth only; Wi-Fi/LWIP is disabled)

## Build

From the project root (adjust `PICO_SDK_PATH` to your install):

```bash
mkdir -p build && cd build
cmake -DPICO_SDK_PATH=/path/to/pico-sdk ..
cmake --build .
```

Or open the folder in **VS Code** with the **Raspberry Pi Pico** extension; it will pick up `CMakeLists.txt` and `pico_sdk_import.cmake`.

Artifacts: `build/Pico-W-SINPUT-Example.uf2` for drag-and-drop flashing.

## Flashing and UART

Default **UART stdio** is on **GP12 (TX)** and **GP13 (RX)** so **GP0/GP1** stay free for boot straps (`CMakeLists.txt` sets `PICO_DEFAULT_UART_TX_PIN` / `RX_PIN`).

## Boot modes (strap pins)

At reset, **GP0** and **GP1** are sampled with internal pull-ups (**active low** when tied to GND):

| GP1 (pair) | GP0 (BT) | Mode |
|------------|---------|------|
| low | (don’t care) | **Bluetooth — pairing** (discoverable) |
| high | low | **Bluetooth — reconnect** with stored host / link key |
| high | high | **USB** HID (TinyUSB) |

Demo **face buttons** for input reports use **GP14** and **GP15** (also pull-up, active low).

## Source layout

| File | Role |
|------|------|
| `main.c` | `main()`, flash-backed pairing storage init, **`sinput_api_hook_*` implementations** |
| `sinput_usb.c` | TinyUSB descriptors, IN report pacing, `sinput_api_output_tunnel()` from SET_REPORT / OUT |
| `sinput_btc.c` | BTstack Classic HID path and pairing persistence callbacks |
| `sinput_flash.c` | Simple flash page helper for host/link data |

## Further reading

- Library overview: `external/SINPUT-LIB-HID/README.md`
- Integration details (report cadence, output commands, feature discovery): `external/SINPUT-LIB-HID/IMPLEMENTATION.md`

## License

This example is licensed under **MIT-0** — see [`LICENSE`](LICENSE). The vendored **SINPUT-LIB-HID** tree under `external/SINPUT-LIB-HID` is also MIT-0 (see that directory’s `LICENSE`). Third-party headers you may keep alongside the project (for example TinyUSB config templates) retain their original notices.
