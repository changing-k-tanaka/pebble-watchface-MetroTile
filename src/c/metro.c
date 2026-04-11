/**
 * Metro UI Watchface
 *
 * Windows Mobile-style tile grid watchface for Pebble.
 * - 6 configurable tiles in a 2-column × 3-row grid
 * - Each tile shows: None, Time, Date, Day, Year, Heart Rate, Steps,
 *   Weather, Battery, Temperature, or Precipitation
 * - Per-tile background + text color (configurable via phone settings page)
 * - Round display support: inscribed safe-area inset keeps content readable
 * - Weather via AppMessage + PebbleKit JS (Open-Meteo API)
 * - Health service for step count and heart rate
 */

#include <pebble.h>

// ============================================================================
// CONSTANTS
// ============================================================================

#define NUM_TILES            6
#define TILE_GAP             4
#define SETTINGS_VERSION     7
#define PERSIST_KEY_SETTINGS ((uint32_t)42)
#define PERSIST_KEY_WEATHER  ((uint32_t)43)
#define BW_LIGHT_GRAY_ARGB   (GColorLightGray.argb)

// ============================================================================
// DATA TYPES
// ============================================================================

typedef enum {
  TILE_NONE       = 0,
  TILE_TIME       = 1,
  TILE_DATE       = 2,
  TILE_DAY        = 3,
  TILE_YEAR       = 4,
  TILE_HEART_RATE = 5,
  TILE_STEPS      = 6,
  TILE_WEATHER    = 7,
  TILE_BATTERY    = 8,
  TILE_TEMPERATURE = 9,
  TILE_PRECIPITATION = 10,
} TileType;

// Packed: 3 bytes, no padding
typedef struct __attribute__((packed)) {
  uint8_t type;  // TileType as uint8_t
  uint8_t bg;    // GColor.argb
  uint8_t fg;    // GColor.argb
} TileConfig;

typedef struct __attribute__((packed)) {
  uint8_t    version;
  TileConfig tiles[NUM_TILES];
  uint8_t    date_format;  // 0=MM/DD, 1=DD/MM, 2=MM月DD日
  uint8_t    temp_unit;    // 0=Celsius, 1=Fahrenheit
  uint8_t    bluetooth_disconnect_vibe;  // 0=OFF, 1=ON
} Settings;  // 1 + 6*3 + 3 = 22 bytes

typedef struct __attribute__((packed)) {
  uint8_t    version;
  TileConfig tiles[NUM_TILES];
  uint8_t    date_format;
  uint8_t    temp_unit;
} LegacySettingsV6;

typedef struct {
  int8_t temperature;
  int8_t precipitation;
  char   conditions[32];
  char   city[32];
} WeatherData;

// ============================================================================
// GLOBAL STATE
// ============================================================================

static Window      *s_window;
static Layer       *s_canvas_layer;
static Settings     s_settings;
static WeatherData  s_weather;
static int32_t      s_steps        = -1;  // -1 = unavailable → "--"
static int32_t      s_heart_rate   =  0;  //  0 = unavailable → "--"
static int32_t      s_battery_pct  = -1;  // -1 = not yet read
static bool         s_bt_connected = false;
static GFont        s_font_label;
static GBitmap     *s_umbrella_black;
static GBitmap     *s_umbrella_white;
static GBitmap     *s_bluetooth_black;
static GBitmap     *s_bluetooth_white;

typedef struct {
  GRect bg;
  GRect content;
  char value[48];
  const char *label;
  uint8_t bg_argb;
  uint8_t fg_argb;
  uint8_t type;
} TileRenderData;

static const char *prv_tile_label(uint8_t type);
static void prv_tile_value(uint8_t type, char *buf, size_t buf_size);
static void prv_settings_save(void);

static bool prv_use_large_display_fonts(void) {
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  return true;
#else
  return false;
#endif
}

static bool prv_use_large_label_font(void) {
#if defined(PBL_PLATFORM_EMERY)
  return true;
#else
  return false;
#endif
}

static int prv_label_height(void) {
  return prv_use_large_label_font() ? 20 : 16;
}

static int __attribute__((unused)) prv_gabbro_round_value_y_nudge(int row) {
#if defined(PBL_PLATFORM_GABBRO)
  if (row == 0) {
    return 6;
  }
  if (row == 1) {
    return -5;
  }
#else
  (void)row;
#endif
  return 0;
}

static GFont __attribute__((unused)) prv_gabbro_round_edge_font(uint8_t type, int row, GFont font) {
#if defined(PBL_PLATFORM_GABBRO)
  if ((type == TILE_TIME || type == TILE_DATE) && (row == 0 || row == 2)) {
    if (font == fonts_get_system_font(FONT_KEY_LECO_32_BOLD_NUMBERS)) {
      return fonts_get_system_font(FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM);
    }
    if (font == fonts_get_system_font(FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM)) {
      return fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS);
    }
  }
#else
  (void)type;
  (void)row;
#endif
  return font;
}

static int __attribute__((unused)) prv_gabbro_round_edge_y_offset(uint8_t type, int row) {
#if defined(PBL_PLATFORM_GABBRO)
  if ((type == TILE_TIME || type == TILE_DATE) && row == 0) {
    return 8;
  }
#else
  (void)type;
  (void)row;
#endif
  return 0;
}

// ============================================================================
// HELPER: GColor from stored argb byte
// ============================================================================

static GColor prv_color(uint8_t argb) {
  return (GColor){ .argb = argb };
}


static bool prv_use_dark_icon(uint8_t bg_argb) {
#if defined(PBL_BW)
  return bg_argb == GColorWhite.argb || bg_argb == BW_LIGHT_GRAY_ARGB;
#else
  int r = (bg_argb >> 4) & 0x3;
  int g = (bg_argb >> 2) & 0x3;
  int b = bg_argb & 0x3;
  return (r + g + b) >= 5;
#endif
}

