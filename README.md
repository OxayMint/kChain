# kChain

kChain is a password vault that lives on an ESP32-C3 Super Mini. This experimental build keeps the radio off. The firmware releases the Bluetooth controller at boot and never starts Wi-Fi. A roller encoder picks an entry, and a double tap types that password through the USB cable into whatever field is focused on the computer. A browser page edits the vault over the same USB port while the board is plugged in.

Passwords are stored in the clear on device flash. There is no encryption and no unlock PIN.

## Pins

The button is active-low. Wire the switch between GPIO 5 and GND. The firmware enables the internal pull-up. Change `PIN_BUTTON` in `firmware/include/config.h` and rebuild. The file rejects pins this board cannot use.

The mouse-wheel encoder navigates (the H-13 mark is the 13 mm body). Wire its common pin to GND, A to GPIO 6, and B to GPIO 7. The common pin is the middle one. Each detent steps one entry. Wheel up moves to the next entry and wheel down moves to the previous one, and both wrap inside the first five entries. Swap A and B if those directions feel backwards.

The blue onboard LED is GPIO 8, active-low. The firmware drives it after boot.

Leave these alone:

- GPIO 2 and 9 are strapping / boot pins. GPIO 9 is the BOOT button. GPIO 8 is also a strapping pin; it is the onboard LED and is not a button input.
- GPIO 20 and 21 stay free for the UART. USB serial uses the native USB port, not those pins.
- GPIO 11–19 are not on the header. They are the SPI flash and the native USB pins.
- 5V, G, and 3.3V are power only.

Usable button GPIOs on the header are 0, 1, 3, 4, 5, 6, 7, and 10.

## Firmware

The PlatformIO target is the ESP32-C3 (DevKitM-1 board definition, the closest match for a Super Mini) with native USB CDC turned on (`ARDUINO_USB_MODE=1` and `ARDUINO_USB_CDC_ON_BOOT=1`).

From the repo root:

```bash
cd firmware
pio run
pio run -t upload
pio device monitor
```

`pio run` compiles. `pio run -t upload` flashes. This repo was not flashed from here; plug the board in and upload it yourself.

If upload never starts, hold BOOT (GPIO 9), tap RST, release BOOT, and run the upload again. The serial monitor does not assert DTR/RTS, so opening it should not reset the chip.

An empty vault is valid. On first boot the firmware creates one. The board starts idle: the LED stays off, and a press does nothing until there is an entry. USB serial keeps running in idle. The radio does not.

A tap wakes the board onto password 1. The LED then repeats: flash that password’s number, wait one second. Flashes are grouped by three. Two is two short blinks. Four is three blinks, 200 ms, then one more. Wheel up and wheel down step through the first five passwords and wrap. A turn also wakes the board onto the entry it lands on. The button and the wheel only reach those entries. A double tap types the selected password through the USB typer, then the board returns to idle. If the USB typer is not running, the double tap does not type. It also returns to idle after 5 seconds with the button released and the wheel still. Typing sends the password characters only, then releases every key. It does not press Enter. The tap or double tap that wakes the board leaves the password untyped; a later double tap types it. A hold is reserved for the menu.

## Client

The editor is a Next.js app. It uses the Web Serial API, so open it in Chrome or Edge. There is no account and no database.

```bash
cd client
npm install
npm run dev
```

Then open [http://localhost:4317](http://localhost:4317). Plug in the board and the page connects. The first time, install the typer’s dependencies too: `cd host && npm install`. The page starts that program when you open it.

## USB typing

The ESP32-C3 USB port is a serial port. It cannot appear as a USB keyboard. Opening this page starts a typer on the computer, and a double tap types the selected password into the focused field.

Plug in the board and open the page. It connects when the board shows up. The first password typed on macOS needs Accessibility permission for `kchain-type` (System Settings → Privacy & Security → Accessibility).

The page and the typer share one serial port. While either has it, a double tap types on this computer.

## Serial protocol

USB serial is 115200 baud, one JSON object per line. The client sends `req` and the device echoes it.

```json
{"op":"list","req":1}
{"op":"add","req":2,"name":"GitHub","password":"correct horse"}
{"op":"edit","req":3,"id":1,"name":"GitHub","password":"new password"}
{"op":"delete","req":4,"id":1}
```

A successful reply includes the full vault. `selectedId` is JSON `null` when the vault is empty. `keyboardConnected` stays false, because this build has no Bluetooth keyboard. `usbTyping` is whether a USB heartbeat arrived in the last few seconds. A double tap types through USB only when `usbTyping` is true.

The device also emits events the client does not have to answer: `ready`, `selected`, `typed`, `type_failed`, and `usb`.

The editor, or the USB typer when it holds the port, sends `{"op":"usb_ready"}` about once a second. A double tap then sends `type_usb` with the password. The computer answers `{"op":"type_ack","ok":true}` or `{"op":"type_ack","ok":false,"error":"..."}`.
