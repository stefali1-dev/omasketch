#pragma once

#include <QColor>

// The app's fixed colours (decisions.md, "Look"). QML reads them through the
// "Colors" context property set in main.cpp.
namespace palette {

inline const QColor page(0xff, 0xff, 0xff);
inline const QColor ink(0x00, 0x00, 0x00);
inline const QColor red(0xe0, 0x31, 0x31);
inline const QColor blue(0x19, 0x71, 0xc2);
inline const QColor ui(0x64, 0x66, 0x69);
inline const QColor accent(0xb3, 0x91, 0x10); // monkeytype #e2b714, darkened for white

} // namespace palette
