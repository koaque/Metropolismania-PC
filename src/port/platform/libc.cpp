// The platform layer, C library: functions of the game's libc (newlib) that ported code calls, written from what they do and
// proven against the recompiled functions by the difftest (plan P7). Prototypes are in config/platform_signatures.txt.

#include "game_functions.h"
#include "sdk.h"

namespace mm
{
    namespace
    {
        // The library's character class table: one flag byte per character, starting one byte before character 0 so that
        // EOF (-1) is valid. Bit 1 marks a lower case letter.
        constexpr uint32_t kCharClassTable = 0x001B5171u;
        constexpr uint32_t kImpurePtr = 0x001AEA88u; // the pointer to the library's per-thread data
        constexpr uint32_t kMemalignR = 0x00120DF8u; // a handler of the runtime library
        constexpr uint32_t kCleanup = 0x001200C8u;   // the standard streams' I/O functions and the cleanup function, set up by __sinit
        constexpr uint32_t kStdRead = 0x001234F0u;
        constexpr uint32_t kStdWrite = 0x00123558u;
        constexpr uint32_t kStdSeek = 0x001235D8u;
        constexpr uint32_t kStdClose = 0x00123640u;
    }

    // The absolute value. The most negative int stays as it is (the original negates in 32 bits).
    int sdk_abs(int value) { return value < 0 ? static_cast<int>(0u - static_cast<uint32_t>(value)) : value; }

    // The first place `character` (as a char) occurs in `string`, the terminator included; null when it does not.
    GuestPtr<char> sdk_index(GuestPtr<char> string, int character)
    {
        const char wanted = static_cast<char>(character);
        for (GuestPtr<char> at = string;; ++at)
        {
            if (*at == wanted)
                return at;
            if (*at == '\0')
                return GuestPtr<char>{};
        }
    }

    // Upper case through the class table.
    int sdk_toupper(int character)
    {
        const uint8_t flags = port::load<uint8_t>(kCharClassTable + static_cast<uint32_t>(character));
        return (flags & 2u) != 0 ? static_cast<int>(static_cast<uint32_t>(character) - 0x20u) : character;
    }

    // The length of the leading part of `string` that has no character of `reject`.
    uint32_t sdk_strcspn(GuestPtr<char> string, GuestPtr<char> reject)
    {
        GuestPtr<char> at = string;
        for (; *at != '\0'; ++at)
            for (GuestPtr<char> other = reject; *other != '\0'; ++other)
                if (*other == *at)
                    return at.address - string.address;
        return at.address - string.address;
    }

    // Aligned allocation: the runtime's handler of _memalign_r does it (the lock calls around it are empty handlers), given the
    // library's reentrancy pointer.
    GuestPtr<void> sdk_memalign(int alignment, int size)
    {
        port::GuestFrame<0x20> frame;
        const uint32_t reentrancy = port::load<uint32_t>(kImpurePtr);
        return GuestPtr<void>{port::callGuest<uint32_t>(kMemalignR, reentrancy, alignment, size)};
    }

    // Sets up one of the standard streams (a FILE in the library's per-thread data): its flags, descriptor, owner and the four I/O
    // functions; the buffer fields are zero. Other fields of the FILE are not touched.
    void sdk_std(GuestPtr<void> stream, int flags, int descriptor, GuestPtr<void> owner)
    {
        const uint32_t s = stream.address;
        port::store<uint32_t>(s + 0x54u, owner.address);
        port::store<uint16_t>(s + 0x0Cu, static_cast<uint16_t>(flags));
        port::store<uint16_t>(s + 0x0Eu, static_cast<uint16_t>(descriptor));
        port::store<uint32_t>(s + 0x20u, kStdRead);
        port::store<uint32_t>(s + 0x24u, kStdWrite);
        port::store<uint32_t>(s + 0x28u, kStdSeek);
        port::store<uint32_t>(s + 0x2Cu, kStdClose);
        for (const uint32_t offset : {0x00u, 0x04u, 0x08u, 0x10u, 0x14u, 0x18u})
            port::store<uint32_t>(s + offset, 0);
        port::store<uint32_t>(s + 0x1Cu, s);
    }

    // The first use of stdio: the cleanup function, the flag that this has been done, and stdin, stdout and stderr in the per-thread data.
    void sdk___sinit(GuestPtr<void> reentrancy)
    {
        port::GuestFrame<0x20> frame;
        const uint32_t r = reentrancy.address;
        port::store<uint32_t>(r + 0x3Cu, kCleanup);
        port::store<uint32_t>(r + 0x38u, 1);
        sdk_std(GuestPtr<void>{r + 0x1E4u}, 4, 0, reentrancy);
        sdk_std(GuestPtr<void>{r + 0x23Cu}, 9, 1, reentrancy);
        sdk_std(GuestPtr<void>{r + 0x294u}, 0xA, 2, reentrancy);
        port::store<uint32_t>(r + 0x1D8u, 0);
        port::store<uint32_t>(r + 0x1DCu, 3);
        port::store<uint32_t>(r + 0x1E0u, r + 0x1E4u);
    }

