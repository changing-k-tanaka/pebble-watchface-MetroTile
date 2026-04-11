/**
 * Metro UI Watchface — PebbleKit JS
 *
 * Handles:
 *   1. Weather fetch from Open-Meteo API (free, no key required)
 *   2. Configuration page (custom HTML, no Clay) for tile settings
 *   3. Sending settings back to watch via AppMessage
 */

'use strict';

// ============================================================================
// UTILITIES
// ============================================================================

function xhrGet(url, callback) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function() { callback(null, this.responseText); };
  xhr.onerror = function() { callback('xhr error'); };
  xhr.open('GET', url);
  xhr.send();
}

function trimString(value) {
  return (value || '').replace(/^\s+|\s+$/g, '');
}

function base64EncodeUtf8(input) {
  var chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  var bytes = unescape(encodeURIComponent(input));
  var out = '';
  var i;

  for (i = 0; i < bytes.length; i += 3) {
    var b0 = bytes.charCodeAt(i);
    var b1 = (i + 1 < bytes.length) ? bytes.charCodeAt(i + 1) : NaN;
    var b2 = (i + 2 < bytes.length) ? bytes.charCodeAt(i + 2) : NaN;
    var n = (b0 << 16) | ((isNaN(b1) ? 0 : b1) << 8) | (isNaN(b2) ? 0 : b2);

    out += chars.charAt((n >> 18) & 63);
    out += chars.charAt((n >> 12) & 63);
    out += isNaN(b1) ? '=' : chars.charAt((n >> 6) & 63);
    out += isNaN(b2) ? '=' : chars.charAt(n & 63);
  }

  return out;
}

function cityFromTimezone(timezone) {
  var source = trimString(timezone);
  if (!source) return '';
  var parts = source.split('/');
  return trimString(parts[parts.length - 1].replace(/_/g, ' '));
}

function sendWeather(temp, cond, city, precipitation) {
  Pebble.sendAppMessage(
    {
      'TEMPERATURE': temp,
      'CONDITIONS': (city || '') + '|' + cond + '|' + precipitation
    },
    function() { console.log('Weather sent'); },
    function(e) { console.log('Weather send failed: ' + JSON.stringify(e)); }
  );
}

function wmoToCondition(code) {
  if (code === 0)  return 'Clear';
  if (code <= 3)   return 'Cloudy';
  if (code <= 48)  return 'Fog';
  if (code <= 55)  return 'Drizzle';
  if (code <= 57)  return 'Fr.Drizzle';
  if (code <= 65)  return 'Rain';
  if (code <= 67)  return 'Fr.Rain';
  if (code <= 75)  return 'Snow';
  if (code <= 77)  return 'Snow';
  if (code <= 82)  return 'Showers';
  if (code <= 86)  return 'Sn.Showers';
  if (code === 95) return 'T-Storm';
  if (code <= 99)  return 'T-Storm';
  return 'Unknown';
}

// ============================================================================
// WEATHER
// ============================================================================

function fetchWeather() {
  var raw = localStorage.getItem(SETTINGS_KEY);
  var parsed = raw ? JSON.parse(raw) : null;
  var requestedCity = trimString(parsed && parsed.weatherCity);

  function fetchForecast(latitude, longitude, cityLabel) {
    var url = 'https://api.open-meteo.com/v1/forecast' +
      '?latitude=' + latitude +
      '&longitude=' + longitude +
      '&current=temperature_2m,weather_code' +
      '&daily=precipitation_probability_max' +
      '&forecast_days=1' +
      '&timezone=auto';
    xhrGet(url, function(err, text) {
      if (err) { console.log('Weather fetch error: ' + err); return; }
      try {
        var data = JSON.parse(text);
        var temp = Math.round(data.current.temperature_2m);
        var cond = wmoToCondition(data.current.weather_code);
        var city = trimString(cityLabel) || cityFromTimezone(data.timezone);
        var precipitation = -1;
        if (data.daily && data.daily.precipitation_probability_max &&
            data.daily.precipitation_probability_max.length > 0 &&
            data.daily.precipitation_probability_max[0] !== null) {
          precipitation = Math.round(data.daily.precipitation_probability_max[0]);
        }
        sendWeather(temp, cond, city, precipitation);
      } catch(ex) {
        console.log('Weather parse error: ' + ex);
      }
    });
  }

  if (requestedCity) {
    var geoUrl = 'https://geocoding-api.open-meteo.com/v1/search?count=1&name=' +
      encodeURIComponent(requestedCity);
    xhrGet(geoUrl, function(err, text) {
      if (err) {
        console.log('City geocode error: ' + err);
        return;
      }
      try {
        var data = JSON.parse(text);
        if (!data.results || !data.results.length) {
          console.log('City not found: ' + requestedCity);
          return;
        }
        var result = data.results[0];
        var city = trimString(result.name) || requestedCity;
        fetchForecast(result.latitude, result.longitude, city);
      } catch(ex) {
        console.log('City geocode parse error: ' + ex);
      }
    });
    return;
  }

  navigator.geolocation.getCurrentPosition(
    function(pos) {
      fetchForecast(pos.coords.latitude, pos.coords.longitude, '');
    },
    function(err) { console.log('Geolocation error: ' + err.message); },
    { timeout: 15000, maximumAge: 300000 }
  );
}

// ============================================================================
// SETTINGS: localStorage persistence
// ============================================================================

var SETTINGS_KEY = 'metro_ui_settings';
var SETTINGS_JS_VERSION = 2;

var DEFAULT_SETTINGS = {
  tiles: [
    { type: 3, bg: 0xC2, fg: 0xFF },
    { type: 10, bg: 0xC9, fg: 0xFF },
    { type: 2, bg: 0xE1, fg: 0xFF },
    { type: 1, bg: 0xE2, fg: 0xFF },
    { type: 8, bg: 0xF4, fg: 0xFF },
    { type: 9, bg: 0xCB, fg: 0xFF }
  ],
  dateFormat: 0,  // 0=MM/DD, 1=DD/MM, 2=MM月DD日
  tempUnit:   0,  // 0=Celsius, 1=Fahrenheit
  bluetoothDisconnectVibe: false,
  weatherCity: '', // blank = current location
  colorPreset: 'default',
  bwPreset: 'default'
};

