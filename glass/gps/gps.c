#include "gm_plugin_lvgl_api.h"
#include "gm_plugin_libc.h"

/* Paired with PhoneSDK/examples/gps. A running / hiking heads-up display.
 * The phone owns the GPS (via its location permission) and streams a compact
 * metrics packet roughly once per second; the glasses show distance, elapsed
 * time, pace and heading in large glanceable text. The button toggles
 * start/pause on the phone (a long press asks the phone to reset the run). */

#define DOWNLINK_CHANNEL UINT16_C(0x4750) /* 'GP': phone -> glasses */
#define UPLINK_CHANNEL UINT16_C(0x4751)   /* 'GQ': glasses -> phone */
#define PROTOCOL_VERSION 1U

#define MSG_METRICS 1U
#define MSG_RESET 2U

#define EVENT_BUTTON UINT8_C(1)

#define FLAG_RUNNING UINT8_C(0x01)
#define FLAG_PAUSED UINT8_C(0x02)
#define FLAG_GPS_OK UINT8_C(0x04)

#define MIN_METRICS_PAYLOAD 15U

#define REFRESH_INTERVAL_MS 200U

#define SCREEN_MARGIN 20
#define DISTANCE_HEIGHT 48
#define TIME_HEIGHT 30
#define PACE_HEIGHT 30
#define HEADING_HEIGHT 30
#define HINT_HEIGHT 24

typedef struct {
    const gm_plugin_host_api_t *host;
    const gm_plugin_lvgl_api_t *ui;
    const gm_plugin_libc_extension_api_t *libc;
    gm_plugin_lvgl_obj_t *screen;
    gm_plugin_lvgl_obj_t *title_label;
    gm_plugin_lvgl_obj_t *distance_label;
    gm_plugin_lvgl_obj_t *time_label;
    gm_plugin_lvgl_obj_t *pace_label;
    gm_plugin_lvgl_obj_t *heading_label;
    gm_plugin_lvgl_obj_t *hint_label;

    uint32_t duration_sec;
    uint32_t distance_m;
    uint16_t pace_sec_km;
    uint16_t bearing;
    uint16_t speed_dm_s;
    uint8_t flags;
    bool have_metrics;
    uint32_t refresh_elapsed_ms;
    uint8_t language; /* 0=fr 1=en 2=es 3=it 4=de 5=zh-CN */
} gps_t;

#define L(fr_str, en_str, es_str, it_str, de_str, zh_str) \
    (gp.language == 1U ? (en_str) : gp.language == 2U ? (es_str) : \
     gp.language == 3U ? (it_str) : gp.language == 4U ? (de_str) : \
     gp.language == 5U ? (zh_str) : (fr_str))

static gps_t gps_state;
static gps_t *gps_ptr;

static uint8_t gps_detect_language(const char *locale)
{
    if (locale[0] == 'e' && locale[1] == 'n') return 1U;
    if (locale[0] == 'e' && locale[1] == 's') return 2U;
    if (locale[0] == 'i' && locale[1] == 't') return 3U;
    if (locale[0] == 'd' && locale[1] == 'e') return 4U;
    if (locale[0] == 'z' && locale[1] == 'h') return 5U;
    return 0U;
}
#define gp (*gps_ptr)

#define number gm_plugin_lvgl_style_number
#define color gm_plugin_lvgl_style_color

static void set_style(gm_plugin_lvgl_obj_t *object,
                       gm_plugin_lvgl_style_prop_t property,
                       gm_plugin_lvgl_style_value_t value)
{
    gp.ui->style_set(object, property, value, GM_PLUGIN_LVGL_SELECTOR_MAIN);
}

