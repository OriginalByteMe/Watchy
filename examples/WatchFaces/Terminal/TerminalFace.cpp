#include "TerminalFace.h"

// ===========================================================================
// TERMINAL - a retro wasteland-terminal usage watch face for Watchy.
//
// Shows Claude Code + Codex usage gauges pulled from a LAN JSON endpoint on a
// 200x200 1-bit e-paper panel, in a green-screen "wasteland terminal" style.
//
// Aggregator contract (GET AGGREGATOR_URL):
//   {"ts": <unix|null>,
//    "claude": {"h5":int,"wk":int,"mo":int|null,"reset_h5":ISO,"reset_wk":ISO}|null,
//    "codex":  {"h5":int,"wk":int,"mo":null,       "reset_h5":ISO,"reset_wk":ISO}|null,
//    "lab":    {"array":"ok"|"warn","containers_down":int,"note":str},
//    "mtd":    {"days_tracked":int,"capped_days":int,"spark":[<=7 ints 0-100]}}
// A provider sub-object is null when THAT provider is individually stale; ts is
// the newest still-fresh push, null only when both are stale. claude.mo is the
// model-scoped weekly percent, drawn as the MAX gauge (codex.mo is always null).
// ===========================================================================

#define BG GxEPD_BLACK
#define FG GxEPD_WHITE

// Cached snapshot + fetch cadence, persisted through deep sleep in RTC memory.
RTC_DATA_ATTR UsageCache g_usage      = {};
RTC_DATA_ATTR int        g_fetchCount = -1;

// ---- layout constants -----------------------------------------------------
static const int16_t HEADER_H  = 15;
static const int16_t TIME_BX   = 6;
static const int16_t TIME_BY   = 45;   // DSEG baseline
static const int16_t MASCOT_X  = 155;
static const int16_t MASCOT_Y  = 16;
static const int16_t DIV_Y     = 57;
static const int16_t GAUGE_TOP = 62;
static const int16_t PROV_HDR  = 10;
static const int16_t ROW_H     = 13;
static const int16_t BAR_X     = 36;
static const int16_t BAR_W     = 116;
static const int16_t BAR_H     = 10;
static const int16_t WLABEL_X  = 12;
static const int16_t RIGHT_X   = 198;
static const int16_t MTD_Y     = 168;
static const int16_t FOOT_DIV  = 183;
static const int16_t FOOT_Y    = 186;

static const int16_t FRESH_MIN = 45;   // footer "OK" window
static const int16_t STALE_MIN = 90;   // per-gauge hard "NO DATA" cutoff

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static void setHHMM(char *dst, const String &iso) {
  // ISO8601 "YYYY-MM-DDTHH:MM:SS..." -> "HH:MM"; blank when unparseable.
  if (iso.length() >= 16 && iso.charAt(13) == ':') {
    dst[0] = iso.charAt(11); dst[1] = iso.charAt(12);
    dst[2] = ':';
    dst[3] = iso.charAt(14); dst[4] = iso.charAt(15);
    dst[5] = 0;
  } else {
    dst[0] = 0;
  }
}

void TerminalFace::printRight(int16_t rightX, int16_t y, const char *s) {
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(rightX - w, y);
  display.print(s);
}

void TerminalFace::scanlines(int16_t x, int16_t y, int16_t w, int16_t h) {
  // Faint CRT scanlines: a lit pixel every other row, offset per row.
  for (int16_t yy = y; yy < y + h; yy += 2)
    for (int16_t xx = x + (yy & 2 ? 0 : 3); xx < x + w; xx += 6)
      display.drawPixel(xx, yy, FG);
}

// ---------------------------------------------------------------------------
// Data plumbing
// ---------------------------------------------------------------------------
long TerminalFace::dataAgeMinutes() const {
  if (!g_usage.everFetched) return -1;
  long now = (long)makeTime(currentTime);
  long d   = now - (long)g_usage.lastFetchLocal;
  return d < 0 ? 0 : d / 60;
}

bool TerminalFace::providerStale(bool online) const {
  if (!g_usage.everFetched || !online) return true;
  long age = dataAgeMinutes();
  return age < 0 || age >= STALE_MIN;
}

void TerminalFace::maybeFetch() {
  if (g_fetchCount < 0) g_fetchCount = FETCH_INTERVAL_MIN; // force first fetch
  if (g_fetchCount >= FETCH_INTERVAL_MIN) {
    fetchUsage();          // updates g_usage on success, keeps cache on failure
    g_fetchCount = 0;
  } else {
    g_fetchCount++;
  }
}