var BW_BLACK = 0xC0;
var BW_LIGHT_GRAY = 0xEA;
var BW_WHITE = 0xFF;
var COLOR_PRESETS = [
  {
    id: 'default',
    name: 'DEFAULT',
    tiles: [
      { bg: 0xC2, fg: 0xFF },
      { bg: 0xC9, fg: 0xFF },
      { bg: 0xE1, fg: 0xFF },
      { bg: 0xE2, fg: 0xFF },
      { bg: 0xF4, fg: 0xFF },
      { bg: 0xCB, fg: 0xFF }
    ]
  },
  {
    id: 'nishikigoi',
    name: 'NISHIKIGOI',
    tiles: [
      { bg: 0xF0, fg: 0xFF },
      { bg: 0xEB, fg: 0xC0 },
      { bg: 0xFF, fg: 0xC0 },
      { bg: 0xF0, fg: 0xFF },
      { bg: 0xEB, fg: 0xC0 },
      { bg: 0xFF, fg: 0xC0 }
    ]
  },
  {
    id: 'mario',
    name: 'MARIO',
    tiles: [
      { bg: 240, fg: 255 },
      { bg: 200, fg: 255 },
      { bg: 252, fg: 192 },
      { bg: 203, fg: 192 },
      { bg: 195, fg: 253 },
      { bg: 255, fg: 192 }
    ]
  },
  {
    id: 'evangelion',
    name: 'EVANGELION',
    tiles: [
      { bg: 227, fg: 192 },
      { bg: 217, fg: 192 },
      { bg: 244, fg: 192 },
      { bg: 203, fg: 192 },
      { bg: 192, fg: 255 },
      { bg: 227, fg: 192 }
    ]
  },
  {
    id: 'doraemon',
    name: 'DORAEMON',
    tiles: [
      { bg: 203, fg: 192 },
      { bg: 255, fg: 192 },
      { bg: 252, fg: 192 },
      { bg: 240, fg: 255 },
      { bg: 203, fg: 192 },
      { bg: 255, fg: 192 }
    ]
  },
  {
    id: 'moby_dick',
    name: 'MOBY DICK',
    tiles: [
      { bg: 196, fg: 255 },
      { bg: 255, fg: 192 },
      { bg: 255, fg: 192 },
      { bg: 196, fg: 255 },
      { bg: 196, fg: 255 },
      { bg: 255, fg: 192 }
    ]
  },
  {
    id: 'pika',
    name: 'PIKA',
    tiles: [
      { bg: 255, fg: 192 },
      { bg: 252, fg: 192 },
      { bg: 252, fg: 192 },
      { bg: 240, fg: 255 },
      { bg: 208, fg: 255 },
      { bg: 252, fg: 192 }
    ]
  }
];
var BW_PRESETS = [
  {
    id: 'default',
    name: 'DEFAULT',
    tiles: [
      { bg: BW_WHITE, fg: BW_BLACK },
      { bg: BW_BLACK, fg: BW_WHITE },
      { bg: BW_LIGHT_GRAY, fg: BW_BLACK },
      { bg: BW_WHITE, fg: BW_BLACK },
      { bg: BW_BLACK, fg: BW_WHITE },
      { bg: BW_LIGHT_GRAY, fg: BW_BLACK }
    ]
  },
  {
    id: 'contrast',
    name: 'CONTRAST',
    tiles: [
      { bg: BW_BLACK, fg: BW_WHITE },
      { bg: BW_WHITE, fg: BW_BLACK },
      { bg: BW_BLACK, fg: BW_WHITE },
      { bg: BW_WHITE, fg: BW_BLACK },
      { bg: BW_BLACK, fg: BW_WHITE },
      { bg: BW_WHITE, fg: BW_BLACK }
    ]
  },
  {
    id: 'soft',
    name: 'SOFT',
    tiles: [
      { bg: BW_LIGHT_GRAY, fg: BW_BLACK },
      { bg: BW_WHITE, fg: BW_BLACK },
      { bg: BW_BLACK, fg: BW_WHITE },
      { bg: BW_LIGHT_GRAY, fg: BW_BLACK },
      { bg: BW_WHITE, fg: BW_BLACK },
      { bg: BW_BLACK, fg: BW_WHITE }
    ]
  },
  {
    id: 'panda',
    name: 'PANDA',
    tiles: [
      { bg: BW_BLACK, fg: BW_WHITE },
      { bg: BW_WHITE, fg: BW_BLACK },
      { bg: BW_WHITE, fg: BW_BLACK },
      { bg: BW_BLACK, fg: BW_WHITE },
      { bg: BW_BLACK, fg: BW_WHITE },
      { bg: BW_WHITE, fg: BW_BLACK }
    ]
  },
  {
    id: 'black',
    name: 'BLACK',
    tiles: [
      { bg: BW_BLACK, fg: BW_WHITE },
      { bg: BW_BLACK, fg: BW_WHITE },
      { bg: BW_BLACK, fg: BW_WHITE },
      { bg: BW_BLACK, fg: BW_WHITE },
      { bg: BW_BLACK, fg: BW_WHITE },
      { bg: BW_BLACK, fg: BW_WHITE }
    ]
  },
  {
    id: 'asphalt',
    name: 'ASPHALT',
    tiles: [
      { bg: BW_BLACK, fg: BW_WHITE },
      { bg: BW_LIGHT_GRAY, fg: BW_BLACK },
      { bg: BW_LIGHT_GRAY, fg: BW_BLACK },
      { bg: BW_BLACK, fg: BW_WHITE },
      { bg: BW_BLACK, fg: BW_WHITE },
      { bg: BW_LIGHT_GRAY, fg: BW_BLACK }
    ]
  },
  {
    id: 'zebbra',
    name: 'ZEBRA',
    tiles: [
      { bg: BW_BLACK, fg: BW_WHITE },
      { bg: BW_BLACK, fg: BW_WHITE },
      { bg: BW_WHITE, fg: BW_BLACK },
      { bg: BW_WHITE, fg: BW_BLACK },
      { bg: BW_LIGHT_GRAY, fg: BW_BLACK },
      { bg: BW_LIGHT_GRAY, fg: BW_BLACK }
    ]
  },
  {
    id: 'piano',
    name: 'PIANO',
    tiles: [
      { bg: BW_BLACK, fg: BW_WHITE },
      { bg: BW_WHITE, fg: BW_BLACK },
      { bg: BW_WHITE, fg: BW_BLACK },
      { bg: BW_WHITE, fg: BW_BLACK },
      { bg: BW_BLACK, fg: BW_WHITE },
      { bg: BW_WHITE, fg: BW_BLACK }
    ]
  }
];
var BW_PALETTE = [BW_BLACK, BW_LIGHT_GRAY, BW_WHITE];
var sWatchInfo = null;
var sDeviceCaps = {
  isBW: null,
  hasHR: null
};
var sPendingConfigOpen = false;
var sPendingConfigTimer = null;

function getWatchInfo() {
  try {
    var info = Pebble.getActiveWatchInfo();
    sWatchInfo = (info && typeof info === 'object') ? info : null;
  } catch(e) {
    sWatchInfo = null;
  }
  return sWatchInfo || {};
}

function watchInfoField(info, key) {
  return trimString(info && info[key] ? String(info[key]) : '').toLowerCase();
}

