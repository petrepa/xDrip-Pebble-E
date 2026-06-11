#include <pebble.h>

// ============================================================================
// xDrip Casio Watchface — Pebble Time 2 (Emery, 200x228)
// ============================================================================

const char FACE_VERSION[] = "xDrip-Casio-PT2";

// -- CGM protocol keys -------------------------------------------------------
#define CGM_ICON_KEY        0
#define CGM_BG_KEY          1
#define CGM_TCGM_KEY        2
#define CGM_DLTA_KEY        4
#define CGM_UBAT_KEY        5
#define CGM_TREND_BEGIN_KEY 7
#define CGM_TREND_DATA_KEY  8
#define CGM_TREND_END_KEY   9
#define CGM_MESSAGE_KEY     10
#define CGM_VIBE_KEY        11
#define CGM_SYNC_KEY        1000
#define PBL_PLATFORM_KEY    1001
#define PBL_APP_VER_KEY     1002

// -- Weather keys (200-202) --------------------------------------------------
#define WEATHER_TEMP_KEY    200
#define WEATHER_COND_KEY    201
#define WEATHER_REQ_KEY     202

// -- Settings keys -----------------------------------------------------------
#define SET_DISP_SECS       100
#define SET_VIBE_REPEAT     103
#define SET_NO_VIBE         104
#define SET_LIGHT_ON_CHG    105
#define SET_MESSAGE_TIMEOUT 113

// -- Casio layout (Emery 200x228) --------------------------------------------
// Section divider Y positions
#define DIV1_Y  66    // BG -> Time
#define DIV2_Y  127   // Time -> Date
#define DIV3_Y  153   // Date -> Weather/HR
#define DIV4_Y  190   // Weather/HR -> Battery

// ============================================================================
// Layers
// ============================================================================
static Window *s_window;
static Layer  *s_canvas;        // frame, dividers, section labels

// BG section
static TextLayer *s_bg_layer;
static Layer     *s_icon_layer;
static GBitmap   *s_icon_bitmap;  // only for special-value bitmaps
static TextLayer   *s_delta_layer;
static TextLayer   *s_cgmtime_layer;
static TextLayer   *s_msg_layer;

// Time / Date
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static GFont      s_time_font = NULL;

// Weather / HR
static Layer     *s_therm_icon;
static TextLayer *s_temp_layer;
static Layer     *s_hr_layer;
static GPath     *s_heart_path;

// Battery
static Layer     *s_phone_icon;
static TextLayer *s_phone_bat;
static Layer     *s_watch_icon;
static TextLayer *s_watch_bat;

// ============================================================================
// Resources
// ============================================================================
static const uint32_t ARROW_RES[10] = {
    RESOURCE_ID_IMAGE_NONE,      // 0
    RESOURCE_ID_IMAGE_UPUP,      // 1
    RESOURCE_ID_IMAGE_UP,        // 2
    RESOURCE_ID_IMAGE_UP45,      // 3
    RESOURCE_ID_IMAGE_FLAT,      // 4
    RESOURCE_ID_IMAGE_DOWN45,    // 5
    RESOURCE_ID_IMAGE_DOWN,      // 6
    RESOURCE_ID_IMAGE_DOWNDOWN,  // 7
    RESOURCE_ID_IMAGE_LOGO,      // 8
    RESOURCE_ID_IMAGE_ERR,       // 9
};
static const uint32_t SPECIAL_RES[8] = {
    RESOURCE_ID_IMAGE_NONE,           // 0 none
    RESOURCE_ID_IMAGE_BROKEN_ANTENNA, // 1 ?NA ?RF
    RESOURCE_ID_IMAGE_BLOOD_DROP,     // 2 ?NC
    RESOURCE_ID_IMAGE_STOP_LIGHT,     // 3 ?SN ?MD ?CD
    RESOURCE_ID_IMAGE_HOURGLASS,      // 4 hourglass
    RESOURCE_ID_IMAGE_QUESTION_MARKS, // 5 ???
    RESOURCE_ID_IMAGE_LOGO,           // 6 loading
    RESOURCE_ID_IMAGE_ERR,            // 7
};

// Heart polygon (20x18 px)
static const GPathInfo HEART_PATH_INFO = {
    .num_points = 12,
    .points = (GPoint[]) {
        {10,18},{0,10},{0,5},{2,1},{5,0},{8,0},
        {10,3},{12,0},{15,0},{18,1},{20,5},{20,10},
    }
};

// ============================================================================
// App state
// ============================================================================
static char     s_bg_str[6]       = "---";
static char     s_icon_str[4]     = "0";
static char     s_delta_str[14]   = "";
static uint32_t s_cgm_time        = 0;
static char     s_phone_bat_str[6]= " ";
static char     s_temp_str[10]    = "--";
static bool     s_specvalue        = false;
static int      s_arrow_idx        = 0;
static bool     s_show_msg         = false;
static char     s_msg_text[13]    = "";
static int      s_hr_bpm           = 0;

