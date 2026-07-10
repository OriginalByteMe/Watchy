#ifndef SETTINGS_H
#define SETTINGS_H

#include <Watchy.h>

// ---------------------------------------------------------------------------
// TERMINAL watch face configuration
// ---------------------------------------------------------------------------

// Optional local overrides (gitignored): put your real AGGREGATOR_URL etc. in
// settings.local.h next to this file; it wins over every default below.
#if defined(__has_include)
#  if __has_include("settings.local.h")
#    include "settings.local.h"
#  endif
#endif

// LAN usage-aggregator endpoint (percentages only, no secrets on the wire).
// GET returns the /watchy.json contract documented in TerminalFace.cpp.
#ifndef AGGREGATOR_URL
#define AGGREGATOR_URL "http://192.168.1.100:8090/watchy.json"
#endif

// How often (in minute-ticks) to spend Wi-Fi on a refresh. The RTC wakes the
// watch once per minute; we only light up the radio every FETCH_INTERVAL_MIN.
#define FETCH_INTERVAL_MIN 30

// 12 or 24 hour clock for the big time readout.
#define HOUR_12_24 24

// NTP / timezone. gmtOffset is used to keep the local clock in sync; the data
// "age" shown on the face is measured against the watch clock, so it is
// timezone-agnostic.
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC (3600 * -5) // e.g. US Eastern (EST). Adjust to taste.

// Buzz on the hour.
#define VIBRATE_OCLOCK true

// The Watchy base class needs a watchySettings instance. Only the sketch
// translation unit (which defines TERMINAL_MAIN before including us) emits it;
// TerminalFace.cpp includes this header purely for the macros above.
#ifdef TERMINAL_MAIN
watchySettings settings{
    .cityID         = "",
    .ntpServer      = NTP_SERVER,
    .gmtOffset      = GMT_OFFSET_SEC,
    .vibrateOClock  = VIBRATE_OCLOCK,
};
#endif

#endif
