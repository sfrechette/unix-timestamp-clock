#include "display.h"
#include "config.h"
#include "wifi_manager.h"

#include <LV_Helper.h>
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>

// ---------------------------------------------------------------------------
// Color palette (AMOLED-optimized — true black saves power)
// ---------------------------------------------------------------------------
#define COLOR_BG      lv_color_hex(0x000000)  // black
#define COLOR_PRIMARY lv_color_hex(0x00C853)  // green  — clock digits
#define COLOR_ACCENT  lv_color_hex(0xFFFFFF)  // white  — labels, active dot
#define COLOR_DIM     lv_color_hex(0x333333)  // dark grey — inactive dots
#define COLOR_DATE    lv_color_hex(0xCCCCCC)  // off-white — date text
#define COLOR_SUBTLE  lv_color_hex(0x777777)  // grey   — ms digits, network details
#define COLOR_WARN    lv_color_hex(0xFF6B35)  // amber  — WiFi error

// ---------------------------------------------------------------------------
// Brightness state (private — cycled by screen tap)
// ---------------------------------------------------------------------------
static uint8_t brightness_index = BRIGHTNESS_INIT_IDX;

// ---------------------------------------------------------------------------
// Per-view label handles (private)
// ---------------------------------------------------------------------------
static lv_obj_t *unix_number_label   = nullptr;
static lv_obj_t *unix_date_label     = nullptr;
static lv_obj_t *utc_number_label    = nullptr;
static lv_obj_t *utc_date_label      = nullptr;
static lv_obj_t *time24_number_label = nullptr;
static lv_obj_t *time24_date_label   = nullptr;
static lv_obj_t *time12_number_label = nullptr;
static lv_obj_t *time12_date_label   = nullptr;
static lv_obj_t *utc_ms_label        = nullptr;
static lv_obj_t *time24_ms_label     = nullptr;
static lv_obj_t *time12_ms_label     = nullptr;
static lv_obj_t *time12_ampm_label   = nullptr;
static lv_obj_t *net_ssid_label      = nullptr;
static lv_obj_t *net_ip_label        = nullptr;
static lv_obj_t *net_rssi_label      = nullptr;

// Tileview and phantom wrap tile
static lv_obj_t *tv      = nullptr;
static lv_obj_t *t5_tile = nullptr;   // phantom tile — activating it wraps to tile 0

// Persistent overlay
static lv_obj_t *wifi_status_label = nullptr;
static constexpr uint8_t PAGE_COUNT = 5;
static lv_obj_t *dot_indicators[PAGE_COUNT] = {};

// ---------------------------------------------------------------------------
// Forward declarations (internal)
// ---------------------------------------------------------------------------
static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font,
                             lv_color_t color, lv_align_t align,
                             lv_coord_t x_ofs, lv_coord_t y_ofs,
                             const char *text);
static lv_obj_t *make_ms_label(lv_obj_t *parent, lv_obj_t *number_label);
static void align_ms_label(lv_obj_t *ms_label, lv_obj_t *number_label);
static void align_ampm_label();
static void create_view0_unix(lv_obj_t *parent);
static void create_view1_utc(lv_obj_t *parent);
static void create_view2_24h(lv_obj_t *parent);
static void create_view3_12h(lv_obj_t *parent);
static void create_view4_network(lv_obj_t *parent);
static void create_overlay_indicators(lv_obj_t *screen);
static void update_dot_indicators(uint8_t active);
static void on_clock_tick(lv_timer_t *t);
static void on_wifi_status_tick(lv_timer_t *t);
static void on_tile_changed(lv_event_t *e);
static void on_screen_tap(lv_event_t *e);
static void on_wrap_timer(lv_timer_t *t);

// ---------------------------------------------------------------------------
// Helper: create a label with font, color and alignment
// ---------------------------------------------------------------------------
static lv_obj_t *make_label(lv_obj_t *parent,
                             const lv_font_t *font,
                             lv_color_t color,
                             lv_align_t align,
                             lv_coord_t x_ofs,
                             lv_coord_t y_ofs,
                             const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_align(label, align, x_ofs, y_ofs);
    return label;
}