// Timing
static AppTimer *s_cgm_timer  = NULL;
static AppTimer *s_bt_timer   = NULL;
static AppTimer *s_msg_timer  = NULL;
static uint8_t   s_cgm_min    = 0;
static uint32_t  s_msg_tmout  = 15000;
static bool      s_show_secs  = false;
static char      s_timefmt[10]= "%H:%M";

// Bluetooth
static bool s_bt_ok        = true;
static bool s_bt_alert     = false;
static bool s_bt_timer_pop = false;

// Settings
static bool s_no_vibe     = true;
static bool s_vibe_repeat = true;
static bool s_backlight   = false;

// Trend buffer (received, not displayed in this layout)
static uint8_t  *s_trend_buf = NULL;
static uint16_t  s_trend_len = 0;
static uint16_t  s_trend_exp = 0;

// ============================================================================
// Helpers
// ============================================================================
// Custom-drawn trend arrows (indices 1-7)
static void icon_layer_proc(Layer *layer, GContext *ctx) {
    // Special-value or loading bitmap takes priority
    if (s_icon_bitmap) {
        graphics_context_set_compositing_mode(ctx, GCompOpSet);
        graphics_draw_bitmap_in_rect(ctx, s_icon_bitmap, layer_get_bounds(layer));
        return;
    }
    if (s_arrow_idx < 1 || s_arrow_idx > 7) return;

    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, 2);

    switch (s_arrow_idx) {
        case 1: // Double up — two upward chevrons
            graphics_draw_line(ctx, GPoint(8, 16), GPoint(16, 7));
            graphics_draw_line(ctx, GPoint(16, 7), GPoint(24, 16));
            graphics_draw_line(ctx, GPoint(8, 24), GPoint(16, 15));
            graphics_draw_line(ctx, GPoint(16, 15), GPoint(24, 24));
            break;
        case 2: // Single up — shaft + arrowhead
            graphics_draw_line(ctx, GPoint(16, 25), GPoint(16, 8));
            graphics_draw_line(ctx, GPoint(16, 8), GPoint(9, 16));
            graphics_draw_line(ctx, GPoint(16, 8), GPoint(23, 16));
            break;
        case 3: // Up 45° — diagonal shaft + corner head
            graphics_draw_line(ctx, GPoint(6, 25), GPoint(25, 6));
            graphics_draw_line(ctx, GPoint(25, 6), GPoint(25, 14));
            graphics_draw_line(ctx, GPoint(25, 6), GPoint(17, 6));
            break;
        case 4: // Flat — horizontal shaft + arrowhead
            graphics_draw_line(ctx, GPoint(5, 16), GPoint(24, 16));
            graphics_draw_line(ctx, GPoint(24, 16), GPoint(17, 9));
            graphics_draw_line(ctx, GPoint(24, 16), GPoint(17, 23));
            break;
        case 5: // Down 45° — diagonal shaft + corner head
            graphics_draw_line(ctx, GPoint(6, 6), GPoint(25, 25));
            graphics_draw_line(ctx, GPoint(25, 25), GPoint(25, 17));
            graphics_draw_line(ctx, GPoint(25, 25), GPoint(17, 25));
            break;
        case 6: // Single down — shaft + arrowhead
            graphics_draw_line(ctx, GPoint(16, 7), GPoint(16, 24));
            graphics_draw_line(ctx, GPoint(16, 24), GPoint(9, 16));
            graphics_draw_line(ctx, GPoint(16, 24), GPoint(23, 16));
            break;
        case 7: // Double down — two downward chevrons
            graphics_draw_line(ctx, GPoint(8, 8), GPoint(16, 17));
            graphics_draw_line(ctx, GPoint(16, 17), GPoint(24, 8));
            graphics_draw_line(ctx, GPoint(8, 16), GPoint(16, 25));
            graphics_draw_line(ctx, GPoint(16, 25), GPoint(24, 16));
            break;
    }
    graphics_context_set_stroke_width(ctx, 1);
}

static void set_icon(uint32_t res_id) {
    if (s_icon_bitmap) { gbitmap_destroy(s_icon_bitmap); s_icon_bitmap = NULL; }
    if (res_id && res_id != RESOURCE_ID_IMAGE_NONE) {
        s_icon_bitmap = gbitmap_create_with_resource(res_id);
    }
    if (s_icon_layer) layer_mark_dirty(s_icon_layer);
}