bool TerminalFace::fetchUsage() {
  // connectWiFi() only powers down on timeout; be explicit on every failure path.
  if (!connectWiFi()) { WiFi.mode(WIFI_OFF); btStop(); return false; }

  bool ok = false;
  HTTPClient http;
  http.setConnectTimeout(3000);   // 3s connect budget
  http.setTimeout(3000);          // 3s read budget
  http.begin(AGGREGATOR_URL);
  int code = http.GET();
  if (code == 200) {
    JSONVar root = JSON.parse(http.getString());
    if (JSON.typeof(root) == "object") {
      g_usage.everFetched    = true;
      g_usage.lastFetchLocal = makeTime(currentTime);
      g_usage.ts = (JSON.typeof(root["ts"]) == "number")
                       ? (uint32_t)(double)root["ts"] : 0;

      // ---- Claude ----
      JSONVar c = root["claude"];
      if (JSON.typeof(c) == "object") {
        g_usage.claudeOnline = true;
        g_usage.c_h5 = (int)c["h5"];
        g_usage.c_wk = (int)c["wk"];
        g_usage.c_mo = (JSON.typeof(c["mo"]) == "number") ? (int)c["mo"] : -1;
        setHHMM(g_usage.c_reset_h5, String((const char *)c["reset_h5"]));
        setHHMM(g_usage.c_reset_wk, String((const char *)c["reset_wk"]));
      } else {
        g_usage.claudeOnline = false;
      }

      // ---- Codex ----
      JSONVar x = root["codex"];
      if (JSON.typeof(x) == "object") {
        g_usage.codexOnline = true;
        g_usage.x_h5 = (int)x["h5"];
        g_usage.x_wk = (int)x["wk"];
        setHHMM(g_usage.x_reset_h5, String((const char *)x["reset_h5"]));
        setHHMM(g_usage.x_reset_wk, String((const char *)x["reset_wk"]));
      } else {
        g_usage.codexOnline = false;
      }

      // ---- Lab side-channel ----
      JSONVar lab = root["lab"];
      if (JSON.typeof(lab) == "object") {
        g_usage.lab_warn = String((const char *)lab["array"]) == "warn";
        g_usage.lab_down = (JSON.typeof(lab["containers_down"]) == "number")
                               ? (int)lab["containers_down"] : 0;
      }

      // ---- Month-to-date sparkline ----
      JSONVar m = root["mtd"];
      g_usage.mtd_n = 0;
      if (JSON.typeof(m) == "object") {
        g_usage.mtd_capped = (JSON.typeof(m["capped_days"]) == "number")
                                 ? (int)m["capped_days"] : 0;
        JSONVar sp = m["spark"];
        if (JSON.typeof(sp) == "array") {
          int n = sp.length();
          if (n > 7) n = 7;
          for (int i = 0; i < n; i++) g_usage.mtd_spark[i] = (int)sp[i];
          g_usage.mtd_n = n;
        }
      }
      ok = true;
    }
  }
  http.end();
  WiFi.mode(WIFI_OFF);          // mirror getWeatherData(): radios off after use
  btStop();
  return ok;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
void TerminalFace::drawWatchFace() {
  maybeFetch();                 // spend Wi-Fi only on the fetch tick

  display.fillScreen(BG);
  display.setTextColor(FG);
  display.setTextWrap(false);

  drawHeader();
  drawTime();
  drawMascot();
  drawGauges();
  drawMtd();
  drawFooter();
}

void TerminalFace::drawHeader() {
  display.fillRect(0, 0, DISPLAY_WIDTH, HEADER_H, FG); // inverted strip
  display.setTextColor(BG);
  display.setFont();
  display.setCursor(3, 4);
  display.print("TERMINAL 3000");

  char date[8];
  snprintf(date, sizeof(date), "%02d.%02d", currentTime.Day, currentTime.Month);
  printRight(RIGHT_X, 4, date);

  if (g_usage.lab_warn) {        // rare: homelab array degraded
    char lab[10];
    snprintf(lab, sizeof(lab), "LAB-%d!", g_usage.lab_down);
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(lab, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((DISPLAY_WIDTH - w) / 2, 4);
    display.print(lab);
  }
  display.setTextColor(FG);
}

void TerminalFace::drawTime() {
  display.setFont(&DSEG7_Classic_Bold_25);
  display.setCursor(TIME_BX, TIME_BY);
  int hh = currentTime.Hour;
  if (HOUR_12_24 == 12) hh = ((currentTime.Hour + 11) % 12) + 1;
  if (hh < 10) display.print("0");
  display.print(hh);
  display.print(":");
  if (currentTime.Minute < 10) display.print("0");
  display.print(currentTime.Minute);

  // divider rule + scanline shimmer under the clock for CRT flavor
  display.drawFastHLine(0, DIV_Y, DISPLAY_WIDTH, FG);
  scanlines(0, DIV_Y + 2, DISPLAY_WIDTH, 3);
  display.setFont();
}

void TerminalFace::drawMascot() {
  // Pick a mood from the worst ONLINE percent (offline providers ignored).
  int worst = -1;
  if (!providerStale(g_usage.claudeOnline)) {
    worst = max(worst, (int)g_usage.c_h5);
    worst = max(worst, (int)g_usage.c_wk);
    if (g_usage.c_mo >= 0) worst = max(worst, (int)g_usage.c_mo);
  }
  if (!providerStale(g_usage.codexOnline)) {
    worst = max(worst, (int)g_usage.x_h5);
    worst = max(worst, (int)g_usage.x_wk);
  }

  const unsigned char *sprite;
  if (worst < 0)        sprite = mascot_neutral; // no live data -> calm
  else if (worst < 50)  sprite = mascot_ok;
  else if (worst < 80)  sprite = mascot_neutral;
  else if (worst < 95)  sprite = mascot_sweat;
  else                  sprite = mascot_ko;

  display.drawBitmap(MASCOT_X, MASCOT_Y, sprite, MASCOT_W, MASCOT_H, FG);
}

void TerminalFace::drawBracketBar(int16_t x, int16_t y, int16_t w, int16_t h,
                                  int8_t pct, bool hot) {
  int16_t rx = x + w - 1;
  // bracket ends: [ ... ]
  display.drawFastVLine(x, y, h, FG);
  display.drawFastHLine(x, y, 2, FG);
  display.drawFastHLine(x, y + h - 1, 2, FG);
  display.drawFastVLine(rx, y, h, FG);
  display.drawFastHLine(rx - 1, y, 2, FG);
  display.drawFastHLine(rx - 1, y + h - 1, 2, FG);

  const int N = 10;
  int16_t segX0 = x + 3, segX1 = rx - 3;
  int16_t innerW = segX1 - segX0 + 1;
  int16_t gap = 1;
  int16_t segW = (innerW - (N - 1) * gap) / N;
  if (segW < 1) segW = 1;

  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  int filled = (pct * N + 50) / 100;   // rounded

  for (int i = 0; i < N; i++) {
    int16_t sx = segX0 + i * (segW + gap);
    if (i < filled) display.fillRect(sx, y + 1, segW, h - 2, FG);
    else            display.drawRect(sx, y + 1, segW, h - 2, FG);
  }

  if (hot) {                            // >=90%: barber-pole hatch over fill
    int16_t hx1 = segX0 + filled * (segW + gap);
    for (int16_t yy = y + 1; yy < y + h - 1; yy++)
      for (int16_t xx = segX0; xx < hx1; xx++)
        if (((xx + yy) % 3) == 0) display.drawPixel(xx, yy, BG);
  }
}

void TerminalFace::drawDashedBar(int16_t x, int16_t y, int16_t w, int16_t h) {
  int16_t my = y + h / 2;
  for (int16_t xx = x; xx < x + w; xx += 6) display.drawFastHLine(xx, my, 3, FG);
}

void TerminalFace::drawGaugeRow(int16_t y, const char *label, int8_t pct,
                                bool online, const char *resetStr) {
  display.setFont();
  display.setTextColor(FG);
  display.setCursor(WLABEL_X, y + 1);
  display.print(label);

  if (!online) {                        // shouldn't reach: providers gate rows
    drawDashedBar(BAR_X, y, BAR_W, BAR_H);
    printRight(RIGHT_X, y + 1, "--");
    return;
  }

  bool hot = pct >= 90;
  drawBracketBar(BAR_X, y, BAR_W, BAR_H, pct, hot);

  char pc[6];
  snprintf(pc, sizeof(pc), "%d%%", pct);
  printRight(RIGHT_X, y + 1, pc);

  if (hot && resetStr && resetStr[0]) { // reset time under the hot bar
    char rs[12];
    snprintf(rs, sizeof(rs), "RST %s", resetStr);
    display.setCursor(BAR_X, y + BAR_H + 1);
    display.print(rs);
  }
}

void TerminalFace::drawGauges() {
  int16_t y = GAUGE_TOP;
  display.setFont();

  // ---- CLAUDE ----
  display.setTextColor(FG);
  display.setCursor(2, y);
  display.print("CLAUDE");
  for (int16_t dx = 2 + 6 * 6; dx < RIGHT_X; dx += 4) display.drawPixel(dx, y + 3, FG);
  y += PROV_HDR;

  if (providerStale(g_usage.claudeOnline)) {
    drawDashedBar(WLABEL_X, y, 120, BAR_H);
    printRight(RIGHT_X, y + 1, "NO DATA");
    y += ROW_H;
  } else {
    drawGaugeRow(y, "5H", g_usage.c_h5, true, g_usage.c_reset_h5);
    y += (g_usage.c_h5 >= 90) ? ROW_H + 9 : ROW_H;
    drawGaugeRow(y, "7D", g_usage.c_wk, true, g_usage.c_reset_wk);
    y += (g_usage.c_wk >= 90) ? ROW_H + 9 : ROW_H;
    if (g_usage.c_mo >= 0) {             // MAX (model-scoped weekly); skip if null
      drawGaugeRow(y, "MAX", g_usage.c_mo, true, g_usage.c_reset_wk);
      y += (g_usage.c_mo >= 90) ? ROW_H + 9 : ROW_H;
    }
  }

  // ---- CODEX ----
  display.setFont();
  display.setTextColor(FG);
  display.setCursor(2, y);
  display.print("CODEX");
  for (int16_t dx = 2 + 5 * 6; dx < RIGHT_X; dx += 4) display.drawPixel(dx, y + 3, FG);
  y += PROV_HDR;

  if (providerStale(g_usage.codexOnline)) {
    drawDashedBar(WLABEL_X, y, 120, BAR_H);
    printRight(RIGHT_X, y + 1, "NO DATA");
    y += ROW_H;
  } else {
    drawGaugeRow(y, "5H", g_usage.x_h5, true, g_usage.x_reset_h5);
    y += (g_usage.x_h5 >= 90) ? ROW_H + 9 : ROW_H;
    drawGaugeRow(y, "7D", g_usage.x_wk, true, g_usage.x_reset_wk);
    y += (g_usage.x_wk >= 90) ? ROW_H + 9 : ROW_H;
  }

  // CRT fill for any slack between the gauges and the MTD strip.
  if (y + 2 < MTD_Y - 2) scanlines(0, y + 2, DISPLAY_WIDTH, MTD_Y - 2 - (y + 2));
}

void TerminalFace::drawMtd() {
  if (g_usage.mtd_n <= 0) return;       // no history -> hide the element

  display.setFont();
  display.setTextColor(FG);
  display.setCursor(2, MTD_Y + 3);
  display.print("MTD");

  const int16_t bx0 = 30, bw = 4, gap = 2, bh = 12;
  int16_t base = MTD_Y + bh;            // sparkline baseline
  for (int i = 0; i < g_usage.mtd_n; i++) {
    int8_t v = g_usage.mtd_spark[i];
    if (v < 0) v = 0; if (v > 100) v = 100;
    int16_t hgt = 1 + (v * (bh - 1)) / 100;
    int16_t sx = bx0 + i * (bw + gap);
    display.fillRect(sx, base - hgt, bw, hgt, FG);
  }
  int16_t sparkEnd = bx0 + g_usage.mtd_n * (bw + gap) + 4;

  char cap[10];
  snprintf(cap, sizeof(cap), "CAP:%d", g_usage.mtd_capped);
  display.setCursor(sparkEnd, MTD_Y + 3);
  display.print(cap);
}

void TerminalFace::drawFooter() {
  display.drawFastHLine(0, FOOT_DIV, DISPLAY_WIDTH, FG);

  display.setFont();
  display.setTextColor(FG);

  // Steps
  char steps[14];
  snprintf(steps, sizeof(steps), "END %lu", (unsigned long)sensor.getCounter());
  display.setCursor(2, FOOT_Y + 3);
  display.print(steps);

  // Battery: map 3.3V..4.2V -> 0..100%
  float v = getBatteryVoltage();
  int bpct = (int)((v - 3.3f) / (4.2f - 3.3f) * 100.0f + 0.5f);
  if (bpct < 0) bpct = 0; if (bpct > 100) bpct = 100;
  char pwr[10];
  snprintf(pwr, sizeof(pwr), "PWR %d%%", bpct);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(pwr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((DISPLAY_WIDTH - w) / 2, FOOT_Y + 3);
  display.print(pwr);

  // Data age / signal
  long age = dataAgeMinutes();
  char sig[14];
  if (age < 0)            snprintf(sig, sizeof(sig), "NO SIGNAL");
  else if (age < FRESH_MIN) snprintf(sig, sizeof(sig), "OK");
  else if (age < 600)     snprintf(sig, sizeof(sig), "OLD %ldm", age);
  else                    snprintf(sig, sizeof(sig), "OLD %ldh", age / 60);
  printRight(RIGHT_X, FOOT_Y + 3, sig);
}
