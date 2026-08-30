#include <libdailycomp/dailycomp.h>

#define ENUM_STR(_enum) [_enum] = #_enum

const char *window_ev_to_str(enum window_ev_type ev)
{
    static const char *types[] = {
        ENUM_STR(WINDOW_EV_NEW_REQUEST),
        ENUM_STR(WINDOW_EV_NEW_RESPONSE),
        ENUM_STR(WINDOW_EV_REGION_DIRTY),
        ENUM_STR(WINDOW_EV_REDRAW),
    };

    if (ev > WINDOW_EV_MAX)
        return "???";
    return types[ev];
}