static void vibrate(uint8_t level) {
    if (s_no_vibe || level == 0) return;
    static const uint32_t low[] = {75,50,50,50,75,50,50,50,75,50,50,50,75};
    static const uint32_t med[] = {500,100,100,100,500,100,100,100,500};
    static const uint32_t hi[]  = {300,100,50,100,300,100,50,100,300,100,50,100,300};
    VibePattern p;
    switch (level) {
        case 1:  p = (VibePattern){.durations=low, .num_segments=13}; break;
        case 2:  p = (VibePattern){.durations=med, .num_segments=9};  break;
        default: p = (VibePattern){.durations=hi,  .num_segments=13}; break;
    }
    vibes_enqueue_custom_pattern(p);
}

// ============================================================================
// Canvas — frame, dividers, section labels
// ============================================================================
static void canvas_update_proc(Layer *layer, GContext *ctx) {
    // Lines removed — trying clean look without dividers
    (void)ctx;
}

// ============================================================================
// Icon update procs
// ============================================================================
static void phone_icon_proc(Layer *layer, GContext *ctx) {
    GRect b = layer_get_bounds(layer);
    int w = b.size.w-4, h = b.size.h-4;
    int x = (b.size.w-w)/2, y = (b.size.h-h)/2;
    graphics_context_set_stroke_color(ctx, GColorWhite);
    // Outline only — no fill
    graphics_draw_round_rect(ctx, GRect(x, y, w, h), 1);
}

static void watch_icon_proc(Layer *layer, GContext *ctx) {
    GRect b = layer_get_bounds(layer);
    int cx = b.size.w/2, cy = b.size.h/2;
    int r  = b.size.w/2 - 4;   // smaller circle
    int sw = 7;                  // wider strap
    int sh = cy - r;
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_fill_color(ctx, GColorWhite);
    if (sh > 0) {
        graphics_fill_rect(ctx, GRect(cx - sw/2, 0, sw, sh), 0, GCornerNone);
        graphics_fill_rect(ctx, GRect(cx - sw/2, cy+r, sw, sh), 0, GCornerNone);
    }
    graphics_draw_circle(ctx, GPoint(cx, cy), r);
}

// Thermometer: tube + bulb, fills its layer bounds
static void therm_icon_proc(Layer *layer, GContext *ctx) {
    GRect b = layer_get_bounds(layer);
    int cx     = b.size.w / 2;
    int bulb_y = b.size.h - 7;
    int bulb_r = 5;
    int t_top  = 3;
    int t_h    = bulb_y - t_top;

    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_fill_color(ctx, GColorWhite);
    // Tube outline
    graphics_draw_round_rect(ctx, GRect(cx-2, t_top, 5, t_h + 2), 1);
    // Bulb outline
    graphics_draw_circle(ctx, GPoint(cx, bulb_y), bulb_r);
    // Mercury fill: lower half of tube + bulb interior
    graphics_fill_rect(ctx, GRect(cx-1, t_top + t_h/2, 3, t_h/2 + 1), 0, GCornerNone);
    graphics_fill_circle(ctx, GPoint(cx, bulb_y), bulb_r - 1);
}