function inferredPlatform(info) {
  var platform = watchInfoField(info, 'platform');
  var model = watchInfoField(info, 'model');
  var name = watchInfoField(info, 'name');
  var haystack = [platform, model, name].join(' ');
  var platforms = ['aplite', 'basalt', 'chalk', 'diorite', 'emery', 'flint', 'gabbro'];
  var i;

  for (i = 0; i < platforms.length; i++) {
    if (platform.indexOf(platforms[i]) !== -1) {
      return platforms[i];
    }
  }

  for (i = 0; i < platforms.length; i++) {
    if (haystack.indexOf(platforms[i]) !== -1) {
      return platforms[i];
    }
  }

  if (haystack.indexOf('pebble classic') !== -1 || haystack.indexOf('pebble steel') !== -1) {
    return 'aplite';
  }
  if (haystack.indexOf('pebble 2') !== -1) {
    return 'diorite';
  }
  if (haystack.indexOf('pebble time 2') !== -1) {
    return 'emery';
  }
  if (haystack.indexOf('pebble time round') !== -1) {
    return 'chalk';
  }
  if (haystack.indexOf('pebble round 2') !== -1) {
    return 'gabbro';
  }
  if (haystack.indexOf('pebble time') !== -1) {
    return 'basalt';
  }
  return '';
}

function isEmulatorWatchInfo(info) {
  var platform = watchInfoField(info, 'platform');
  var model = watchInfoField(info, 'model');
  var name = watchInfoField(info, 'name');
  return model.indexOf('qemu') !== -1 ||
         platform.indexOf('qemu') !== -1 ||
         name.indexOf('qemu') !== -1 ||
         model.indexOf('emulator') !== -1 ||
         platform.indexOf('emulator') !== -1 ||
         name.indexOf('emulator') !== -1;
}

function hasResolvedDeviceCaps() {
  return typeof sDeviceCaps.isBW === 'boolean' &&
         typeof sDeviceCaps.hasHR === 'boolean';
}

function defaultTileColors(bw) {
  return bw ?
    [[BW_BLACK, BW_WHITE], [BW_BLACK, BW_WHITE], [BW_LIGHT_GRAY, BW_BLACK],
     [BW_LIGHT_GRAY, BW_BLACK], [BW_WHITE, BW_BLACK], [BW_BLACK, BW_WHITE],
     [BW_WHITE, BW_BLACK], [BW_WHITE, BW_BLACK], [BW_BLACK, BW_WHITE],
     [BW_LIGHT_GRAY, BW_BLACK], [BW_BLACK, BW_WHITE]] :
    [[0xC0, 0xFF], [0xE2, 0xFF], [0xE1, 0xFF],
     [0xC2, 0xFF], [0xC3, 0xFF], [0xCC, 0xFF],
     [0xFC, 0xC0], [0xCF, 0xC0], [0xF4, 0xFF],
     [0xCB, 0xFF], [0xC9, 0xFF]];
}

function defaultTileTypes(hrDevice) {
  return [3, 10, 2, 1, 8, 9];
}

function getPresetList(bw) {
  return bw ? BW_PRESETS : COLOR_PRESETS;
}

function getDefaultPresetId(bw) {
  var presets = getPresetList(bw);
  return presets.length ? presets[0].id : 'default';
}

function getPresetById(bw, presetId) {
  var presets = getPresetList(bw);
  for (var i = 0; i < presets.length; i++) {
    if (presets[i].id === presetId) {
      return presets[i];
    }
  }
  return null;
}

function detectMatchingPresetId(settings, bw) {
  var presets = getPresetList(bw);
  for (var i = 0; i < presets.length; i++) {
    var preset = presets[i];
    var matched = true;
    for (var t = 0; t < 6; t++) {
      if (settings.tiles[t].bg !== preset.tiles[t].bg ||
          settings.tiles[t].fg !== preset.tiles[t].fg) {
        matched = false;
        break;
      }
    }
    if (matched) {
      return preset.id;
    }
  }
  return 'custom';
}

function isBWColor(argb) {
  for (var i = 0; i < BW_PALETTE.length; i++) {
    if (argb === BW_PALETTE[i]) return true;
  }
  return false;
}

function contrastingBWColor(argb) {
  return (argb === BW_WHITE || argb === BW_LIGHT_GRAY) ? BW_BLACK : BW_WHITE;
}

function ensureTileContrast(tile, colors, preserve) {
  var fallback = colors[tile.type] || colors[0];

  if (tile.bg !== tile.fg) return;

  if (preserve === 'fg') {
    if (fallback[0] !== tile.fg) {
      tile.bg = fallback[0];
    } else if (fallback[1] !== tile.fg) {
      tile.bg = fallback[1];
    } else {
      tile.bg = contrastingBWColor(tile.fg);
    }
    return;
  }

  if (fallback[1] !== tile.bg) {
    tile.fg = fallback[1];
  } else if (fallback[0] !== tile.bg) {
    tile.fg = fallback[0];
  } else {
    tile.fg = contrastingBWColor(tile.bg);
  }
}

function normalizeSettings(raw) {
  var bw = isBW();
  var hrDevice = hasHR();
  var colors = defaultTileColors(bw);
  var types = defaultTileTypes(hrDevice);
  var out = {
    tiles: [],
    dateFormat: 0,
    tempUnit: 0,
    bluetoothDisconnectVibe: false,
    colorPreset: getDefaultPresetId(false),
    bwPreset: getDefaultPresetId(true)
  };

  for (var i = 0; i < 6; i++) {
    var src = raw && raw.tiles && raw.tiles[i] ? raw.tiles[i] : {};
    var type = +src.type;
    if (!(type >= 0 && type <= 10)) type = types[i];
    if (!hrDevice && type === 5) type = 8;

    var fallback = colors[type];
    var bg = +src.bg;
    var fg = +src.fg;

    if (bw) {
      if (!isBWColor(bg)) {
        bg = fallback[0];
        fg = fallback[1];
      } else if (!isBWColor(fg)) {
        fg = fallback[1];
      }
    } else {
      if (!(bg >= 0xC0 && bg <= 0xFF)) bg = fallback[0];
      if (!(fg >= 0xC0 && fg <= 0xFF)) fg = fallback[1];
    }

    var tile = { type: type, bg: bg, fg: fg };
    ensureTileContrast(tile, colors);
    out.tiles.push(tile);
  }

  var dateFormat = raw ? +raw.dateFormat : 0;
  out.dateFormat = (dateFormat === 1) ? 1 : 0;

  var tempUnit = raw ? +raw.tempUnit : 0;
  out.tempUnit = (tempUnit === 1) ? 1 : 0;

  out.bluetoothDisconnectVibe = !!(raw && raw.bluetoothDisconnectVibe);

  var weatherCity = raw ? trimString(raw.weatherCity) : '';
  out.weatherCity = weatherCity;
  out.colorPreset = trimString(raw && raw.colorPreset);
  out.bwPreset = trimString(raw && raw.bwPreset);

  if (!out.colorPreset) {
    out.colorPreset = detectMatchingPresetId(out, false);
  }
  if (!out.bwPreset) {
    out.bwPreset = detectMatchingPresetId(out, true);
  }

  if (out.colorPreset !== 'custom' && !getPresetById(false, out.colorPreset)) {
    out.colorPreset = getDefaultPresetId(false);
  }
  if (out.bwPreset !== 'custom' && !getPresetById(true, out.bwPreset)) {
    out.bwPreset = getDefaultPresetId(true);
  }

  // プリセットが 'custom' 以外の場合、プリセット定義の色を再適用する。
  // これにより、定義変更後も古い localStorage キャッシュが残らない。
  if (!bw && out.colorPreset !== 'custom') {
    applyPresetToSettings(out, false, out.colorPreset);
  }
  if (bw && out.bwPreset !== 'custom') {
    applyPresetToSettings(out, true, out.bwPreset);
  }

  return out;
}

