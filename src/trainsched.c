#include <assert.h>
#include <pebble_os.h>
#include <pebble_app.h>
#include <pebble_fonts.h>
#include "mini-printf.h"

/* If you change these you'll want to tweak the UI layout to fit them */
#define NUM_STATIONS 3
#define MAX_LOCATION_LEN 11
#define ALARM_WARNING_MINUTES 15

/* UI defines */
#define STATION_FONT FONT_KEY_GOTHIC_24
#define STATION_FONT_BOLD FONT_KEY_GOTHIC_24_BOLD
#define STATUS_FONT FONT_KEY_GOTHIC_14
#define STATUS_H 16
#define SCREEN_H 168
#define SCREEN_W 144

#define STATION_Y 20
#define STATION_H 30
#define STATION_NAME_X 0
#define STATION_NAME_W 89
#define STATION_TIME_W 54
#define STATION_TIME_X 90

#define APP_STATUS_Y SCREEN_H-STATUS_H-20
#define APP_STATUS_H 17
#define APP_STATUS_W (SCREEN_W/3)
#define APP_STATUS_X(slot) (slot)*APP_STATUS_W

#define ALARM_X 0
#define NEXTTIME_X APP_STAT


#define APP_TITLE "Train Schedule"
#define APP_COMPANY "Zack Landau"
#define APP_VERSION_MAJOR 1
#define APP_VERSION_MINOR 0

#define MY_UUID { 0x38, 0xD8, 0x48, 0x10, 0x26, 0x73, 0x46, 0x64, 0xAE, 0x49, 0xEF, 0x1B, 0xA8, 0xED, 0x06, 0x52 }
PBL_APP_INFO(MY_UUID,
             APP_TITLE, APP_COMPANY,
             APP_VERSION_MAJOR, APP_VERSION_MINOR,
             DEFAULT_MENU_ICON,
             APP_INFO_STANDARD_APP);

#define N_OF(x) (sizeof((x)) / sizeof((x)[0]))
#define FIRST_STATION 0
#define ALARM_UNSET 0xFFFF

Window window;

struct {
  TextLayer stationName[NUM_STATIONS];
  TextLayer stationTime[NUM_STATIONS];

  TextLayer pageCount;
  TextLayer nextTime;

  /* This should be an alarm bitmap later.. */
  TextLayer alarmIcon;
} uiLayers;

#define TIME_STR_SIZE 7
struct {
  char stationTime[NUM_STATIONS][TIME_STR_SIZE];

  char pageCount[sizeof("(XX/YY)")];
  char alarmIcon[sizeof("A")];
  char nextTime[sizeof("XXhYYm")];
} strBuffers;

unsigned short scheduleIdx;
unsigned short scheduleAlarmIdx;

struct location_t {
  char name[MAX_LOCATION_LEN+1];
  PblTm time;
};

const struct schedule_t {
  struct location_t station[NUM_STATIONS];
} schedules[] = {
  #include "schedule.h"
};

int has_time_passed(const PblTm *curTime, const PblTm *cmpTime)
{
  if (cmpTime->tm_hour < curTime->tm_hour)
    return 1;
  if (cmpTime->tm_hour > curTime->tm_hour)
    return 0;
  return cmpTime->tm_min < curTime->tm_min;
}

int time_in_minutes(const PblTm *tm)
{
  return (tm->tm_hour * 60) + tm->tm_min;
}

void set_time_until(const PblTm *curTime, const PblTm *next_time)
{
  layer_set_hidden(&uiLayers.nextTime.layer, 0);

  int diff = time_in_minutes(next_time) - time_in_minutes(curTime);
  if (diff == 0) {
    strcpy(strBuffers.nextTime, "now");
  } else {
    if (diff >= 60) {
      mini_snprintf(strBuffers.nextTime, sizeof(strBuffers.nextTime),
                    "%dh%dm", diff / 60, diff % 60);
    } else {
      mini_snprintf(strBuffers.nextTime, sizeof(strBuffers.nextTime),
                    "%dm", diff % 60);
    }
  }
}