static GBitmap *prv_umbrella_bitmap_for_bg(uint8_t bg_argb) {
  return prv_use_dark_icon(bg_argb) ? s_umbrella_black : s_umbrella_white;
}

static GBitmap *prv_bluetooth_bitmap_for_bg(uint8_t bg_argb) {
  return prv_use_dark_icon(bg_argb) ? s_bluetooth_black : s_bluetooth_white;
}

static void prv_set_tile_defaults(TileConfig *tile, TileType type) {
  tile->type = (uint8_t)type;

  switch (type) {
    case TILE_TIME:
      tile->bg = PBL_IF_COLOR_ELSE((GColor){ .argb = 0xE2 }, GColorWhite).argb;
      tile->fg = PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack).argb;
      break;
    case TILE_DATE:
      tile->bg = PBL_IF_COLOR_ELSE((GColor){ .argb = 0xE1 }, GColorLightGray).argb;
      tile->fg = PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack).argb;
      break;
    case TILE_DAY:
      tile->bg = PBL_IF_COLOR_ELSE((GColor){ .argb = 0xC2 }, GColorWhite).argb;
      tile->fg = PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack).argb;
      break;
    case TILE_YEAR:
      tile->bg = PBL_IF_COLOR_ELSE(GColorOxfordBlue, GColorBlack).argb;
      tile->fg = GColorWhite.argb;
      break;
    case TILE_HEART_RATE:
      tile->bg = PBL_IF_COLOR_ELSE(GColorRed, GColorBlack).argb;
      tile->fg = GColorWhite.argb;
      break;
    case TILE_STEPS:
      tile->bg = PBL_IF_COLOR_ELSE(GColorOrange, GColorWhite).argb;
      tile->fg = PBL_IF_COLOR_ELSE(GColorBlack, GColorBlack).argb;
      break;
    case TILE_WEATHER:
      tile->bg = PBL_IF_COLOR_ELSE(GColorCyan, GColorBlack).argb;
      tile->fg = PBL_IF_COLOR_ELSE(GColorBlack, GColorWhite).argb;
      break;
    case TILE_BATTERY:
      tile->bg = PBL_IF_COLOR_ELSE((GColor){ .argb = 0xF4 }, GColorBlack).argb;
      tile->fg = GColorWhite.argb;
      break;
    case TILE_TEMPERATURE:
      tile->bg = PBL_IF_COLOR_ELSE((GColor){ .argb = 0xCB }, GColorLightGray).argb;
      tile->fg = PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack).argb;
      break;
    case TILE_PRECIPITATION:
      tile->bg = PBL_IF_COLOR_ELSE((GColor){ .argb = 0xC9 }, GColorBlack).argb;
      tile->fg = GColorWhite.argb;
      break;
    case TILE_NONE:
    default:
      tile->bg = GColorBlack.argb;
      tile->fg = GColorWhite.argb;
      break;
  }
}

static uint8_t prv_contrasting_bw_argb(uint8_t argb) {
  return (argb == GColorWhite.argb || argb == BW_LIGHT_GRAY_ARGB) ?
         GColorBlack.argb : GColorWhite.argb;
}

#if defined(PBL_BW)
static bool prv_is_bw_palette_argb(uint8_t argb) {
  return argb == GColorBlack.argb ||
         argb == BW_LIGHT_GRAY_ARGB ||
         argb == GColorWhite.argb;
}
#endif

static void prv_ensure_tile_contrast(TileConfig *tile) {
  TileConfig fallback;
  prv_set_tile_defaults(&fallback, (TileType)tile->type);

  if (tile->bg != tile->fg) {
    return;
  }

  if (fallback.fg != tile->bg) {
    tile->fg = fallback.fg;
  } else if (fallback.bg != tile->bg) {
    tile->fg = fallback.bg;
  } else {
    tile->fg = prv_contrasting_bw_argb(tile->bg);
  }
}

static void prv_settings_sanitize(void) {
  for (int i = 0; i < NUM_TILES; i++) {
    TileConfig *tile = &s_settings.tiles[i];

    if (tile->type > TILE_PRECIPITATION) {
      prv_set_tile_defaults(tile, TILE_NONE);
      continue;
    }

#if !defined(PBL_PLATFORM_DIORITE) && !defined(PBL_PLATFORM_EMERY)
    if (tile->type == TILE_HEART_RATE) {
      prv_set_tile_defaults(tile, TILE_BATTERY);
      continue;
    }
#endif

#if defined(PBL_BW)
    TileConfig fallback;
    prv_set_tile_defaults(&fallback, (TileType)tile->type);
    if (!prv_is_bw_palette_argb(tile->bg)) {
      tile->bg = fallback.bg;
    }
    if (!prv_is_bw_palette_argb(tile->fg)) {
      tile->fg = fallback.fg;
    }
#endif

    prv_ensure_tile_contrast(tile);
  }

  if (s_settings.date_format > 1) {
    s_settings.date_format = 0;
  }
  if (s_settings.temp_unit > 1) {
    s_settings.temp_unit = 0;
  }
  s_settings.bluetooth_disconnect_vibe =
      s_settings.bluetooth_disconnect_vibe ? 1 : 0;
}

// ============================================================================
// SETTINGS: DEFAULTS, LOAD, SAVE
// ============================================================================

