
#include "hasplib.h"

#if defined(HASP_USE_CUSTOM) && true

#include "custom/my_custom.h"
#include "hasp/hasp_attribute.h"
#include "hasp/hasp_style.cpp"
#include "esp32-hal-i2c.h"
#include "hasp_debug.h"
#include "BH1750.h"


lv_obj_t      *p0b16, *p0b30, *p0b31, *p0b32, *p0b33, *p1b09,
              *p1b10, *p1b11, *p1b12, *p1b13, *p1b14, *p1b42,
              *p1b43, *p1b99, *p7b57, *p8b21, *p8b23, *p8b35,
              *p8b36, *p8b60, *p8b61, *p8b62,
              *obj_level_minus[6], *obj_level_plus[6], *obj_level_value[6],
              *obj_threshold_minus[5], *obj_threshold_plus[5], *obj_threshold_value[5];
lv_style_t    lv_style_default, lv_style_down, lv_style_up;

BH1750        light_meter;

const int     TAG_BACKLIGHT = 1,
              TAG_BH1750    = 2,
              TAG_LEVEL     = 3,
              TAG_THRESHOLD = 4;
float         lux_external, temperature,
              lux_internal = -1,
              lux_avarage  = -1;
short int     backlight_auto, backlight_level, backlight_power, backlight_raw, mtreg,
              active_page         = 1,
              backlight_level_new = 100,
              multiplier          = 1,
              p1b10_height        = 0,
              level[6]            = { 0 },
              threshold[5]        = { 0 };
volatile bool bh1750_initialized = false,
              i2cbus_initialized = false,
              failure            = false,
              started            = false,
              warnings           = true;


void custom_calibrate_bh1750()
{
    short int mtreg_new = (lux_internal > 40000.0) ? 32 : (lux_internal > 10.0) ? 69 : 138;
    if (mtreg != mtreg_new) {
        if (light_meter.setMTreg(mtreg_new)) {
            mtreg = mtreg_new;
            LOG_INFO(TAG_CUSTOM, "bh1750: mtreg set to %d", mtreg);
        } else {
            LOG_WARNING(TAG_CUSTOM, "bh1750: failed to set mtreg set to %d", mtreg);
        }
    }
}

void custom_dispatch_topic(const int* tag)
{
    static char buffer[50] = "";
    switch(*tag) {
        case TAG_BACKLIGHT:
            snprintf(buffer, sizeof(buffer), "{\"level\":%d,\"raw\":%d,\"auto\":%d}",
                     backlight_level, backlight_raw, backlight_auto);
            dispatch_state_subtopic("backlightinfo", buffer);
            break;
        case TAG_BH1750:
            snprintf(buffer, sizeof(buffer), "{\"bh1750\":%f}", lux_internal);
            dispatch_state_subtopic("illuminance", buffer);
            break;
        case TAG_LEVEL:
            snprintf(buffer, sizeof(buffer), "{\"l1\":%d,\"l2\":%d,\"l3\":%d,\"l4\":%d,\"l5\":%d}",
                     level[1], level[2], level[3], level[4], level[5]);
            dispatch_state_subtopic("autobacklightlevel", buffer);
            break;
        case TAG_THRESHOLD:
            snprintf(buffer, sizeof(buffer), "{\"t1\":%d,\"t2\":%d,\"t3\":%d,\"t4\":%d}",
                     threshold[1], threshold[2], threshold[3], threshold[4]);
            dispatch_state_subtopic("autobacklightthreshold", buffer);
            break;
    }
}

void custom_draw_backlight_bar(short int* backlight_level_new)
{
    static char lvbuffer[19] = "";
    short int   width        = 4 * *backlight_level_new,
                xpos         = 787 - width;
    snprintf(lvbuffer, sizeof(lvbuffer), "%i", *backlight_level_new);
    lv_label_set_text(p8b62, lvbuffer);
    lv_obj_set_x(p0b16, xpos);
    lv_obj_set_width(p0b16, width);
    if (*backlight_level_new > 30) {
        lv_obj_set_x(p0b30, xpos);
        lv_obj_set_width(p0b30, width);
        snprintf(lvbuffer, sizeof(lvbuffer), "#000000 %i%%", *backlight_level_new);
    } else {
        lv_obj_set_x(p0b30, xpos - 40);
        lv_obj_set_width(p0b30, 30);
        snprintf(lvbuffer, sizeof(lvbuffer), "#F0FFFF %i%%", *backlight_level_new);
    }
    lv_label_set_text(p0b30, lvbuffer);
}