static void style_panel(gm_plugin_lvgl_obj_t *object, uint8_t fill,
                         uint8_t border, uint8_t radius)
{
    set_style(object, GM_PLUGIN_LVGL_STYLE_BG_COLOR, color(fill));
    set_style(object, GM_PLUGIN_LVGL_STYLE_BG_OPA, number(255));
    set_style(object, GM_PLUGIN_LVGL_STYLE_BORDER_COLOR, color(0xA0));
    set_style(object, GM_PLUGIN_LVGL_STYLE_BORDER_OPA, number(255));
    set_style(object, GM_PLUGIN_LVGL_STYLE_BORDER_WIDTH, number(border));
    set_style(object, GM_PLUGIN_LVGL_STYLE_RADIUS, number(radius));
    gp.ui->obj_clear_flag(object, GM_PLUGIN_LVGL_FLAG_SCROLLABLE);
}

static gm_plugin_lvgl_obj_t *make_label(gm_plugin_lvgl_obj_t *parent,
                                         const char *text, uint8_t shade)
{
    gm_plugin_lvgl_obj_t *label = gp.ui->label_create(parent);
    if (label == 0) return 0;
    gp.ui->label_set_text(label, text);
    gp.ui->label_set_long_mode(label, GM_PLUGIN_LVGL_LABEL_DOT);
    set_style(label, GM_PLUGIN_LVGL_STYLE_TEXT_COLOR, color(shade));
    set_style(label, GM_PLUGIN_LVGL_STYLE_TEXT_ALIGN,
              number(GM_PLUGIN_LVGL_TEXT_ALIGN_CENTER));
    return label;
}

static void use_large_font(gm_plugin_lvgl_obj_t *label)
{
    gm_plugin_lvgl_style_value_t large_font = {0};
    large_font.ptr = gp.ui->font_large;
    set_style(label, GM_PLUGIN_LVGL_STYLE_TEXT_FONT, large_font);
}

/* Formats the distance: metres below 1 km, kilometres with 2 decimals above. */
static void format_distance(char *buffer, uint32_t size)
{
    if (gp.distance_m < 1000U) {
        gp.libc->snprintf(buffer, size, "%u m", (unsigned int)gp.distance_m);
    } else {
        uint32_t km = gp.distance_m / 1000U;
        uint32_t hundredths = (gp.distance_m % 1000U) / 10U;
        gp.libc->snprintf(buffer, size, "%u.%02u km",
                          (unsigned int)km, (unsigned int)hundredths);
    }
}

static void format_duration(char *buffer, uint32_t size)
{
    uint32_t hours = gp.duration_sec / 3600U;
    uint32_t minutes = (gp.duration_sec % 3600U) / 60U;
    uint32_t seconds = gp.duration_sec % 60U;
    if (hours > 0U)
        gp.libc->snprintf(buffer, size, "%u:%02u:%02u",
                          (unsigned int)hours, (unsigned int)minutes,
                          (unsigned int)seconds);
    else
        gp.libc->snprintf(buffer, size, "%u:%02u",
                          (unsigned int)minutes, (unsigned int)seconds);
}

static const char *compass_point(uint16_t bearing)
{
    uint16_t index = (uint16_t)(((uint32_t)bearing + 22U) / 45U) % 8U;
    switch (index) {
    case 0: return "N";
    case 1: return "NE";
    case 2: return "E";
    case 3: return "SE";
    case 4: return "S";
    case 5: return "SO";
    case 6: return "O";
    default: return "NO";
    }
}