static void prv_settings_load_defaults(void) {
  s_settings.version = SETTINGS_VERSION;

  prv_set_tile_defaults(&s_settings.tiles[0], TILE_DAY);
  prv_set_tile_defaults(&s_settings.tiles[1], TILE_PRECIPITATION);
  prv_set_tile_defaults(&s_settings.tiles[2], TILE_DATE);
  prv_set_tile_defaults(&s_settings.tiles[3], TILE_TIME);
  prv_set_tile_defaults(&s_settings.tiles[4], TILE_BATTERY);
  prv_set_tile_defaults(&s_settings.tiles[5], TILE_TEMPERATURE);

  s_settings.date_format = 0;  // MM/DD
  s_settings.temp_unit   = 0;  // Celsius
  s_settings.bluetooth_disconnect_vibe = 0;  // OFF
}

static void prv_settings_load(void) {
  if (persist_exists(PERSIST_KEY_SETTINGS)) {
    Settings loaded;
    int n = persist_read_data(PERSIST_KEY_SETTINGS, &loaded, sizeof(Settings));
    if (n == (int)sizeof(Settings) && loaded.version == SETTINGS_VERSION) {
      s_settings = loaded;
      prv_settings_sanitize();
      return;
    }

    if (n == (int)sizeof(LegacySettingsV6) && loaded.version == (SETTINGS_VERSION - 1)) {
      LegacySettingsV6 legacy;
      persist_read_data(PERSIST_KEY_SETTINGS, &legacy, sizeof(LegacySettingsV6));

      memset(&s_settings, 0, sizeof(s_settings));
      s_settings.version = SETTINGS_VERSION;
      memcpy(s_settings.tiles, legacy.tiles, sizeof(legacy.tiles));
      s_settings.date_format = legacy.date_format;
      s_settings.temp_unit = legacy.temp_unit;
      s_settings.bluetooth_disconnect_vibe = 0;
      prv_settings_sanitize();
      prv_settings_save();
      return;
    }
  }
  prv_settings_load_defaults();
}

static void prv_settings_save(void) {
  persist_write_data(PERSIST_KEY_SETTINGS, &s_settings, sizeof(Settings));
}

// ============================================================================
// WEATHER: LOAD CACHED, REQUEST
// ============================================================================

static void prv_weather_load_cached(void) {
  memset(&s_weather, 0, sizeof(WeatherData));
  s_weather.temperature = -128;
  s_weather.precipitation = -1;
  if (persist_exists(PERSIST_KEY_WEATHER)) {
    persist_read_data(PERSIST_KEY_WEATHER, &s_weather, sizeof(WeatherData));
  }
}

static void prv_write_device_caps(DictionaryIterator *iter) {
#if defined(PBL_BW)
  dict_write_uint8(iter, MESSAGE_KEY_DEVICE_IS_BW, 1);
#else
  dict_write_uint8(iter, MESSAGE_KEY_DEVICE_IS_BW, 0);
#endif

#if defined(PBL_PLATFORM_DIORITE) || defined(PBL_PLATFORM_EMERY)
  dict_write_uint8(iter, MESSAGE_KEY_DEVICE_HAS_HR, 1);
#else
  dict_write_uint8(iter, MESSAGE_KEY_DEVICE_HAS_HR, 0);
#endif
}

static void prv_send_device_caps(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) {
    return;
  }
  prv_write_device_caps(iter);
  app_message_outbox_send();
}

static void prv_request_weather(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  prv_write_device_caps(iter);
  dict_write_uint8(iter, MESSAGE_KEY_REQUEST_WEATHER, 1);
  app_message_outbox_send();
}

static const char *prv_weather_city_label(void) {
  return s_weather.city[0] != '\0' ? s_weather.city : NULL;
}

static void prv_mark_canvas_dirty(void) {
  if (s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }
}

// ============================================================================
// BLUETOOTH
// ============================================================================

static void prv_bluetooth_handler(bool connected) {
  bool was_connected = s_bt_connected;
  s_bt_connected = connected;

  if (was_connected && !connected && s_settings.bluetooth_disconnect_vibe) {
    vibes_double_pulse();
  }

  if (!was_connected && connected) {
    prv_request_weather();
  }

  prv_mark_canvas_dirty();
}

// ============================================================================
// HEALTH SERVICE
// ============================================================================

// ============================================================================
// BATTERY SERVICE
// ============================================================================

static void prv_battery_update(void) {
  BatteryChargeState state = battery_state_service_peek();
  s_battery_pct = state.charge_percent;
}

static void prv_battery_handler(BatteryChargeState state) {
  s_battery_pct = state.charge_percent;
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}

// ============================================================================
// HEALTH SERVICE
// ============================================================================

static void prv_health_update(void) {
#if defined(PBL_HEALTH)
  // Step count
  HealthServiceAccessibilityMask steps_mask =
    health_service_metric_accessible(HealthMetricStepCount,
                                     time_start_of_today(), time(NULL));
  if (steps_mask & HealthServiceAccessibilityMaskAvailable) {
    s_steps = (int32_t)health_service_sum_today(HealthMetricStepCount);
  } else {
    s_steps = -1;
  }

  // Heart rate
  HealthServiceAccessibilityMask hr_mask =
    health_service_metric_accessible(HealthMetricHeartRateBPM,
                                     time(NULL) - 30, time(NULL));
  if (hr_mask & HealthServiceAccessibilityMaskAvailable) {
    s_heart_rate = (int32_t)health_service_peek_current_value(HealthMetricHeartRateBPM);
  } else {
    s_heart_rate = 0;
  }
#endif
}

#if defined(PBL_HEALTH)
static void prv_health_event_handler(HealthEventType event, void *context) {
  if (event == HealthEventMovementUpdate || event == HealthEventHeartRateUpdate
      || event == HealthEventSignificantUpdate) {
    prv_health_update();
    if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
  }
}
#endif

// ============================================================================
// MATH HELPERS
// ============================================================================

