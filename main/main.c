#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "lcd.h"

// ── config ────────────────────────────────────────────────────
#define WIFI_SSID       "XIAOU-RTX4V"
#define WIFI_PASS       "antiquev7"
#define BINANCE_URL     "https://api.binance.com/api/v3/ticker/price?symbol=BTCUSDT"
#define FETCH_DELAY_MS  10000

#define MQTT_BROKER     "mqtts_broker-link"
#define MQTT_PORT       PORT
#define MQTT_USER       "USER"
#define MQTT_PASS       "PASSWORD"
#define MQTT_TOPIC      "69series/alert"

static const char *TAG = "btc";

// ── shared state ──────────────────────────────────────────────
static SemaphoreHandle_t lcd_mutex;   // protect lcd from two tasks writing simultaneously

// ── http buffer ───────────────────────────────────────────────
static char http_buf[512];
static int  http_buf_len = 0;

// ── wifi ──────────────────────────────────────────────────────
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
        esp_wifi_connect();
    else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected, retrying...");
        esp_wifi_connect();
    }
    else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "WiFi connected!");
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                &wifi_event_handler, NULL);

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    esp_wifi_start();

    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                        false, true, portMAX_DELAY);
}

// ── http event handler ────────────────────────────────────────
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        int copy_len = evt->data_len;
        if (http_buf_len + copy_len < sizeof(http_buf)) {
            memcpy(http_buf + http_buf_len, evt->data, copy_len);
            http_buf_len += copy_len;
            http_buf[http_buf_len] = '\0';
        }
    }
    return ESP_OK;
}

// ── mqtt event handler ────────────────────────────────────────
static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch (event_id) {

        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected!");
            // subscribe to alert topic
            esp_mqtt_client_subscribe(event->client, MQTT_TOPIC, 1);
            ESP_LOGI(TAG, "Subscribed to: %s", MQTT_TOPIC);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT disconnected");
            break;

        case MQTT_EVENT_DATA:
            // new message arrived!
            ESP_LOGI(TAG, "MQTT message on topic: %.*s",
                     event->topic_len, event->topic);
            ESP_LOGI(TAG, "Message: %.*s",
                     event->data_len, event->data);

            // copy message to null-terminated buffer
            char msg[33];   // 32 chars max (2 lcd lines) + null
            int  len = event->data_len;
            if (len > 32) len = 32;
            memcpy(msg, event->data, len);
            msg[len] = '\0';

            // display on lcd — take mutex first
            if (xSemaphoreTake(lcd_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                lcd_clear();
                lcd_set_cursor(0, 0);
                // line 1: first 16 chars
                char line1[17] = {0};
                strncpy(line1, msg, 16);
                lcd_print(line1);

                // line 2: next 16 chars if message is longer
                if (len > 16) {
                    lcd_set_cursor(0, 1);
                    lcd_print(msg + 16);
                }
                xSemaphoreGive(lcd_mutex);
            }

            // hold message for 5 seconds then resume price display
            vTaskDelay(pdMS_TO_TICKS(5000));
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error");
            break;

        default:
            break;
    }
}

// ── mqtt init ─────────────────────────────────────────────────
static void mqtt_init(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri  = MQTT_BROKER,
                .port = MQTT_PORT,
            },
            .verification = {
                .crt_bundle_attach = esp_crt_bundle_attach,
            },
        },
        .credentials = {
            .username = MQTT_USER,
            .authentication = {
                .password = MQTT_PASS,
            },
        },
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

// ── btc task ──────────────────────────────────────────────────
static void btc_task(void *pvParam)
{
    esp_http_client_config_t config = {
        .url               = BINANCE_URL,
        .event_handler     = http_event_handler,
        .transport_type    = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    while (1) {
        http_buf_len = 0;
        memset(http_buf, 0, sizeof(http_buf));

        esp_err_t err = esp_http_client_perform(client);

        if (err == ESP_OK) {
            cJSON *root  = cJSON_Parse(http_buf);
            cJSON *price = cJSON_GetObjectItem(root, "price");

            if (price && cJSON_IsString(price)) {
                ESP_LOGI(TAG, "BTC: %s", price->valuestring);

                char display[17];
                snprintf(display, sizeof(display), "%s", price->valuestring);

                // take mutex before writing to lcd
                if (xSemaphoreTake(lcd_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    lcd_clear();
                    lcd_set_cursor(0, 0);
                    lcd_print("BTC/USDT:");
                    lcd_set_cursor(0, 1);
                    lcd_print(display);
                    xSemaphoreGive(lcd_mutex);
                }
            }
            cJSON_Delete(root);
        } else {
            ESP_LOGE(TAG, "HTTP failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(FETCH_DELAY_MS));
    }
}

// ── app main ──────────────────────────────────────────────────
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // create lcd mutex before any task uses lcd
    lcd_mutex = xSemaphoreCreateMutex();

    lcd_init();
    lcd_set_cursor(0, 0);
    lcd_print("  69series  ");
    lcd_set_cursor(0, 1);
    lcd_print("Connecting...");

    wifi_init();

    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_print("WiFi OK!");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // start mqtt (event driven, no task needed)
    mqtt_init();

    // launch btc task on core 1
    xTaskCreatePinnedToCore(btc_task, "btc_task",
                            8192, NULL, 5, NULL, 1);
}