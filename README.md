# kChain

kChain is a vault that lives on an ESP32-S3. This experimental build keeps the radio off. The firmware releases the Bluetooth controller at boot and never starts Wi-Fi. The display shows a menu. The wheel moves the highlight, a tap opens a section, and a double tap types the highlighted entry through the USB cable into whatever field is focused on the computer. A browser page edits the vault over the same USB port while the board is plugged in, and shows which entry the wheel is on.

Entries are stored in the clear in a file on the upper 2 MB of device flash. There is no encryption and no unlock PIN. The first boot after older firmware copies a vault out of the small flash area, then erases it there. An entry is one of three types. Generic is a name and a password. Website is a hostname, a username or email, and a password. Crypto is a label and several words separated by spaces.

## Pins

The header is labeled 1–13, GND, 5V, 3V, RX, and TX. The number is the GPIO.

The button is active-low. Wire the switch between GPIO 7 and GND. The firmware enables the internal pull-up. Change `PIN_BUTTON` in `firmware/include/config.h` and rebuild. The file rejects pins this board cannot use.

The mouse-wheel encoder navigates (the H-13 mark is the 13 mm body). Wire its common pin to GND, A to GPIO 8, and B to GPIO 9. The common pin is the middle one. Each detent moves the highlight one row. Wheel up moves forward and wheel down moves back. Swap A and B if those directions feel backwards.

The panel is a 2.0 inch ST7789, 240×320, on SPI. Power it from 3.3 V. Module SCL (clock) is GPIO 1, SDA (MOSI) is GPIO 2, RST is GPIO 3, DC is GPIO 4, CS is GPIO 5, and BL (backlight) is GPIO 6. The glass shows the menu: kChain at the top left, and a gold outline on the selected row.

Leave these alone:

- GPIO 0, 45, and 46 are strapping pins. They are not on this header. GPIO 3 is also a strap; it is the display reset and is held high after boot.
- RX and TX stay free for the UART. USB serial uses the native USB port, not those pins.
- GPIO 19 and 20 are the native USB pins. GPIO 26–32 are the SPI flash. None of them are on the header.
- 5V, GND, and 3V are power only.

## Firmware

The PlatformIO target is the ESP32-S3 (DevKitC-1 board definition, the closest match for this header) with native USB CDC turned on (`ARDUINO_USB_CDC_ON_BOOT=1`). The image is 4 MB, which is the flash on this header. A larger module still accepts it. There is no PSRAM. The factory app stays in the lower 2 MB. The vault file uses the upper 2 MB.

From the repo root:

```bash
cd firmware
pio run
pio run -t upload
pio device monitor
```

`pio run` compiles. `pio run -t upload` flashes. This repo was not flashed from here; plug the board in and upload it yourself.

If upload never starts, hold BOOT (GPIO 0), tap RST, release BOOT, and run the upload again. The serial monitor does not assert DTR/RTS, so opening it should not reset the chip.

An empty vault is valid. On first boot the firmware creates one. The board starts idle. USB serial keeps running in idle. The radio does not. While the editor is connected it shows “Device is idle”.

The glass shows the menu as soon as the panel is up. kChain is at the top left of every screen. Home lists Single, Web, Crypto, and Help. The highlighted row has a gold outline. The wheel moves that highlight and wraps. Wheel up moves to the next row. Wheel down moves to the previous one. A tap opens the highlighted home row. The tap is reported only after the double-tap window, so a double tap does not also open it. Holding the button for a second returns to home.

Single lists generic names. Web lists each website with the account under the hostname. Crypto lists labels. Passwords and phrases stay off the glass. Help reads “Nothing here yet.” An empty section reads “None saved.”

A double tap on a highlighted stored entry types it as a USB keyboard, then the board tells the editor it is idle. The list stays on the glass until the idle timeout. Home, Help, and an empty list do not type. If the computer has not set up the keyboard, the double tap does not type. A generic entry types its password. A website entry types the username, a Tab key, then the password. The hostname is not typed. A crypto entry types its words, spaces included. After 5 seconds with the button released and the wheel still, the panel enters sleep and the board tells the editor it is idle. Sleep stops the scan and turns the backlight off. The press or wheel turn that leaves sleep only wakes the glass. The next one moves the highlight, opens a row, or types. Typing releases every key when it finishes. It does not press Enter. The glass does not jump to the login the extension has marked; that mark is only stored.