#ifdef PBL_ROUND
// Integer square root (Newton's method) — only needed for round geometry
static int prv_isqrt(int n) {
  if (n <= 0) return 0;
  int x = n, y = (x + 1) / 2;
  while (y < x) { x = y; y = (x + n / x) / 2; }
  return x;
}
// Compute the left and right x positions that are safely inside the circle
// at a given y, for a circle of radius R centered at (R, R).
// margin: additional inset from the circle boundary.
static void prv_circle_safe_x(int y, int R, int margin,
                               int *out_left, int *out_right) {
  int dy = y - R;
  if (dy < 0) dy = -dy;
  if (dy >= R) { *out_left = R; *out_right = R; return; }
  int dx = prv_isqrt(R * R - dy * dy);
  *out_left  = R - dx + margin;
  *out_right = R + dx - margin;
}
#endif

// ============================================================================
// TILE LAYOUT
// ============================================================================

static GRect prv_tile_rect(int idx, GRect bounds) {
  // On round: tiles extend to screen edges — hardware circle clips the visual boundary.
  // On rect: same full-screen grid, no inset.
  int cw = (bounds.size.w - TILE_GAP) / 2;
  int rh = (bounds.size.h - 2 * TILE_GAP) / 3;
  int col = idx % 2;
  int row = idx / 2;
  int x = col * (cw + TILE_GAP);
  int y = row * (rh + TILE_GAP);
  // Right column and bottom row absorb any remaining pixels
  int w = (col == 0) ? cw : (bounds.size.w - x);
  int h = (row < 2)  ? rh : (bounds.size.h - y);
  return GRect(x, y, w, h);
}

static TileRenderData prv_make_tile_render_data(int idx, GRect bounds) {
  static const int PAD = 3;

  TileConfig *tile = &s_settings.tiles[idx];
  TileRenderData data = {
    .bg = prv_tile_rect(idx, bounds),
    .bg_argb = tile->bg,
    .fg_argb = tile->fg,
    .type = tile->type,
    .label = prv_tile_label(tile->type),
  };

  data.content = GRect(data.bg.origin.x + PAD, data.bg.origin.y + PAD,
                       data.bg.size.w - 2 * PAD, data.bg.size.h - 2 * PAD);
  prv_tile_value(tile->type, data.value, sizeof(data.value));

  return data;
}

// ============================================================================
// FONT SELECTION
// ============================================================================

// Select value font based on tile type, available pixel height, and available width.
// Both dimensions must fit; downgrades to a smaller font if either is too tight.
// Numeric tiles use compact LECO fonts; text/mixed tiles use Gothic.
// Width thresholds are per-type because different value strings have different widths:
//   TIME  "HH:MM"  5 chars w/ narrow colon → ~88/70/52 px at 32/26/20
//   YEAR  "2026"   4 digits                → ~80/64/48 px
//   STEPS "99999"  5 wide digits, no colon → ~100/80/60 px  (widest numeric)
//   BPM   "999"    3 digits                → ~60/48/36 px
static GFont prv_tile_font(uint8_t type, int value_h, int value_w) {
  int leco_32_h = prv_use_large_display_fonts() ? 30 : 32;
  int leco_26_h = prv_use_large_display_fonts() ? 24 : 26;
  int goth_28_h = prv_use_large_display_fonts() ? 26 : 28;
  int goth_24_h = prv_use_large_display_fonts() ? 22 : 24;
  int goth_18_h = prv_use_large_display_fonts() ? 16 : 18;

  switch (type) {
    case TILE_TIME:
      if (value_h >= leco_32_h && value_w >= 88) return fonts_get_system_font(FONT_KEY_LECO_32_BOLD_NUMBERS);
      if (value_h >= leco_26_h && value_w >= 70) return fonts_get_system_font(FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM);
      return fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS);

    case TILE_YEAR:
      if (value_h >= leco_32_h && value_w >= 82) return fonts_get_system_font(FONT_KEY_LECO_32_BOLD_NUMBERS);
      if (value_h >= leco_26_h && value_w >= 66) return fonts_get_system_font(FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM);
      return fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS);

    case TILE_STEPS:
      // "99999": 5 full-width digits need more room than "HH:MM" (which has a narrow colon)
      if (value_h >= leco_32_h && value_w >= 102) return fonts_get_system_font(FONT_KEY_LECO_32_BOLD_NUMBERS);
      if (value_h >= leco_26_h && value_w >=  82) return fonts_get_system_font(FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM);
      return fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS);

    case TILE_HEART_RATE:
      // "999": only 3 digits, fits at smaller thresholds
      if (value_h >= leco_32_h && value_w >= 62) return fonts_get_system_font(FONT_KEY_LECO_32_BOLD_NUMBERS);
      if (value_h >= leco_26_h && value_w >= 50) return fonts_get_system_font(FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM);
      return fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS);

    case TILE_BATTERY:
      // "100%" — 4 chars with %, use Gothic
      if (value_h >= goth_28_h && value_w >= 60) return fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
      if (value_h >= goth_24_h && value_w >= 50) return fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
      if (value_h >= goth_18_h && value_w >= 38) return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
      return fonts_get_system_font(FONT_KEY_GOTHIC_14);

    case TILE_TEMPERATURE:
      if (value_h >= goth_28_h && value_w >= 64) return fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
      if (value_h >= goth_24_h && value_w >= 54) return fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
      if (value_h >= goth_18_h && value_w >= 42) return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
      return fonts_get_system_font(FONT_KEY_GOTHIC_14);

    case TILE_DATE:
#if defined(PBL_PLATFORM_GABBRO)
      if (value_h >= leco_32_h && value_w >= 88) return fonts_get_system_font(FONT_KEY_LECO_32_BOLD_NUMBERS);
      if (value_h >= leco_26_h && value_w >= 70) return fonts_get_system_font(FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM);
      return fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS);
