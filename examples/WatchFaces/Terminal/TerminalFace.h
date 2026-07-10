#ifndef TERMINAL_FACE_H
#define TERMINAL_FACE_H

#include <Watchy.h>
#include "DSEG7_Classic_Bold_25.h"
#include "icons.h"
#include "settings.h"

// Cached usage snapshot, persisted across deep sleep in RTC slow memory.
// POD only (no String / heap) so it survives ESP32 deep sleep intact.
struct UsageCache {
  bool     everFetched;    // have we ever received a valid payload?
  time_t   lastFetchLocal; // watch-clock epoch of last successful HTTP 200
  uint32_t ts;             // top-level payload ts (0 when both providers stale)

  // Claude window percents (0-100). mo == MAX (model-scoped weekly); -1 = null.
  bool     claudeOnline;   // claude sub-object was present (non-null) last fetch
  int8_t   c_h5, c_wk, c_mo;
  char     c_reset_h5[6];  // "HH:MM" local, or "" when unknown
  char     c_reset_wk[6];

  // Codex window percents. Codex has no MAX window (mo always null upstream).
  bool     codexOnline;
  int8_t   x_h5, x_wk;
  char     x_reset_h5[6];
  char     x_reset_wk[6];

  // Homelab side-channel.
  bool     lab_warn;       // array == "warn"
  int8_t   lab_down;       // containers_down

  // Locally-tracked month-to-date stats (providers expose no monthly window).
  int8_t   mtd_n;          // valid spark samples 0..7 (0 => hide the element)
  int8_t   mtd_capped;     // capped_days (red-alert count this month)
  int8_t   mtd_spark[7];   // oldest -> newest, 0-100
};

class TerminalFace : public Watchy {
  using Watchy::Watchy;

public:
  void drawWatchFace() override;

private:
  // Data plumbing
  void maybeFetch();          // decide + perform a refresh on the fetch tick
  bool fetchUsage();          // one connect->GET->parse->store cycle
  bool providerStale(bool online) const; // gauge should read "NO DATA"?
  long dataAgeMinutes() const;

  // Rendering
  void drawHeader();
  void drawTime();
  void drawMascot();
  void drawGauges();
  void drawGaugeRow(int16_t y, const char *label, int8_t pct, bool online,
                    const char *resetStr);
  void drawMtd();
  void drawFooter();

  // Small primitives
  void drawBracketBar(int16_t x, int16_t y, int16_t w, int16_t h, int8_t pct,
                      bool hot);
  void drawDashedBar(int16_t x, int16_t y, int16_t w, int16_t h);
  void printRight(int16_t rightX, int16_t y, const char *s);
  void scanlines(int16_t x, int16_t y, int16_t w, int16_t h);
};

#endif // TERMINAL_FACE_H
