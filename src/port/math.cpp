// Math helpers.

#include "game_functions.h"

namespace mm
{
    // floor(log2(value)) for value >= 1; 0 for anything smaller.
    int Math_Log2(int value)
    {
        int bits = 0;
        for (; value > 1; value >>= 1)
            ++bits;
        return bits;
    }

    PORT_FN(Math_Log2);
}