function loadSettings() {
  var settings;
  try {
    var raw = localStorage.getItem(SETTINGS_KEY);
    var parsed = raw ? JSON.parse(raw) : null;
    // バージョンが一致しない古いデータはデフォルトにリセットする
    if (parsed && parsed.jsVersion !== SETTINGS_JS_VERSION) {
      parsed = null;
    }
    settings = normalizeSettings(parsed);
    saveSettings(settings);
    return settings;
  } catch(e) {}
  settings = normalizeSettings(DEFAULT_SETTINGS);
  saveSettings(settings);
  return settings;
}

function saveSettings(s) {
  s.jsVersion = SETTINGS_JS_VERSION;
  try { localStorage.setItem(SETTINGS_KEY, JSON.stringify(s)); } catch(e) {}
}

function activePresetId(settings, bw) {
  return bw ? settings.bwPreset : settings.colorPreset;
}

function setActivePresetId(settings, bw, presetId) {
  if (bw) {
    settings.bwPreset = presetId;
  } else {
    settings.colorPreset = presetId;
  }
}

function applyPresetToSettings(settings, bw, presetId) {
  var preset = getPresetById(bw, presetId);
  if (!preset) return false;

  var tileColors = defaultTileColors(bw);
  for (var i = 0; i < 6; i++) {
    settings.tiles[i].bg = preset.tiles[i].bg;
    settings.tiles[i].fg = preset.tiles[i].fg;
    ensureTileContrast(settings.tiles[i], tileColors);
  }

  setActivePresetId(settings, bw, preset.id);
  return true;
}

// ============================================================================
// CONFIG PAGE HTML BUILDER
// ============================================================================

var TILE_TYPE_NAMES = ['None','Time','Date','Day of Week','Year','Heart Rate','Steps','Weather','Battery','Temperature','Precipitation'];

// Detect B&W watch (aplite / diorite)
function isBW() {
  if (typeof sDeviceCaps.isBW === 'boolean') return sDeviceCaps.isBW;
  var info = getWatchInfo();
  var platform = inferredPlatform(info);
  return platform === 'aplite' || platform === 'diorite';
}

// Detect heart rate capable watch (diorite / emery only)
function hasHR() {
  if (typeof sDeviceCaps.hasHR === 'boolean') return sDeviceCaps.hasHR;
  var info = getWatchInfo();
  var platform = inferredPlatform(info);
  return platform === 'diorite' || platform === 'emery';
}

function settingsUseOnlyBWPalette(settings) {
  if (!settings || !settings.tiles) return false;
  for (var i = 0; i < settings.tiles.length; i++) {
    var tile = settings.tiles[i] || {};
    if (!isBWColor(tile.bg) || !isBWColor(tile.fg)) {
      return false;
    }
  }
  return true;
}

