// Helpers for the game's destructors: the member clean-up they repeat (delete the object a field points to,
// then zero the field) and the virtual delete through a vtable's third slot.

#pragma once

#include "game_functions.h"
#include "sdk.h"

namespace mm::lifetime
{
    // If the pointer at `field` is not 0: delete the text box and zero the field.
    inline void deleteTextBox(uint32_t field)
    {
        const uint32_t object = port::load<uint32_t>(field);
        if (object != 0u)
        {
            TextBox_dtor(GuestPtr<TextBox>{object}, 1);
            port::store<uint32_t>(field, 0u);
        }
    }

    // The same for a frame.
    inline void deleteFrame(uint32_t field)
    {
        const uint32_t object = port::load<uint32_t>(field);
        if (object != 0u)
        {
            Frame_DeletingDtor(GuestPtr<Frame>{object}, 1);
            port::store<uint32_t>(field, 0u);
        }
    }

    // If the pointer at `field` is not 0: run `destroy` on the object, then zero the field.
    template <typename Destroy>
    inline void deleteWith(uint32_t field, Destroy destroy)
    {
        const uint32_t object = port::load<uint32_t>(field);
        if (object != 0u)
        {
            destroy(object);
            port::store<uint32_t>(field, 0u);
        }
    }

    // The same for an object that is deleted through its virtual deleting destructor (vtable slot 2). The vtable
    // pointer sits at `vtableOffset` in the object (0 for the plain classes, 8 for the widgets).
    inline void deleteVirtual(uint32_t field, uint32_t vtableOffset)
    {
        const uint32_t object = port::load<uint32_t>(field);
        if (object != 0u)
        {
            const uint32_t vtable = port::load<uint32_t>(object + vtableOffset);
            port::callGuest<void>(port::load<uint32_t>(vtable + 8u), object, 1);
            port::store<uint32_t>(field, 0u);
        }
    }
}