void custom_draw_lux_bar(float *lux)
{
    if (*lux > -1) {
        static char lxbuffer[19]      = "";
        bool        overflow = *lux <= 200;
        short int   p1b10_height_new  = (*lux < 200) ? (short int)(*lux * 1.025) : 205;
        snprintf(lxbuffer, sizeof(lxbuffer), "%.1f lx", *lux);
        lv_label_set_text(p1b42, lxbuffer);
        if (p1b10_height != p1b10_height_new) {
            short int ypos = 220 - p1b10_height_new;
            lv_obj_set_y(p1b10, ypos);
            lv_obj_set_height(p1b10, p1b10_height_new);
            (*lux >= 200) ? lv_obj_set_height(p1b11, 2) : lv_obj_set_height(p1b11, 0);
            (*lux >= 150) ? lv_obj_set_height(p1b12, 2) : lv_obj_set_height(p1b12, 0);
            (*lux >= 100) ? lv_obj_set_height(p1b13, 2) : lv_obj_set_height(p1b13, 0);
            (*lux >= 50)  ? lv_obj_set_height(p1b14, 2) : lv_obj_set_height(p1b14, 0);
            lv_obj_set_hidden(p1b09, overflow);
            lv_obj_set_hidden(p1b99, overflow);
            p1b10_height = p1b10_height_new;
        }
    }
}

void custom_eval_backlight(float *lux)
{
    if (backlight_auto && backlight_power && *lux > -1) {
        short int backlight_raw_new = (*lux > threshold[4]) ? level[5] :
                                        (*lux > threshold[3]) ? level[4] :
                                          (*lux > threshold[2]) ? level[3] :
                                            (*lux > threshold[1]) ? level[2] : level[1];
        if (backlight_raw_new != backlight_raw) {
            haspDevice.set_backlight_level(backlight_raw_new);
            backlight_level_new = std::round(backlight_raw_new / 2.55);
        }
        if (backlight_level_new != backlight_level) {
            custom_draw_backlight_bar(&backlight_level_new);
        }
    }
}

static void custom_event_cb_backlight_minus_plus(lv_obj_t * obj, lv_event_t evnt)
{
    switch(evnt) {
        case LV_EVENT_PRESSED:
            lv_obj_add_style(obj, LV_LABEL_PART_MAIN, &lv_style_down);
            lv_obj_refresh_style(obj, LV_LABEL_PART_MAIN, LV_STYLE_PROP_ALL);
            break;
        case LV_EVENT_RELEASED:
            lv_obj_remove_style(obj, LV_LABEL_PART_MAIN, &lv_style_down);
            lv_obj_refresh_style(obj, LV_LABEL_PART_MAIN, LV_STYLE_PROP_ALL);
            if (!backlight_auto) {
                short int backlight_level_new = backlight_level;
                if (obj == p8b60 && (backlight_level - multiplier > 0)) {
                    backlight_level_new -= multiplier;
                } else if (obj == p8b61 && (backlight_level + multiplier < 101)) {
                    backlight_level_new += multiplier;
                }
                if (backlight_level_new != backlight_level) {
                    backlight_level = backlight_level_new;
                    haspDevice.set_backlight_level(backlight_level * 2.55);
                    custom_draw_backlight_bar(&backlight_level);
                }
            }
    }
}

static void custom_event_cb_level_minus(lv_obj_t * obj, lv_event_t evnt)
{
    switch(evnt) {
        case LV_EVENT_PRESSED:
            lv_obj_add_style(obj, LV_LABEL_PART_MAIN, &lv_style_down);
            lv_obj_refresh_style(obj, LV_LABEL_PART_MAIN, LV_STYLE_PROP_ALL);
            break;
        case LV_EVENT_RELEASED:
            lv_obj_remove_style(obj, LV_LABEL_PART_MAIN, &lv_style_down);
            lv_obj_refresh_style(obj, LV_LABEL_PART_MAIN, LV_STYLE_PROP_ALL);
            for (short int i = 1; i < 6; i++) {
                if (obj == obj_level_minus[i]) {
                    if (level[i] - multiplier > level[i - 1]) {
                        static char mbuffer[19] = "";
                        level[i] -= multiplier;
                        snprintf(mbuffer, sizeof(mbuffer), "%i", level[i]);
                        lv_label_set_text(obj_level_value[i], mbuffer);
                    }
                    break;
                }
            }
    }
}