GFont get_entry_font(const PblTm *curTime, const PblTm *locationTime)
{
  if (has_time_passed(curTime, locationTime)) {
    return fonts_get_system_font(STATION_FONT_BOLD);
  } else {
    return fonts_get_system_font(STATION_FONT);
  }
}

unsigned int get_next_idx()
{
  PblTm curTime;
  unsigned int i;

  get_time(&curTime);

  for (i = 0; i < (unsigned short)N_OF(schedules); i++) {
    if (!has_time_passed(&curTime, &schedules[i].station[FIRST_STATION].time))
      return i;
  }

  return 0;
}

void format_time(const PblTm *time, char *buf)
{
    if (clock_is_24h_style()) {
      string_format_time(buf, TIME_STR_SIZE, "%H:%M", time);
    } else {
      string_format_time(buf, TIME_STR_SIZE, "%l:%Ma", time);
      if (time->tm_hour >= 12)
        buf[TIME_STR_SIZE-2] = 'p';
    }
}

void update_display()
{
  assert(N_OF(schedules) > 0);
  int i;

  const struct schedule_t *sched = &schedules[scheduleIdx];
  PblTm cur_time;

  get_time(&cur_time);

  for (i = 0; i < NUM_STATIONS; i++) {
    text_layer_set_text(&uiLayers.stationName[i], sched->station[i].name);
    text_layer_set_font(&uiLayers.stationName[i], get_entry_font(&cur_time, &sched->station[i].time));

    format_time(&sched->station[i].time, strBuffers.stationTime[i]);
    text_layer_set_text(&uiLayers.stationTime[i], strBuffers.stationTime[i]);
    text_layer_set_font(&uiLayers.stationTime[i], get_entry_font(&cur_time, &sched->station[i].time));
  }

  layer_set_hidden(&uiLayers.nextTime.layer, 1);

  for (i = 0; i < NUM_STATIONS; i++) {
    if (!has_time_passed(&cur_time, &sched->station[i].time)) {
      set_time_until(&cur_time, &sched->station[i].time);
      break;
    }
  }

  layer_set_hidden(&uiLayers.alarmIcon.layer, scheduleIdx != scheduleAlarmIdx);

  mini_snprintf(strBuffers.pageCount, sizeof(strBuffers.pageCount),
              "(%d/%d)", scheduleIdx+1, N_OF(schedules));
}

void select_schedule(void)
{
  assert(N_OF(schedules) > 0);
  scheduleIdx = get_next_idx();
}

void handle_tick(AppContextRef ctx, PebbleTickEvent *event)
{
  if (scheduleAlarmIdx != ALARM_UNSET) {
    PblTm time;
    const struct schedule_t *sched = &schedules[scheduleAlarmIdx];
    get_time(&time);

    if ((time_in_minutes(&sched->station[FIRST_STATION].time) - ALARM_WARNING_MINUTES) == time_in_minutes(&time)) {
      vibes_long_pulse();
      vibes_long_pulse();
      scheduleAlarmIdx = ALARM_UNSET;
    }
  }

  update_display();
}

void up_single_click_handler(ClickRecognizerRef recognizer, Window *window) {
  (void)recognizer;
  (void)window;

  if (scheduleIdx > 0) {
    scheduleIdx--;
    update_display();
  }
}

void down_single_click_handler(ClickRecognizerRef recognizer, Window *window) {
  (void)recognizer;
  (void)window;

  if ((scheduleIdx + 1) < (short)N_OF(schedules)) {
    scheduleIdx++;
    update_display();
  }
}

void select_single_click_handler(ClickRecognizerRef recognizer, Window *window) {
  (void)recognizer;
  (void)window;

  if (scheduleAlarmIdx == scheduleIdx) {
    scheduleAlarmIdx = ALARM_UNSET;
  } else {
    scheduleAlarmIdx = scheduleIdx;
  }

  update_display();
}

