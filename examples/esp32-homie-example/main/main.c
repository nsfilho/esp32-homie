#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <nvs_flash.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_event_loop.h>
#include <esp_log.h>

#include "homie.h"

static const char* TAG = "EXAMPLE";

static EventGroupHandle_t wifi_event_group;
const static int CONNECTED_BIT = BIT0;

static esp_err_t my_mqtt_handler(esp_mqtt_event_handle_t event)
{
    esp_err_t err = ESP_FAIL;

    switch (event->event_id)
    {
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT in my_mqtt_handler");
        break;
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED in my_mqtt_handler");
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED in my_mqtt_handler");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED in my_mqtt_handler");
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED in my_mqtt_handler");
        break;
    case MQTT_EVENT_PUBLISHED:
        break;
    case MQTT_EVENT_DATA:
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR in my_mqtt_handler");
        break;
    default:
        ESP_LOGW(TAG, "Unknown event ID in my_mqtt_handler. event ID: %d", event->event_id);
        err = ESP_FAIL;
        goto fail;
    }
    err = ESP_OK;
fail:
    return err;
}

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void wifi_init(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_LOGI(TAG, "start the WIFI SSID:[%s] password:[%s]", CONFIG_WIFI_SSID, "******");
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Waiting for wifi");
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
}

void app_main()
{
    esp_mqtt_client_handle_t client;
    esp_err_t err;
    ESP_ERROR_CHECK(nvs_flash_init());
    EventGroupHandle_t homie_event_group;
    EventBits_t event_bit;
    char topic[HOMIE_MAX_MQTT_TOPIC_LEN];
    char mac_string[] = "aabbccddeeff";
    char nice_mac_string[] = "aa:bb:cc:dd:ee:ff";

    wifi_init();

    static homie_config_t homie_conf = {
        .mqtt_config = {
            .event_handle = NULL,
            .client_id = "foo",
            .username = CONFIG_MQTT_USERNAME,
            .password = CONFIG_MQTT_PASSWORD,
            .uri = CONFIG_MQTT_URI,
            .keepalive = 15,
            .task_stack = configMINIMAL_STACK_SIZE * 10,
            .cert_pem = NULL,
        },
        .device_name = "My Device",
        .base_topic = "homie",
        .firmware_name = "Example",
        .firmware_version = "0.0.1",
        .ota_enabled = true,
        .reboot_enabled = true,
        .mqtt_handler = my_mqtt_handler,
        .event_group = NULL,
        .http_config = {
            .url = CONFIG_OTA_URL,
            .cert_pem = NULL,
        },
    };

    homie_event_group = xEventGroupCreate();
    if (homie_event_group == NULL) {
        ESP_LOGE(TAG, "xEventGroupCreate()");
        goto fail;
    }
    homie_conf.event_group = &homie_event_group;

    err = homie_init(&homie_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "homie_init()");
        goto fail;
    }

    ESP_ERROR_CHECK(homie_mktopic(topic, "#", sizeof(topic)));
    ESP_ERROR_CHECK(homie_get_mac(mac_string, sizeof(mac_string), false));
    ESP_ERROR_CHECK(homie_get_mac(nice_mac_string, sizeof(nice_mac_string), true));

    printf("MQTT URI: `%s`\n", CONFIG_MQTT_URI);
    printf("MAC address: `%s` / `%s`\n", mac_string, nice_mac_string);
    printf("The topic of the device: `%s` (use this topic path to see published attributes)\n", topic);
    printf("An example command:\n");
    printf("\tmosquitto_sub -v -h ip.add.re.ss -t '%s'\n", topic);
    printf("\t(replace ip.add.re.ss with MQTT broker's IP address or host name)\n");

    ESP_LOGI(TAG, "Starting Homie client");
    client = homie_run();
    if (client == NULL) {
        ESP_LOGE(TAG, "homie_run()");
        goto fail;
    }

    while (1) {
        ESP_LOGI(TAG, "Waiting for HOMIE_MQTT_CONNECTED_BIT to be set");
        event_bit = xEventGroupWaitBits(homie_event_group,
                HOMIE_MQTT_CONNECTED_BIT,
                pdFALSE,
                pdFALSE,
                1000 / portTICK_PERIOD_MS);
        if ((event_bit & HOMIE_MQTT_CONNECTED_BIT) == HOMIE_MQTT_CONNECTED_BIT) {
            break;
        }
    }
    ESP_LOGI(TAG, "MQTT client has connected to the broker");

    // Keep the main task around
fail:
    while (1) {
        vTaskDelay(1000/portTICK_PERIOD_MS);
    }
}