// Build config HTML.
// カラーグリッドはブラウザ側JSで動的生成することでHTMLサイズを抑え、
// Chrome に渡す data: URL が ARG_MAX を超えないようにする。
function buildConfigHTML(settings) {
  var h = [];
  var deviceBW = isBW();
  var hrDevice = hasHR();
  var selectedPresetId;

  settings = normalizeSettings(settings);
  var bw = deviceBW || settingsUseOnlyBWPalette(settings);
  var tileColors = defaultTileColors(bw);
  var presets = getPresetList(bw);
  selectedPresetId = activePresetId(settings, bw);

  h.push('<!DOCTYPE html><html><head>');
  h.push('<meta name="viewport" content="width=device-width,initial-scale=1">');
  h.push('<meta charset="utf-8">');
  h.push('<title>Metro Settings</title>');
  h.push('<style>');
  h.push('*{box-sizing:border-box;margin:0;padding:0}');
  h.push('body{background:#1a1a1a;color:#fff;font-family:sans-serif;font-size:14px;padding:12px}');
  h.push('h1{font-size:18px;margin-bottom:14px;color:#0078d7}');
  h.push('.tile{border:1px solid #333;border-radius:4px;padding:10px;margin-bottom:10px}');
  h.push('.tt{font-size:13px;font-weight:bold;color:#aaa;margin-bottom:8px}');
  h.push('.sub{font-size:12px;line-height:1.45;color:#aaa;margin-bottom:10px}');
  h.push('.pv{display:grid;grid-template-columns:1fr 1fr;gap:4px}');
  h.push('.pt{min-height:78px;padding:8px;border-radius:3px;display:flex;flex-direction:column;justify-content:space-between}');
  h.push('.pl{font-size:11px;font-weight:bold;letter-spacing:.04em}');
  h.push('.pvw{font-size:22px;font-weight:bold;line-height:1}');
  h.push('textarea{width:100%;min-height:168px;background:#2a2a2a;color:#fff;border:1px solid #555;padding:8px;border-radius:3px;margin-bottom:10px;font-size:12px;font-family:monospace;resize:vertical}');
  h.push('select{width:100%;background:#2a2a2a;color:#fff;border:1px solid #555;padding:6px;border-radius:3px;margin-bottom:10px;font-size:13px}');
  h.push('input{width:100%;background:#2a2a2a;color:#fff;border:1px solid #555;padding:8px;border-radius:3px;margin-bottom:10px;font-size:13px}');
  h.push('.lbl{font-size:11px;color:#888;margin-bottom:4px}');
  h.push('.sg{display:flex;flex-wrap:wrap;gap:4px;margin-bottom:10px;background:#111;padding:6px;border-radius:3px;cursor:pointer}');
  h.push('i{display:inline-block;width:40px;height:40px;border-radius:2px;border:2px solid transparent;font-style:normal}');
  h.push('i.x{border-color:#fff}');
  h.push('.hint{font-size:12px;line-height:1.4;color:#aaa;margin-top:2px}');
  h.push('.rowbtn{width:100%;background:#444;color:#fff;border:none;padding:10px;border-radius:4px;font-size:13px;cursor:pointer;margin-bottom:8px}');
  h.push('#sb{width:100%;background:#0078d7;color:#fff;border:none;padding:14px;border-radius:4px;font-size:16px;cursor:pointer;margin-top:8px}');
  h.push('</style></head><body>');
  h.push('<h1>METRO UI</h1>');

  // --- インラインスクリプト ---
  // カラーデータ・グリッド生成・クリック処理・保存をすべてブラウザ側で行う。
  // これにより HTML ソース自体は小さく保たれ data: URL のサイズ制限を回避する。
  h.push('<script>');
  h.push('var S=' + JSON.stringify(settings) + ';');
  h.push('var AP=' + JSON.stringify(selectedPresetId) + ';');
  h.push('var PR={};');
  h.push('var PN=["","TIME","DATE","DOW","YEAR","BPM","STEPS","WX","BAT","TEMP","RAIN"];');
  h.push('var PV=["","23:13","04/09","Thu","2026","72","8342","Cloudy","80%","15\\u00b0C","20%"];');
  for (var p = 0; p < presets.length; p++) {
    h.push('PR[' + JSON.stringify(presets[p].id) + ']=' + JSON.stringify(presets[p]) + ';');
  }

  // 64色配列をブラウザ側で生成（B&W機は限定グレースケールのみ）
  h.push('var BW=' + (bw ? 'true' : 'false') + ';');
  h.push('var C=BW?[{a:0xC0,s:"rgb(0,0,0)"},{a:0xEA,s:"rgb(170,170,170)"},{a:0xFF,s:"rgb(255,255,255)"}]:[];');
  h.push('if(!BW)for(var r=0;r<4;r++)for(var g=0;g<4;g++)for(var b=0;b<4;b++)C.push({a:0xC0|(r<<4)|(g<<2)|b,s:"rgb("+r*85+","+g*85+","+b*85+")"});');

  // グリッド生成関数: DOM API を使うことで innerHTML の文字列エスケープを回避
  h.push("function G(eid,gid,sel){var d=document.createElement('div');d.className='sg';d.id='sg_'+gid;d.dataset.gid=gid;C.forEach(function(c){var i=document.createElement('i');i.style.background=c.s;i.dataset.a=c.a;if(c.a===sel)i.className='x';d.appendChild(i)});document.getElementById(eid).appendChild(d);}");

  // イベント委譲: グリッド上のクリックをまとめて処理
  h.push("document.addEventListener('click',function(e){var t=e.target,g=t.parentNode;if(!t.dataset||!t.dataset.a||!g.dataset||!g.dataset.gid)return;var p=g.querySelector('.x');if(p)p.className='';t.className='x';var pt=g.dataset.gid.split('_');st(+pt[0].slice(1),pt[1],+t.dataset.a);});");

  // タイプ変更時のデフォルト色 [bg, fg] (カラー機 / B&W機)
  h.push('var TC=' + JSON.stringify(tileColors) + ';');
  // カラー内訳: None=黒/白, Time=紺/白, Date=暗灰/白, Day=緑/黒, Year=紺/白,
  //             HR=赤/白, Steps=橙/黒, Weather=水色/黒, Battery=暗緑/白

  // グリッドの選択スウォッチを差し替えるヘルパー
  h.push('function ug(gid,v){var g=document.getElementById(gid);if(!g)return;var p=g.querySelector(".x");if(p)p.className="";var ch=g.children;for(var s=0;s<ch.length;s++){if(+ch[s].dataset.a===v){ch[s].className="x";break;}}}');

  h.push('function ec(t,p){var d=TC[t.type]||TC[0];if(t.bg!==t.fg)return;if(p==="fg"){if(d[0]!==t.fg)t.bg=d[0];else if(d[1]!==t.fg)t.bg=d[1];else t.bg=(t.fg===255?192:255);}else{if(d[1]!==t.bg)t.fg=d[1];else if(d[0]!==t.bg)t.fg=d[0];else t.fg=(t.bg===255?192:255);}}');
  h.push('function rt(i){ug("sg_t"+i+"_bg",S.tiles[i].bg);ug("sg_t"+i+"_fg",S.tiles[i].fg)}');
  h.push('function rc(a){var r=(a>>4)&3,g=(a>>2)&3,b=a&3;return "rgb("+(r*85)+","+(g*85)+","+(b*85)+")";}');
  h.push('function rp(){for(var i=0;i<6;i++){var t=S.tiles[i],e=document.getElementById("pv"+i);if(!e)continue;e.style.background=rc(t.bg);e.style.color=rc(t.fg);var l=e.querySelector(".pl"),v=e.querySelector(".pvw");if(l)l.textContent=PN[t.type]||"";if(v)v.textContent=PV[t.type]||"";}}');
  h.push('function sx(){var x=document.getElementById("json");if(!x)return;var d={tiles:[]};for(var i=0;i<6;i++)d.tiles.push({bg:S.tiles[i].bg,fg:S.tiles[i].fg});x.value=JSON.stringify(d,null,2);}');
  h.push('function ix(){var x=document.getElementById("json");if(!x)return;try{var d=JSON.parse(x.value),p=document.getElementById("preset");if(!d||!d.tiles||d.tiles.length!==6)throw new Error("tiles");for(var i=0;i<6;i++){var s=d.tiles[i]||{};if(typeof s.bg==="undefined"||typeof s.fg==="undefined")throw new Error("tile"+i);S.tiles[i].bg=+s.bg;S.tiles[i].fg=+s.fg;ec(S.tiles[i],"bg");rt(i)}if(p)p.value="custom";AP="custom";rp();sx();}catch(err){alert("Invalid JSON format");}}');
  h.push('function st(i,k,v){S.tiles[i][k]=v;ec(S.tiles[i],k);rt(i);rp();sx();var pr=document.getElementById("preset");if(pr)pr.value="custom";AP="custom";}');

  // タイプ選択変更ハンドラ: 新しいタイプのデフォルト色を S に反映しグリッドも更新
  h.push('function ct(idx,v){v=+v;var d=TC[v];if(d){S.tiles[idx].bg=d[0];S.tiles[idx].fg=d[1];}S.tiles[idx].type=v;ec(S.tiles[idx],"type");rt(idx);rp();sx();var pr=document.getElementById("preset");if(pr)pr.value="custom";AP="custom";}');
  h.push('function cp(v){if(v==="custom"){AP="custom";rp();sx();return;}var p=PR[v];if(!p)return;AP=v;for(var i=0;i<6;i++){S.tiles[i].bg=p.tiles[i].bg;S.tiles[i].fg=p.tiles[i].fg;ec(S.tiles[i],"type");rt(i)}rp();sx();}');

  // 保存: エミュレータ(return_to)と実機(pebblejs://close#)を自動判別
  h.push('function save(){');
  h.push('for(var i=0;i<6;i++){S.tiles[i].type=+document.getElementById("tt"+i).value;ec(S.tiles[i],"type")}');
  h.push('S.dateFormat=+document.getElementById("df").value;');
  h.push('S.tempUnit=+document.getElementById("tu").value;');
  h.push('S.bluetoothDisconnectVibe=!!document.getElementById("bdv").checked;');
  h.push('S.weatherCity=(document.getElementById("wc").value||"").replace(/^\\s+|\\s+$/g,"");');
  h.push(bw ? 'S.bwPreset=AP;' : 'S.colorPreset=AP;');
  h.push('var c=encodeURIComponent(JSON.stringify(S));');
  h.push('var m=location.href.match(/[?&]return_to=([^&]+)/);');
  h.push('location.href=m?decodeURIComponent(m[1])+c:"pebblejs://close#"+c;');
  h.push('}');
  h.push('<\/script>');

  h.push('<div class="tile">');
  h.push('<div class="tt">Preview</div>');
  // h.push('<div class="sub">タイル色、文字色、表示内容の組み合わせをこの場で確認できます。</div>');
  h.push('<div class="pv">');
  for (var previewIdx = 0; previewIdx < 6; previewIdx++) {
    h.push('<div class="pt" id="pv' + previewIdx + '"><div class="pl"></div><div class="pvw"></div></div>');
  }
  h.push('</div>');
  h.push('<script>rp()<\/script>');
  h.push('</div>');

  h.push('<div class="tile">');
  h.push('<div class="tt">Color JSON</div>');
  // h.push('<div class="sub">6 タイル分の背景色・文字色がリアルタイムで JSON に反映されます。貼り付けて読み込むこともできます。</div>');
  h.push('<textarea id="json" spellcheck="false"></textarea>');
  h.push('<button class="rowbtn" onclick="ix()">Load Colors From JSON</button>');
  // h.push('<div class="hint">形式: {"tiles":[{"bg":198,"fg":192}, ... ]}</div>');
  h.push('<script>sx()<\/script>');
  h.push('</div>');

  h.push('<div class="tile">');
  h.push('<div class="tt">' + (bw ? 'Monochrome Preset' : 'Color Preset') + '</div>');
  // h.push('<div class="sub">' + (bw ? 'モノクロモデルにはモノクロ用プリセットのみ表示します。' : 'カラーモデルにはカラー用プリセットのみ表示します。') + '</div>');
  h.push('<select id="preset" onchange="cp(this.value)">');
  for (var presetIdx = 0; presetIdx < presets.length; presetIdx++) {
    var preset = presets[presetIdx];
    h.push('<option value="' + preset.id + '"' + (selectedPresetId === preset.id ? ' selected' : '') + '>' + preset.name + '</option>');
  }
  h.push('<option value="custom"' + (selectedPresetId === 'custom' ? ' selected' : '') + '>Custom</option>');
  h.push('</select>');
  h.push('</div>');

  // --- タイル設定セクション ---
  for (var i = 0; i < 6; i++) {
    var tile = settings.tiles[i];
    h.push('<div class="tile">');
    h.push('<div class="tt">Tile ' + (i + 1) + '</div>');

    // 種別セレクト
    // HR非対応機ではtype=5(Heart Rate)をtype=8(Battery)に強制変換して表示
    var displayType = tile.type;
    if (displayType === 5 && !hrDevice) displayType = 8;
    h.push('<select id="tt' + i + '" onchange="ct(' + i + ',this.value)">');
    for (var t = 0; t < TILE_TYPE_NAMES.length; t++) {
      var isHROption = (t === 5);
      var disabled = (isHROption && !hrDevice) ? ' disabled' : '';
      var label = TILE_TYPE_NAMES[t] + (isHROption && !hrDevice ? ' (n/a)' : '');
      var tsel = (displayType === t) ? ' selected' : '';
      h.push('<option value="' + t + '"' + tsel + disabled + '>' + label + '</option>');
    }
    h.push('</select>');

    // if (bw) {
    //   h.push('<div class="hint">B&W model: only black, gray, and white are available.</div>');
    // }

    // 背景色グリッド（コンテナ div → スクリプトで埋める）
    h.push('<div class="lbl">Background</div>');
    h.push('<div id="bg' + i + '"></div>');
    h.push('<script>G("bg' + i + '","t' + i + '_bg",' + tile.bg + ')<\/script>');

    // 文字色グリッド
    h.push('<div class="lbl">Text</div>');
    h.push('<div id="fg' + i + '"></div>');
    h.push('<script>G("fg' + i + '","t' + i + '_fg",' + tile.fg + ')<\/script>');

    h.push('</div>');
  }

  // --- フォーマット設定セクション ---
  var selDF  = settings.dateFormat || 0;
  var selTmp = settings.tempUnit   || 0;

  h.push('<div class="tile">');
  h.push('<div class="tt">Format Settings</div>');

  h.push('<div class="lbl">Date</div>');
  h.push('<select id="df">');
  h.push('<option value="0"' + (selDF===0?' selected':'') + '>MM/DD</option>');
  h.push('<option value="1"' + (selDF===1?' selected':'') + '>DD/MM</option>');
  h.push('</select>');

  h.push('<div class="lbl">Temperature</div>');
  h.push('<select id="tu">');
  h.push('<option value="0"' + (selTmp===0?' selected':'') + '>Celsius (\u00b0C)</option>');
  h.push('<option value="1"' + (selTmp===1?' selected':'') + '>Fahrenheit (\u00b0F)</option>');
  h.push('</select>');

  h.push('<div class="lbl">Bluetooth Disconnect Vibration</div>');
  h.push('<label class="hint" style="display:flex;align-items:center;gap:8px;margin-bottom:10px">');
  h.push('<input id="bdv" type="checkbox" style="width:auto;margin:0"'
         + (settings.bluetoothDisconnectVibe ? ' checked' : '') + '>');
  h.push('<span>Vibrate when Bluetooth connection is lost</span>');
  h.push('</label>');

  h.push('<div class="lbl">Weather City</div>');
  h.push('<input id="wc" type="text" maxlength="31" placeholder="Blank = current location" value="' +
         settings.weatherCity.replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;') +
         '">');
  h.push('<div class="hint">Leave blank to use current location. Enter a city name to fetch weather for that city.</div>');

  h.push('</div>');

  h.push('<button id="sb" onclick="save()">SAVE</button>');
  h.push('</body></html>');

  return h.join('');
}