    // sprintf for the conversions the game's format strings use: %d %i %u %x %X %c %s and %%, with the flags - and 0 and a width.
    // Anything else stops with an error (a new format would need to be added here). The variadic arguments arrive in a2 to t3 and then on
    // the stack (eight at most; the game passes up to seven). Returns the length written, the terminating zero not counted.
    int sdk_sprintf(GuestPtr<char> destination, GuestPtr<char> format, uint64_t a2, uint64_t a3, uint64_t t0, uint64_t t1, uint64_t t2, uint64_t t3,
                    uint64_t stack0, uint64_t stack1)
    {
        port::GuestFrame<0xC0> frame;
        const uint64_t arguments[8] = {a2, a3, t0, t1, t2, t3, stack0, stack1};
        size_t nextArgument = 0;
        const uint32_t reentrancy = port::load<uint32_t>(kImpurePtr);
        if (port::load<int32_t>(reentrancy + 0x38u) == 0)
            sdk___sinit(GuestPtr<void>{reentrancy});

        uint32_t out = destination.address;
        const auto put = [&](char c) { port::store<uint8_t>(out++, static_cast<uint8_t>(c)); };
        const auto pad = [&](int count, char c)
        {
            for (; count > 0; --count)
                put(c);
        };
        const auto argument = [&]() -> uint64_t
        {
            if (nextArgument >= 8)
                port::fatal("sprintf: more than eight arguments", format.address);
            return arguments[nextArgument++];
        };

        for (uint32_t at = format.address;;)
        {
            const char c = static_cast<char>(port::load<uint8_t>(at++));
            if (c == '\0')
                break;
            if (c != '%')
            {
                put(c);
                continue;
            }
            bool left = false, zero = false;
            for (;; ++at)
            {
                const char flag = static_cast<char>(port::load<uint8_t>(at));
                if (flag == '-')
                    left = true;
                else if (flag == '0')
                    zero = true;
                else
                    break;
            }
            int width = 0;
            for (char d = static_cast<char>(port::load<uint8_t>(at)); d >= '0' && d <= '9'; d = static_cast<char>(port::load<uint8_t>(at)))
            {
                width = width * 10 + (d - '0');
                ++at;
            }
            const char conversion = static_cast<char>(port::load<uint8_t>(at++));
            char digits[16];
            int length = 0;
            bool negative = false;
            uint32_t string = 0;
            switch (conversion)
            {
            case '%':
                put('%');
                continue;
            case 'c':
                digits[length++] = static_cast<char>(argument());
                break;
            case 's':
            {
                string = static_cast<uint32_t>(argument());
                if (string == 0)
                {
                    for (const char n : {'(', 'n', 'u', 'l', 'l', ')'})
                        digits[length++] = n;
                    string = 0;
                }
                else
                    for (uint32_t p = string; port::load<uint8_t>(p) != 0; ++p)
                        ++length;
                break;
            }
            case 'd':
            case 'i':
            case 'u':
            case 'x':
            case 'X':
            {
                const uint32_t raw = static_cast<uint32_t>(argument());
                uint32_t magnitude = raw;
                if ((conversion == 'd' || conversion == 'i') && static_cast<int32_t>(raw) < 0)
                {
                    negative = true;
                    magnitude = 0u - raw;
                }
                const uint32_t base = (conversion == 'x' || conversion == 'X') ? 16u : 10u;
                const char *alphabet = conversion == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
                char reversed[16];
                int count = 0;
                do
                {
                    reversed[count++] = alphabet[magnitude % base];
                    magnitude /= base;
                } while (magnitude != 0);
                while (count > 0)
                    digits[length++] = reversed[--count];
                break;
            }
            default:
                port::fatal("sprintf: a conversion the game does not use", format.address);
            }
            // pad to the width: spaces first, or after when left aligned; zeros go between the sign and the digits
            const int total = length + (negative ? 1 : 0);
            const bool zeroPad = zero && !left && conversion != 'c' && conversion != 's';
            if (!left && !zeroPad)
                pad(width - total, ' ');
            if (negative)
                put('-');
            if (zeroPad)
                pad(width - total, '0');
            if (conversion == 's' && string != 0)
                for (uint32_t p = string; port::load<uint8_t>(p) != 0; ++p)
                    put(static_cast<char>(port::load<uint8_t>(p)));
            else
                for (int i = 0; i < length; ++i)
                    put(digits[i]);
            if (left)
                pad(width - total, ' ');
        }
        put('\0');
        return static_cast<int>(out - destination.address) - 1;
    }

    PORT_FN(sdk_memalign);
    PORT_FN(sdk_std);
    PORT_FN(sdk___sinit);
    PORT_FN(sdk_sprintf);
    PORT_FN(sdk_abs);
    PORT_FN(sdk_index);
    PORT_FN(sdk_toupper);
    PORT_FN(sdk_strcspn);
}