static void custom_event_cb_level_plus(lv_obj_t * obj, lv_event_t evnt)
{
    switch(evnt) {
        case LV_EVENT_PRESSED:
            lv_obj_add_style(obj, LV_LABEL_PART_MAIN, &lv_style_down);
            lv_obj_refresh_style(obj, LV_LABEL_PART_MAIN, LV_STYLE_PROP_ALL);
            break;
        case LV_EVENT_RELEASED:
            lv_obj_remove_style(obj, LV_LABEL_PART_MAIN, &lv_style_down);
            lv_obj_refresh_style(obj, LV_LABEL_PART_MAIN, LV_STYLE_PROP_ALL);
            for (short int i = 1; i < 6; i++) {
                if (obj == obj_level_plus[i]) {
                    if (level[i] + multiplier < level[i + 1]) {
                        static char mbuffer[19] = "";
                        level[i] += multiplier;
                        snprintf(mbuffer, sizeof(mbuffer), "%i", level[i]);
                        lv_label_set_text(obj_level_value[i], mbuffer);
                    }
                    break;
                }
            }
    }
}

static void custom_event_cb_threshold_minus(lv_obj_t * obj, lv_event_t evnt)
{
    switch(evnt) {
        case LV_EVENT_PRESSED:
            lv_obj_add_style(obj, LV_LABEL_PART_MAIN, &lv_style_down);
            lv_obj_refresh_style(obj, LV_LABEL_PART_MAIN, LV_STYLE_PROP_ALL);
            break;
        case LV_EVENT_RELEASED:
            lv_obj_remove_style(obj, LV_LABEL_PART_MAIN, &lv_style_down);
            lv_obj_refresh_style(obj, LV_LABEL_PART_MAIN, LV_STYLE_PROP_ALL);
            for (short int i = 1; i < 5; i++) {
                if (obj == obj_threshold_minus[i]) {
                    if (threshold[i] - multiplier > threshold[i - 1]) {
                        static char mbuffer[19] = "";
                        threshold[i] -= multiplier;
                        snprintf(mbuffer, sizeof(mbuffer), "%i", threshold[i]);
                        lv_label_set_text(obj_threshold_value[i], mbuffer);
                    }
                    break;
                }
            }
    }
}

static void custom_event_cb_threshold_plus(lv_obj_t * obj, lv_event_t evnt)
{
    switch(evnt) {
        case LV_EVENT_PRESSED:
            lv_obj_add_style(obj, LV_LABEL_PART_MAIN, &lv_style_down);
            lv_obj_refresh_style(obj, LV_LABEL_PART_MAIN, LV_STYLE_PROP_ALL);
            break;
        case LV_EVENT_RELEASED:
            lv_obj_remove_style(obj, LV_LABEL_PART_MAIN, &lv_style_down);
            lv_obj_refresh_style(obj, LV_LABEL_PART_MAIN, LV_STYLE_PROP_ALL);
            for (short int i = 1; i < 5; i++) {
                if (obj == obj_threshold_plus[i]) {
                    if (threshold[i] + multiplier < threshold[i - 1]) {
                        static char mbuffer[19] = "";
                        threshold[i] += multiplier;
                        snprintf(mbuffer, sizeof(mbuffer), "%i", threshold[i]);
                        lv_label_set_text(obj_threshold_value[i], mbuffer);
                    }
                    break;
                }
            }
    }
}

static void custom_event_cb_toggle_btn(lv_obj_t * obj, lv_event_t evnt)
{
    switch(evnt) {
        case LV_EVENT_PRESSED:
            lv_obj_add_style(obj, LV_LABEL_PART_MAIN, &lv_style_down);
            lv_obj_refresh_style(obj, LV_LABEL_PART_MAIN, LV_STYLE_PROP_ALL);
            break;
        case LV_EVENT_RELEASED:
            lv_obj_remove_style(obj, LV_LABEL_PART_MAIN, &lv_style_down);
            lv_obj_refresh_style(obj, LV_LABEL_PART_MAIN, LV_STYLE_PROP_ALL);
            if (obj == p8b21) {
                backlight_auto = (backlight_auto == 1) ? 0 : 1;
                custom_set_backlight_auto();
                break;
            }
            if (obj == p8b23) {
                static char mbuffer[19] = "";
                multiplier = (multiplier == 1) ? 10 : 1;
                snprintf(mbuffer, sizeof(mbuffer), "x %i", multiplier);
                lv_label_set_text(p8b23, mbuffer);
                break;
            }
            if (obj == p8b35) {
                static char mbuffer[19] = "";
                lv_obj_set_hidden(p0b31, warnings);
                lv_obj_set_hidden(p0b32, warnings);
                lv_obj_set_hidden(p0b33, warnings);
                warnings = !warnings;
                warnings ? snprintf(mbuffer, sizeof(mbuffer), "An") :
                           snprintf(mbuffer, sizeof(mbuffer), "Aus");
                lv_label_set_text(p8b35, mbuffer);
                break;
            }
            if (obj == p8b36) {
                dispatch_run_script(NULL, "L:/page1.cmd", TAG_CUSTOM);
                break;
            }
    }
}

