import ApplicationServices
import CoreGraphics
import Darwin
import Foundation

if CommandLine.arguments.contains("--check") {
  if AXIsProcessTrusted() {
    fputs("trusted\n", stdout)
    exit(0)
  }
  fputs("not-trusted\n", stdout)
  exit(2)
}

let promptKey = kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String
let prompt = [promptKey: true] as CFDictionary
if !AXIsProcessTrustedWithOptions(prompt) {
  fputs("Allow kchain-type in Privacy & Security, Accessibility, then hold the button again.\n", stderr)
  exit(2)
}

let data = FileHandle.standardInput.readDataToEndOfFile()
guard let text = String(data: data, encoding: .utf8), !text.isEmpty else {
  fputs("Nothing to type.\n", stderr)
  exit(1)
}

let source = CGEventSource(stateID: .hidSystemState)
for character in text {
  let units = Array(String(character).utf16)
  guard let down = CGEvent(keyboardEventSource: source, virtualKey: 0, keyDown: true),
        let up = CGEvent(keyboardEventSource: source, virtualKey: 0, keyDown: false) else {
    fputs("Could not create a key event.\n", stderr)
    exit(1)
  }
  units.withUnsafeBufferPointer { buffer in
    if let base = buffer.baseAddress {
      down.keyboardSetUnicodeString(stringLength: units.count, unicodeString: base)
    }
  }
  down.post(tap: .cghidEventTap)
  up.post(tap: .cghidEventTap)
  usleep(10_000)
}
