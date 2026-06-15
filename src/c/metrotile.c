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
#define SETTINGS_VERSION     11
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
  TILE_WEATHER_ICON = 11,
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
  uint8_t    date_format;  // 0=MM/DD, 1=DD/MM
  uint8_t    temp_unit;    // 0=Celsius, 1=Fahrenheit
  uint8_t    bluetooth_disconnect_vibe;  // 0=OFF, 1=ON
  uint8_t    icon_hidden_mask;  // bit i = tile i's icon hidden (icon+value tiles only)
} Settings;  // 1 + 6*3 + 4 = 23 bytes

typedef struct __attribute__((packed)) {
  uint8_t    version;
  TileConfig tiles[NUM_TILES];
  uint8_t    date_format;
  uint8_t    temp_unit;
} LegacySettingsV6;

typedef struct __attribute__((packed)) {
  uint8_t    version;
  TileConfig tiles[NUM_TILES];
  uint8_t    date_format;
  uint8_t    temp_unit;
  uint8_t    bluetooth_disconnect_vibe;
} LegacySettingsV7;

typedef struct __attribute__((packed)) {
  uint8_t    version;
  TileConfig tiles[NUM_TILES];
  uint8_t    date_format;
  uint8_t    temp_unit;
  uint8_t    bluetooth_disconnect_vibe;
  uint8_t    non_time_large_fonts;
} LegacySettingsV8;

typedef struct __attribute__((packed)) {
  uint8_t    version;
  TileConfig tiles[NUM_TILES];
  uint8_t    date_format;
  uint8_t    temp_unit;
  uint8_t    bluetooth_disconnect_vibe;
  uint8_t    label_visible_mask;
} LegacySettingsV9;

typedef struct __attribute__((packed)) {
  uint8_t    version;
  TileConfig tiles[NUM_TILES];
  uint8_t    date_format;
  uint8_t    temp_unit;
  uint8_t    bluetooth_disconnect_vibe;
} LegacySettingsV10;

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
static bool         s_battery_charging = false;
static bool         s_bt_connected = false;
static GBitmap     *s_bluetooth_black;
static GBitmap     *s_bluetooth_white;
static GBitmap     *s_heart_black;
static GBitmap     *s_heart_white;
static GBitmap     *s_foot_black;
static GBitmap     *s_foot_white;
static GBitmap     *s_battery_30_black;
static GBitmap     *s_battery_30_white;
static GBitmap     *s_battery_50_black;
static GBitmap     *s_battery_50_white;
static GBitmap     *s_battery_80_black;
static GBitmap     *s_battery_80_white;
static GBitmap     *s_battery_full_black;
static GBitmap     *s_battery_full_white;
static GBitmap     *s_battery_charging_empty_black;
static GBitmap     *s_battery_charging_empty_white;
static GBitmap     *s_battery_charging_full_black;
static GBitmap     *s_battery_charging_full_white;
static GBitmap     *s_weather_cloud_falling_black;
static GBitmap     *s_weather_cloud_falling_white;
static GBitmap     *s_weather_2x_cloud_falling_black;
static GBitmap     *s_weather_2x_cloud_falling_white;
static GBitmap     *s_weather_2x_cloudy_black;
static GBitmap     *s_weather_2x_cloudy_white;
static GBitmap     *s_weather_2x_foggy_black;
static GBitmap     *s_weather_2x_foggy_white;
static GBitmap     *s_weather_2x_partly_cloudy_black;
static GBitmap     *s_weather_2x_partly_cloudy_white;
static GBitmap     *s_weather_2x_rainy_black;
static GBitmap     *s_weather_2x_rainy_white;
static GBitmap     *s_weather_2x_snowy_black;
static GBitmap     *s_weather_2x_snowy_white;
static GBitmap     *s_weather_2x_sunny_black;
static GBitmap     *s_weather_2x_sunny_white;
static GBitmap     *s_weather_2x_thundery_black;
static GBitmap     *s_weather_2x_thundery_white;
typedef struct {
  GRect bg;
  GRect content;
  char value[48];
  const char *label;
  uint8_t bg_argb;
  uint8_t fg_argb;
  uint8_t type;
  bool    icon_hidden;
} TileRenderData;

static const char *prv_tile_label(uint8_t type);
static void prv_tile_value(uint8_t type, char *buf, size_t buf_size);
static void prv_settings_save(void);
static bool prv_is_weather_tile(uint8_t type);
static bool prv_tile_has_icon_value(uint8_t type);

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

static GBitmap *prv_bluetooth_bitmap_for_bg(uint8_t bg_argb) {
  return prv_use_dark_icon(bg_argb) ? s_bluetooth_black : s_bluetooth_white;
}

static GBitmap *prv_heart_bitmap_for_bg(uint8_t bg_argb) {
  return prv_use_dark_icon(bg_argb) ? s_heart_black : s_heart_white;
}

// Unlike the other icons (which pick a variant for contrast against the
// background), the foot icon should visually match the value text color, so
// the variant is chosen from fg_argb rather than bg_argb.
static GBitmap *prv_foot_bitmap_for_fg(uint8_t fg_argb) {
  return prv_use_dark_icon(fg_argb) ? s_foot_white : s_foot_black;
}

// The precipitation icon is rendered beside the percentage text, so it should
// match the value text color rather than contrast with the tile background.
static GBitmap *prv_precipitation_bitmap_for_fg(uint8_t fg_argb) {
  return prv_use_dark_icon(fg_argb) ? s_weather_cloud_falling_white
                                    : s_weather_cloud_falling_black;
}

static GBitmap *prv_weather_icon_variant(uint8_t bg_argb, GBitmap *dark_bitmap,
                                         GBitmap *light_bitmap) {
  return prv_use_dark_icon(bg_argb) ? dark_bitmap : light_bitmap;
}