static void refresh_display(void)
{
    char text[48];

    if (!gp.have_metrics) {
        gp.ui->label_set_text(gp.distance_label, "--");
        gp.ui->label_set_text(gp.time_label, "0:00");
        gp.ui->label_set_text(gp.pace_label, "");
        gp.ui->label_set_text(gp.heading_label, "");
        gp.ui->label_set_text(gp.hint_label,
            L("En attente du telephone", "Waiting for the phone", "Esperando al teléfono", "In attesa del telefono", "Warte auf das Telefon", "正在等待手机"));
        return;
    }

    format_distance(text, sizeof(text));
    gp.ui->label_set_text(gp.distance_label, text);

    format_duration(text, sizeof(text));
    gp.ui->label_set_text(gp.time_label, text);

    if (!(gp.flags & FLAG_GPS_OK)) {
        gp.ui->label_set_text(gp.pace_label, L("Recherche GPS...", "Searching GPS...", "Buscando GPS...", "Ricerca GPS...", "Suche GPS...", "正在搜索 GPS..."));
        gp.ui->label_set_text(gp.heading_label, "");
    } else if (gp.pace_sec_km == 0U) {
        gp.ui->label_set_text(gp.pace_label, L("Allure --", "Pace --", "Ritmo --", "Ritmo --", "Tempo --", "配速 --"));
        gp.libc->snprintf(text, sizeof(text), "%s %u deg",
                          compass_point(gp.bearing), (unsigned int)gp.bearing);
        gp.ui->label_set_text(gp.heading_label, text);
    } else {
        gp.libc->snprintf(text, sizeof(text), L("Allure %u:%02u /km",
                          "Pace %u:%02u /km",
                          "Ritmo %u:%02u /km",
                          "Ritmo %u:%02u /km",
                          "Tempo %u:%02u /km",
                          "配速 %u:%02u /km"),
                          (unsigned int)(gp.pace_sec_km / 60U),
                          (unsigned int)(gp.pace_sec_km % 60U));
        gp.ui->label_set_text(gp.pace_label, text);
        gp.libc->snprintf(text, sizeof(text), "%s %u deg   %u.%u km/h",
                          compass_point(gp.bearing), (unsigned int)gp.bearing,
                          (unsigned int)(((uint32_t)gp.speed_dm_s * 36U) / 100U),
                          (unsigned int)(((uint32_t)gp.speed_dm_s * 36U) / 10U % 10U));
        gp.ui->label_set_text(gp.heading_label, text);
    }

    if (gp.flags & FLAG_PAUSED)
        gp.ui->label_set_text(gp.hint_label, L("Bouton : reprendre", "Button: resume", "Botón: reanudar", "Pulsante: riprendi", "Taste: Weiter", "按钮：继续"));
    else if (gp.flags & FLAG_RUNNING)
        gp.ui->label_set_text(gp.hint_label, L("Bouton : pause", "Button: pause", "Botón: pausa", "Pulsante: pausa", "Taste: Pause", "按钮：暂停"));
    else
        gp.ui->label_set_text(gp.hint_label, L("Bouton : demarrer", "Button: start", "Botón: iniciar", "Pulsante: avvia", "Taste: Start", "按钮：开始"));
}

static void handle_downlink(const uint8_t *data, uint32_t length)
{
    if (data == 0 || length < 2U || data[0] != PROTOCOL_VERSION) return;

    if (data[1] == MSG_RESET) {
        gp.duration_sec = 0U;
        gp.distance_m = 0U;
        gp.pace_sec_km = 0U;
        gp.bearing = 0U;
        gp.speed_dm_s = 0U;
        gp.flags = 0U;
        gp.have_metrics = true;
        refresh_display();
        return;
    }

    if (data[1] != MSG_METRICS || length < MIN_METRICS_PAYLOAD) return;

    gp.duration_sec = (uint32_t)data[2] | ((uint32_t)data[3] << 8);
    gp.distance_m = (uint32_t)data[4] | ((uint32_t)data[5] << 8) |
                    ((uint32_t)data[6] << 16) | ((uint32_t)data[7] << 24);
    gp.pace_sec_km = (uint16_t)(data[8] | (data[9] << 8));
    gp.bearing = (uint16_t)(data[10] | (data[11] << 8));
    gp.speed_dm_s = (uint16_t)(data[12] | (data[13] << 8));
    gp.flags = data[14];
    gp.have_metrics = true;
    refresh_display();
}

