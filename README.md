# kChain

kChain is a vault that lives on an ESP32-C3 Super Mini. This experimental build keeps the radio off. The firmware releases the Bluetooth controller at boot and never starts Wi-Fi. A roller encoder picks an entry, and a double tap types it through the USB cable into whatever field is focused on the computer. A browser page edits the vault over the same USB port while the board is plugged in, and shows which entry the wheel is on.

Entries are stored in the clear on device flash. There is no encryption and no unlock PIN. An entry is one of three types. Generic is a name and a password. Website is a hostname, a username or email, and a password. Crypto is a label and several words separated by spaces.

## Pins

The button is active-low. Wire the switch between GPIO 5 and GND. The firmware enables the internal pull-up. Change `PIN_BUTTON` in `firmware/include/config.h` and rebuild. The file rejects pins this board cannot use.

The mouse-wheel encoder navigates (the H-13 mark is the 13 mm body). Wire its common pin to GND, A to GPIO 6, and B to GPIO 7. The common pin is the middle one. Each detent steps one entry. Wheel up moves to the next entry and wheel down moves to the previous one, and both wrap inside the first five entries. Swap A and B if those directions feel backwards.

The blue onboard LED is GPIO 8, active-low. The firmware leaves it undriven. The editor shows whether the board is idle or which entry is selected. A later screen module is the device UI.

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

An empty vault is valid. On first boot the firmware creates one. The board starts idle, and a press does nothing until there is an entry. USB serial keeps running in idle. The radio does not. While the editor is connected it shows “Device is idle”.

A tap wakes the board onto entry 1. Wheel up and wheel down step through the first five entries and wrap. A turn also wakes the board onto the entry it lands on. The button and the wheel only reach those entries. The editor then shows that entry, for example “On the device: 2. github.com”. A double tap types the selected entry through the USB typer, then the board returns to idle. If the USB typer is not running, the double tap does not type. A generic entry types its password. A website entry types the username, a Tab key, then the password. The hostname is not typed. A crypto entry types its words, spaces included. It also returns to idle after 5 seconds with the button released and the wheel still. Typing releases every key when it finishes. It does not press Enter. The tap or double tap that wakes the board leaves the entry untyped; a later double tap types it. A hold is reserved for the menu.

## Client

The editor is a Next.js app. It uses the Web Serial API, so open it in Chrome or Edge. There is no account and no database.

```bash
cd client
npm install
npm run dev
```

Then open [http://localhost:4317](http://localhost:4317). Plug in the board and the page connects. The first time, install the typer’s dependencies too: `cd host && npm install`. The page starts that program when you open it.

## USB typing

The ESP32-C3 USB port is a serial port. It cannot appear as a USB keyboard. Opening this page starts a typer on the computer, and a double tap types the selected entry into the focused field. A website entry is the username, a Tab, then the password.

Plug in the board and open the page. It connects when the board shows up. The first password typed on macOS needs Accessibility permission for `kchain-type` (System Settings → Privacy & Security → Accessibility).

The page and the typer share one serial port. While either has it, a double tap types on this computer.

## Serial protocol

USB serial is 115200 baud, one JSON object per line. The client sends `req` and the device echoes it.

```json
{"op":"list","req":1}
{"op":"add","req":2,"type":"generic","name":"GitHub","password":"correct horse"}
{"op":"add","req":3,"type":"website","name":"github.com","username":"ada@example.com","password":"correct horse"}
{"op":"add","req":4,"type":"crypto","name":"Ledger","phrase":"correct horse battery staple"}
{"op":"edit","req":5,"id":1,"type":"generic","name":"GitHub","password":"new password"}
{"op":"delete","req":6,"id":1}
```

A missing `type` on add or edit is generic, so an older name-and-password command still works. Edit cannot change an entry’s type. A record already on flash with no `type` loads as generic.

A successful reply includes the full vault. `selectedId` is JSON `null` when the vault is empty. `active` is whether the board is awake on an entry. `keyboardConnected` stays false, because this build has no Bluetooth keyboard. `usbTyping` is whether a USB heartbeat arrived in the last few seconds. A double tap types through USB only when `usbTyping` is true.

The device also emits events the client does not have to answer: `ready`, `selected`, `idle`, `typed`, `type_failed`, and `usb`. `selected` means the board is awake on that entry. `idle` means it has gone back to sleep.

The editor, or the USB typer when it holds the port, sends `{"op":"usb_ready"}` about once a second. A double tap then sends `type_usb` with a `steps` array. A step of `"\t"` is the Tab key, and every other step is text. The computer answers `{"op":"type_ack","ok":true}` or `{"op":"type_ack","ok":false,"error":"..."}`. A body that only has `password` still types that string.