// BPM text (left 3/4) + heart icon (right 1/4)
static void hr_layer_proc(Layer *layer, GContext *ctx) {
    GRect b = layer_get_bounds(layer);
    int icon_w = b.size.w / 4;
    int text_w = b.size.w - icon_w;

    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_context_set_stroke_color(ctx, GColorWhite);

    // BPM text in left 3/4
    static char buf[5];
    if (s_hr_bpm > 0) snprintf(buf, sizeof(buf), "%d", s_hr_bpm);
    else               snprintf(buf, sizeof(buf), "--");
    graphics_draw_text(ctx, buf,
        fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
        GRect(0, 0, text_w - 2, b.size.h),
        GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    // Heart centred in right icon column (20x18 path)
    int hx = text_w + (icon_w - 20) / 2;
    int hy = (b.size.h - 19) / 2;
    gpath_move_to(s_heart_path, GPoint(hx, hy));
    gpath_draw_filled(ctx, s_heart_path);
}

// ============================================================================
// Battery handler
// ============================================================================
static void battery_handler(BatteryChargeState state) {
    static char buf[6];
    snprintf(buf, sizeof(buf), "%d%%", state.charge_percent);
    if (!s_watch_bat) return;

    text_layer_set_text_color(s_watch_bat, GColorWhite);
    text_layer_set_text(s_watch_bat, buf);
    if (s_backlight) light_enable(state.is_plugged);
}

// ============================================================================
// Bluetooth
// ============================================================================
static void bt_timer_cb(void *data);

static void bt_handler(bool connected) {
    if (!connected) {
        if (s_bt_alert) return;
        if (s_bt_timer == NULL) {
            if (!s_bt_timer_pop) {
                s_bt_timer = app_timer_register(10000, bt_timer_cb, NULL);
                return;
            }
        } else { return; }
        vibrate(1);
        s_bt_alert = true;
        if (s_vibe_repeat) s_bt_timer_pop = false;
        if (s_delta_layer) text_layer_set_text(s_delta_layer, "NO BT");
        if (s_cgmtime_layer) text_layer_set_text(s_cgmtime_layer, "");
    } else {
        s_bt_alert = false;
        if (s_bt_timer == NULL) s_bt_timer_pop = false;
        if (s_delta_layer) text_layer_set_text_color(s_delta_layer, GColorWhite);
    }
}

static void bt_timer_cb(void *data) {
    s_bt_timer_pop = true;
    s_bt_timer = NULL;
    s_bt_ok = bluetooth_connection_service_peek();
    bt_handler(s_bt_ok);
}

// ============================================================================
// CGM display updaters
// ============================================================================
static void update_icon(void) {
    if (s_specvalue) return;
    // Clear any special-value bitmap, set arrow index for custom drawing
    if (s_icon_bitmap) { gbitmap_destroy(s_icon_bitmap); s_icon_bitmap = NULL; }
    int idx = s_icon_str[0] - '0';
    s_arrow_idx = (idx >= 1 && idx <= 7) ? idx : 0;
    if (s_icon_layer) layer_mark_dirty(s_icon_layer);
}

// Returns color for BG value: red=low, green=normal, blue=high
static GColor get_bg_color(void) {
    if (s_bg_str[0] == '-') return GColorGreen;   // "---" no data yet
    if (strcmp(s_bg_str, "LOW") == 0) return GColorRed;
    if (strcmp(s_bg_str, "HIGH") == 0) return GColorVividCerulean;
    if (s_specvalue) return GColorGreen;

    bool is_mmol = false;
    int v = 0;
    for (int i = 0; s_bg_str[i]; i++) {
        if (s_bg_str[i] == '.' || s_bg_str[i] == ',') is_mmol = true;
        else if (s_bg_str[i] >= '0' && s_bg_str[i] <= '9')
            v = v * 10 + (s_bg_str[i] - '0');
    }
    // mmol stored as int*10 (e.g. "7.5" → v=75)
    // Low: <4.0 mmol / <70 mg/dL  |  High: >=10.0 mmol / >=180 mg/dL
    if (is_mmol) {
        if (v < 40) return GColorRed;
        if (v >= 100) return GColorVividCerulean;
    } else {
        if (v < 70) return GColorRed;
        if (v >= 180) return GColorVividCerulean;
    }
    return GColorGreen;
}

static void update_bg(void) {
    s_specvalue = false;
    if (s_icon_bitmap) { gbitmap_destroy(s_icon_bitmap); s_icon_bitmap = NULL; }

    static const char * const SPEC_VALS[] =
        {"?SN","?MD","?NA","?NC","?CD","hourglass","???","?RF"};
    static const uint8_t SPEC_IDX[] = {3,3,1,2,3,4,5,1};

    for (int i = 0; i < 8; i++) {
        if (strcmp(s_bg_str, SPEC_VALS[i]) == 0) {
            text_layer_set_text(s_bg_layer, "");
            set_icon(SPECIAL_RES[SPEC_IDX[i]]);
            s_specvalue = true;
            return;
        }
    }

    if (s_bg_str[0] == '-' && strcmp(s_bg_str, "---") != 0) {
        text_layer_set_text(s_bg_layer, "ERR");
        text_layer_set_text_color(s_bg_layer, GColorGreen);
        return;
    }
    text_layer_set_text(s_bg_layer, s_bg_str);
    text_layer_set_text_color(s_bg_layer, get_bg_color());
}

static void update_delta(void) {
    if (!s_bt_ok) return;
    if (strcmp(s_delta_str, "LOAD") == 0) {
        text_layer_set_text(s_bg_layer, " ");
        text_layer_set_text(s_delta_layer, "LOADING");
        set_icon(SPECIAL_RES[6]);
        s_specvalue = false;
        return;
    }
    text_layer_set_text(s_delta_layer, s_delta_str);
    text_layer_set_text_color(s_delta_layer, GColorWhite);
}

static void update_time_ago(void) {
    if (s_cgm_time == 0) { text_layer_set_text(s_cgmtime_layer, ""); return; }
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    time_t off = lt->tm_gmtoff + (lt->tm_isdst > 0 ? 3600 : 0);
    int32_t elapsed = (int32_t)(now + off) - (int32_t)s_cgm_time;
    if (elapsed < 0) elapsed = -elapsed;
    static char buf[8];
    if (elapsed < 60)    snprintf(buf, sizeof(buf), "now");
    else if (elapsed < 3600) snprintf(buf, sizeof(buf), "%dm", (int)(elapsed/60));
    else if (elapsed < 86400) snprintf(buf, sizeof(buf), "%dh", (int)(elapsed/3600));
    else snprintf(buf, sizeof(buf), "---");
    text_layer_set_text(s_cgmtime_layer, buf);
}

static void update_phone_bat(void) {
    if (strcmp(s_phone_bat_str, " ") == 0) {
        text_layer_set_text(s_phone_bat, "--");
        text_layer_set_text_color(s_phone_bat, GColorDarkGray);
        return;
    }
    int val = 0;
    for (int i = 0; s_phone_bat_str[i]; i++) {
        if (s_phone_bat_str[i] >= '0' && s_phone_bat_str[i] <= '9')
            val = val*10 + s_phone_bat_str[i]-'0';
    }
    if (val < 0 || val > 100) { text_layer_set_text(s_phone_bat, "ERR"); return; }
    static char buf[6];
    snprintf(buf, sizeof(buf), "%d%%", val);
    text_layer_set_text(s_phone_bat, buf);
    text_layer_set_text_color(s_phone_bat, GColorWhite);
}

static void update_time_date(struct tm *t) {
    static char tbuf[10], dbuf[13];
    strftime(tbuf, sizeof(tbuf), s_timefmt, t);
    text_layer_set_text(s_time_layer, tbuf);
    strftime(dbuf, sizeof(dbuf), "%a %d %b", t);
    text_layer_set_text(s_date_layer, dbuf);
}

static void update_hr(void) {
    if (!s_hr_layer) return;
    HealthServiceAccessibilityMask m =
        health_service_metric_accessible(HealthMetricHeartRateBPM, time(NULL), time(NULL));
    s_hr_bpm = (m & HealthServiceAccessibilityMaskAvailable)
        ? (int)health_service_peek_current_value(HealthMetricHeartRateBPM) : 0;
    layer_mark_dirty(s_hr_layer);
}

// ============================================================================
// Message blink
// ============================================================================
static void msg_timer_cb(void *data) {
    if (s_msg_layer) {
        if (s_show_msg) {
            layer_set_hidden((Layer*)s_msg_layer,
                !layer_get_hidden((Layer*)s_msg_layer));
        } else {
            layer_set_hidden((Layer*)s_msg_layer, true);
        }
    }
    s_msg_timer = app_timer_register(s_msg_tmout, msg_timer_cb, NULL);
}

// ============================================================================
// Sync to xDrip
// ============================================================================
static void send_sync(void) {
    if (s_bt_alert) return;
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
    dict_write_uint32(iter, CGM_SYNC_KEY, CGM_SYNC_KEY);
    dict_write_uint8(iter, PBL_PLATFORM_KEY, 4);
    dict_write_cstring(iter, PBL_APP_VER_KEY, FACE_VERSION);
    dict_write_end(iter);
    app_message_outbox_send();
}

// ============================================================================
// CGM timer
// ============================================================================
static void weather_req_cb(void *data) {
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
        dict_write_uint8(iter, WEATHER_REQ_KEY, 1);
        app_message_outbox_send();
    }
}