// ---------------------------------------------------------------------------
// Helper: smaller grey ".mmm" label that trails a clock's HH:MM:SS label.
// Kept separate so the fast-changing digits never shift the big digits.
// ---------------------------------------------------------------------------
static lv_obj_t *make_ms_label(lv_obj_t *parent, lv_obj_t *number_label)
{
    lv_obj_t *label = make_label(parent,
                                 &lv_font_montserrat_32,
                                 COLOR_SUBTLE,
                                 LV_ALIGN_CENTER, 0, 0,
                                 "");
    align_ms_label(label, number_label);
    return label;
}

// lv_obj_align_to is one-shot, so re-run it whenever the main label's
// text (and therefore width) changes — i.e. once per second.
static void align_ms_label(lv_obj_t *ms_label, lv_obj_t *number_label)
{
    lv_obj_align_to(ms_label, number_label, LV_ALIGN_OUT_RIGHT_BOTTOM, 2, -6);
}

// AM/PM sits a fixed distance right of HH:MM:SS — reserved space for the
// widest ".000" plus a gap — so it never moves as the ms digits change.
static void align_ampm_label()
{
    static lv_coord_t ms_slot_w = 0;
    if (ms_slot_w == 0) {
        lv_point_t size;
        lv_txt_get_size(&size, ".000", &lv_font_montserrat_32, 0, 0,
                        LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        ms_slot_w = size.x + 2 + 14;   // 2 = ms label offset, 14 = gap
    }
    // Bottom-align, then shift so both fonts share the same text baseline
    lv_coord_t baseline_ofs = lv_font_montserrat_40.base_line -
                              lv_font_montserrat_48.base_line;
    lv_obj_align_to(time12_ampm_label, time12_number_label,
                    LV_ALIGN_OUT_RIGHT_BOTTOM, ms_slot_w, baseline_ofs);
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------
void build_ui()
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    // Tileview fills the whole screen
    tv = lv_tileview_create(screen);
    lv_obj_set_size(tv, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(tv, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);

    // Five content tiles + one phantom wrap tile (col 5, invisible)
    // t4 uses LV_DIR_HOR so the user can swipe forward into the phantom t5.
    // When t5 activates, on_tile_changed instantly snaps to tile 0 — circular wrap.
    lv_obj_t *t0 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
    lv_obj_t *t1 = lv_tileview_add_tile(tv, 1, 0, LV_DIR_HOR);
    lv_obj_t *t2 = lv_tileview_add_tile(tv, 2, 0, LV_DIR_HOR);
    lv_obj_t *t3 = lv_tileview_add_tile(tv, 3, 0, LV_DIR_HOR);
    lv_obj_t *t4 = lv_tileview_add_tile(tv, 4, 0, LV_DIR_HOR);   // HOR allows forward swipe
    t5_tile      = lv_tileview_add_tile(tv, 5, 0, LV_DIR_LEFT);   // phantom — triggers wrap

    lv_obj_set_style_bg_color(t0,      COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(t1,      COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(t2,      COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(t3,      COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(t4,      COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(t5_tile, COLOR_BG, LV_PART_MAIN);

    create_view0_unix(t0);
    create_view1_utc(t1);
    create_view2_24h(t2);
    create_view3_12h(t3);
    create_view4_network(t4);
    // t5 intentionally empty — wraps instantly to t0 on activate

    // Overlay sits on top of tileview (rendered after, so always visible)
    create_overlay_indicators(screen);

    lv_obj_add_event_cb(tv, on_tile_changed, LV_EVENT_VALUE_CHANGED, nullptr);

    // Register tap on each tile — tiles receive direct touch, the tileview
    // container does not reliably fire SHORT_CLICKED due to scroll detection.
    lv_obj_add_event_cb(t0, on_screen_tap, LV_EVENT_SHORT_CLICKED, nullptr);
    lv_obj_add_event_cb(t1, on_screen_tap, LV_EVENT_SHORT_CLICKED, nullptr);
    lv_obj_add_event_cb(t2, on_screen_tap, LV_EVENT_SHORT_CLICKED, nullptr);
    lv_obj_add_event_cb(t3, on_screen_tap, LV_EVENT_SHORT_CLICKED, nullptr);
    lv_obj_add_event_cb(t4, on_screen_tap, LV_EVENT_SHORT_CLICKED, nullptr);

    lv_timer_create(on_clock_tick, 33, nullptr);   // ~30 fps for the ms digits
    lv_timer_create(on_wifi_status_tick, 2000, nullptr);
}

// ---------------------------------------------------------------------------
// View 0: Unix Timestamp
// ---------------------------------------------------------------------------
static void create_view0_unix(lv_obj_t *parent)
{
    make_label(parent,
               &lv_font_montserrat_32,
               COLOR_ACCENT,
               LV_ALIGN_TOP_MID, 0, 30,
               "Unix Timestamp");

    unix_number_label = make_label(parent,
                                   &lv_font_montserrat_48,
                                   COLOR_PRIMARY,
                                   LV_ALIGN_CENTER, 0, 0,
                                   "Syncing...");
    lv_obj_set_width(unix_number_label, LV_HOR_RES - 4);
    lv_label_set_long_mode(unix_number_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(unix_number_label, LV_TEXT_ALIGN_CENTER, 0);

    unix_date_label = make_label(parent,
                                 &lv_font_montserrat_24,
                                 COLOR_DATE,
                                 LV_ALIGN_CENTER, 0, 55,
                                 "");
}

// ---------------------------------------------------------------------------
// View 1: UTC Clock
// ---------------------------------------------------------------------------
static void create_view1_utc(lv_obj_t *parent)
{
    make_label(parent,
               &lv_font_montserrat_32,
               COLOR_ACCENT,
               LV_ALIGN_TOP_MID, 0, 30,
               "UTC");

    utc_number_label = make_label(parent,
                                  &lv_font_montserrat_48,
                                  COLOR_PRIMARY,
                                  LV_ALIGN_CENTER, -40, 0,
                                  "Syncing...");
    // Shifted left so HH:MM:SS + trailing ms label read as one centered group
    utc_ms_label = make_ms_label(parent, utc_number_label);

    utc_date_label = make_label(parent,
                                &lv_font_montserrat_24,
                                COLOR_DATE,
                                LV_ALIGN_CENTER, 0, 55,
                                "");
}

// ---------------------------------------------------------------------------
// View 2: 24h Clock
// ---------------------------------------------------------------------------
static void create_view2_24h(lv_obj_t *parent)
{
    make_label(parent,
               &lv_font_montserrat_32,
               COLOR_ACCENT,
               LV_ALIGN_TOP_MID, 0, 30,
               "24 Hour");

    time24_number_label = make_label(parent,
                                     &lv_font_montserrat_48,
                                     COLOR_PRIMARY,
                                     LV_ALIGN_CENTER, -40, 0,
                                     "Syncing...");
    // Shifted left so HH:MM:SS + trailing ms label read as one centered group
    time24_ms_label = make_ms_label(parent, time24_number_label);

    time24_date_label = make_label(parent,
                                   &lv_font_montserrat_24,
                                   COLOR_DATE,
                                   LV_ALIGN_CENTER, 0, 55,
                                   "");
}

// ---------------------------------------------------------------------------
// View 3: 12h Clock
// ---------------------------------------------------------------------------
static void create_view3_12h(lv_obj_t *parent)
{
    make_label(parent,
               &lv_font_montserrat_32,
               COLOR_ACCENT,
               LV_ALIGN_TOP_MID, 0, 30,
               "12 Hour");

    time12_number_label = make_label(parent,
                                     &lv_font_montserrat_48,
                                     COLOR_PRIMARY,
                                     LV_ALIGN_CENTER, -75, 0,
                                     "Syncing...");
    // Shifted left so HH:MM:SS + ms + AM/PM read as one centered group
    time12_ms_label = make_ms_label(parent, time12_number_label);

    time12_ampm_label = make_label(parent,
                                   &lv_font_montserrat_40,
                                   COLOR_PRIMARY,
                                   LV_ALIGN_CENTER, 0, 0,
                                   "");

    time12_date_label = make_label(parent,
                                   &lv_font_montserrat_24,
                                   COLOR_DATE,
                                   LV_ALIGN_CENTER, 0, 55,
                                   "");
}

// ---------------------------------------------------------------------------
// View 4: Network Info
// ---------------------------------------------------------------------------
static void create_view4_network(lv_obj_t *parent)
{
    make_label(parent,
               &lv_font_montserrat_32,
               COLOR_ACCENT,
               LV_ALIGN_TOP_MID, 0, 30,
               "Network");

    net_ssid_label = make_label(parent,
                                &lv_font_montserrat_24,
                                COLOR_PRIMARY,
                                LV_ALIGN_CENTER, 0, -30,
                                "Connecting...");
    lv_obj_set_width(net_ssid_label, LV_HOR_RES - 8);
    lv_label_set_long_mode(net_ssid_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(net_ssid_label, LV_TEXT_ALIGN_CENTER, 0);

    net_ip_label = make_label(parent,
                              &lv_font_montserrat_24,
                              COLOR_SUBTLE,
                              LV_ALIGN_CENTER, 0, 10,
                              "");

    net_rssi_label = make_label(parent,
                                &lv_font_montserrat_24,
                                COLOR_SUBTLE,
                                LV_ALIGN_CENTER, 0, 50,
                                "");
}

// ---------------------------------------------------------------------------
// Persistent overlay: WiFi icon + page-dot indicators
// ---------------------------------------------------------------------------
static void create_overlay_indicators(lv_obj_t *screen)
{
    // WiFi icon — top-right
    wifi_status_label = lv_label_create(screen);
    lv_label_set_text(wifi_status_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(wifi_status_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(wifi_status_label, COLOR_DIM, 0);
    lv_obj_set_style_bg_opa(wifi_status_label, LV_OPA_TRANSP, 0);
    lv_obj_align(wifi_status_label, LV_ALIGN_TOP_RIGHT, -10, 10);

    // Page dots at the bottom, horizontally centered
    const lv_coord_t dot_size = 8;
    const lv_coord_t dot_gap  = 14;   // center-to-center spacing
    const lv_coord_t total_w  = (PAGE_COUNT - 1) * dot_gap;
    const lv_coord_t start_x  = (LV_HOR_RES - total_w) / 2 - dot_size / 2;
    const lv_coord_t dot_y    = LV_VER_RES - 22;

    for (int i = 0; i < PAGE_COUNT; i++) {
        dot_indicators[i] = lv_obj_create(screen);
        lv_obj_set_size(dot_indicators[i], dot_size, dot_size);
        lv_obj_set_style_radius(dot_indicators[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot_indicators[i], 0, 0);
        lv_obj_set_style_pad_all(dot_indicators[i], 0, 0);
        lv_obj_set_style_bg_color(dot_indicators[i],
                                  i == 0 ? COLOR_ACCENT : COLOR_DIM, 0);
        lv_obj_set_pos(dot_indicators[i], start_x + i * dot_gap, dot_y);
    }
}

static void update_dot_indicators(uint8_t active)
{
    for (int i = 0; i < PAGE_COUNT; i++) {
        lv_obj_set_style_bg_color(dot_indicators[i],
                                  i == active ? COLOR_ACCENT : COLOR_DIM, 0);
    }
}

// ---------------------------------------------------------------------------
// Tile change event + circular wrap
// ---------------------------------------------------------------------------

// One-shot timer callback: deferred snap to tile 0 after LVGL finishes
// processing the VALUE_CHANGED event for the phantom tile 5.
// Calling lv_obj_set_tile_id directly inside VALUE_CHANGED corrupts the
// internal scroll state and breaks subsequent swipes from tile 0.
static void on_wrap_timer(lv_timer_t *t)
{
    lv_obj_set_tile_id(tv, 0, 0, LV_ANIM_OFF);
    lv_timer_del(t);
}

static void on_tile_changed(lv_event_t *e)
{
    lv_obj_t *tv_evt   = lv_event_get_target(e);
    lv_obj_t *tile_act = lv_tileview_get_tile_act(tv_evt);

    // Phantom tile 5 activated — schedule wrap to tile 0 for next tick
    if (tile_act == t5_tile) {
        update_dot_indicators(0);                    // dots show tile 0 immediately
        lv_timer_t *wrap_timer = lv_timer_create(on_wrap_timer, 1, nullptr);
        lv_timer_set_repeat_count(wrap_timer, 1);
        return;
    }

    uint8_t idx = (uint8_t)lv_obj_get_index(tile_act);
    if (idx < PAGE_COUNT) update_dot_indicators(idx);
}

// ---------------------------------------------------------------------------
// Clock tick — shared 33ms timer updates all clock views.
// Second-level text is only rebuilt when the second changes; the small ms
// labels update every tick (off-screen ones are not redrawn by LVGL).
// ---------------------------------------------------------------------------
static void on_clock_tick(lv_timer_t * /*t*/)
{
    struct timeval tv_now;
    gettimeofday(&tv_now, nullptr);
    time_t now = tv_now.tv_sec;
    if (now <= 1000000000UL) return;   // stay on "Syncing..." until NTP ready

    static char buf[32];
    static char date_buf[32];
    static time_t last_sec = 0;
    struct tm timeinfo;

    if (now != last_sec) {
        last_sec = now;
        localtime_r(&now, &timeinfo);

        // Date string is identical across local-time views — compute once
        strftime(date_buf, sizeof(date_buf), "%A, %B %d %Y", &timeinfo);

        // View 0: raw Unix timestamp
        snprintf(buf, sizeof(buf), "%llu", (unsigned long long)now);
        lv_label_set_text(unix_number_label, buf);
        lv_label_set_text(unix_date_label, date_buf);

        // View 1: UTC HH:MM:SS
        gmtime_r(&now, &timeinfo);
        strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
        strftime(date_buf, sizeof(date_buf), "%A, %B %d %Y", &timeinfo);
        lv_label_set_text(utc_number_label, buf);
        lv_label_set_text(utc_date_label, date_buf);
        align_ms_label(utc_ms_label, utc_number_label);

        // View 2: 24h HH:MM:SS
        localtime_r(&now, &timeinfo);
        strftime(date_buf, sizeof(date_buf), "%A, %B %d %Y", &timeinfo);
        strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
        lv_label_set_text(time24_number_label, buf);
        lv_label_set_text(time24_date_label, date_buf);
        align_ms_label(time24_ms_label, time24_number_label);

        // View 3: 12h HH:MM:SS + static AM/PM label
        int hour12 = timeinfo.tm_hour % 12;
        if (hour12 == 0) hour12 = 12;
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                 hour12, timeinfo.tm_min, timeinfo.tm_sec);
        lv_label_set_text(time12_number_label, buf);
        lv_label_set_text(time12_date_label, date_buf);
        align_ms_label(time12_ms_label, time12_number_label);
        lv_label_set_text_static(time12_ampm_label,
                                 timeinfo.tm_hour < 12 ? "AM" : "PM");
        align_ampm_label();
    }

    // Milliseconds — every tick
    int ms = (int)(tv_now.tv_usec / 1000);
    snprintf(buf, sizeof(buf), ".%03d", ms);
    lv_label_set_text(utc_ms_label, buf);
    lv_label_set_text(time24_ms_label, buf);
    lv_label_set_text(time12_ms_label, buf);
}

// ---------------------------------------------------------------------------
// WiFi status icon refresh (every 2s)
// ---------------------------------------------------------------------------
static void on_wifi_status_tick(lv_timer_t * /*t*/)
{
    static char buf[64];

    if (WiFi.status() == WL_CONNECTED) {
        lv_obj_set_style_text_color(wifi_status_label, COLOR_ACCENT, 0);

        // Update network info view
        lv_label_set_text(net_ssid_label, wifi_ssid.c_str());
        lv_label_set_text(net_ip_label,   WiFi.localIP().toString().c_str());
        snprintf(buf, sizeof(buf), "Signal: %d dBm", WiFi.RSSI());
        lv_label_set_text(net_rssi_label, buf);
    } else if (WiFi.status() == WL_CONNECT_FAILED ||
               WiFi.status() == WL_NO_SSID_AVAIL) {
        lv_obj_set_style_text_color(wifi_status_label, COLOR_WARN, 0);
        lv_label_set_text(net_ssid_label, wifi_ssid.c_str());
        lv_label_set_text(net_ip_label,   "Not connected");
        lv_label_set_text(net_rssi_label, "");
    } else {
        lv_obj_set_style_text_color(wifi_status_label, COLOR_DIM, 0);
        lv_label_set_text(net_ssid_label, wifi_ssid.c_str());
        lv_label_set_text(net_ip_label,   "Connecting...");
        lv_label_set_text(net_rssi_label, "");
    }
}

// ---------------------------------------------------------------------------
// Screen tap — cycles through 4 brightness levels
// LVGL suppresses LV_EVENT_SHORT_CLICKED when a swipe is detected, so
// swiping between views does NOT trigger a brightness change.
// ---------------------------------------------------------------------------
static void on_screen_tap(lv_event_t * /*e*/)
{
    brightness_index = (brightness_index + 1) % BRIGHTNESS_COUNT;
    uint8_t level = BRIGHTNESS_LEVELS[brightness_index];
    amoled.setBrightness(level);
    Serial.printf("[Brightness] Level %u/4 → %u\n", brightness_index + 1, level);
}