static gm_plugin_result_t gps_start(void *context)
{
    gm_plugin_display_info_t display;
    gm_plugin_lvgl_obj_t *root;
    char locale[GM_PLUGIN_LOCALE_TAG_MAX];
    (void)context;

    gp.language = 0U;
    if (gp.host->locale_get != 0 &&
        gp.host->locale_get(locale) == GM_PLUGIN_OK)
        gp.language = gps_detect_language(locale);

    if (gp.host->display_get_info(&display) != GM_PLUGIN_OK ||
        display.width <= SCREEN_MARGIN * 2U ||
        display.height < DISTANCE_HEIGHT + TIME_HEIGHT + PACE_HEIGHT +
                           HEADING_HEIGHT + HINT_HEIGHT + 60)
        return GM_PLUGIN_ESTATE;

    root = gp.ui->root_get();
    if (root == 0) return GM_PLUGIN_ESTATE;
    gp.ui->obj_clean(root);

    gp.screen = gp.ui->obj_create(root);
    if (gp.screen == 0) return GM_PLUGIN_ENOMEM;
    gp.ui->obj_set_size(gp.screen, display.width, display.height);
    gp.ui->obj_align(gp.screen, GM_PLUGIN_LVGL_ALIGN_CENTER, 0, 0);
    style_panel(gp.screen, 0x00, 0, 0);

    gp.title_label = make_label(gp.screen, L("Course", "Run", "Carrera", "Corsa", "Lauf", "跑步"), 0x80);
    if (gp.title_label == 0) goto no_memory;
    gp.ui->obj_set_size(gp.title_label, display.width - SCREEN_MARGIN * 2U, 24);
    gp.ui->obj_align(gp.title_label, GM_PLUGIN_LVGL_ALIGN_TOP_MID, 0, 8);

    gp.distance_label = make_label(gp.screen, "--", 0xFF);
    if (gp.distance_label == 0) goto no_memory;
    use_large_font(gp.distance_label);
    gp.ui->obj_set_size(gp.distance_label, display.width - SCREEN_MARGIN * 2U,
                        DISTANCE_HEIGHT);
    gp.ui->obj_align(gp.distance_label, GM_PLUGIN_LVGL_ALIGN_CENTER, 0, -46);

    gp.time_label = make_label(gp.screen, "", 0xE0);
    if (gp.time_label == 0) goto no_memory;
    use_large_font(gp.time_label);
    gp.ui->obj_set_size(gp.time_label, display.width - SCREEN_MARGIN * 2U, TIME_HEIGHT);
    gp.ui->obj_align(gp.time_label, GM_PLUGIN_LVGL_ALIGN_CENTER, 0, -2);

    gp.pace_label = make_label(gp.screen, "", 0xB0);
    if (gp.pace_label == 0) goto no_memory;
    gp.ui->obj_set_size(gp.pace_label, display.width - SCREEN_MARGIN * 2U, PACE_HEIGHT);
    gp.ui->obj_align(gp.pace_label, GM_PLUGIN_LVGL_ALIGN_CENTER, 0, 40);

    gp.heading_label = make_label(gp.screen, "", 0x90);
    if (gp.heading_label == 0) goto no_memory;
    gp.ui->obj_set_size(gp.heading_label, display.width - SCREEN_MARGIN * 2U, HEADING_HEIGHT);
    gp.ui->obj_align(gp.heading_label, GM_PLUGIN_LVGL_ALIGN_CENTER, 0, 72);

    gp.hint_label = make_label(gp.screen, "", 0x70);
    if (gp.hint_label == 0) goto no_memory;
    gp.ui->obj_set_size(gp.hint_label, display.width - SCREEN_MARGIN * 2U, HINT_HEIGHT);
    gp.ui->obj_align(gp.hint_label, GM_PLUGIN_LVGL_ALIGN_BOTTOM_MID, 0, -8);

    gp.have_metrics = false;
    gp.refresh_elapsed_ms = REFRESH_INTERVAL_MS;

    refresh_display();
    return GM_PLUGIN_OK;

no_memory:
    gp.ui->obj_clean(root);
    gp.screen = 0;
    gp.title_label = 0;
    gp.distance_label = 0;
    gp.time_label = 0;
    gp.pace_label = 0;
    gp.heading_label = 0;
    gp.hint_label = 0;
    return GM_PLUGIN_ENOMEM;
}