function buildCompactConfigHTML(settings) {
  var h = [];
  var bw = isBW();
  var hrDevice = hasHR();
  var presets = getPresetList(bw);
  var selectedPresetId;

  settings = normalizeSettings(settings);
  selectedPresetId = activePresetId(settings, bw);

  h.push('<!DOCTYPE html><html><head>');
  h.push('<meta charset="utf-8">');
  h.push('<meta name="viewport" content="width=device-width,initial-scale=1">');
  h.push('<title>Metro Settings</title>');
  h.push('<style>');
  h.push('*{box-sizing:border-box}body{margin:0;padding:12px;background:#111;color:#fff;font-family:sans-serif;font-size:14px}h1{margin:0 0 12px;color:#0078d7;font-size:18px}.card{background:#1d1d1d;border:1px solid #333;border-radius:6px;padding:10px;margin-bottom:10px}.ttl{font-size:13px;font-weight:bold;color:#9ecfff;margin-bottom:8px}.lbl{display:block;font-size:11px;color:#aaa;margin:8px 0 4px}select,input{width:100%;padding:8px;border-radius:4px;border:1px solid #555;background:#2a2a2a;color:#fff;font-size:13px}input{margin:0}button{width:100%;padding:14px;border:0;border-radius:5px;background:#0078d7;color:#fff;font-size:16px}.hint{font-size:12px;line-height:1.45;color:#aaa;margin-top:6px}.row{display:grid;grid-template-columns:1fr 1fr;gap:8px}');
  h.push('</style></head><body>');
  h.push('<h1>METRO UI</h1>');
  h.push('<div class="card"><div class="ttl">' +
         (bw ? 'Monochrome Settings' : 'Compact Settings') +
         '</div>' +
         // '<div class="hint">' +
         // (bw ? '白黒モデルでは黒・灰・白の 3 色だけを選べます。' :
         //       '軽量ページを使用します。色は 192-255 の数値で指定します。') +
         // '</div>' +
      '</div>');

  h.push('<script>');
  h.push('var S=' + JSON.stringify(settings) + ';');
  h.push('var AP=' + JSON.stringify(selectedPresetId) + ';');
  h.push('var PR={};');
  for (var presetIdx = 0; presetIdx < presets.length; presetIdx++) {
    h.push('PR[' + JSON.stringify(presets[presetIdx].id) + ']=' + JSON.stringify(presets[presetIdx]) + ';');
  }
  h.push('function ec(t,p){var d=(' + JSON.stringify(defaultTileColors(bw)) + ')[t.type]||(' + JSON.stringify(defaultTileColors(bw)) + ')[0];if(t.bg!==t.fg)return;if(p==="fg"){if(d[0]!==t.fg)t.bg=d[0];else if(d[1]!==t.fg)t.bg=d[1];else t.bg=(t.fg===255?192:255);}else{if(d[1]!==t.bg)t.fg=d[1];else if(d[0]!==t.bg)t.fg=d[0];else t.fg=(t.bg===255?192:255);}}');
  h.push('function applyPreset(v){AP=v;if(v==="custom")return;var p=PR[v];if(!p)return;for(var i=0;i<6;i++){S.tiles[i].bg=p.tiles[i].bg;S.tiles[i].fg=p.tiles[i].fg;ec(S.tiles[i],"type");document.getElementById("bg"+i).value=S.tiles[i].bg;document.getElementById("fg"+i).value=S.tiles[i].fg;}}');
  h.push('function save(){for(var i=0;i<6;i++){S.tiles[i].type=+document.getElementById("tt"+i).value;S.tiles[i].bg=+document.getElementById("bg"+i).value;S.tiles[i].fg=+document.getElementById("fg"+i).value;ec(S.tiles[i],"type");}S.dateFormat=+document.getElementById("df").value;S.tempUnit=+document.getElementById("tu").value;S.bluetoothDisconnectVibe=(+document.getElementById("bdv").value)===1;S.weatherCity=(document.getElementById("wc").value||"").replace(/^\\s+|\\s+$/g,"");');
  h.push(bw ? 'S.bwPreset=AP;' : 'S.colorPreset=AP;');
  h.push('var c=encodeURIComponent(JSON.stringify(S));var m=location.href.match(/[?&]return_to=([^&]+)/);location.href=m?decodeURIComponent(m[1])+c:"pebblejs://close#"+c;}');
  h.push('<\/script>');

  h.push('<div class="card"><div class="ttl">' + (bw ? 'Monochrome Preset' : 'Color Preset') + '</div><select id="preset" onchange="applyPreset(this.value)">');
  for (var listIdx = 0; listIdx < presets.length; listIdx++) {
    var preset = presets[listIdx];
    h.push('<option value="' + preset.id + '"' + (selectedPresetId === preset.id ? ' selected' : '') + '>' + preset.name + '</option>');
  }
  h.push('<option value="custom"' + (selectedPresetId === 'custom' ? ' selected' : '') + '>カスタム</option>');
  h.push('</select></div>');

  for (var i = 0; i < 6; i++) {
    var tile = settings.tiles[i];
    var displayType = tile.type;
    if (displayType === 5 && !hrDevice) displayType = 8;
    h.push('<div class="card">');
    h.push('<div class="ttl">Tile ' + (i + 1) + '</div>');
    h.push('<label class="lbl" for="tt' + i + '">Type</label>');
    h.push('<select id="tt' + i + '" onchange="document.getElementById(\'preset\').value=\'custom\';AP=\'custom\'">');
    for (var t = 0; t < TILE_TYPE_NAMES.length; t++) {
      var isHROption = (t === 5);
      var disabled = (isHROption && !hrDevice) ? ' disabled' : '';
      var label = TILE_TYPE_NAMES[t] + (isHROption && !hrDevice ? ' (n/a)' : '');
      var tsel = (displayType === t) ? ' selected' : '';
      h.push('<option value="' + t + '"' + tsel + disabled + '>' + label + '</option>');
    }
    h.push('</select>');
    h.push('<div class="row">');
    if (bw) {
      h.push('<div><label class="lbl" for="bg' + i + '">Background</label><select id="bg' + i + '" onchange="document.getElementById(\'preset\').value=\'custom\';AP=\'custom\'">');
      h.push('<option value="' + BW_BLACK + '"' + (tile.bg === BW_BLACK ? ' selected' : '') + '>Black</option>');
      h.push('<option value="' + BW_LIGHT_GRAY + '"' + (tile.bg === BW_LIGHT_GRAY ? ' selected' : '') + '>Gray</option>');
      h.push('<option value="' + BW_WHITE + '"' + (tile.bg === BW_WHITE ? ' selected' : '') + '>White</option>');
      h.push('</select></div>');
      h.push('<div><label class="lbl" for="fg' + i + '">Text</label><select id="fg' + i + '" onchange="document.getElementById(\'preset\').value=\'custom\';AP=\'custom\'">');
      h.push('<option value="' + BW_BLACK + '"' + (tile.fg === BW_BLACK ? ' selected' : '') + '>Black</option>');
      h.push('<option value="' + BW_LIGHT_GRAY + '"' + (tile.fg === BW_LIGHT_GRAY ? ' selected' : '') + '>Gray</option>');
      h.push('<option value="' + BW_WHITE + '"' + (tile.fg === BW_WHITE ? ' selected' : '') + '>White</option>');
      h.push('</select></div>');
    } else {
      h.push('<div><label class="lbl" for="bg' + i + '">Background</label><input id="bg' + i + '" type="number" min="192" max="255" value="' + tile.bg + '" oninput="document.getElementById(\'preset\').value=\'custom\';AP=\'custom\'"></div>');
      h.push('<div><label class="lbl" for="fg' + i + '">Text</label><input id="fg' + i + '" type="number" min="192" max="255" value="' + tile.fg + '" oninput="document.getElementById(\'preset\').value=\'custom\';AP=\'custom\'"></div>');
    }
    h.push('</div>');
    h.push('</div>');
  }

  h.push('<div class="card"><div class="ttl">Format Settings</div>');
  h.push('<label class="lbl" for="df">Date</label><select id="df">');
  h.push('<option value="0"' + (settings.dateFormat === 0 ? ' selected' : '') + '>MM/DD</option>');
  h.push('<option value="1"' + (settings.dateFormat === 1 ? ' selected' : '') + '>DD/MM</option>');
  h.push('</select>');
  h.push('<label class="lbl" for="tu">Temperature</label><select id="tu">');
  h.push('<option value="0"' + (settings.tempUnit === 0 ? ' selected' : '') + '>Celsius (°C)</option>');
  h.push('<option value="1"' + (settings.tempUnit === 1 ? ' selected' : '') + '>Fahrenheit (°F)</option>');
  h.push('</select>');
  h.push('<label class="lbl" for="bdv">Bluetooth Disconnect Vibration</label>');
  h.push('<div class="hint" style="margin-bottom:6px">Vibrate when Bluetooth connection is lost.</div>');
  h.push('<div class="row" style="grid-template-columns:1fr"><select id="bdv">');
  h.push('<option value="0"' + (!settings.bluetoothDisconnectVibe ? ' selected' : '') + '>Off</option>');
  h.push('<option value="1"' + (settings.bluetoothDisconnectVibe ? ' selected' : '') + '>On</option>');
  h.push('</select></div>');
  h.push('<label class="lbl" for="wc">Weather City</label>');
  h.push('<input id="wc" type="text" maxlength="31" placeholder="Blank = current location" value="' +
         settings.weatherCity.replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;') +
         '">');
  h.push('<div class="hint">Leave blank for your current location. Enter a city name to get the weather for that city.</div>');
  h.push('</div>');

  h.push('<button onclick="save()">SAVE</button>');
  h.push('</body></html>');

  return h.join('');
}

