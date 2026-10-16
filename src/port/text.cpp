// Number formatting: decimal digits by repeated subtraction of the powers of ten in the table at
// 0x1B9AB0 (100000000 down to 10), with leading zeros shown as blanks.

#include "game_functions.h"
#include "game_globals.h"

namespace mm
{
    namespace
    {
        constexpr uint32_t kPowersOfTen = 0x001B9AB0u; // nine int32_t

        // Calls emit(digit, leadingBlank) for the last `count` of the nine high digits (the ones digit is
        // left in `value`). A value below zero gives all zeros and a wrapped last digit, like the original.
        template <typename Emit>
        int splitDigits(int value, int count, Emit emit)
        {
            bool leading = true;
            for (int i = 0; i < 9; ++i)
            {
                const int32_t power = port::load<int32_t>(kPowersOfTen + 4u * static_cast<uint32_t>(i));
                int digit = 0;
                for (; power <= value; value -= power)
                    ++digit;
                if (digit != 0)
                    leading = false;
                if (i > 9 - count)
                    emit(digit, digit == 0 && leading);
            }
            return value;
        }
    }

    // Digit values for digit sprites: 0-9, 10 for a leading blank, then the ones digit and 0xFF.
    void Text_SplitDigits(GuestPtr<uint8_t> out, int value, int count)
    {
        value = splitDigits(value, count, [&](int digit, bool blank) { *out = static_cast<uint8_t>(blank ? 10 : digit); ++out; });
        out[0] = static_cast<uint8_t>(value);
        out[1] = 0xFF;
    }

    // Right-aligned decimal text with leading spaces.
    void Text_FormatPadded(GuestPtr<char> out, int value, int width)
    {
        value = splitDigits(value, width, [&](int digit, bool blank) { *out = blank ? ' ' : static_cast<char>(digit + '0'); ++out; });
        out[0] = static_cast<char>(value + '0');
        out[1] = '\0';
    }

    PORT_FN(Text_SplitDigits);
    PORT_FN(Text_FormatPadded);
}
