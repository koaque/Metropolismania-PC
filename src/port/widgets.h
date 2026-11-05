// Calls through the widget class table (Widget.vtable, docs/map/ui.md) for code that holds a widget by its guest address.

#pragma once

#include "game_types.h"

namespace mm
{
    // The method in `slot` of the class of the widget at `widget`, e.g. widgetMethod(x, &WidgetVtable::update).
    inline uint32_t widgetMethod(uint32_t widget, uint32_t WidgetVtable::*slot) { return (*GuestPtr<Widget>{widget}->vtable).*slot; }
}
