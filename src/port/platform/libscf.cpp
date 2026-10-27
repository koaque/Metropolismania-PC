// The platform layer, the console configuration library's local time (plan P7), written from what the functions do and proven
// against the recompiled functions by the difftest. Prototypes are in config/platform_signatures.txt.
//
// The RTC gives the time in Japan Standard Time as an 8 byte record of BCD bytes: +1 second, +2 minute, +3 hour, +5 day, +6 month,
// +7 year (two digits); the local time is that time moved by the console's time zone (minutes east of Greenwich, from the OSD
// configuration word) and, in summer time, one hour more. The helpers work on the record in binary, moving a minute count in whole
// hours and a day across the months of the table at 0x1BAE48.

#include "game_functions.h"
#include "sdk.h"

#include <initializer_list>

namespace mm
{
    namespace
    {
        constexpr uint32_t kRomName = 0x001B38C8u;       // the first 14 bytes of rom0:ROMVER, read once (0 until then)
        constexpr uint32_t kRomRegionLetter = 0x001B38CCu; // 'T' in the ROM version of a development tool
        constexpr uint32_t kToolTimeZone = 0x001B38C0u;  // on a development tool the library keeps these two itself
        constexpr uint32_t kToolSummerTime = 0x001B38C6u;
        constexpr uint32_t kDaysInMonth = 0x001BAE48u;   // 31 28 31 30 31 30 31 31 30 31 30 31
        constexpr uint32_t kRomVerPath = 0x001BAE08u;    // "rom0:ROMVER"
        constexpr uint32_t kOpenFailed = 0x001BAE18u;    // messages printed when the file cannot be opened or read
        constexpr uint32_t kReadFailed = 0x001BAE30u;
        constexpr uint32_t kGetOsdConfigParam = 0x0010DE10u;  // runtime handlers
        constexpr uint32_t kSyscallGetOsdConfigParam2 = 0x6Fu; // the kernel call behind the library's wrapper at 0x0010E070
        constexpr int kJapanMinutes = 540;               // the RTC runs on JST, UTC+9

        uint8_t &field(GuestPtr<uint8_t> clock, uint32_t offset) { return *(clock + static_cast<int32_t>(offset)); }

        // The number of days in the clock's month, with February at 29 in a year divisible by 4.
        uint32_t daysInMonth(uint32_t month, uint32_t year)
        {
            if (month == 2 && (year & 3u) == 0)
                return 29;
            return port::load<uint8_t>(kDaysInMonth + month - 1u);
        }
    }

    // Binary to BCD and back, for one byte.
    int sdk_tobcd(int value)
    {
        const uint32_t v = static_cast<uint32_t>(value) & 0xFFu;
        return static_cast<int>(((v / 10u) * 6u + v) & 0xFFu);
    }

    int sdk_frombcd(int value)
    {
        const uint32_t v = static_cast<uint32_t>(value) & 0xFFu;
        return static_cast<int>((v - (v >> 4) * 6u) & 0xFFu);
    }

    // The six time fields of the record (year, month, day, hour, minute, second) between BCD and binary.
    void sdk_scf_BcdToTime(GuestPtr<uint8_t> clock)
    {
        for (const uint32_t offset : {7u, 6u, 5u, 3u, 2u, 1u})
            field(clock, offset) = static_cast<uint8_t>(sdk_frombcd(field(clock, offset)));
    }

    void sdk_scf_TimeToBcd(GuestPtr<uint8_t> clock)
    {
        for (const uint32_t offset : {7u, 6u, 5u, 3u, 2u, 1u})
            field(clock, offset) = static_cast<uint8_t>(sdk_tobcd(field(clock, offset)));
    }

    // The next day: past the end of the month it is the 1st of the next, past December the 1st of January of the next year (99
    // wraps to 0).
    void sdk_adddate(GuestPtr<uint8_t> clock)
    {
        const uint32_t year = field(clock, 7);
        field(clock, 5) = static_cast<uint8_t>(field(clock, 5) + 1u);
        if (static_cast<int8_t>(daysInMonth(field(clock, 6), year)) < static_cast<int32_t>(field(clock, 5)))
        {
            field(clock, 5) = 1;
            field(clock, 6) = static_cast<uint8_t>(field(clock, 6) + 1u);
            if (field(clock, 6) == 13)
            {
                field(clock, 7) = year == 99 ? 0 : static_cast<uint8_t>(year + 1u);
                field(clock, 6) = 1;
            }
        }
    }

    // The day before: from the 1st it is the last of the previous month (December of the previous year from January, 0 wraps to 99);
    // February's length comes from the year before the change.
    void sdk_subdate(GuestPtr<uint8_t> clock)
    {
        const uint32_t year = field(clock, 7);
        field(clock, 5) = static_cast<uint8_t>(field(clock, 5) - 1u);
        if (field(clock, 5) != 0)
            return;
        field(clock, 6) = static_cast<uint8_t>(field(clock, 6) - 1u);
        if (field(clock, 6) == 0)
        {
            field(clock, 7) = year == 0 ? 99 : static_cast<uint8_t>(year - 1u);
            field(clock, 6) = 12;
        }
        field(clock, 5) = static_cast<uint8_t>(daysInMonth(field(clock, 6), year));
    }