#endif

    case TILE_DAY:
    case TILE_PRECIPITATION:
      // Keep DOW, precipitation, and date on the same visual size band.
      if (prv_use_large_display_fonts()) {
        if (value_h >= goth_28_h && value_w >= 60) return fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
        if (value_h >= goth_24_h && value_w >= 44) return fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
        if (value_h >= goth_18_h && value_w >= 32) return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
        return fonts_get_system_font(FONT_KEY_GOTHIC_14);
      }
      if (value_h >= goth_24_h && value_w >= 60) return fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
      if (value_h >= goth_18_h && value_w >= 44) return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
      return fonts_get_system_font(FONT_KEY_GOTHIC_14);

    case TILE_WEATHER:
      // Mixed text values can run long, so cap at GOTHIC_18 and stay conservative
      // on the smallest platforms to avoid clipping.
#if defined(PBL_PLATFORM_CHALK) || defined(PBL_PLATFORM_FLINT)
      return fonts_get_system_font(FONT_KEY_GOTHIC_14);
#else
      if (value_h >= goth_18_h && value_w >= 50) return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
      return fonts_get_system_font(FONT_KEY_GOTHIC_14);
#endif

    default:
      // Other text strings
      if (value_h >= goth_28_h && value_w >= 78) return fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
      if (value_h >= goth_24_h && value_w >= 60) return fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
      if (value_h >= goth_18_h && value_w >= 44) return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
      return fonts_get_system_font(FONT_KEY_GOTHIC_14);
  }
}

// ============================================================================
// TILE CONTENT
// ============================================================================

static const char *prv_tile_label(uint8_t type) {
  const char *city = prv_weather_city_label();

  switch (type) {
    case TILE_TIME:       return "TIME";
    case TILE_DATE:       return "DATE";
    case TILE_DAY:        return "DOW";
    case TILE_YEAR:       return "YEAR";
    case TILE_HEART_RATE: return "BPM";
    case TILE_STEPS:      return "STEPS";
    case TILE_WEATHER:    return city ? city : "WX";
    case TILE_BATTERY:    return "BAT";
    case TILE_TEMPERATURE:return city ? city : "TEMP";
    case TILE_PRECIPITATION:return city ? city : "RAIN";
    default:              return "";
  }
}

static bool prv_is_weather_tile(uint8_t type) {
  return type == TILE_WEATHER ||
         type == TILE_TEMPERATURE ||
         type == TILE_PRECIPITATION;
}

static bool prv_show_bluetooth_disconnected_value(uint8_t type) {
  return !s_bt_connected && prv_is_weather_tile(type);
}

static void prv_tile_value(uint8_t type, char *buf, size_t buf_size) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  if (prv_show_bluetooth_disconnected_value(type)) {
    snprintf(buf, buf_size, "X");
    return;
  }

  switch (type) {
    case TILE_TIME:
      strftime(buf, buf_size, clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
      break;
    case TILE_DATE:
      switch (s_settings.date_format) {
        case 1:  // DD/MM
          strftime(buf, buf_size, "%d/%m", t);
          break;
        case 2:  // MM月DD日
          snprintf(buf, buf_size, "%d\xe6\x9c\x88%d\xe6\x97\xa5",
                   t->tm_mon + 1, t->tm_mday);
          break;
        default:  // 0 = MM/DD
          strftime(buf, buf_size, "%m/%d", t);
          break;
      }
      break;
    case TILE_DAY:
      strftime(buf, buf_size, "%a", t);
      break;
    case TILE_YEAR:
      strftime(buf, buf_size, "%Y", t);
      break;
    case TILE_STEPS:
      if (s_steps >= 0) {
        snprintf(buf, buf_size, "%d", (int)s_steps);
      } else {
        snprintf(buf, buf_size, "--");
      }
      break;
    case TILE_HEART_RATE:
      if (s_heart_rate > 0) {
        snprintf(buf, buf_size, "%d", (int)s_heart_rate);
      } else {
        snprintf(buf, buf_size, "--");
      }
      break;
    case TILE_BATTERY:
      if (s_battery_pct >= 0) {
        snprintf(buf, buf_size, "%d%%", (int)s_battery_pct);
      } else {
        snprintf(buf, buf_size, "--");
      }
      break;
    case TILE_WEATHER:
      if (s_weather.conditions[0] != '\0') {
        snprintf(buf, buf_size, "%s", s_weather.conditions);
      } else {
        snprintf(buf, buf_size, "...");
      }
      break;
    case TILE_TEMPERATURE:
      if (s_weather.temperature > -100) {
        int temp_c = (int)s_weather.temperature;
        int temp_display = (s_settings.temp_unit == 1)
                           ? (temp_c * 9 / 5 + 32) : temp_c;
        const char *unit = (s_settings.temp_unit == 1) ? "\u00b0F" : "\u00b0C";
        snprintf(buf, buf_size, "%d%s", temp_display, unit);
      } else {
        snprintf(buf, buf_size, "...");
      }
      break;
    case TILE_PRECIPITATION:
      if (s_weather.precipitation >= 0) {
        snprintf(buf, buf_size, "%d%%", (int)s_weather.precipitation);
      } else {
        snprintf(buf, buf_size, "...");
      }
      break;
    default:
      buf[0] = '\0';
      break;
  }
}