static void cgm_timer_cb(void *data) {
    s_cgm_timer = NULL;
    if (s_cgm_min == 0) {
        s_cgm_min = 6;
        send_sync();
    } else {
        s_cgm_min--;
        update_time_ago();
        update_delta();
    }
    s_cgm_timer = app_timer_register(60000, cgm_timer_cb, NULL);
}

// ============================================================================
// Tick handler
// ============================================================================
static void tick_handler(struct tm *t, TimeUnits changed) {
    update_time_date(t);

    // Request weather every 30 minutes
    if ((changed & MINUTE_UNIT) && t->tm_min % 30 == 0) {
        DictionaryIterator *iter;
        if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
            dict_write_uint8(iter, WEATHER_REQ_KEY, 1);
            app_message_outbox_send();
        }
    }
}

// ============================================================================
// AppMessage inbox
// ============================================================================
static void inbox_received(DictionaryIterator *iter, void *ctx) {
    Tuple *t = dict_read_first(iter);
    while (t) {
        switch (t->key) {

        case CGM_ICON_KEY:
            strncpy(s_icon_str, t->value->cstring, sizeof(s_icon_str)-1);
            s_icon_str[sizeof(s_icon_str)-1] = '\0';
            update_icon();
            break;

        case CGM_BG_KEY:
            strncpy(s_bg_str, t->value->cstring, sizeof(s_bg_str)-1);
            s_bg_str[sizeof(s_bg_str)-1] = '\0';
            update_bg();
            break;

        case CGM_TCGM_KEY:
            s_cgm_time = t->value->uint32;
            update_time_ago();
            if (s_cgm_time != 0) s_cgm_min = 6;
            break;

        case CGM_DLTA_KEY:
            strncpy(s_delta_str, t->value->cstring, sizeof(s_delta_str)-1);
            s_delta_str[sizeof(s_delta_str)-1] = '\0';
            update_delta();
            break;

        case CGM_UBAT_KEY:
            strncpy(s_phone_bat_str, t->value->cstring, sizeof(s_phone_bat_str)-1);
            s_phone_bat_str[sizeof(s_phone_bat_str)-1] = '\0';
            update_phone_bat();
            break;

        case CGM_TREND_BEGIN_KEY:
            s_trend_exp = t->value->uint16;
            if (s_trend_buf) free(s_trend_buf);
            s_trend_buf = malloc(s_trend_exp);
            s_trend_len = 0;
            break;

        case CGM_TREND_DATA_KEY:
            if (s_trend_buf && (s_trend_len + t->length) <= s_trend_exp) {
                memcpy(s_trend_buf + s_trend_len, t->value->data, t->length);
                s_trend_len += t->length;
            }
            break;

        case CGM_TREND_END_KEY:
            if (s_trend_buf) { free(s_trend_buf); s_trend_buf = NULL; }
            s_trend_len = s_trend_exp = 0;
            break;

        case CGM_MESSAGE_KEY:
            snprintf(s_msg_text, sizeof(s_msg_text), "%s", t->value->cstring);
            text_layer_set_text(s_msg_layer, s_msg_text);
            if (strlen(t->value->cstring) == 0) {
                s_show_msg = false;
                layer_set_hidden((Layer*)s_msg_layer, true);
                layer_set_hidden((Layer*)s_delta_layer, false);
                layer_set_hidden((Layer*)s_cgmtime_layer, false);
            } else {
                s_show_msg = true;
                layer_set_hidden((Layer*)s_msg_layer, false);
                layer_set_hidden((Layer*)s_delta_layer, true);
                layer_set_hidden((Layer*)s_cgmtime_layer, true);
            }
            break;

        case CGM_VIBE_KEY:
            if (t->value->uint8 > 0 && t->value->uint8 < 4 && !s_bt_alert)
                vibrate(t->value->uint8);
            break;

        case CGM_SYNC_KEY:
            send_sync();
            break;

        case WEATHER_TEMP_KEY: {
            int temp = t->value->int32;
            snprintf(s_temp_str, sizeof(s_temp_str), "%d\xc2\xb0""C", temp);
            persist_write_int(WEATHER_TEMP_KEY, temp);
            if (s_temp_layer) text_layer_set_text(s_temp_layer, s_temp_str);
            break;
        }

        case WEATHER_COND_KEY:
            break;

        case SET_DISP_SECS:
            s_show_secs = (t->value->uint8 > 0);
            persist_write_bool(SET_DISP_SECS, s_show_secs);
            if (s_show_secs) {
                tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
                snprintf(s_timefmt, sizeof(s_timefmt), "%s",
                    clock_is_24h_style() ? "%H:%M:%S" : "%l:%M:%S");
            } else {
                tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
                snprintf(s_timefmt, sizeof(s_timefmt), "%s",
                    clock_is_24h_style() ? "%H:%M" : "%l:%M");
            }
            break;

        case SET_NO_VIBE:
            s_no_vibe = (t->value->uint8 > 0);
            persist_write_bool(SET_NO_VIBE, s_no_vibe);
            break;

        case SET_VIBE_REPEAT:
            s_vibe_repeat = (t->value->uint8 > 0);
            persist_write_bool(SET_VIBE_REPEAT, s_vibe_repeat);
            break;

        case SET_LIGHT_ON_CHG:
            s_backlight = (t->value->uint8 > 0);
            persist_write_bool(SET_LIGHT_ON_CHG, s_backlight);
            break;

        case SET_MESSAGE_TIMEOUT:
            s_msg_tmout = (uint32_t)t->value->int32 * 1000;
            persist_write_int(SET_MESSAGE_TIMEOUT, (int32_t)s_msg_tmout);
            if (!app_timer_reschedule(s_msg_timer, s_msg_tmout))
                s_msg_timer = app_timer_register(s_msg_tmout, msg_timer_cb, NULL);
            break;

        default: break;
        }
        t = dict_read_next(iter);
    }
}

