
#ifndef HASP_CUSTOM_H
#define HASP_CUSTOM_H

#include "hasplib.h"
#if defined(HASP_USE_CUSTOM)

void custom_calibrate_bh1750();

void custom_draw_backlight_bar(short int* backlight_level_new);

void custom_dispatch_topic(const int* tag);

void custom_eval_backlight(float* lux);

void custom_every_second();

void custom_every_5seconds();

void custom_draw_lux_bar(float* lux);

float custom_find_lux_source();

void custom_get_sensors(JsonDocument& doc);

bool custom_init_objects();

void custom_loop();

bool custom_pin_in_use(uint8_t pin);

void custom_set_backlight_auto();

void custom_setting_decrease(short int* value, short int limiter, lv_obj_t* obj);

void custom_setting_increase(short int* value, short int limiter, lv_obj_t* obj);

void custom_setup();

void custom_state_subtopic(const char* subtopic, const char* payload);

void custom_topic_payload(const char* topic, const char* payload, uint8_t source);

#endif // HASP_USE_CUSTOM
#endif // HASP_CUSTOM_H