static void prv_draw_icon_value(GContext *ctx, GRect rect,
                                const char *value_buf, GFont value_font,
                                GBitmap *bitmap) {
  GSize text_size = graphics_text_layout_get_content_size(
      value_buf, value_font, rect, GTextOverflowModeTrailingEllipsis,
      GTextAlignmentLeft);
  GRect icon_bounds = bitmap ? gbitmap_get_bounds(bitmap) : GRect(0, 0, 0, 0);
  int icon_h = icon_bounds.size.h;
  int icon_w = icon_bounds.size.w;
  if (icon_h > 0 && icon_w > 0) {
    int target_h = icon_bounds.size.h + 2;
    if (target_h > rect.size.h) {
      target_h = rect.size.h;
    }
    if (target_h < icon_bounds.size.h) {
      target_h = icon_bounds.size.h;
    }
    icon_h = target_h;
    icon_w = (icon_bounds.size.w * icon_h) / icon_bounds.size.h;
    if (icon_w < icon_bounds.size.w) {
      icon_w = icon_bounds.size.w;
    }
  }
  int gap = 3;
  int group_w = icon_w + gap + text_size.w;
  int start_x = rect.origin.x + (rect.size.w - group_w) / 2;
  if (start_x < rect.origin.x) {
    start_x = rect.origin.x;
  }

  int icon_y = rect.origin.y + (rect.size.h - icon_h) / 2;
#if defined(PBL_PLATFORM_EMERY)
  if (icon_h > 0 && rect.size.h > icon_h + 8) {
    icon_y -= 8;
  }
#endif
  if (bitmap) {
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, bitmap, GRect(start_x, icon_y, icon_w, icon_h));
    graphics_context_set_compositing_mode(ctx, GCompOpAssign);
  }

  GRect text_rect = GRect(start_x + icon_w + gap, rect.origin.y,
                          rect.size.w - (start_x - rect.origin.x) - icon_w - gap,
                          rect.size.h);
  graphics_draw_text(ctx, value_buf, value_font, text_rect,
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void prv_draw_precipitation_value(GContext *ctx, GRect rect,
                                         const char *value_buf, GFont value_font,
                                         uint8_t bg_argb) {
  prv_draw_icon_value(ctx, rect, value_buf, value_font,
                      prv_umbrella_bitmap_for_bg(bg_argb));
}

static void prv_draw_tile_value(GContext *ctx, const TileRenderData *tile,
                                GRect rect, GFont value_font,
                                GTextOverflowMode overflow,
                                GTextAlignment alignment) {
  if (tile->type == TILE_PRECIPITATION) {
    prv_draw_precipitation_value(ctx, rect, tile->value, value_font, tile->bg_argb);
    return;
  }

  if (prv_show_bluetooth_disconnected_value(tile->type)) {
    prv_draw_icon_value(ctx, rect, tile->value, value_font,
                        prv_bluetooth_bitmap_for_bg(tile->bg_argb));
    return;
  }

  graphics_draw_text(ctx, tile->value, value_font, rect, overflow, alignment, NULL);
}

#ifdef PBL_ROUND
static void prv_draw_round_tile(GContext *ctx, const TileRenderData *tile,
                                int idx, GRect bounds) {
  static const int PAD = 3;
  static const int k_label_nudge[] = { 5, 1, 1 };
  static const int k_bottom_nudge[] = { 5, -2, -5 };
  static const int k_value_gap[] = { 0, 0, 0 };
  int label_h = prv_label_height();

  int radius = bounds.size.w / 2;
  int row = idx / 2;
  int col = idx % 2;
  int label_y = tile->bg.origin.y + PAD + k_label_nudge[row];
  int tile_bottom = tile->bg.origin.y + tile->bg.size.h - PAD + k_bottom_nudge[row];
  int tile_left = tile->bg.origin.x;
  int tile_right = tile->bg.origin.x + tile->bg.size.w;

  if (label_y >= tile_bottom) {
    return;
  }

  int value_top = label_y + label_h + k_value_gap[row];
  int value_max_h = tile_bottom - value_top;
  bool show_label = (value_max_h >= 10);

  if (show_label) {
    GRect ref_bg = prv_tile_rect(col, bounds);
    int ref_label_y = ref_bg.origin.y + PAD + k_label_nudge[0];
    int label_left_top, label_right_top, label_left_bottom, label_right_bottom;
    prv_circle_safe_x(ref_label_y, radius, PAD, &label_left_top, &label_right_top);
    prv_circle_safe_x(ref_label_y + label_h - 2, radius, PAD,
                      &label_left_bottom, &label_right_bottom);

    int label_left = (label_left_top > label_left_bottom) ? label_left_top : label_left_bottom;
    int label_right = (label_right_top < label_right_bottom) ? label_right_top : label_right_bottom;
    if (label_left < tile_left + PAD) {
      label_left = tile_left + PAD;
    }
    if (label_right > tile_right - PAD) {
      label_right = tile_right - PAD;
    }
    if (label_right > label_left) {
      GRect label_rect = GRect(label_left, label_y, label_right - label_left, label_h - 2);
      graphics_draw_text(ctx, tile->label, s_font_label, label_rect,
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }

    int value_mid_y = value_top + value_max_h / 2;
    int value_left, value_right;
    prv_circle_safe_x(value_mid_y, radius, PAD, &value_left, &value_right);
    if (value_left < tile_left + PAD) {
      value_left = tile_left + PAD;
    }
    if (value_right > tile_right - PAD) {
      value_right = tile_right - PAD;
    }

    if (value_right > value_left) {
      int final_w = value_right - value_left;
      int value_y = value_top;
      GFont value_font = prv_tile_font(tile->type, value_max_h, final_w);
      value_font = prv_gabbro_round_edge_font(tile->type, row, value_font);
      GSize value_size = graphics_text_layout_get_content_size(
          tile->value, value_font, GRect(0, 0, final_w, value_max_h),
          GTextOverflowModeWordWrap, GTextAlignmentCenter);

      int value_h = value_size.h;
      if (value_h <= 0 || value_h > value_max_h) {
        value_h = value_max_h;
      }

      if (row == 1 && value_max_h > value_h) {
        value_y = value_top + (value_max_h - value_h) / 2;
      }
      if (row == 0 && tile->type == TILE_STEPS && value_max_h > value_h + 2) {
        value_y += 2;
      }
      if (tile->type == TILE_PRECIPITATION && value_max_h > value_h + 4) {
        value_y += 4;
      }
      if (value_max_h > value_h) {
        int max_offset = value_max_h - value_h;
        int nudge = prv_gabbro_round_value_y_nudge(row);
        int offset = value_y - value_top + nudge;
        if (offset < 0) {
          offset = 0;
        }
        if (offset > max_offset) {
          offset = max_offset;
        }
        value_y = value_top + offset;
      }

      value_y += prv_gabbro_round_edge_y_offset(tile->type, row);
      if (value_y + value_h > tile_bottom) {
        value_y = tile_bottom - value_h;
      }

      int value_x = value_left;
      if (row == 2 && col == 1) {
        value_x -= 8;
      }

      GRect value_rect = GRect(value_x, value_y, final_w, value_h);
      prv_draw_tile_value(ctx, tile, value_rect, value_font,
                          GTextOverflowModeWordWrap, GTextAlignmentCenter);
    }
    return;
  }

  int value_left, value_right;
  prv_circle_safe_x((value_top + tile_bottom) / 2, radius, PAD, &value_left, &value_right);
  if (value_left < tile_left + PAD) {
    value_left = tile_left + PAD;
  }
  if (value_right > tile_right - PAD) {
    value_right = tile_right - PAD;
  }

  int full_h = tile_bottom - value_top;
  if (value_right > value_left && full_h > 0) {
    int value_w = value_right - value_left;
    GRect value_rect = GRect(value_left, value_top, value_w, full_h);
    GFont value_font = prv_tile_font(tile->type, full_h, value_w);
    value_font = prv_gabbro_round_edge_font(tile->type, row, value_font);
    prv_draw_tile_value(ctx, tile, value_rect, value_font,
                        GTextOverflowModeWordWrap, GTextAlignmentCenter);
  }
}
#else
static void prv_draw_rect_tile(GContext *ctx, const TileRenderData *tile) {
  int label_h = prv_label_height();

  if (tile->content.size.w <= 0 || tile->content.size.h <= 0) {
    return;
  }

  if (tile->content.size.h < label_h + 10) {
    GFont value_font = prv_tile_font(tile->type, tile->content.size.h, tile->content.size.w);
    prv_draw_tile_value(ctx, tile, tile->content, value_font,
                        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter);
    return;
  }

  GRect label_rect = GRect(tile->content.origin.x, tile->content.origin.y,
                           tile->content.size.w, label_h - 2);
  graphics_draw_text(ctx, tile->label, s_font_label, label_rect,
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  int value_y = tile->content.origin.y + label_h;
  int value_h = tile->content.size.h - label_h;
  GRect value_rect = GRect(tile->content.origin.x, value_y, tile->content.size.w, value_h);
  GFont value_font = prv_tile_font(tile->type, value_h, tile->content.size.w);
  prv_draw_tile_value(ctx, tile, value_rect, value_font,
                      GTextOverflowModeWordWrap, GTextAlignmentCenter);
}
#endif

// ============================================================================
// CANVAS DRAW
// ============================================================================

static void prv_canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  // Fill entire window black (gaps + round-display arc corners)
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  for (int i = 0; i < NUM_TILES; i++) {
    TileConfig *tile = &s_settings.tiles[i];
    if (tile->type == TILE_NONE) continue;

    TileRenderData render_data = prv_make_tile_render_data(i, bounds);

    // Draw tile background
    graphics_context_set_fill_color(ctx, prv_color(render_data.bg_argb));
    graphics_fill_rect(ctx, render_data.bg, 0, GCornerNone);

    graphics_context_set_text_color(ctx, prv_color(render_data.fg_argb));

#ifdef PBL_ROUND
    prv_draw_round_tile(ctx, &render_data, i, bounds);
#else
    prv_draw_rect_tile(ctx, &render_data);
#endif
  }
}

// ============================================================================
// APPMESSAGE
// ============================================================================

static void prv_parse_weather_conditions_payload(const char *payload) {
  const char *first_sep = strchr(payload, '|');
  const char *second_sep = first_sep ? strchr(first_sep + 1, '|') : NULL;

  if (!first_sep) {
    s_weather.city[0] = '\0';
    strncpy(s_weather.conditions, payload, sizeof(s_weather.conditions) - 1);
    s_weather.conditions[sizeof(s_weather.conditions) - 1] = '\0';
    s_weather.precipitation = -1;
    return;
  }

  size_t city_len = (size_t)(first_sep - payload);
  if (city_len >= sizeof(s_weather.city)) {
    city_len = sizeof(s_weather.city) - 1;
  }
  memcpy(s_weather.city, payload, city_len);
  s_weather.city[city_len] = '\0';

  if (second_sep) {
    size_t conditions_len = (size_t)(second_sep - (first_sep + 1));
    if (conditions_len >= sizeof(s_weather.conditions)) {
      conditions_len = sizeof(s_weather.conditions) - 1;
    }
    memcpy(s_weather.conditions, first_sep + 1, conditions_len);
    s_weather.conditions[conditions_len] = '\0';
    s_weather.precipitation = (int8_t)atoi(second_sep + 1);
    return;
  }

  strncpy(s_weather.conditions, first_sep + 1, sizeof(s_weather.conditions) - 1);
  s_weather.conditions[sizeof(s_weather.conditions) - 1] = '\0';
  s_weather.precipitation = -1;
}

static bool prv_apply_weather_message(DictionaryIterator *iter) {
  Tuple *temp_t = dict_find(iter, MESSAGE_KEY_TEMPERATURE);
  Tuple *cond_t = dict_find(iter, MESSAGE_KEY_CONDITIONS);

  if (!temp_t && !cond_t) {
    return false;
  }

  if (temp_t) {
    s_weather.temperature = (int8_t)temp_t->value->int32;
  }
  if (cond_t) {
    prv_parse_weather_conditions_payload(cond_t->value->cstring);
  }

  persist_write_data(PERSIST_KEY_WEATHER, &s_weather, sizeof(WeatherData));
  prv_mark_canvas_dirty();
  return true;
}

static bool prv_apply_settings_message(DictionaryIterator *iter) {
  Tuple *first_tile_t = dict_find(iter, MESSAGE_KEY_TILE0_TYPE);
  if (!first_tile_t) {
    return false;
  }

  for (int i = 0; i < NUM_TILES; i++) {
    uint32_t key_type = (uint32_t)(MESSAGE_KEY_TILE0_TYPE + i * 3);
    uint32_t key_bg   = (uint32_t)(MESSAGE_KEY_TILE0_BG   + i * 3);
    uint32_t key_fg   = (uint32_t)(MESSAGE_KEY_TILE0_FG   + i * 3);
    Tuple *type_t = dict_find(iter, key_type);
    Tuple *bg_t   = dict_find(iter, key_bg);
    Tuple *fg_t   = dict_find(iter, key_fg);

    if (type_t) {
      s_settings.tiles[i].type = (uint8_t)type_t->value->int32;
    }
    if (bg_t) {
      s_settings.tiles[i].bg = (uint8_t)(bg_t->value->int32 & 0xFF);
    }
    if (fg_t) {
      s_settings.tiles[i].fg = (uint8_t)(fg_t->value->int32 & 0xFF);
    }
  }

  Tuple *date_fmt_t  = dict_find(iter, MESSAGE_KEY_DATE_FORMAT);
  Tuple *temp_unit_t = dict_find(iter, MESSAGE_KEY_TEMP_UNIT);
  Tuple *bt_vibe_t   = dict_find(iter, MESSAGE_KEY_BLUETOOTH_DISCONNECT_VIBE);
  if (date_fmt_t) {
    s_settings.date_format = (uint8_t)date_fmt_t->value->int32;
  }
  if (temp_unit_t) {
    s_settings.temp_unit = (uint8_t)temp_unit_t->value->int32;
  }
  if (bt_vibe_t) {
    s_settings.bluetooth_disconnect_vibe = (uint8_t)(bt_vibe_t->value->int32 ? 1 : 0);
  }

  prv_settings_sanitize();
  prv_settings_save();
  prv_mark_canvas_dirty();
  return true;
}

static void prv_inbox_received(DictionaryIterator *iter, void *context) {
  if (dict_find(iter, MESSAGE_KEY_REQUEST_DEVICE_CAPS)) {
    prv_send_device_caps();
    return;
  }

  if (prv_apply_weather_message(iter)) {
    return;
  }

  prv_apply_settings_message(iter);
}

static void prv_inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", (int)reason);
}

static void prv_outbox_failed(DictionaryIterator *iterator,
                               AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "Outbox failed: %d", (int)reason);
}