void custom_every_second()
{
    if (bh1750_initialized && light_meter.measurementReady(true)) {
        lux_internal = std::round((float)light_meter.readLightLevel());
    }

    if (started) {
        backlight_power = (short int)haspDevice.get_backlight_power();
        backlight_raw   = haspDevice.get_backlight_level() * backlight_power;
        backlight_level = std::round(backlight_raw / 2.55);
        temperature     = std::round(temperatureRead());
    }
}

void custom_every_5seconds()
{
    float lux = custom_find_lux_source();
    if (!started && !failure && millis() >= 20000) {
        started = custom_init_objects();
        failure = !started;
        (started) ? LOG_INFO(TAG_CUSTOM, "LVGL objects initialized") :
                    LOG_INFO(TAG_CUSTOM, "Error during LVGL object initialization");
    }
    if (started) {
        custom_eval_backlight(&lux);
        custom_draw_lux_bar(&lux);
    }
    if (bh1750_initialized) {
        custom_calibrate_bh1750();
    }
    if (started && bh1750_initialized) {
        static char bhbuffer[19] = "";
        snprintf(bhbuffer, sizeof(bhbuffer), "%f", lux_internal);
        lv_label_set_text(p7b57, bhbuffer);
    }
}

float custom_find_lux_source()
{
    float lux_tmp            = -1;
    static char lxsource[19] = "";
    if (lux_avarage > -1) {
        lux_tmp = lux_avarage;
        snprintf(lxsource, sizeof(lxsource), "(avarage)");
    } else if (lux_internal > -1) {
        lux_tmp = lux_internal;
        snprintf(lxsource, sizeof(lxsource), "(internal)");
    } else if (lux_external > -1) {
        lux_tmp = lux_external;
        snprintf(lxsource, sizeof(lxsource), "(external)");
    } else {
        snprintf(lxsource, sizeof(lxsource), "(none)");
    }
    if (started) {
        lv_label_set_text(p1b43, lxsource);
    }
    return lux_tmp;
}

void custom_get_sensors(JsonDocument& doc)
{
    if (started) {
        JsonObject internal = doc.createNestedObject("internal");
        internal["temperature"] = temperature;
        custom_dispatch_topic(&TAG_BACKLIGHT);
        custom_dispatch_topic(&TAG_LEVEL);
        custom_dispatch_topic(&TAG_THRESHOLD);
    }

    if (bh1750_initialized) {
        custom_dispatch_topic(&TAG_BH1750);
    }
}