    // One hour on and back, carrying into the date.
    void sdk_addhour(GuestPtr<uint8_t> clock)
    {
        field(clock, 3) = static_cast<uint8_t>(field(clock, 3) + 1u);
        if (field(clock, 3) == 24)
        {
            field(clock, 3) = 0;
            sdk_adddate(clock);
        }
    }

    void sdk_subhour(GuestPtr<uint8_t> clock)
    {
        if (field(clock, 3) != 0)
        {
            field(clock, 3) = static_cast<uint8_t>(field(clock, 3) - 1u);
            return;
        }
        field(clock, 3) = 23;
        sdk_subdate(clock);
    }

    // Moves the (BCD) time by a number of minutes: in binary, whole hours are carried until the minute count is in range (a count of
    // 60 itself stays), and the record is turned back to BCD.
    void sdk_AdjustTime(GuestPtr<uint8_t> clock, int minutes)
    {
        port::GuestFrame<0x30> frame;
        sdk_scf_BcdToTime(clock);
        int total = static_cast<int>(field(clock, 2)) + minutes;
        if (total < 0)
        {
            while (total < 0)
            {
                total += 60;
                sdk_subhour(clock);
            }
            field(clock, 2) = static_cast<uint8_t>(total);
        }
        else if (total < 61)
            field(clock, 2) = static_cast<uint8_t>(total);
        else
        {
            do
            {
                total -= 60;
                sdk_addhour(clock);
            } while (total >= 61);
            field(clock, 2) = static_cast<uint8_t>(total);
        }
        sdk_scf_TimeToBcd(clock);
    }

    // The ROM version string, read once from rom0:ROMVER into a 14 byte buffer (a failure prints a message). Returns the buffer.
    GuestPtr<char> sdk_GetRomName()
    {
        port::GuestFrame<0x50> frame;
        if (port::load<int8_t>(kRomName) == 0)
        {
            const int file = sdk::sceOpen(kRomVerPath, 1);
            if (file == -1)
                sdk::printf(kOpenFailed);
            if (sdk::sceRead(file, kRomName, 14) == -1)
                sdk::printf(kReadFailed);
            sdk::sceClose(file);
        }
        return GuestPtr<char>{kRomName};
    }

    // Whether this is a development tool: the ROM version's fifth character is 'T'.
    int sdk_IsT10K()
    {
        port::GuestFrame<0x20> frame;
        if (port::load<int8_t>(kRomName) == 0)
            sdk_GetRomName();
        return port::load<int8_t>(kRomRegionLetter) == 0x54 ? 1 : 0;
    }

    // The time zone in minutes east of Greenwich: from the OSD configuration word (bits 21 and up, signed), 540 (Japan) when the
    // configuration holds no language setting (bits 13 to 15 clear); a development tool keeps its own value.
    int sdk_sceScfGetTimeZone()
    {
        port::GuestFrame<0x20> frame;
        if (sdk_IsT10K())
            return port::load<int16_t>(kToolTimeZone);
        const uint32_t sp = frame.ptr().address;
        port::callGuest<int>(kGetOsdConfigParam, sp);
        const int32_t config = port::load<int32_t>(sp);
        return ((static_cast<uint32_t>(config) >> 13) & 7u) == 0 ? kJapanMinutes : (config >> 21);
    }

    // Whether summer time is set: bit 4 of the first byte of the second configuration word, 0 when the configuration holds no language
    // setting; a development tool keeps its own value.
    int sdk_sceScfGetSummerTime()
    {
        port::GuestFrame<0x20> frame;
        if (sdk_IsT10K())
            return port::load<uint8_t>(kToolSummerTime);
        const uint32_t sp = frame.ptr().address;
        port::callGuest<int>(kGetOsdConfigParam, sp);
        if (((port::load<uint32_t>(sp) >> 13) & 7u) == 0)
            return 0;
        port::syscall(kSyscallGetOsdConfigParam2, sp + 4u, 1, 1);
        return (port::load<uint8_t>(sp + 4u) >> 4) & 1;
    }

    // The clock the RTC gave, in local time: the time zone and the summer hour as a minute count from Japan time.
    void sdk_sceScfGetLocalTimefromRTC(GuestPtr<uint8_t> clock)
    {
        port::GuestFrame<0x30> frame;
        const int zone = sdk_sceScfGetTimeZone();
        const int summer = sdk_sceScfGetSummerTime();
        sdk_AdjustTime(clock, zone + (summer * 60 - kJapanMinutes));
    }

    PORT_FN(sdk_tobcd);
    PORT_FN(sdk_frombcd);
    PORT_FN(sdk_scf_BcdToTime);
    PORT_FN(sdk_scf_TimeToBcd);
    PORT_FN(sdk_adddate);
    PORT_FN(sdk_subdate);
    PORT_FN(sdk_addhour);
    PORT_FN(sdk_subhour);
    PORT_FN(sdk_AdjustTime);
    PORT_FN(sdk_GetRomName);
    PORT_FN(sdk_IsT10K);
    PORT_FN(sdk_sceScfGetTimeZone);
    PORT_FN(sdk_sceScfGetSummerTime);
    PORT_FN(sdk_sceScfGetLocalTimefromRTC);
}