## Client

The editor is a Next.js app. It uses the Web Serial API, so open it in Chrome or Edge. There is no account and no database.

```bash
cd client
npm install
npm run dev
```

Then open [http://localhost:4317](http://localhost:4317). Plug in the board and the page connects. The first time, install the host program’s dependencies too: `cd host && npm install`. The page starts that program when you open it.

## USB typing

The native USB port is a serial port and a keyboard at the same time. A double tap types the selected entry into the focused field. A website entry is the username, a Tab, then the password. This works with the page closed. The computer does not need an Accessibility permission.

Keys follow a US keyboard layout, so a symbol lands where that layout puts it. The first time macOS sees the keyboard it may ask which key is next to Shift. Dismiss that, then a double tap types.

The host program opens the serial port for this page and for the extension. It does not type. The page can open the port itself instead. Either way, a double tap types through the keyboard.

## Chrome extension

The extension fills website logins from the device and saves new ones when a login form is submitted. It talks to the host program on this computer. It does not keep a copy of the vault. Autofill works while that program has the board. A double tap types through the USB keyboard whenever the board is plugged in.

```bash
cd extension
npm install
npm run build
```

In Chrome, open `chrome://extensions`, turn on Developer mode, and choose Load unpacked. Select `extension/dist`.

On macOS, install the native host once so the extension can start the host program:

```bash
extension/native/install-mac.sh
```

Run that from a terminal where `node` is the Node you use for this repo. If the native host is not installed, start the host program yourself with `cd host && npm start`, or open the web editor, which starts it.

A key icon appears on a username, email, or password field. Click it to fill a matching website entry, or to generate a password. When that page is the active tab and the site has a saved login, the extension stores that login on the device. The menu does not open it; the wheel chooses the row. If several logins match and a username or email is filled in, the stored login is that one. Otherwise it is the first saved login for the site. Leaving the page clears the mark. Submitting a login offers to save a new website entry, or to update one when the password changed. The toolbar popup lists logins for the current site. Open vault shows the same add, edit, and delete actions as the web editor, including generic and crypto entries.

Chrome keeps saved passwords private from extensions. Import from Chrome, in the popup or the vault panel, opens Chrome's password settings so you can download the CSV, then imports the website logins that fit. The device holds 2048 entries. A login already on the device is updated when its password differs. App logins, notes, and rows the device cannot type are skipped. Delete the CSV after importing. Chrome writes it as plain text.

The extension id is fixed by the key in `extension/manifest.json`, and the host program only accepts that extension origin.

## Serial protocol

USB serial is 115200 baud, one JSON object per line. The client sends `req` and the device echoes it.

```json
{"op":"list","req":1}
{"op":"add","req":2,"type":"generic","name":"GitHub","password":"correct horse"}
{"op":"add","req":3,"type":"website","name":"github.com","username":"ada@example.com","password":"correct horse"}
{"op":"add","req":4,"type":"crypto","name":"Ledger","phrase":"correct horse battery staple"}
{"op":"edit","req":5,"id":1,"type":"generic","name":"GitHub","password":"new password"}
{"op":"delete","req":6,"id":1}
{"op":"focus","req":7,"id":3}
{"op":"focus","req":8,"id":null}
```

`focus` stores the login the extension picked for the current page. It does not move the selection, and the menu does not open that login. `id` null clears the mark. The extension sends it for the active tab when that page has a username, email, or password field and the vault has a login for the site.

A missing `type` on add or edit is generic, so an older name-and-password command still works. Edit cannot change an entry’s type. A record already on flash with no `type` loads as generic.

A successful reply includes the full vault. `selectedId` is JSON `null` when the vault is empty. `active` is whether the board is awake on an entry. `keyboardConnected` stays false, because this build has no Bluetooth keyboard. `usbTyping` is whether the computer has configured the USB keyboard. A double tap types only when `usbTyping` is true.

The device also emits events the client does not have to answer: `ready`, `selected`, `idle`, `typed`, `type_failed`, and `usb`. `selected` means the board is awake on that entry. `idle` means it has gone back to sleep. `usb` reports the same keyboard state as `usbTyping`.