static void gps_loop(void *context, uint32_t elapsed_ms)
{
    (void)context;
    (void)elapsed_ms;

    if (gp.refresh_elapsed_ms < REFRESH_INTERVAL_MS) {
        uint32_t remaining = REFRESH_INTERVAL_MS - gp.refresh_elapsed_ms;
        gp.refresh_elapsed_ms = elapsed_ms >= remaining
            ? REFRESH_INTERVAL_MS : gp.refresh_elapsed_ms + elapsed_ms;
    }
    if (gp.refresh_elapsed_ms >= REFRESH_INTERVAL_MS) {
        gp.refresh_elapsed_ms = 0U;
        refresh_display();
    }
}

static bool gps_event(void *context, const gm_plugin_event_t *event)
{
    gps_t *self = &gp;
    (void)context;
    if (event == 0) return false;

    if (event->type == GM_PLUGIN_EVENT_BT_MESSAGE) {
        if (event->data.bt.channel != DOWNLINK_CHANNEL || event->data.bt.data == 0)
            return false;
        handle_downlink(event->data.bt.data, event->data.bt.length);
        return true;
    }

    if (event->type == GM_PLUGIN_EVENT_BUTTON &&
        event->data.button.button == GM_PLUGIN_BUTTON_PRIMARY) {
        uint8_t payload[3];
        if (event->data.button.action == GM_PLUGIN_BUTTON_ACTION_LONG) {
            payload[0] = PROTOCOL_VERSION;
            payload[1] = EVENT_BUTTON;
            payload[2] = 1U; /* long press: request a reset */
            (void)self->host->bt_send(UPLINK_CHANNEL, payload, sizeof(payload));
            return true;
        }
        if (event->data.button.action == GM_PLUGIN_BUTTON_ACTION_SINGLE) {
            payload[0] = PROTOCOL_VERSION;
            payload[1] = EVENT_BUTTON;
            payload[2] = 0U; /* short press: toggle start/pause */
            (void)self->host->bt_send(UPLINK_CHANNEL, payload, sizeof(payload));
            return true;
        }
    }
    return false;
}

static void gps_stop(void *context)
{
    gm_plugin_lvgl_obj_t *root;
    (void)context;
    root = gp.ui->root_get();
    if (root != 0) gp.ui->obj_clean(root);
    gp.screen = 0;
    gp.title_label = 0;
    gp.distance_label = 0;
    gp.time_label = 0;
    gp.pace_label = 0;
    gp.heading_label = 0;
    gp.hint_label = 0;
}

gm_plugin_result_t gm_plugin_entry(const gm_plugin_host_api_t *host,
                                    gm_plugin_descriptor_t *plugin)
{
    const gm_plugin_capabilities_t required =
        GM_PLUGIN_CAP_BLUETOOTH | GM_PLUGIN_CAP_BUTTON | GM_PLUGIN_CAP_LOCALE;

    gps_ptr = &gps_state;

    if (host == 0 || plugin == 0 || host->log == 0 || host->bt_send == 0 ||
        host->display_get_info == 0 || host->graphics.lvgl == 0 ||
        !GM_PLUGIN_VERSION_COMPATIBLE(host->abi_version,
                                      GM_PLUGIN_ABI_MIN_VERSION) ||
        host->struct_size < GM_PLUGIN_HOST_API_MIN_SIZE ||
        plugin->struct_size < GM_PLUGIN_DESCRIPTOR_MIN_SIZE ||
        (host->capabilities & required) != required)
        return GM_PLUGIN_ENOTSUP;

    gp.host = host;
    gp.ui = host->graphics.lvgl;
    if (gm_plugin_libc_get(host, &gp.libc) != GM_PLUGIN_OK)
        return GM_PLUGIN_ENOTSUP;
    if (gp.ui->struct_size < GM_PLUGIN_LVGL_API_MIN_SIZE ||
        !GM_PLUGIN_VERSION_COMPATIBLE(gp.ui->api_version,
                                      GM_PLUGIN_LVGL_API_MIN_VERSION))
        return GM_PLUGIN_EVERSION;

    plugin->abi_version = GM_PLUGIN_ABI_MIN_VERSION;
    plugin->context = &gp;
    plugin->on_start = gps_start;
    plugin->on_loop = gps_loop;
    plugin->on_event = gps_event;
    plugin->on_stop = gps_stop;
    return GM_PLUGIN_OK;
}
