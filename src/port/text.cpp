// Number formatting: decimal digits by repeated subtraction of the powers of ten in the table at
// 0x1B9AB0 (100000000 down to 10), with leading zeros shown as blanks.

#include "game_functions.h"
#include "game_globals.h"
#include "sdk.h"

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

    // The value in decimal with a comma every three digits, built from the right by prepending each
    // character (memmove of the string so far); a minus sign for negative values. Returns the length.
    int Text_FormatThousands(GuestPtr<char> out, int value)
    {
        const bool negative = value < 0;
        out[0] = '\0';
        if (negative)
            value = sdk::abs(value);
        int length = 0;
        const auto prepend = [&](char c)
        {
            ++length;
            sdk::memmove(out.address + 1u, out.address, length);
            out[0] = c;
        };
        int digits = 0;
        do
        {
            const int digit = value % 10;
            value /= 10;
            if (digits != 0 && digits % 3 == 0)
                prepend(',');
            prepend(static_cast<char>('0' + digit));
            ++digits;
        } while (value != 0);
        if (negative)
            prepend('-');
        return length;
    }

    // "$" and the value with thousands separators; right-aligned in `width` characters (padded with spaces)
    // unless leftAlign.
    void Text_FormatMoney(GuestPtr<char> out, int value, int width, int leftAlign)
    {
        const int length = Text_FormatThousands(out, value) + 1; // with the terminator
        sdk::memmove(out.address + 1u, out.address, length);
        out[0] = '$';
        if (leftAlign != 0)
            return;
        const int pad = width - length;
        if (pad > 0)
        {
            sdk::memmove(out.address + static_cast<uint32_t>(pad), out.address, length + 1);
            sdk::memset(out.address, ' ', pad);
        }
    }

    PORT_FN(Text_SplitDigits);
    PORT_FN(Text_FormatMoney);
    PORT_FN(Text_FormatThousands);
    PORT_FN(Text_FormatPadded);
}
