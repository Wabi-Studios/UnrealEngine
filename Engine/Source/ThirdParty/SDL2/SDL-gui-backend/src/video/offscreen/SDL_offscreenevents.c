/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2014 Sam Lantinga <slouken@libsdl.org>
  Copyright (C) 2016-2019 Epic Games, Inc.

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_OFFSCREEN

/* Being a offscreen driver, there's no event stream. We just define stubs for
   most of the API. */

#include "../../events/SDL_events_c.h"

#include "SDL_offscreenvideo.h"
#include "SDL_offscreenevents_c.h"

void
OFFSCREEN_PumpEvents(_THIS)
{
    /* do nothing. */
}

#endif /* SDL_VIDEO_DRIVER_OFFSCREEN */

/* vi: set ts=4 sw=4 expandtab: */
