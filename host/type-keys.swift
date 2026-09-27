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
let steps: [String]
if let json = try? JSONSerialization.jsonObject(with: data) as? [String], !json.isEmpty {
  steps = json
} else if let text = String(data: data, encoding: .utf8), !text.isEmpty {
  steps = [text]
} else {
  fputs("Nothing to type.\n", stderr)
  exit(1)
}

let source = CGEventSource(stateID: .hidSystemState)
let tabKey: CGKeyCode = 0x30

func fail(_ message: String) -> Never {
  fputs(message, stderr)
  exit(1)
}

for step in steps {
  if step == "\t" {
    guard let down = CGEvent(keyboardEventSource: source, virtualKey: tabKey, keyDown: true),
          let up = CGEvent(keyboardEventSource: source, virtualKey: tabKey, keyDown: false) else {
      fail("Could not create a key event.\n")
    }
    down.post(tap: .cghidEventTap)
    up.post(tap: .cghidEventTap)
    usleep(10_000)
    continue
  }
  for character in step {
    let units = Array(String(character).utf16)
    guard let down = CGEvent(keyboardEventSource: source, virtualKey: 0, keyDown: true),
          let up = CGEvent(keyboardEventSource: source, virtualKey: 0, keyDown: false) else {
      fail("Could not create a key event.\n")
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
}