void select_long_click_handler(ClickRecognizerRef recognizer, Window *window) {
  (void)recognizer;
  (void)window;

  select_schedule();
  update_display();
}

void click_config_provider(ClickConfig **config, Window *window) {
  (void)window;

  config[BUTTON_ID_SELECT]->click.handler = (ClickHandler) select_single_click_handler;
  config[BUTTON_ID_SELECT]->long_click.handler = (ClickHandler) select_long_click_handler;

  config[BUTTON_ID_UP]->click.handler = (ClickHandler) up_single_click_handler;
  config[BUTTON_ID_UP]->click.repeat_interval_ms = 100;

  config[BUTTON_ID_DOWN]->click.handler = (ClickHandler) down_single_click_handler;
  config[BUTTON_ID_DOWN]->click.repeat_interval_ms = 100;
}

void handle_init(AppContextRef ctx) {
  (void)ctx;
  int i;

  window_init(&window, APP_TITLE);
  window_stack_push(&window, true /* Animated */);

  scheduleIdx = 0;
  scheduleAlarmIdx = ALARM_UNSET;

  for (i = 0; i < NUM_STATIONS; i++) {
    text_layer_init(&uiLayers.stationName[i], GRect(STATION_NAME_X, STATION_Y+(i*STATION_H), STATION_NAME_W, STATION_H));
    layer_add_child(&window.layer, &uiLayers.stationName[i].layer);

    text_layer_init(&uiLayers.stationTime[i], GRect(STATION_TIME_X, STATION_Y+(i*STATION_H), STATION_TIME_W, STATION_H));
    layer_add_child(&window.layer, &uiLayers.stationTime[i].layer);
  }

  text_layer_init(&uiLayers.alarmIcon, GRect(APP_STATUS_X(0), APP_STATUS_Y, APP_STATUS_W, APP_STATUS_H));
  text_layer_set_font(&uiLayers.alarmIcon, fonts_get_system_font(STATUS_FONT));
  strcpy(strBuffers.alarmIcon, "A");
  text_layer_set_text(&uiLayers.alarmIcon, strBuffers.alarmIcon);
  layer_set_hidden(&uiLayers.alarmIcon.layer, 1);
  layer_add_child(&window.layer, &uiLayers.alarmIcon.layer);

  text_layer_init(&uiLayers.nextTime, GRect(APP_STATUS_X(1), APP_STATUS_Y, APP_STATUS_W, APP_STATUS_H));
  text_layer_set_font(&uiLayers.nextTime, fonts_get_system_font(STATUS_FONT));
  text_layer_set_text(&uiLayers.nextTime, strBuffers.nextTime);
  text_layer_set_text_alignment(&uiLayers.nextTime, GTextAlignmentCenter);
  layer_set_hidden(&uiLayers.nextTime.layer, 1);
  layer_add_child(&window.layer, &uiLayers.nextTime.layer);

  text_layer_init(&uiLayers.pageCount, GRect(APP_STATUS_X(2), APP_STATUS_Y, APP_STATUS_W, APP_STATUS_H));
  text_layer_set_font(&uiLayers.pageCount, fonts_get_system_font(STATUS_FONT));
  text_layer_set_text(&uiLayers.pageCount, strBuffers.pageCount);
  text_layer_set_text_alignment(&uiLayers.pageCount, GTextAlignmentRight);
  layer_add_child(&window.layer, &uiLayers.pageCount.layer);

  select_schedule();
  update_display();

  window_set_click_config_provider(&window, (ClickConfigProvider)click_config_provider);
}

void pbl_main(void *params) {
  PebbleAppHandlers handlers = {
    .init_handler = &handle_init,
    .tick_info = {
      .tick_handler = &handle_tick,
      .tick_units = MINUTE_UNIT
    }
  };
  app_event_loop(params, &handlers);
}

/* vim: set ts=2 sw=2 sts=2 expandtab: */
