// The platform layer, the small functions of the MPEG library the movie player calls (plan P7), written from what they do and proven
// against the recompiled functions by the difftest. Prototypes are in config/platform_signatures.txt. The decoder object's pointer
// at +0x40 leads to a state block. sceMpegIsEnd is not here: the runtime's MPEG library answers it (sdk.h).

#include "game_functions.h"
#include "sdk.h"

namespace mm
{
    // Deleting the decoder needs nothing here: 1.
    int sdk_sceMpegDelete(int mpeg)
    {
        (void)mpeg;
        return 1;
    }

    // Whether the reference buffer (the second word of the state block) holds nothing.
    int sdk_sceMpegIsRefBuffEmpty(int mpeg) { return port::load<uint32_t>(port::load<uint32_t>(static_cast<uint32_t>(mpeg) + 0x40u) + 4u) == 0u ? 1 : 0; }

    PORT_FN(sdk_sceMpegDelete);
    PORT_FN(sdk_sceMpegIsRefBuffEmpty);
}
