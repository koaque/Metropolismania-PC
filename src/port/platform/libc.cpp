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
        constexpr uint32_t kCleanup = addr::sdk_cleanup_r; // the standard streams' I/O functions and the cleanup function, set up by __sinit
        constexpr uint32_t kStdRead = addr::sdk_sread;
        constexpr uint32_t kStdWrite = addr::sdk_swrite;
        constexpr uint32_t kStdSeek = addr::sdk_sseek;
        constexpr uint32_t kStdClose = addr::sdk_sclose;

        constexpr uint32_t kErrno = 0x00718260u; // the library's global error number
        constexpr uint32_t kRead = 0x0010E3A8u;  // handlers of the runtime library (the system's read, write and fflush)
        constexpr uint32_t kWrite = 0x0010E328u;
        constexpr uint32_t kFlush = 0x0011FDE0u;
        constexpr uint16_t kPositionValid = 0x1000; // stream flags: the position field holds the descriptor's position
        constexpr uint16_t kAppend = 0x0100;        // and: every write goes to the end
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

    // The standard streams' I/O functions, which __sinit hands to the stream and the library calls through it (the library's stream
    // record is 0x58 bytes: flags at +0xC, the descriptor at +0xE, the position at +0x50, the owner's data block at +0x54). Each
    // goes through the reentrant system call layer below it. sprintf writes to a string and never reaches them; they run when
    // something reads, writes or closes stdin, stdout or stderr, or at the exit cleanup.
    int sdk_sread(GuestPtr<void> stream, GuestPtr<char> buffer, int count)
    {
        port::GuestFrame<0x10> frame;
        const uint32_t s = stream.address;
        const int result = sdk_read_r(GuestPtr<void>{port::load<uint32_t>(s + 0x54u)}, port::load<int16_t>(s + 0x0Eu), buffer, count);
        if (result >= 0)
            port::store<uint32_t>(s + 0x50u, port::load<uint32_t>(s + 0x50u) + static_cast<uint32_t>(result));
        else
            port::store<uint16_t>(s + 0x0Cu, static_cast<uint16_t>(port::load<uint16_t>(s + 0x0Cu) & ~kPositionValid));
        return result;
    }

    int sdk_swrite(GuestPtr<void> stream, GuestPtr<char> buffer, int count)
    {
        port::GuestFrame<0x20> frame;
        const uint32_t s = stream.address;
        const GuestPtr<void> owner{port::load<uint32_t>(s + 0x54u)};
        if ((port::load<uint16_t>(s + 0x0Cu) & kAppend) != 0)
            sdk_lseek_r(owner, port::load<int16_t>(s + 0x0Eu), 0, 2);
        port::store<uint16_t>(s + 0x0Cu, static_cast<uint16_t>(port::load<uint16_t>(s + 0x0Cu) & ~kPositionValid));
        return sdk_write_r(owner, port::load<int16_t>(s + 0x0Eu), buffer, count);
    }

    int sdk_sseek(GuestPtr<void> stream, int offset, int whence)
    {
        port::GuestFrame<0x10> frame;
        const uint32_t s = stream.address;
        const int result = sdk_lseek_r(GuestPtr<void>{port::load<uint32_t>(s + 0x54u)}, port::load<int16_t>(s + 0x0Eu), offset, whence);
        uint16_t flags = port::load<uint16_t>(s + 0x0Cu);
        if (result == -1)
            flags = static_cast<uint16_t>(flags & ~kPositionValid);
        else
        {
            port::store<uint32_t>(s + 0x50u, static_cast<uint32_t>(result));
            flags |= kPositionValid;
        }
        port::store<uint16_t>(s + 0x0Cu, flags);
        return result;
    }

    int sdk_sclose(GuestPtr<void> stream)
    {
        const uint32_t s = stream.address;
        return sdk_close_r(GuestPtr<void>{port::load<uint32_t>(s + 0x54u)}, port::load<int16_t>(s + 0x0Eu));
    }

    // The reentrant system calls: the error number is cleared, the plain call made, and when it fails with -1 the error number it
    // left is copied to the caller's data block (its first word) if there is one.
    int sdk_read_r(GuestPtr<void> reentrancy, int descriptor, GuestPtr<char> buffer, int count)
    {
        port::GuestFrame<0x20> frame;
        port::store<uint32_t>(kErrno, 0);
        const int result = port::callGuest<int>(kRead, descriptor, buffer.address, count);
        if (result == -1 && port::load<uint32_t>(kErrno) != 0)
            port::store<uint32_t>(reentrancy.address, port::load<uint32_t>(kErrno));
        return result;
    }

    int sdk_write_r(GuestPtr<void> reentrancy, int descriptor, GuestPtr<char> buffer, int count)
    {
        port::GuestFrame<0x20> frame;
        port::store<uint32_t>(kErrno, 0);
        const int result = port::callGuest<int>(kWrite, descriptor, buffer.address, count);
        if (result == -1 && port::load<uint32_t>(kErrno) != 0)
            port::store<uint32_t>(reentrancy.address, port::load<uint32_t>(kErrno));
        return result;
    }

    int sdk_lseek_r(GuestPtr<void> reentrancy, int descriptor, int offset, int whence)
    {
        port::GuestFrame<0x20> frame;
        port::store<uint32_t>(kErrno, 0);
        const int result = sdk_lseek(descriptor, offset, whence);
        if (result == -1 && port::load<uint32_t>(kErrno) != 0)
            port::store<uint32_t>(reentrancy.address, port::load<uint32_t>(kErrno));
        return result;
    }

    int sdk_close_r(GuestPtr<void> reentrancy, int descriptor)
    {
        port::GuestFrame<0x20> frame;
        port::store<uint32_t>(kErrno, 0);
        const int result = sdk_close(descriptor);
        if (result == -1 && port::load<uint32_t>(kErrno) != 0)
            port::store<uint32_t>(reentrancy.address, port::load<uint32_t>(kErrno));
        return result;
    }

    // The library has no file system of its own: close and lseek fail without touching the error number.
    int sdk_close(int) { return -1; }
    int sdk_lseek(int, int, int) { return -1; }

    // Calls `function` on every stream in use (flags not zero) of the data block's list of stream arrays (each: next, count, the
    // array of 0x58 byte records) and gives the or of the results.
    int sdk_fwalk(GuestPtr<void> reentrancy, uint32_t function)
    {
        port::GuestFrame<0x30> frame;
        int result = 0;
        for (uint32_t list = reentrancy.address + 0x1D8u; list != 0; list = port::load<uint32_t>(list))
        {
            uint32_t stream = port::load<uint32_t>(list + 8u);
            for (int32_t left = port::load<int32_t>(list + 4u) - 1; left >= 0; --left, stream += 0x58u)
                if (port::load<int16_t>(stream + 0x0Cu) != 0)
                    result |= port::callGuest<int>(function, stream);
        }
        return result;
    }

    // The exit cleanup: flushes every stream.
    int sdk_cleanup_r(GuestPtr<void> reentrancy) { return sdk_fwalk(reentrancy, kFlush); }

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

    PORT_FN(sdk_sread);
    PORT_FN(sdk_swrite);
    PORT_FN(sdk_sseek);
    PORT_FN(sdk_sclose);
    PORT_FN(sdk_read_r);
    PORT_FN(sdk_write_r);
    PORT_FN(sdk_lseek_r);
    PORT_FN(sdk_close_r);
    PORT_FN(sdk_close);
    PORT_FN(sdk_lseek);
    PORT_FN(sdk_fwalk);
    PORT_FN(sdk_cleanup_r);
    PORT_FN(sdk_memalign);
    PORT_FN(sdk_std);
    PORT_FN(sdk___sinit);
    PORT_FN(sdk_sprintf);
    PORT_FN(sdk_abs);
    PORT_FN(sdk_index);
    PORT_FN(sdk_toupper);
    PORT_FN(sdk_strcspn);
}
