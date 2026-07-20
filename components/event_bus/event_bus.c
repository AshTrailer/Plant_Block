#include "event_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "EVENT_BUS";

#define MAX_SUBSCRIBERS_PER_EVENT 4

typedef struct {
   event_callback_t callback;
   void *user_ctx;
   bool used;
} subscriber_t;

typedef struct {
   subscriber_t subs[MAX_SUBSCRIBERS_PER_EVENT];
} event_channel_t;

static event_channel_t s_channels[EVENT_COUNT];
static SemaphoreHandle_t s_mutex = NULL;

void event_bus_init(void)
{
   s_mutex = xSemaphoreCreateMutex();
   memset(s_channels, 0, sizeof(s_channels));
   ESP_LOGI(TAG, "Event bus initialized, %d event types, max %d subscribers each",
            EVENT_COUNT, MAX_SUBSCRIBERS_PER_EVENT);
}

bool event_bus_subscribe(event_type_t type, event_callback_t cb, void *user_ctx)
{
   if (type >= EVENT_COUNT || cb == NULL) return false;
   if (s_mutex == NULL) return false;

   bool ok = false;
   xSemaphoreTake(s_mutex, portMAX_DELAY);

   for (int i = 0; i < MAX_SUBSCRIBERS_PER_EVENT; i++) {
      if (!s_channels[type].subs[i].used) {
         s_channels[type].subs[i].callback = cb;
         s_channels[type].subs[i].user_ctx = user_ctx;
         s_channels[type].subs[i].used = true;
         ok = true;
         break;
      }
   }

   xSemaphoreGive(s_mutex);

   if (!ok) {
      ESP_LOGW(TAG, "No free subscriber slot for event type %d", type);
   }
   return ok;
}

bool event_bus_unsubscribe(event_type_t type, event_callback_t cb)
{
   if (type >= EVENT_COUNT || cb == NULL) return false;
   if (s_mutex == NULL) return false;

   xSemaphoreTake(s_mutex, portMAX_DELAY);

   for (int i = 0; i < MAX_SUBSCRIBERS_PER_EVENT; i++) {
      if (s_channels[type].subs[i].used && s_channels[type].subs[i].callback == cb) {
         s_channels[type].subs[i].used = false;
         s_channels[type].subs[i].callback = NULL;
         s_channels[type].subs[i].user_ctx = NULL;
      }
   }

   xSemaphoreGive(s_mutex);
   return true;
}

void event_bus_publish(event_type_t type, const void *data, size_t len)
{
   if (type >= EVENT_COUNT) return;
   if (s_mutex == NULL) return;

   // 短暂持锁复制订阅者列表，避免在回调中死锁
   subscriber_t local_subs[MAX_SUBSCRIBERS_PER_EVENT];
   xSemaphoreTake(s_mutex, portMAX_DELAY);
   memcpy(local_subs, s_channels[type].subs, sizeof(local_subs));
   xSemaphoreGive(s_mutex);

   for (int i = 0; i < MAX_SUBSCRIBERS_PER_EVENT; i++) {
      if (local_subs[i].used && local_subs[i].callback) {
         local_subs[i].callback(type, data, len, local_subs[i].user_ctx);
      }
   }
}