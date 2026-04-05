#pragma once

#include <Arduino.h>

namespace cli_shell {

inline bool readLine(String& outLine) {
  static constexpr size_t kMaxLineLength = 127;
  static String buffer;
  static bool reserved = false;
  if (!reserved) {
    (void)buffer.reserve(kMaxLineLength);
    reserved = true;
  }
  static bool overflowed = false;
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\b' || c == 0x7F) {
      if (!overflowed && buffer.length() > 0) {
        buffer.remove(buffer.length() - 1);
      }
      continue;
    }
    if (c == '\r' || c == '\n') {
      if (overflowed) {
        buffer = "";
        overflowed = false;
        continue;
      }
      if (buffer.length() == 0) {
        continue;
      }
      outLine = buffer;
      buffer = "";
      outLine.trim();
      return outLine.length() > 0;
    }
    if (overflowed) {
      continue;
    }
    if (buffer.length() < kMaxLineLength) {
      buffer += c;
    } else {
      buffer = "";
      overflowed = true;
    }
  }
  return false;
}

}  // namespace cli_shell