static GBitmap *prv_weather_bitmap_for_bg(uint8_t bg_argb) {
  const char *condition = s_weather.conditions;

  if (condition[0] == '\0') {
    return prv_weather_icon_variant(bg_argb, s_weather_2x_partly_cloudy_black,
                                    s_weather_2x_partly_cloudy_white);
  }
  if (strcmp(condition, "Clear") == 0) {
    return prv_weather_icon_variant(bg_argb, s_weather_2x_sunny_black, s_weather_2x_sunny_white);
  }
  if (strcmp(condition, "Cloudy") == 0) {
    return prv_weather_icon_variant(bg_argb, s_weather_2x_cloudy_black, s_weather_2x_cloudy_white);
  }
  if (strcmp(condition, "Fog") == 0) {
    return prv_weather_icon_variant(bg_argb, s_weather_2x_foggy_black, s_weather_2x_foggy_white);
  }
  if (strcmp(condition, "Rain") == 0 || strcmp(condition, "Fr.Rain") == 0) {
    return prv_weather_icon_variant(bg_argb, s_weather_2x_rainy_black, s_weather_2x_rainy_white);
  }
  if (strcmp(condition, "Drizzle") == 0 || strcmp(condition, "Fr.Drizzle") == 0 ||
      strcmp(condition, "Showers") == 0) {
    return prv_weather_icon_variant(bg_argb, s_weather_2x_cloud_falling_black,
                                    s_weather_2x_cloud_falling_white);
  }
  if (strcmp(condition, "Snow") == 0 || strcmp(condition, "Sn.Showers") == 0) {
    return prv_weather_icon_variant(bg_argb, s_weather_2x_snowy_black, s_weather_2x_snowy_white);
  }
  if (strcmp(condition, "T-Storm") == 0) {
    return prv_weather_icon_variant(bg_argb, s_weather_2x_thundery_black, s_weather_2x_thundery_white);
  }
  return prv_weather_icon_variant(bg_argb, s_weather_2x_partly_cloudy_black,
                                  s_weather_2x_partly_cloudy_white);
}