function isIOSPhone() {
  try {
    var ua = (navigator && navigator.userAgent) ? navigator.userAgent : '';
    return /iPhone|iPad|iPod/i.test(ua);
  } catch(e) {}
  return false;
}

function buildConfigUrl() {
  var info = getWatchInfo();
  var settings = loadSettings();
  var bw = isBW();
  var isEmulator = isEmulatorWatchInfo(info);
  var useCompactConfig = !isEmulator && isIOSPhone();
  var html = useCompactConfig ? buildCompactConfigHTML(settings) : buildConfigHTML(settings);
  var url;

  if (isEmulator) {
    url = 'data:text/html;charset=utf-8,' + encodeURIComponent(html);
  } else {
    url = 'data:text/html;charset=utf-8;base64,' + base64EncodeUtf8(html);
  }

  console.log('Opening config: platform=' + inferredPlatform(info) +
              ' model=' + watchInfoField(info, 'model') +
              ' emulator=' + isEmulator +
              ' bw=' + bw +
              ' compact=' + useCompactConfig +
              ' caps=' + JSON.stringify(sDeviceCaps) +
              ' url_len=' + url.length);
  return url;
}

function clearPendingConfigTimer() {
  if (sPendingConfigTimer) {
    clearTimeout(sPendingConfigTimer);
    sPendingConfigTimer = null;
  }
}

