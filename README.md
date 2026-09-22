# kChain

kChain is a password vault that lives on an ESP32-C3 Super Mini. It pairs over Bluetooth as a keyboard named **kChain**. Three buttons pick an entry and type that password into whatever field is focused on the computer or phone. A browser page edits the vault over USB while the board is plugged in.

Passwords are stored in the clear on device flash. There is no encryption and no unlock PIN.

## Pins

Buttons are active-low. Wire each switch between its GPIO and GND. The firmware enables the internal pull-up.

| Button | GPIO |
| --- | --- |
| Previous entry | 4 |
| Type the selected password | 5 |
| Next entry | 6 |

Change those three values in `firmware/include/config.h` and rebuild. The file rejects pins this board cannot use.

Leave these alone:

- GPIO 2, 8, and 9 are strapping / boot pins. GPIO 9 is the BOOT button.
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

An empty vault is valid. On first boot the firmware creates one. Buttons do nothing until there is an entry. Typing sends the password characters only, then releases every key. It does not press Enter.

The three buttons sit behind a small input interface. A later wheel-with-button can emit the same previous / type / next events without changing selection or typing.

## Client

The editor is a Next.js app. It uses the Web Serial API, so open it in Chrome or Edge. There is no account and no database.

```bash
cd client
npm install
npm run dev
```

Then open [http://localhost:4317](http://localhost:4317). Connect the board, choose the Espressif serial port (vendor ID `303A`), and list, add, edit, or delete entries. If the picker is empty, use “Show every serial port”.

## Serial protocol

USB serial is 115200 baud, one JSON object per line. The client sends `req` and the device echoes it.

```json
{"op":"list","req":1}
{"op":"add","req":2,"name":"GitHub","password":"correct horse"}
{"op":"edit","req":3,"id":1,"name":"GitHub","password":"new password"}
{"op":"delete","req":4,"id":1}
```

A successful reply includes the full vault. `selectedId` is JSON `null` when the vault is empty. `keyboardConnected` is whether a Bluetooth host is paired.

The device also emits events the client does not have to answer: `ready`, `selected`, `typed`, `type_failed`, and `keyboard`.