static GBitmap *prv_battery_bitmap_for_bg(uint8_t bg_argb) {
  bool use_dark = prv_use_dark_icon(bg_argb);
  GBitmap *charging_empty = use_dark ? s_battery_charging_empty_black : s_battery_charging_empty_white;
  GBitmap *charging_full = use_dark ? s_battery_charging_full_black : s_battery_charging_full_white;

  if (s_battery_charging) {
    return s_battery_pct >= 80 ? charging_full : charging_empty;
  }
  if (s_battery_pct >= 100) {
    return use_dark ? s_battery_full_black : s_battery_full_white;
  }
  if (s_battery_pct >= 80) {
    return use_dark ? s_battery_80_black : s_battery_80_white;
  }
  if (s_battery_pct >= 50) {
    return use_dark ? s_battery_50_black : s_battery_50_white;
  }
  if (s_battery_pct >= 30) {
    return use_dark ? s_battery_30_black : s_battery_30_white;
  }
  return use_dark ? s_battery_30_black : s_battery_30_white;
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
    case TILE_WEATHER_ICON:
      tile->bg = PBL_IF_COLOR_ELSE(GColorCyan, GColorBlack).argb;
      tile->fg = PBL_IF_COLOR_ELSE(GColorBlack, GColorWhite).argb;
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

    if (tile->type > TILE_WEATHER_ICON) {
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
    // Read into the largest known on-disk layout (V9, same size as the
    // current Settings struct) so `n` reflects the actual stored size.
    LegacySettingsV9 raw;
    int n = persist_read_data(PERSIST_KEY_SETTINGS, &raw, sizeof(raw));

    if (n == (int)sizeof(Settings) && raw.version == SETTINGS_VERSION) {
      memcpy(&s_settings, &raw, sizeof(Settings));
      prv_settings_sanitize();
      return;
    }

    if (n == (int)sizeof(LegacySettingsV10) && raw.version == (SETTINGS_VERSION - 1)) {
      LegacySettingsV10 legacy;
      memcpy(&legacy, &raw, sizeof(legacy));

      memset(&s_settings, 0, sizeof(s_settings));
      s_settings.version = SETTINGS_VERSION;
      memcpy(s_settings.tiles, legacy.tiles, sizeof(legacy.tiles));
      s_settings.date_format = legacy.date_format;
      s_settings.temp_unit = legacy.temp_unit;
      s_settings.bluetooth_disconnect_vibe = legacy.bluetooth_disconnect_vibe;
      s_settings.icon_hidden_mask = 0;
      prv_settings_sanitize();
      prv_settings_save();
      return;
    }

    if (n == (int)sizeof(LegacySettingsV9) && raw.version == (SETTINGS_VERSION - 2)) {
      memset(&s_settings, 0, sizeof(s_settings));
      s_settings.version = SETTINGS_VERSION;
      memcpy(s_settings.tiles, raw.tiles, sizeof(raw.tiles));
      s_settings.date_format = raw.date_format;
      s_settings.temp_unit = raw.temp_unit;
      s_settings.bluetooth_disconnect_vibe = raw.bluetooth_disconnect_vibe;
      s_settings.icon_hidden_mask = 0;
      prv_settings_sanitize();
      prv_settings_save();
      return;
    }

    if (n == (int)sizeof(LegacySettingsV8) && raw.version == (SETTINGS_VERSION - 3)) {
      LegacySettingsV8 legacy;
      memcpy(&legacy, &raw, sizeof(legacy));

      memset(&s_settings, 0, sizeof(s_settings));
      s_settings.version = SETTINGS_VERSION;
      memcpy(s_settings.tiles, legacy.tiles, sizeof(legacy.tiles));
      s_settings.date_format = legacy.date_format;
      s_settings.temp_unit = legacy.temp_unit;
      s_settings.bluetooth_disconnect_vibe = legacy.bluetooth_disconnect_vibe;
      s_settings.icon_hidden_mask = 0;
      prv_settings_sanitize();
      prv_settings_save();
      return;
    }

    if (n == (int)sizeof(LegacySettingsV7) && raw.version == (SETTINGS_VERSION - 4)) {
      LegacySettingsV7 legacy;
      memcpy(&legacy, &raw, sizeof(legacy));

      memset(&s_settings, 0, sizeof(s_settings));
      s_settings.version = SETTINGS_VERSION;
      memcpy(s_settings.tiles, legacy.tiles, sizeof(legacy.tiles));
      s_settings.date_format = legacy.date_format;
      s_settings.temp_unit = legacy.temp_unit;
      s_settings.bluetooth_disconnect_vibe = legacy.bluetooth_disconnect_vibe;
      s_settings.icon_hidden_mask = 0;
      prv_settings_sanitize();
      prv_settings_save();
      return;
    }

    if (n == (int)sizeof(LegacySettingsV6) && raw.version == (SETTINGS_VERSION - 5)) {
      LegacySettingsV6 legacy;
      memcpy(&legacy, &raw, sizeof(legacy));

      memset(&s_settings, 0, sizeof(s_settings));
      s_settings.version = SETTINGS_VERSION;
      memcpy(s_settings.tiles, legacy.tiles, sizeof(legacy.tiles));
      s_settings.date_format = legacy.date_format;
      s_settings.temp_unit = legacy.temp_unit;
      s_settings.bluetooth_disconnect_vibe = 0;
      s_settings.icon_hidden_mask = 0;
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
  s_battery_charging = state.is_charging;
}

static void prv_battery_handler(BatteryChargeState state) {
  s_battery_pct = state.charge_percent;
  s_battery_charging = state.is_charging;
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
    .icon_hidden = prv_tile_has_icon_value(tile->type) &&
                   (s_settings.icon_hidden_mask & (1 << idx)) != 0,
  };

  data.content = GRect(data.bg.origin.x + PAD, data.bg.origin.y + PAD,
                       data.bg.size.w - 2 * PAD, data.bg.size.h - 2 * PAD);
  prv_tile_value(tile->type, data.value, sizeof(data.value));

  return data;
}

// ============================================================================
// FONT SELECTION
// ============================================================================
//
// Value and label fonts are chosen by measuring the natural (unwrapped) size
// of a representative "worst case" string for each tile type and picking the
// largest candidate font (ordered largest -> smallest) whose natural size
// fits the pixel area actually available. This makes font selection
// self-adjusting across platforms/screen sizes and round vs. rectangular
// layouts without per-platform threshold tables.

// Natural (unwrapped) content size of `text` rendered in `font`.
static GSize prv_text_natural_size(const char *text, GFont font) {
  return graphics_text_layout_get_content_size(
      text, font, GRect(0, 0, 1000, 200), GTextOverflowModeFill, GTextAlignmentLeft);
}

// Height of a single line of text in `font`.
static int prv_font_line_height(GFont font) {
  return prv_text_natural_size("Ag", font).h;
}

// Largest font from `candidates` (ordered largest -> smallest) whose natural
// rendering of `text` fits within avail_w x avail_h. Falls back to the
// smallest candidate if none fit.
static GFont prv_fit_font(const char *text, int avail_w, int avail_h,
                          const char *const *candidates, int n) {
  for (int i = 0; i < n; i++) {
    GFont font = fonts_get_system_font(candidates[i]);
    GSize size = prv_text_natural_size(text, font);
    if (size.w <= avail_w && size.h <= avail_h) {
      return font;
    }
  }
  return fonts_get_system_font(candidates[n - 1]);
}

// Total width consumed by an icon_w x icon_h bitmap drawn at up to
// (rect_h - 4)px tall (matching prv_draw_icon_value / prv_draw_precipitation_value),
// plus the gap before the adjacent text.
static int prv_icon_display_width(int icon_w, int icon_h, int rect_h, int gap) {
  int max_h = rect_h - 4;
  if (max_h < 1) {
    max_h = rect_h;
  }
  if (icon_h > max_h && max_h > 0) {
    icon_w = (icon_w * max_h) / icon_h;
  }
  return icon_w + gap;
}

// Candidate fonts, ordered largest -> smallest. The trailing Gothic entries
// are fallbacks for icon+text tiles (e.g. STEPS "icon + 99999") on narrow
// round tiles where even the smallest LECO numeric font would overflow.
static const char *const FONT_CANDIDATES_NUMERIC[] = {
  FONT_KEY_LECO_42_NUMBERS,
  FONT_KEY_LECO_38_BOLD_NUMBERS,
  FONT_KEY_LECO_36_BOLD_NUMBERS,
  FONT_KEY_LECO_32_BOLD_NUMBERS,
  FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM,
  FONT_KEY_LECO_20_BOLD_NUMBERS,
  FONT_KEY_GOTHIC_18_BOLD,
  FONT_KEY_GOTHIC_14_BOLD,
};

// LECO fonts whose character set includes '%', followed by Gothic fallbacks
// for screens too narrow to fit "100%" (plus an icon) even at LECO_26.
static const char *const FONT_CANDIDATES_PERCENT[] = {
  FONT_KEY_LECO_36_BOLD_NUMBERS,
  FONT_KEY_LECO_32_BOLD_NUMBERS,
  FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM,
  FONT_KEY_GOTHIC_24_BOLD,
  FONT_KEY_GOTHIC_18_BOLD,
  FONT_KEY_GOTHIC_14_BOLD,
  FONT_KEY_GOTHIC_14,
  FONT_KEY_GOTHIC_09,
};

static const char *const FONT_CANDIDATES_TEXT[] = {
  FONT_KEY_GOTHIC_28_BOLD,
  FONT_KEY_GOTHIC_24_BOLD,
  FONT_KEY_GOTHIC_18_BOLD,
  FONT_KEY_GOTHIC_14_BOLD,
  FONT_KEY_GOTHIC_14,
};

static const char *const FONT_CANDIDATES_LABEL[] = {
  FONT_KEY_GOTHIC_18_BOLD,
  FONT_KEY_GOTHIC_14,
};

#define FONT_CANDIDATES_COUNT(arr) (int)(sizeof(arr) / sizeof((arr)[0]))

// Default (non-city) label text per tile type. Used both for display and for
// sizing the label font.
static const char *prv_label_default_text(uint8_t type) {
  switch (type) {
    case TILE_TIME:          return "TIME";
    case TILE_DATE:          return "DATE";
    case TILE_DAY:           return "DOW";
    case TILE_YEAR:          return "YEAR";
    case TILE_HEART_RATE:    return "BPM";
    case TILE_STEPS:         return "STEPS";
    case TILE_WEATHER:       return "WX";
    case TILE_BATTERY:       return "BAT";
    case TILE_TEMPERATURE:   return "TEMP";
    case TILE_PRECIPITATION: return "RAIN";
    case TILE_WEATHER_ICON:  return "WX";
    default:                 return "";
  }
}

// "Worst case" string used to size the value font for each tile type.
static const char *prv_value_size_text(uint8_t type) {
  switch (type) {
    case TILE_TIME:          return "24:59";
    case TILE_DATE:          return "12/31";
    case TILE_DAY:           return "Thu";
    case TILE_YEAR:          return "2999";
    case TILE_HEART_RATE:    return "180";
    case TILE_STEPS:         return "99999";
    case TILE_WEATHER:       return "Sn.Showers";
    case TILE_TEMPERATURE:   return "104°F";
    case TILE_PRECIPITATION: return "100%";
    case TILE_BATTERY:       return "100%";
    case TILE_WEATHER_ICON:  return "X";
    default:                 return "";
  }
}

// Candidate value-font list for each tile type. Numeric tiles use LECO;
// percentages are restricted to the LECO fonts whose charset includes '%';
// text/mixed tiles use Gothic.
static void prv_value_font_candidates(uint8_t type, const char *const **out_candidates,
                                      int *out_n) {
  switch (type) {
    case TILE_TIME:
    case TILE_DATE:
    case TILE_YEAR:
    case TILE_STEPS:
    case TILE_HEART_RATE:
      *out_candidates = FONT_CANDIDATES_NUMERIC;
      *out_n = FONT_CANDIDATES_COUNT(FONT_CANDIDATES_NUMERIC);
      return;
    case TILE_PRECIPITATION:
    case TILE_BATTERY:
      *out_candidates = FONT_CANDIDATES_PERCENT;
      *out_n = FONT_CANDIDATES_COUNT(FONT_CANDIDATES_PERCENT);
      return;
    default:  // DAY, WEATHER, TEMPERATURE, WEATHER_ICON
      *out_candidates = FONT_CANDIDATES_TEXT;
      *out_n = FONT_CANDIDATES_COUNT(FONT_CANDIDATES_TEXT);
      return;
  }
}

// Largest font that fits this tile type's worst-case value string within
// avail_w x avail_h.
static GFont prv_tile_font(uint8_t type, int avail_w, int avail_h) {
  const char *const *candidates;
  int n;
  prv_value_font_candidates(type, &candidates, &n);
  return prv_fit_font(prv_value_size_text(type), avail_w, avail_h, candidates, n);
}

// Largest label font that fits this tile type's label text within avail_w x avail_h.
static GFont prv_label_font(uint8_t type, int avail_w, int avail_h) {
  return prv_fit_font(prv_label_default_text(type), avail_w, avail_h,
                      FONT_CANDIDATES_LABEL, FONT_CANDIDATES_COUNT(FONT_CANDIDATES_LABEL));
}

// Bitmap drawn alongside the value text for icon+text tiles (NULL if `tile`
// has no icon, or its icon has been hidden via per-tile setting).
static GBitmap *prv_tile_icon_bitmap(const TileRenderData *tile) {
  if (tile->icon_hidden) {
    return NULL;
  }

  switch (tile->type) {
    case TILE_HEART_RATE:
      return prv_heart_bitmap_for_bg(tile->bg_argb);
    case TILE_STEPS:
      return prv_foot_bitmap_for_fg(tile->fg_argb);
    case TILE_PRECIPITATION:
      return prv_use_dark_icon(tile->bg_argb) ? s_weather_cloud_falling_black
                                              : s_weather_cloud_falling_white;
    case TILE_BATTERY:
      return prv_battery_bitmap_for_bg(tile->bg_argb);
    default:
      return NULL;
  }
}

// Value font for a tile, accounting for the icon (if any) drawn alongside
// the text within the same rect.
static GFont prv_tile_value_font(const TileRenderData *tile, int avail_w, int avail_h) {
  GBitmap *icon = prv_tile_icon_bitmap(tile);
  if (icon) {
    GRect icon_bounds = gbitmap_get_bounds(icon);
    int gap = (tile->type == TILE_PRECIPITATION) ? 2 : 3;
    avail_w -= prv_icon_display_width(icon_bounds.size.w, icon_bounds.size.h, avail_h, gap);
    if (avail_w < 0) {
      avail_w = 0;
    }
  }
  return prv_tile_font(tile->type, avail_w, avail_h);
}

// ============================================================================
// TILE CONTENT
// ============================================================================

static const char *prv_tile_label(uint8_t type) {
  if (prv_is_weather_tile(type)) {
    const char *city = prv_weather_city_label();
    if (city) {
      return city;
    }
  }
  return prv_label_default_text(type);
}

static bool prv_is_weather_tile(uint8_t type) {
  return type == TILE_WEATHER ||
         type == TILE_TEMPERATURE ||
         type == TILE_PRECIPITATION ||
         type == TILE_WEATHER_ICON;
}

// Tile types that render as an icon next to the value text. Their icon can
// be hidden via the per-tile "icon_hidden_mask" setting to grow the value font.
static bool prv_tile_has_icon_value(uint8_t type) {
  return type == TILE_HEART_RATE ||
         type == TILE_STEPS ||
         type == TILE_BATTERY ||
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
    case TILE_WEATHER_ICON:
      if (s_weather.conditions[0] == '\0') {
        snprintf(buf, buf_size, "...");
      } else {
        buf[0] = '\0';
      }
      break;
    default:
      buf[0] = '\0';
      break;
  }
}