// ============================================================================
// TICK HANDLER
// ============================================================================

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  prv_mark_canvas_dirty();
  // Request weather refresh every hour
  if (units_changed & HOUR_UNIT) {
    prv_request_weather();
  }
}

// ============================================================================
// WINDOW LIFECYCLE
// ============================================================================

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_font_label = fonts_get_system_font(
      prv_use_large_label_font() ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_14);
  s_umbrella_black = gbitmap_create_with_resource(RESOURCE_ID_ICON_UMBRELLA);
  s_umbrella_white = gbitmap_create_with_resource(RESOURCE_ID_ICON_UMBRELLA_WHITE);
  s_bluetooth_black = gbitmap_create_with_resource(RESOURCE_ID_ICON_BLUETOOTH);
  s_bluetooth_white = gbitmap_create_with_resource(RESOURCE_ID_ICON_BLUETOOTH_WHITE);

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, prv_canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);
}

static void prv_window_unload(Window *window) {
  if (s_umbrella_black) {
    gbitmap_destroy(s_umbrella_black);
    s_umbrella_black = NULL;
  }
  if (s_umbrella_white) {
    gbitmap_destroy(s_umbrella_white);
    s_umbrella_white = NULL;
  }
  if (s_bluetooth_black) {
    gbitmap_destroy(s_bluetooth_black);
    s_bluetooth_black = NULL;
  }
  if (s_bluetooth_white) {
    gbitmap_destroy(s_bluetooth_white);
    s_bluetooth_white = NULL;
  }
  layer_destroy(s_canvas_layer);
  s_canvas_layer = NULL;
}

// ============================================================================
// APPLICATION LIFECYCLE
// ============================================================================

static void prv_init(void) {
  prv_settings_load();
  prv_weather_load_cached();
  prv_battery_update();
  prv_health_update();
  s_bt_connected = bluetooth_connection_service_peek();

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
  battery_state_service_subscribe(prv_battery_handler);
  bluetooth_connection_service_subscribe(prv_bluetooth_handler);

#if defined(PBL_HEALTH)
  health_service_events_subscribe(prv_health_event_handler, NULL);
#endif

  app_message_register_inbox_received(prv_inbox_received);
  app_message_register_inbox_dropped(prv_inbox_dropped);
  app_message_register_outbox_failed(prv_outbox_failed);
  app_message_open(512, 64);

  // Request initial weather fetch
  prv_request_weather();
}

static void prv_deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  bluetooth_connection_service_unsubscribe();
#if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
#endif
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