// ============================================================================
// Health (HR)
// ============================================================================
static void health_handler(HealthEventType event, void *ctx) {
    if (event == HealthEventHeartRateUpdate) update_hr();
}

// ============================================================================
// Window load
// ============================================================================
static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);

    // Canvas drawn first — behind everything else
    s_canvas = layer_create(GRect(0, 0, 200, 228));
    layer_set_update_proc(s_canvas, canvas_update_proc);
    layer_add_child(root, s_canvas);

    // ── BG section — left/right split, no separator ──────────────────────────
    // LEFT column (x=3..99): glucose value, center-aligned, full height
    s_bg_layer = text_layer_create(GRect(3, 4, 96, 60));
    text_layer_set_background_color(s_bg_layer, GColorClear);
    text_layer_set_text_color(s_bg_layer, GColorGreen);
    text_layer_set_font(s_bg_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
    text_layer_set_text_alignment(s_bg_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_bg_layer));

    // RIGHT column (x=100..196): time ago top-right, arrow centred, delta bottom
    // Time ago — pinned to top-right corner
    s_cgmtime_layer = text_layer_create(GRect(100, 4, 93, 14));
    text_layer_set_background_color(s_cgmtime_layer, GColorClear);
    text_layer_set_text_color(s_cgmtime_layer, GColorWhite);
    text_layer_set_font(s_cgmtime_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
    text_layer_set_text_alignment(s_cgmtime_layer, GTextAlignmentRight);
    layer_add_child(root, text_layer_get_layer(s_cgmtime_layer));

    // Trend arrow — centred in right column (column centre x=148, y=34)
    s_icon_layer = layer_create(GRect(132, 18, 32, 32));
    layer_set_update_proc(s_icon_layer, icon_layer_proc);
    layer_add_child(root, s_icon_layer);

    // Delta — small text at bottom of right column (alerts / BT status)
    s_delta_layer = text_layer_create(GRect(100, 50, 93, 14));
    text_layer_set_background_color(s_delta_layer, GColorClear);
    text_layer_set_text_color(s_delta_layer, GColorWhite);
    text_layer_set_font(s_delta_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
    text_layer_set_text_alignment(s_delta_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_delta_layer));

    // Message overlay — covers full right column for alert text
    s_msg_layer = text_layer_create(GRect(100, 4, 93, 60));
    text_layer_set_background_color(s_msg_layer, GColorBlack);
    text_layer_set_text_color(s_msg_layer, GColorRed);
    text_layer_set_font(s_msg_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    text_layer_set_text_alignment(s_msg_layer, GTextAlignmentCenter);
    layer_set_hidden((Layer*)s_msg_layer, true);
    layer_add_child(root, text_layer_get_layer(s_msg_layer));

    // ── Time section (y=68..126) — LECO LCD digits, vertically centred ────────
    s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_GOTHAM_BOLD_60));
    s_time_layer = text_layer_create(GRect(4, 66, 192, 61));
    text_layer_set_background_color(s_time_layer, GColorClear);
    text_layer_set_text_color(s_time_layer, GColorWhite);
    text_layer_set_font(s_time_layer, s_time_font);
    text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_time_layer));

    // ── Date section (y=128..151) ────────────────────────────────────────────
    s_date_layer = text_layer_create(GRect(4, 129, 192, 22));
    text_layer_set_background_color(s_date_layer, GColorClear);
    text_layer_set_text_color(s_date_layer, GColorWhite);
    text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_date_layer));

    // ── Weather / HR section (y=154..190, 36px) ──────────────────────────────
    // Each half-cell: icon left 1/4 (24px), value right 3/4 (71px)
    // Left cell: TEMP
    s_therm_icon = layer_create(GRect(4, 154, 24, 36));
    layer_set_update_proc(s_therm_icon, therm_icon_proc);
    layer_add_child(root, s_therm_icon);

    s_temp_layer = text_layer_create(GRect(28, 154, 71, 36));
    text_layer_set_background_color(s_temp_layer, GColorClear);
    text_layer_set_text_color(s_temp_layer, GColorWhite);
    text_layer_set_font(s_temp_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    text_layer_set_text_alignment(s_temp_layer, GTextAlignmentCenter);
    text_layer_set_text(s_temp_layer, s_temp_str);
    layer_add_child(root, text_layer_get_layer(s_temp_layer));

    // Right cell: HR (icon+text drawn together in proc using 1/4+3/4)
    s_hr_layer = layer_create(GRect(101, 154, 95, 36));
    layer_set_update_proc(s_hr_layer, hr_layer_proc);
    layer_add_child(root, s_hr_layer);

    // ── Battery section (y=191..224, 33px) ───────────────────────────────────
    // Each half-cell: icon left 1/4 (24px), value right 3/4 (71px)
    // Left cell: Phone
    s_phone_icon = layer_create(GRect(4, 191, 24, 33));
    layer_set_update_proc(s_phone_icon, phone_icon_proc);
    layer_add_child(root, s_phone_icon);

    s_phone_bat = text_layer_create(GRect(28, 191, 71, 33));
    text_layer_set_background_color(s_phone_bat, GColorClear);
    text_layer_set_text_color(s_phone_bat, GColorWhite);
    text_layer_set_font(s_phone_bat, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    text_layer_set_text_alignment(s_phone_bat, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_phone_bat));

    // Right cell: Watch — value left 3/4, icon right 1/4
    s_watch_bat = text_layer_create(GRect(101, 191, 71, 33));
    text_layer_set_background_color(s_watch_bat, GColorClear);
    text_layer_set_text_color(s_watch_bat, GColorWhite);
    text_layer_set_font(s_watch_bat, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    text_layer_set_text_alignment(s_watch_bat, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_watch_bat));

    s_watch_icon = layer_create(GRect(172, 191, 24, 33));
    layer_set_update_proc(s_watch_icon, watch_icon_proc);
    layer_add_child(root, s_watch_icon);

    // ── Create heart path and initial data load ───────────────────────────────
    s_heart_path = gpath_create(&HEART_PATH_INFO);

    update_bg();
    update_icon();
    update_delta();
    update_time_ago();
    update_phone_bat();
    update_hr();
    battery_handler(battery_state_service_peek());

    time_t now = time(NULL);
    update_time_date(localtime(&now));
}

