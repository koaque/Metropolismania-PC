// Random numbers: Knuth's subtractive generator (the "ran3" of Numerical Recipes). The table holds 55
// values in [0, 1e9) as 64-bit integers and is refilled ("stirred") once every 55 draws.

#include "game_functions.h"
#include "game_globals.h"

namespace mm
{
    namespace
    {
        constexpr int64_t kModulus = 1000000000;

        int64_t randomEntry(int i) { return port::load<int64_t>(globals::kRandomTable + 8u * static_cast<uint32_t>(i)); }
        void setRandomEntry(int i, int64_t value) { port::store<int64_t>(globals::kRandomTable + 8u * static_cast<uint32_t>(i), value); }

        int64_t subtractWrapped(int64_t a, int64_t b)
        {
            const int64_t d = a - b;
            return d < 0 ? d + kModulus : d;
        }
    }

    // One refill pass: entry i -= entry i+31 for the first 24, entry i -= entry i-24 for the rest.
    void Random_Stir()
    {
        for (int i = 1; i <= 24; ++i)
            setRandomEntry(i, subtractWrapped(randomEntry(i), randomEntry(i + 31)));
        for (int i = 25; i <= 55; ++i)
            setRandomEntry(i, subtractWrapped(randomEntry(i), randomEntry(i - 24)));
    }

    // Fills the table from the seed in the order 21, 42, 8, ... (21 * i mod 55), then stirs it three times.
    void Random_Seed(int seed)
    {
        int64_t previous = seed;
        int64_t value = 1;
        setRandomEntry(55, previous);
        for (int i = 1; i < 55; ++i)
        {
            const int slot = (21 * i) % 55;
            setRandomEntry(slot, value);
            value = subtractWrapped(previous, value);
            previous = randomEntry(slot);
        }
        Random_Stir();
        Random_Stir();
        Random_Stir();
        port::store<int32_t>(globals::kRandomNext, 55);
    }

    // The next value. Callers keep the low 32 bits; the full 64-bit entry is what v0 holds.
    int64_t Random_Next()
    {
        int32_t next = port::load<int32_t>(globals::kRandomNext) + 1;
        if (next > 55)
        {
            Random_Stir();
            next = 1;
        }
        port::store<int32_t>(globals::kRandomNext, next);
        return randomEntry(next);
    }

    // Steps the cascade and adds the count of timer 0 (T0_COUNT, the low 16 bits) into its first word, which
    // is the result: noise for sound variations and footsteps, independent of the generator above.
    int Random_TimerMix()
    {
        constexpr uint32_t kTimer0Count = 0x10000000u;
        const uint32_t timer = port::readHardware32(kTimer0Count);
        const GuestPtr<uint32_t> mix{globals::kTimerMix};
        mix[1] += mix[0];
        mix[2] += mix[1];
        mix[3] += mix[2];
        mix[4] += mix[3];
        mix[0] = timer + (mix[4] + mix[0]);
        return static_cast<int>(mix[0]);
    }

    PORT_FN(Random_TimerMix);
    PORT_FN(Random_Stir);
    PORT_FN(Random_Seed);
    PORT_FN(Random_Next);
}