bool custom_init_objects()
{
    bool        result     = true;
    static char mbuffer[19] = "";

    level[6] = 255;
    threshold[5] = 255;

    p0b16 = hasp_find_obj_from_page_id(0, 16);
    p0b30 = hasp_find_obj_from_page_id(0, 30);
    p0b31 = hasp_find_obj_from_page_id(0, 31);
    p0b32 = hasp_find_obj_from_page_id(0, 32);
    p0b33 = hasp_find_obj_from_page_id(0, 33);
    p1b09 = hasp_find_obj_from_page_id(1, 9);
    p1b10 = hasp_find_obj_from_page_id(1, 10);
    p1b11 = hasp_find_obj_from_page_id(1, 11);
    p1b12 = hasp_find_obj_from_page_id(1, 12);
    p1b13 = hasp_find_obj_from_page_id(1, 13);
    p1b14 = hasp_find_obj_from_page_id(1, 14);
    p1b42 = hasp_find_obj_from_page_id(1, 42);
    p1b43 = hasp_find_obj_from_page_id(1, 43);
    p1b99 = hasp_find_obj_from_page_id(1, 99);
    p7b57 = hasp_find_obj_from_page_id(7, 57);
    p8b21 = hasp_find_obj_from_page_id(8, 21);
    p8b23 = hasp_find_obj_from_page_id(8, 23);
    p8b35 = hasp_find_obj_from_page_id(8, 35);
    p8b36 = hasp_find_obj_from_page_id(8, 36);
    p8b60 = hasp_find_obj_from_page_id(8, 60);
    p8b61 = hasp_find_obj_from_page_id(8, 61);
    p8b62 = hasp_find_obj_from_page_id(8, 62);

    result = p0b16 && p0b30 && p0b31 && p0b32 && p0b33 && p1b09 && p1b10 &&
             p1b11 && p1b12 && p1b13 && p1b14 && p1b42 && p1b43 && p1b99 &&
             p7b57 && p8b21 && p8b23 && p8b35 && p8b36 && p8b60 && p8b61 &&
             p8b62;
    if (!result) {
        return result;
    }
    lv_obj_add_style(p8b21, LV_LABEL_PART_MAIN, &lv_style_default);
    lv_obj_refresh_style(p8b21, LV_LABEL_PART_MAIN, LV_STYLE_PROP_ALL);
    lv_obj_set_event_cb(p8b21, custom_event_cb_toggle_btn);

    lv_obj_add_style(p8b23, LV_LABEL_PART_MAIN, &lv_style_default);
    lv_obj_refresh_style(p8b23, LV_LABEL_PART_MAIN, LV_STYLE_PROP_ALL);
    lv_obj_set_event_cb(p8b23, custom_event_cb_toggle_btn);

    lv_obj_add_style(p8b35, LV_LABEL_PART_MAIN, &lv_style_default);
    lv_obj_refresh_style(p8b35, LV_LABEL_PART_MAIN, LV_STYLE_PROP_ALL);
    lv_obj_set_event_cb(p8b35, custom_event_cb_toggle_btn);

    lv_obj_add_style(p8b36, LV_LABEL_PART_MAIN, &lv_style_default);
    lv_obj_refresh_style(p8b36, LV_LABEL_PART_MAIN, LV_STYLE_PROP_ALL);
    lv_obj_set_event_cb(p8b36, custom_event_cb_toggle_btn);

    lv_obj_add_style(p8b60, LV_LABEL_PART_MAIN, &lv_style_default);
    lv_obj_refresh_style(p8b60, LV_LABEL_PART_MAIN, LV_STYLE_PROP_ALL);
    lv_obj_set_event_cb(p8b60, custom_event_cb_backlight_minus_plus);

    lv_obj_add_style(p8b61, LV_LABEL_PART_MAIN, &lv_style_default);
    lv_obj_refresh_style(p8b61, LV_LABEL_PART_MAIN, LV_STYLE_PROP_ALL);
    lv_obj_set_event_cb(p8b61, custom_event_cb_backlight_minus_plus);

    for(short int i = 1; i < 6; i++) {
        short int id = 60 + i * 3;
        obj_level_minus[i] = hasp_find_obj_from_page_id(8, id);
        obj_level_plus[i]  = hasp_find_obj_from_page_id(8, id + 1);
        obj_level_value[i] = hasp_find_obj_from_page_id(8, id + 2);
        result = result && obj_level_minus[i] && obj_level_plus[i] && obj_level_value[i];
        if (!result) {
            return false;
        }
        lv_obj_add_style(obj_level_minus[i], LV_LABEL_PART_MAIN, &lv_style_default);
        lv_obj_refresh_style(obj_level_minus[i], LV_LABEL_PART_MAIN, LV_STYLE_PROP_ALL);
        lv_obj_set_event_cb(obj_level_minus[1], custom_event_cb_level_minus);

        lv_obj_add_style(obj_level_plus[i], LV_LABEL_PART_MAIN, &lv_style_default);
        lv_obj_refresh_style(obj_level_plus[i], LV_LABEL_PART_MAIN, LV_STYLE_PROP_ALL);
        lv_obj_set_event_cb(obj_level_plus[1], custom_event_cb_level_plus);
        
        snprintf(mbuffer, sizeof(mbuffer), "%i", level[i]);
        lv_label_set_text(obj_level_value[i], mbuffer);
    }
    for(short int i = 1; i < 5; i++) {
        short int id = 75 + i * 3;
        obj_threshold_minus[i] = hasp_find_obj_from_page_id(8, id);
        obj_threshold_plus[i]  = hasp_find_obj_from_page_id(8, id + 1);
        obj_threshold_value[i] = hasp_find_obj_from_page_id(8, id + 2);
        result = result && obj_threshold_minus[i] && obj_threshold_plus[i] && obj_threshold_value[i];
        if (!result) {
            return false;
        }
        lv_obj_add_style(obj_threshold_minus[i], LV_LABEL_PART_MAIN, &lv_style_default);
        lv_obj_refresh_style(obj_threshold_minus[i], LV_LABEL_PART_MAIN, LV_STYLE_PROP_ALL);
        lv_obj_set_event_cb(obj_threshold_minus[1], custom_event_cb_threshold_minus);

        lv_obj_add_style(obj_threshold_plus[i], LV_LABEL_PART_MAIN, &lv_style_default);
        lv_obj_refresh_style(obj_threshold_plus[i], LV_LABEL_PART_MAIN, LV_STYLE_PROP_ALL);
        lv_obj_set_event_cb(obj_threshold_plus[1], custom_event_cb_threshold_plus);

        snprintf(mbuffer, sizeof(mbuffer), "%i", threshold[i]);
        lv_label_set_text(obj_threshold_value[i], mbuffer);
    }
    return result;
}

