#pragma once

#include "compat/stdtype.h"
#include "compat/hid.h"

//#ifdef _WIN32
#include <SDL2/SDL.h>
//#else
//#include <SDL.h>
//#endif

typedef enum
{
    PAD_A,
    PAD_B,
    PAD_SELECT,
    PAD_START,
    PAD_DRIGHT,
    PAD_DLEFT,
    PAD_DUP,
    PAD_DDOWN,
    PAD_R,
    PAD_L,
    PAD_X,
    PAD_Y,
    PAD_TOUCH,
    PAD_MAX,
    PAD_SPECIAL = PAD_TOUCH
} PadKeys;