function openConfigurationPage() {
  clearPendingConfigTimer();
  sPendingConfigOpen = false;
  Pebble.openURL(buildConfigUrl());
}

function requestDeviceCapsForConfig() {
  Pebble.sendAppMessage(
    { 'REQUEST_DEVICE_CAPS': 1 },
    function() {
      console.log('Requested device caps for config');
    },
    function(err) {
      console.log('Device caps request failed: ' + JSON.stringify(err));
      if (sPendingConfigOpen) {
        openConfigurationPage();
      }
    }
  );
}

// ============================================================================
// PEBBLE EVENT LISTENERS
// ============================================================================

Pebble.addEventListener('ready', function() {
  getWatchInfo();
  console.log('PebbleKit JS ready');
  requestDeviceCapsForConfig();
  fetchWeather();
});

Pebble.addEventListener('appmessage', function(e) {
  console.log('AppMessage from watch: ' + JSON.stringify(e.payload));
  if (typeof e.payload['DEVICE_IS_BW'] !== 'undefined') {
    sDeviceCaps.isBW = !!e.payload['DEVICE_IS_BW'];
  }
  if (typeof e.payload['DEVICE_HAS_HR'] !== 'undefined') {
    sDeviceCaps.hasHR = !!e.payload['DEVICE_HAS_HR'];
  }
  if (sPendingConfigOpen && hasResolvedDeviceCaps()) {
    openConfigurationPage();
    return;
  }
  if (e.payload['REQUEST_WEATHER']) {
    fetchWeather();
  }
});

Pebble.addEventListener('showConfiguration', function() {
  var needsCapabilityProbe = !hasResolvedDeviceCaps();

  if (needsCapabilityProbe) {
    console.log('Config requested before device caps resolved; probing watch first');
    sPendingConfigOpen = true;
    clearPendingConfigTimer();
    sPendingConfigTimer = setTimeout(function() {
      if (sPendingConfigOpen) {
        console.log('Config probe timed out; opening with fallback detection');
        openConfigurationPage();
      }
    }, 800);
    requestDeviceCapsForConfig();
    return;
  }

  openConfigurationPage();
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e.response || e.response === 'CANCELLED') {
    console.log('Config cancelled');
    return;
  }
  try {
    var settings = normalizeSettings(JSON.parse(decodeURIComponent(e.response)));
    saveSettings(settings);

    var msg = {};
    for (var i = 0; i < 6; i++) {
      var tile = settings.tiles[i];
      // Use the key names matching package.json messageKeys order
      msg['TILE' + i + '_TYPE'] = tile.type;
      msg['TILE' + i + '_BG']   = tile.bg;
      msg['TILE' + i + '_FG']   = tile.fg;
    }
    var df = settings.dateFormat || 0;
    if (df === 2) df = 0;  // MM月DD日 was removed; fall back to MM/DD
    msg['DATE_FORMAT'] = df;
    msg['TEMP_UNIT']   = settings.tempUnit   || 0;
    msg['BLUETOOTH_DISCONNECT_VIBE'] = settings.bluetoothDisconnectVibe ? 1 : 0;
    Pebble.sendAppMessage(msg,
      function() { console.log('Settings sent to watch'); },
      function(err) { console.log('Settings send failed: ' + JSON.stringify(err)); }
    );
    fetchWeather();
  } catch(ex) {
    console.log('webviewclosed parse error: ' + ex);
  }
});