void custom_loop()
{
}

bool custom_pin_in_use(uint8_t pin)
{
    return (pin == 19) || (pin == 20);
}

void custom_set_backlight_auto()
{
    if (started) {
        static char mbuffer[19] = "";
        backlight_auto ? snprintf(mbuffer, sizeof(mbuffer), "An") :
                         snprintf(mbuffer, sizeof(mbuffer), "Aus");
        lv_label_set_text(p8b21, mbuffer);
        custom_dispatch_topic(&TAG_BACKLIGHT);
        if (warnings) {
            lv_obj_set_hidden(p0b33, !backlight_auto);
        }
    }
}

void custom_setup()
{
    lv_style_init(&lv_style_default);
    lv_style_set_bg_opa(&lv_style_default, LV_STATE_DEFAULT, LV_OPA_0);
    lv_style_set_bg_opa(&lv_style_default, LV_STATE_PRESSED, LV_OPA_100);
    lv_style_set_bg_color(&lv_style_default, LV_STATE_PRESSED, LV_COLOR_MAKE(0xF0, 0xFF, 0xFF));

    lv_style_init(&lv_style_down);
    lv_style_set_text_color(&lv_style_down, LV_LABEL_PART_MAIN, LV_COLOR_BLACK);

    lv_style_init(&lv_style_up);
    lv_style_set_text_color(&lv_style_up, LV_LABEL_PART_MAIN, LV_COLOR_MAKE(0xF0, 0xFF, 0xFF));

    if ((bool)i2cIsInit(0) && light_meter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
        mtreg = 69;
        bh1750_initialized = true;
        LOG_INFO(TAG_CUSTOM, "bh1750: Initialized sensor in Continous High Res Mode");
    } else {
        LOG_WARNING(TAG_CUSTOM, "bh1750: Error during sensor initialization");
    }
}

void custom_state_subtopic(const char* subtopic, const char* payload){
}

void custom_topic_payload(const char* topic, const char* payload, uint8_t source)
{
    if (strlen(payload)) {
        if (!strcmp(topic, "autobacklight")) {
            bool backlight_auto_new = (bool)std::atoi(payload);
            if (backlight_auto_new != backlight_auto) {
                backlight_auto = backlight_auto_new;
                custom_set_backlight_auto();
            }
        } else if (!strcmp(topic, "backlight")) {
            if (started && !backlight_auto) {
                backlight_raw   = std::atoi(payload);
                backlight_level = std::round(backlight_raw / 2.55);
                haspDevice.set_backlight_level(backlight_raw);
                custom_draw_backlight_bar(&backlight_level);
                custom_dispatch_topic(&TAG_BACKLIGHT);
            }
        } else if (!strcmp(topic, "lux_avarage")) {
            lux_avarage = std::atof(payload);
        } else if (strstr(topic, "threshold")) {
            short int index = std::stoi(std::string(1, topic[strlen(topic) - 1]));
            threshold[index] = std::atoi(payload);
            if (started) {
                static char mbuffer[19] = "";
                snprintf(mbuffer, sizeof(mbuffer), "%s", payload);
                lv_label_set_text(obj_threshold_value[index], mbuffer);
            }
        } else if (strstr(topic, "level")) {
            short int index = std::stoi(std::string(1, topic[strlen(topic) - 1]));
            level[index] = std::atoi(payload);
            if (started) {
                static char mbuffer[19] = "";
                snprintf(mbuffer, sizeof(mbuffer), "%s", payload);
                lv_label_set_text(obj_level_value[index], mbuffer);
            }
        }
    }
}

#endif
