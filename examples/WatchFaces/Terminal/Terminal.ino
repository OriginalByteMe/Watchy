// TERMINAL - a retro wasteland-terminal usage watch face for Watchy.
// See TerminalFace.cpp for the aggregator contract and layout notes.
#define TERMINAL_MAIN            // emit the watchySettings instance in settings.h
#include "TerminalFace.h"

TerminalFace watchy(settings);

void setup() { watchy.init(); }

void loop() {}