// Vertical position for an icon next to a single line of text.
//
// graphics_draw_text() vertically centers its text within the rect passed to
// it (here, `rect` itself), regardless of the text's natural content height.
// So with middle_align true, the icon's bounding box is centered in `rect`
// too, landing it on the same center line as the text. Icons whose glyph is
// itself centered within its bounding box (e.g. battery) then line up with
// the text's ink, suiting icons that have no baseline of their own and would
// otherwise look like they float above or sink below the text.
//
// With middle_align false, the icon's bottom edge is anchored to
// rect.origin.y + text_size.h, which lands at the text line's baseline
// (bottom of the digits' ink, ignoring descenders), keeping icon and text
// visually aligned regardless of how tall the icon is relative to the font.
//
// Clamped to stay within rect either way.
static int prv_icon_value_y(GRect rect, GSize text_size, int icon_h, bool middle_align) {
  int y = middle_align ? (rect.origin.y + (rect.size.h - icon_h) / 2)
                       : (rect.origin.y + text_size.h - icon_h);
  if (y < rect.origin.y) {
    y = rect.origin.y;
  }
  int max_y = rect.origin.y + rect.size.h - icon_h;
  if (y > max_y) {
    y = max_y;
  }
  return y;
}

static void prv_draw_icon_value(GContext *ctx, GRect rect,
                                const char *value_buf, GFont value_font,
                                GBitmap *bitmap, bool middle_align) {
  GSize text_size = graphics_text_layout_get_content_size(
      value_buf, value_font, rect, GTextOverflowModeTrailingEllipsis,
      GTextAlignmentLeft);
  GRect icon_bounds = bitmap ? gbitmap_get_bounds(bitmap) : GRect(0, 0, 0, 0);
  int icon_h = icon_bounds.size.h;
  int icon_w = icon_bounds.size.w;
  if (icon_h > 0 && icon_w > 0) {
    int max_h = rect.size.h - 4;
    if (max_h < 1) {
      max_h = rect.size.h;
    }
    if (icon_h > max_h && max_h > 0) {
      icon_w = (icon_w * max_h) / icon_h;
      icon_h = max_h;
    }
  }

  // Vertically center the icon+text group as a whole within rect, rather
  // than anchoring the text's top-aligned line at rect's top edge (which
  // leaves the whole group sitting near the top when rect is much taller
  // than the line, e.g. an icon tile with a small value font).
  int group_h = (icon_h > text_size.h) ? icon_h : text_size.h;
  int voffset = (rect.size.h - group_h) / 2;
  if (voffset < 0) {
    voffset = 0;
  }
  GRect group_rect = GRect(rect.origin.x, rect.origin.y + voffset,
                           rect.size.w, rect.size.h - voffset);

  int gap = 3;
  int group_w = icon_w + gap + text_size.w;
  int start_x = rect.origin.x + (rect.size.w - group_w) / 2;
  if (start_x < rect.origin.x) {
    start_x = rect.origin.x;
  }

  int icon_y = prv_icon_value_y(group_rect, text_size, icon_h, middle_align);
  if (bitmap) {
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, bitmap, GRect(start_x, icon_y, icon_w, icon_h));
    graphics_context_set_compositing_mode(ctx, GCompOpAssign);
  }

  GRect text_rect = GRect(start_x + icon_w + gap, group_rect.origin.y,
                          rect.size.w - (start_x - rect.origin.x) - icon_w - gap,
                          group_rect.size.h);
  graphics_draw_text(ctx, value_buf, value_font, text_rect,
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void prv_draw_precipitation_value(GContext *ctx, GRect rect,
                                         const char *value_buf, GFont value_font,
                                         uint8_t bg_argb, uint8_t fg_argb) {
  GSize text_size = graphics_text_layout_get_content_size(
      value_buf, value_font, rect, GTextOverflowModeTrailingEllipsis,
      GTextAlignmentLeft);
  GBitmap *bitmap = prv_precipitation_bitmap_for_fg(fg_argb);
  GRect icon_bounds = bitmap ? gbitmap_get_bounds(bitmap) : GRect(0, 0, 0, 0);
  int icon_h = icon_bounds.size.h;
  int icon_w = icon_bounds.size.w;
  if (icon_bounds.size.h > 0 && icon_bounds.size.w > 0) {
    int max_h = rect.size.h - 4;
    if (max_h < 1) {
      max_h = rect.size.h;
    }
    if (icon_h > max_h && max_h > 0) {
      icon_w = (icon_w * max_h) / icon_h;
      icon_h = max_h;
    }
  }
  int group_h = (icon_h > text_size.h) ? icon_h : text_size.h;
  int voffset = (rect.size.h - group_h) / 2;
  if (voffset < 0) {
    voffset = 0;
  }
  GRect group_rect = GRect(rect.origin.x, rect.origin.y + voffset,
                           rect.size.w, rect.size.h - voffset);

  int gap = 2;
  int group_w = icon_w + gap + text_size.w;
  int start_x = rect.origin.x + (rect.size.w - group_w) / 2;
  if (start_x < rect.origin.x) {
    start_x = rect.origin.x;
  }

  int icon_y = prv_icon_value_y(group_rect, text_size, icon_h, false);
  if (bitmap) {
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, bitmap, GRect(start_x, icon_y, icon_w, icon_h));
    graphics_context_set_compositing_mode(ctx, GCompOpAssign);
  }

  GRect text_rect = GRect(start_x + icon_w + gap, group_rect.origin.y,
                          rect.size.w - (start_x - rect.origin.x) - icon_w - gap,
                          group_rect.size.h);
  graphics_context_set_text_color(ctx, prv_color(fg_argb));
  graphics_draw_text(ctx, value_buf, value_font, text_rect,
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void prv_draw_battery_value(GContext *ctx, GRect rect,
                                   const char *value_buf, GFont value_font,
                                   uint8_t bg_argb) {
  prv_draw_icon_value(ctx, rect, value_buf, value_font,
                      prv_battery_bitmap_for_bg(bg_argb), true);
}

static void prv_draw_weather_value(GContext *ctx, GRect rect, uint8_t bg_argb) {
  GBitmap *bitmap = prv_weather_bitmap_for_bg(bg_argb);
  if (!bitmap) {
    return;
  }

  GRect icon_bounds = gbitmap_get_bounds(bitmap);
  int icon_h = icon_bounds.size.h;
  int icon_w = icon_bounds.size.w;
  if (icon_h <= 0 || icon_w <= 0) {
    return;
  }

  int max_h = rect.size.h - 4;
  if (max_h < 1) {
    max_h = rect.size.h;
  }
  if (icon_h > max_h && max_h > 0) {
    icon_w = (icon_w * max_h) / icon_h;
    icon_h = max_h;
  }

  int icon_x = rect.origin.x + (rect.size.w - icon_w) / 2;
  int icon_y = rect.origin.y + (rect.size.h - icon_h) / 2;
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, bitmap, GRect(icon_x, icon_y, icon_w, icon_h));
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
}

static void prv_draw_tile_value(GContext *ctx, const TileRenderData *tile,
                                GRect rect, GFont value_font,
                                GTextOverflowMode overflow,
                                GTextAlignment alignment) {
  // Icon suppressed via per-tile setting: draw the value text alone, filling
  // the space the icon would otherwise have occupied.
  if (tile->icon_hidden) {
    graphics_draw_text(ctx, tile->value, value_font, rect, overflow, alignment, NULL);
    return;
  }

  if (tile->type == TILE_BATTERY) {
    prv_draw_battery_value(ctx, rect, tile->value, value_font, tile->bg_argb);
    return;
  }

  if (tile->type == TILE_PRECIPITATION) {
    prv_draw_precipitation_value(ctx, rect, tile->value, value_font,
                                 tile->bg_argb, tile->fg_argb);
    return;
  }

  if (tile->type == TILE_HEART_RATE) {
    prv_draw_icon_value(ctx, rect, tile->value, value_font,
                        prv_heart_bitmap_for_bg(tile->bg_argb), false);
    return;
  }

  if (tile->type == TILE_STEPS) {
    prv_draw_icon_value(ctx, rect, tile->value, value_font,
                        prv_foot_bitmap_for_fg(tile->fg_argb), false);
    return;
  }

  if (tile->type == TILE_WEATHER_ICON) {
    if (prv_show_bluetooth_disconnected_value(tile->type)) {
      prv_draw_icon_value(ctx, rect, tile->value, value_font,
                          prv_bluetooth_bitmap_for_bg(tile->bg_argb), false);
      return;
    }
    prv_draw_weather_value(ctx, rect, tile->bg_argb);
    return;
  }

  if (prv_show_bluetooth_disconnected_value(tile->type)) {
    prv_draw_icon_value(ctx, rect, tile->value, value_font,
                        prv_bluetooth_bitmap_for_bg(tile->bg_argb), false);
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

  // Default: no label drawn, value occupies the whole row.
  int value_top = label_y;

  // Label width is taken from row 0's geometry so labels line up across
  // rows regardless of how wide the current row's circular safe area is.
  // The bottom row sits in the wide middle of the circle, so it gets its
  // own (wider) geometry, letting its label use a larger/bolder font.
  GRect ref_bg = (row == 2) ? tile->bg : prv_tile_rect(col, bounds);
  int ref_label_y = (row == 2) ? label_y : ref_bg.origin.y + PAD + k_label_nudge[0];
  int probe_left, probe_right;
  prv_circle_safe_x(ref_label_y, radius, PAD, &probe_left, &probe_right);
  if (probe_left < tile_left + PAD) {
    probe_left = tile_left + PAD;
  }
  if (probe_right > tile_right - PAD) {
    probe_right = tile_right - PAD;
  }

  if (probe_right > probe_left) {
    GFont label_font = prv_label_font(tile->type, probe_right - probe_left, 28);
    int label_h = prv_font_line_height(label_font) + 2;

    if (tile_bottom - (label_y + label_h) >= 10) {
      int label_left_b, label_right_b;
      prv_circle_safe_x(label_y + label_h - 2, radius, PAD, &label_left_b, &label_right_b);
      int label_left = (probe_left > label_left_b) ? probe_left : label_left_b;
      int label_right = (probe_right < label_right_b) ? probe_right : label_right_b;
      if (label_left < tile_left + PAD) {
        label_left = tile_left + PAD;
      }
      if (label_right > tile_right - PAD) {
        label_right = tile_right - PAD;
      }

      if (label_right > label_left) {
        GRect label_rect = GRect(label_left, label_y, label_right - label_left, label_h - 2);
        // Bottom row's label rect spans most of the tile width (its
        // circular safe area is wide), so align toward the inner edge
        // (right for the left tile, left for the right tile) to match
        // the other rows' visual position instead of centering.
        GTextAlignment label_align = (row == 2)
            ? (col == 0 ? GTextAlignmentRight : GTextAlignmentLeft)
            : GTextAlignmentCenter;
        graphics_draw_text(ctx, tile->label, label_font, label_rect,
                           GTextOverflowModeTrailingEllipsis, label_align, NULL);
        value_top = label_y + label_h;
      }
    }
  }

  int value_max_h = tile_bottom - value_top;
  if (value_max_h <= 0) {
    return;
  }

  int value_mid_y = (value_top + tile_bottom) / 2;
  int value_left, value_right;
  prv_circle_safe_x(value_mid_y, radius, PAD, &value_left, &value_right);
  if (value_left < tile_left + PAD) {
    value_left = tile_left + PAD;
  }
  if (value_right > tile_right - PAD) {
    value_right = tile_right - PAD;
  }
  if (row == 2 && col == 1) {
    value_left -= 8;
  }

  if (value_right <= value_left) {
    return;
  }

  int final_w = value_right - value_left;

  // Icon-bearing tiles: the icon-drawing helpers scale their bitmap to
  // (rect.size.h - 4), so size (and fit the font to) a rect tall enough for
  // the icon's native size, rather than the text's natural height. This
  // keeps the font fit and the actual draw rect using the same height, so
  // text doesn't get vertically clipped.
  bool bt_icon = prv_show_bluetooth_disconnected_value(tile->type);
  GBitmap *icon = bt_icon ? prv_bluetooth_bitmap_for_bg(tile->bg_argb)
                          : prv_tile_icon_bitmap(tile);

  int value_h;
  GFont value_font;
  if (tile->type == TILE_WEATHER_ICON && !bt_icon) {
    // Icon-only tile: the "value" text (if any) is just a sizing/placeholder
    // string, not what's drawn, so it occupies the whole available height.
    value_h = value_max_h;
    value_font = prv_tile_value_font(tile, final_w, value_h);
  } else if (icon) {
    int icon_h = gbitmap_get_bounds(icon).size.h + 4;
    value_h = (icon_h < value_max_h) ? icon_h : value_max_h;
    value_font = prv_tile_value_font(tile, final_w, value_h);
  } else {
    value_font = prv_tile_value_font(tile, final_w, value_max_h);
    GSize value_size = graphics_text_layout_get_content_size(
        tile->value, value_font, GRect(0, 0, final_w, value_max_h),
        GTextOverflowModeWordWrap, GTextAlignmentCenter);
    value_h = value_size.h;
    if (value_h <= 0 || value_h > value_max_h) {
      value_h = value_max_h;
    }
  }

  int value_y = value_top + (value_max_h - value_h) / 2;
  GRect value_rect = GRect(value_left, value_y, final_w, value_h);
  prv_draw_tile_value(ctx, tile, value_rect, value_font,
                      GTextOverflowModeWordWrap, GTextAlignmentCenter);
}
#else
static void prv_draw_rect_tile(GContext *ctx, const TileRenderData *tile) {
  GRect content = tile->content;
  if (content.size.w <= 0 || content.size.h <= 0) {
    return;
  }

  GFont label_font = prv_label_font(tile->type, content.size.w, content.size.h);
  int label_h = prv_font_line_height(label_font) + 2;

  if (content.size.h - label_h >= 10) {
    GRect label_rect = GRect(content.origin.x, content.origin.y, content.size.w, label_h - 2);
    graphics_draw_text(ctx, tile->label, label_font, label_rect,
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    int value_y = content.origin.y + label_h;
    int value_h = content.size.h - label_h;
    GRect value_rect = GRect(content.origin.x, value_y, content.size.w, value_h);
    GFont value_font = prv_tile_value_font(tile, content.size.w, value_h);
    prv_draw_tile_value(ctx, tile, value_rect, value_font,
                        GTextOverflowModeWordWrap, GTextAlignmentCenter);
    return;
  }

  GFont value_font = prv_tile_value_font(tile, content.size.w, content.size.h);
  prv_draw_tile_value(ctx, tile, content, value_font,
                      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter);
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
  Tuple *icon_hidden_t = dict_find(iter, MESSAGE_KEY_ICON_HIDDEN_MASK);
  if (date_fmt_t) {
    s_settings.date_format = (uint8_t)date_fmt_t->value->int32;
  }
  if (temp_unit_t) {
    s_settings.temp_unit = (uint8_t)temp_unit_t->value->int32;
  }
  if (bt_vibe_t) {
    s_settings.bluetooth_disconnect_vibe = (uint8_t)(bt_vibe_t->value->int32 ? 1 : 0);
  }
  if (icon_hidden_t) {
    s_settings.icon_hidden_mask = (uint8_t)(icon_hidden_t->value->int32 & 0xFF);
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

  s_bluetooth_black = gbitmap_create_with_resource(RESOURCE_ID_ICON_BLUETOOTH);
  s_bluetooth_white = gbitmap_create_with_resource(RESOURCE_ID_ICON_BLUETOOTH_WHITE);
  s_heart_black = gbitmap_create_with_resource(RESOURCE_ID_ICON_HEART_BLACK);
  s_heart_white = gbitmap_create_with_resource(RESOURCE_ID_ICON_HEART);
  s_foot_black = gbitmap_create_with_resource(RESOURCE_ID_ICON_FOOT_BLACK);
  s_foot_white = gbitmap_create_with_resource(RESOURCE_ID_ICON_FOOT);
  s_battery_30_black = gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_30_BLACK);
  s_battery_30_white = gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_30);
  s_battery_50_black = gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_50_BLACK);
  s_battery_50_white = gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_50);
  s_battery_80_black = gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_80_BLACK);
  s_battery_80_white = gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_80);
  s_battery_full_black = gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_FULL_BLACK);
  s_battery_full_white = gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_FULL);
  s_battery_charging_empty_black =
      gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_CHARGING_EMPTY_BLACK);
  s_battery_charging_empty_white =
      gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_CHARGING_EMPTY);
  s_battery_charging_full_black =
      gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_CHARGING_FULL_BLACK);
  s_battery_charging_full_white =
      gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY_CHARGING_FULL);
  s_weather_cloud_falling_black =
      gbitmap_create_with_resource(RESOURCE_ID_ICON_WEATHER_CLOUD_FALLING_BLACK);
  s_weather_cloud_falling_white =
      gbitmap_create_with_resource(RESOURCE_ID_ICON_WEATHER_CLOUD_FALLING);
  s_weather_2x_cloud_falling_black =
      gbitmap_create_with_resource(RESOURCE_ID_ICON_WEATHER_2X_CLOUD_FALLING_BLACK);
  s_weather_2x_cloud_falling_white =
      gbitmap_create_with_resource(RESOURCE_ID_ICON_WEATHER_2X_CLOUD_FALLING);
  s_weather_2x_cloudy_black =
      gbitmap_create_with_resource(RESOURCE_ID_ICON_WEATHER_2X_CLOUDY_BLACK);
  s_weather_2x_cloudy_white =
      gbitmap_create_with_resource(RESOURCE_ID_ICON_WEATHER_2X_CLOUDY);
  s_weather_2x_foggy_black =
      gbitmap_create_with_resource(RESOURCE_ID_ICON_WEATHER_2X_FOGGY_BLACK);
  s_weather_2x_foggy_white =
      gbitmap_create_with_resource(RESOURCE_ID_ICON_WEATHER_2X_FOGGY);
  s_weather_2x_partly_cloudy_black =
      gbitmap_create_with_resource(RESOURCE_ID_ICON_WEATHER_2X_PARTLY_CLOUDY_BLACK);
  s_weather_2x_partly_cloudy_white =
      gbitmap_create_with_resource(RESOURCE_ID_ICON_WEATHER_2X_PARTLY_CLOUDY);
  s_weather_2x_rainy_black =
      gbitmap_create_with_resource(RESOURCE_ID_ICON_WEATHER_2X_RAINY_BLACK);
  s_weather_2x_rainy_white =
      gbitmap_create_with_resource(RESOURCE_ID_ICON_WEATHER_2X_RAINY);
  s_weather_2x_snowy_black =
      gbitmap_create_with_resource(RESOURCE_ID_ICON_WEATHER_2X_SNOWY_BLACK);
  s_weather_2x_snowy_white =
      gbitmap_create_with_resource(RESOURCE_ID_ICON_WEATHER_2X_SNOWY);
  s_weather_2x_sunny_black =
      gbitmap_create_with_resource(RESOURCE_ID_ICON_WEATHER_2X_SUNNY_BLACK);
  s_weather_2x_sunny_white =
      gbitmap_create_with_resource(RESOURCE_ID_ICON_WEATHER_2X_SUNNY);
  s_weather_2x_thundery_black =
      gbitmap_create_with_resource(RESOURCE_ID_ICON_WEATHER_2X_THUNDERY_BLACK);
  s_weather_2x_thundery_white =
      gbitmap_create_with_resource(RESOURCE_ID_ICON_WEATHER_2X_THUNDERY);

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, prv_canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);
}

