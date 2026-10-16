// Talks: the talk session and its views, and the WORDS script handlers.

#include "game_functions.h"
#include "game_globals.h"

namespace mm
{
    // The pose stored for a family member in the session's 32-entry table; 0 when there is none.
    int Talk_LookupPose(GuestPtr<TalkSession> talk, uint32_t family, uint32_t member, GuestPtr<uint32_t> pose)
    {
        for (const TalkPose &entry : talk->poses)
            if (entry.family == family && entry.member == member)
            {
                *pose = entry.pose;
                return 1;
            }
        return 0;
    }

    PORT_FN(Talk_LookupPose);
}