// ============================================================================
// Window unload
// ============================================================================
static void window_unload(Window *window) {
    if (s_heart_path) { gpath_destroy(s_heart_path); s_heart_path = NULL; }

    text_layer_destroy(s_bg_layer);
    layer_destroy(s_icon_layer);
    if (s_icon_bitmap) { gbitmap_destroy(s_icon_bitmap); s_icon_bitmap = NULL; }
    text_layer_destroy(s_delta_layer);
    text_layer_destroy(s_cgmtime_layer);
    text_layer_destroy(s_msg_layer);
    text_layer_destroy(s_time_layer);
    if (s_time_font) { fonts_unload_custom_font(s_time_font); s_time_font = NULL; }
    text_layer_destroy(s_date_layer);
    layer_destroy(s_therm_icon);
    text_layer_destroy(s_temp_layer);
    layer_destroy(s_hr_layer);
    layer_destroy(s_phone_icon);
    text_layer_destroy(s_phone_bat);
    layer_destroy(s_watch_icon);
    text_layer_destroy(s_watch_bat);
    layer_destroy(s_canvas);

    if (s_trend_buf) { free(s_trend_buf); s_trend_buf = NULL; }
}

// ============================================================================
// Init / Deinit
// ============================================================================
static void init(void) {
    s_show_secs   = persist_exists(SET_DISP_SECS)       ? persist_read_bool(SET_DISP_SECS) : false;
    s_vibe_repeat = persist_exists(SET_VIBE_REPEAT)     ? persist_read_bool(SET_VIBE_REPEAT) : true;
    s_no_vibe     = persist_exists(SET_NO_VIBE)         ? persist_read_bool(SET_NO_VIBE) : true;
    s_backlight   = persist_exists(SET_LIGHT_ON_CHG)    ? persist_read_bool(SET_LIGHT_ON_CHG) : false;
    s_msg_tmout   = persist_exists(SET_MESSAGE_TIMEOUT) ? (uint32_t)persist_read_int(SET_MESSAGE_TIMEOUT) : 15000;

    // Restore last known temperature so it shows immediately on launch
    if (persist_exists(WEATHER_TEMP_KEY)) {
        int saved_temp = persist_read_int(WEATHER_TEMP_KEY);
        snprintf(s_temp_str, sizeof(s_temp_str), "%dC", saved_temp);
    }

    snprintf(s_timefmt, sizeof(s_timefmt), "%s",
        clock_is_24h_style() ? "%H:%M" : "%l:%M");

    s_window = window_create();
    window_set_background_color(s_window, GColorBlack);
    window_set_window_handlers(s_window, (WindowHandlers){
        .load = window_load, .unload = window_unload
    });
    window_stack_push(s_window, true);

    // Register callbacks BEFORE opening AppMessage (skill requirement)
    app_message_register_inbox_received(inbox_received);
    app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    bluetooth_connection_service_subscribe(bt_handler);
    battery_state_service_subscribe(battery_handler);
    health_service_events_subscribe(health_handler, NULL);

    s_cgm_timer = app_timer_register(2000, cgm_timer_cb, NULL);
    s_msg_timer = app_timer_register(s_msg_tmout, msg_timer_cb, NULL);
    // Request weather shortly after launch (JS ready event should also do this,
    // but belt-and-suspenders in case the ready event fires before app_message opens)
    app_timer_register(6000, weather_req_cb, NULL);
}

static void deinit(void) {
    tick_timer_service_unsubscribe();
    bluetooth_connection_service_unsubscribe();
    battery_state_service_unsubscribe();
    health_service_events_unsubscribe();
    app_message_deregister_callbacks();

    if (s_cgm_timer) { app_timer_cancel(s_cgm_timer); s_cgm_timer = NULL; }
    if (s_bt_timer)  { app_timer_cancel(s_bt_timer);  s_bt_timer  = NULL; }
    if (s_msg_timer) { app_timer_cancel(s_msg_timer); s_msg_timer = NULL; }

    window_destroy(s_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