static void prv_window_unload(Window *window) {
  if (s_bluetooth_black) {
    gbitmap_destroy(s_bluetooth_black);
    s_bluetooth_black = NULL;
  }
  if (s_bluetooth_white) {
    gbitmap_destroy(s_bluetooth_white);
    s_bluetooth_white = NULL;
  }
  if (s_heart_black) {
    gbitmap_destroy(s_heart_black);
    s_heart_black = NULL;
  }
  if (s_heart_white) {
    gbitmap_destroy(s_heart_white);
    s_heart_white = NULL;
  }
  if (s_foot_black) {
    gbitmap_destroy(s_foot_black);
    s_foot_black = NULL;
  }
  if (s_foot_white) {
    gbitmap_destroy(s_foot_white);
    s_foot_white = NULL;
  }
  if (s_battery_30_black) {
    gbitmap_destroy(s_battery_30_black);
    s_battery_30_black = NULL;
  }
  if (s_battery_30_white) {
    gbitmap_destroy(s_battery_30_white);
    s_battery_30_white = NULL;
  }
  if (s_battery_50_black) {
    gbitmap_destroy(s_battery_50_black);
    s_battery_50_black = NULL;
  }
  if (s_battery_50_white) {
    gbitmap_destroy(s_battery_50_white);
    s_battery_50_white = NULL;
  }
  if (s_battery_80_black) {
    gbitmap_destroy(s_battery_80_black);
    s_battery_80_black = NULL;
  }
  if (s_battery_80_white) {
    gbitmap_destroy(s_battery_80_white);
    s_battery_80_white = NULL;
  }
  if (s_battery_full_black) {
    gbitmap_destroy(s_battery_full_black);
    s_battery_full_black = NULL;
  }
  if (s_battery_full_white) {
    gbitmap_destroy(s_battery_full_white);
    s_battery_full_white = NULL;
  }
  if (s_battery_charging_empty_black) {
    gbitmap_destroy(s_battery_charging_empty_black);
    s_battery_charging_empty_black = NULL;
  }
  if (s_battery_charging_empty_white) {
    gbitmap_destroy(s_battery_charging_empty_white);
    s_battery_charging_empty_white = NULL;
  }
  if (s_battery_charging_full_black) {
    gbitmap_destroy(s_battery_charging_full_black);
    s_battery_charging_full_black = NULL;
  }
  if (s_battery_charging_full_white) {
    gbitmap_destroy(s_battery_charging_full_white);
    s_battery_charging_full_white = NULL;
  }
  if (s_weather_cloud_falling_black) {
    gbitmap_destroy(s_weather_cloud_falling_black);
    s_weather_cloud_falling_black = NULL;
  }
  if (s_weather_cloud_falling_white) {
    gbitmap_destroy(s_weather_cloud_falling_white);
    s_weather_cloud_falling_white = NULL;
  }
  if (s_weather_2x_cloud_falling_black) {
    gbitmap_destroy(s_weather_2x_cloud_falling_black);
    s_weather_2x_cloud_falling_black = NULL;
  }
  if (s_weather_2x_cloud_falling_white) {
    gbitmap_destroy(s_weather_2x_cloud_falling_white);
    s_weather_2x_cloud_falling_white = NULL;
  }
  if (s_weather_2x_cloudy_black) {
    gbitmap_destroy(s_weather_2x_cloudy_black);
    s_weather_2x_cloudy_black = NULL;
  }
  if (s_weather_2x_cloudy_white) {
    gbitmap_destroy(s_weather_2x_cloudy_white);
    s_weather_2x_cloudy_white = NULL;
  }
  if (s_weather_2x_foggy_black) {
    gbitmap_destroy(s_weather_2x_foggy_black);
    s_weather_2x_foggy_black = NULL;
  }
  if (s_weather_2x_foggy_white) {
    gbitmap_destroy(s_weather_2x_foggy_white);
    s_weather_2x_foggy_white = NULL;
  }
  if (s_weather_2x_partly_cloudy_black) {
    gbitmap_destroy(s_weather_2x_partly_cloudy_black);
    s_weather_2x_partly_cloudy_black = NULL;
  }
  if (s_weather_2x_partly_cloudy_white) {
    gbitmap_destroy(s_weather_2x_partly_cloudy_white);
    s_weather_2x_partly_cloudy_white = NULL;
  }
  if (s_weather_2x_rainy_black) {
    gbitmap_destroy(s_weather_2x_rainy_black);
    s_weather_2x_rainy_black = NULL;
  }
  if (s_weather_2x_rainy_white) {
    gbitmap_destroy(s_weather_2x_rainy_white);
    s_weather_2x_rainy_white = NULL;
  }
  if (s_weather_2x_snowy_black) {
    gbitmap_destroy(s_weather_2x_snowy_black);
    s_weather_2x_snowy_black = NULL;
  }
  if (s_weather_2x_snowy_white) {
    gbitmap_destroy(s_weather_2x_snowy_white);
    s_weather_2x_snowy_white = NULL;
  }
  if (s_weather_2x_sunny_black) {
    gbitmap_destroy(s_weather_2x_sunny_black);
    s_weather_2x_sunny_black = NULL;
  }
  if (s_weather_2x_sunny_white) {
    gbitmap_destroy(s_weather_2x_sunny_white);
    s_weather_2x_sunny_white = NULL;
  }
  if (s_weather_2x_thundery_black) {
    gbitmap_destroy(s_weather_2x_thundery_black);
    s_weather_2x_thundery_black = NULL;
  }
  if (s_weather_2x_thundery_white) {
    gbitmap_destroy(s_weather_2x_thundery_white);
    s_weather_2x_thundery_white = NULL;
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
