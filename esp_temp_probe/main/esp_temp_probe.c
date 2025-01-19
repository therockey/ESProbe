#include <stdio.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "mqtt_client.h"
#include "esp_wifi.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "owb.h"
#include "owb_rmt.h"
#include "ds18b20.h"
#include "ssd1306.h"
#include "font8x8_basic.h"

#define SCREENTAG "SSD1306"
#define TEMPTAG "ESP_TEMP_PROBE"
#define SSID (CONFIG_SSID)
#define PASSWORD (CONFIG_PASSWORD)
#define MQTT_BROKER_IP (CONFIG_MQTT_IP)
#define MQTT_BROKER_PORT (CONFIG_MQTT_PORT)
#define MQTT_USERNAME (CONFIG_MQTT_USERNAME)
#define MQTT_PASSWORD (CONFIG_MQTT_PASSWORD)
#define MQTT_URI_BUFFER_SIZE 64

#define GPIO_DS18B20_0       (CONFIG_ONE_WIRE_GPIO)
#define DS18B20_RESOLUTION   (DS18B20_RESOLUTION_12_BIT)

esp_mqtt_client_handle_t mqtt_client;
SSD1306_t dev;

static int delta_time = 1000;
static OneWireBus *owb = NULL;
static DS18B20_Info *ds18b20_info = NULL;

void ssd1306_display_temp(float temp) {
    char temp_str[10];
    snprintf(temp_str, sizeof(temp_str), " %.2f C", temp);
    ssd1306_display_text(&dev, 2, temp_str, strlen(temp_str), false);
}

void ssd1306_display_delta(int delta) {
    char delta_str[10];
    snprintf(delta_str, sizeof(delta_str), " %d", delta);
    ssd1306_display_text(&dev, 6, delta_str, strlen(delta_str), false);
}

static void mqtt_event_handler_cb(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    if (event_id == MQTT_EVENT_CONNECTED) {
        printf("Connected to mqtt broker\n");
        esp_mqtt_client_subscribe(mqtt_client, "home/livingroom/temperature/delta", 1);
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        printf("Disconnected from mqtt broker");
    }

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t) event_data;
    if (event_id == MQTT_EVENT_DATA) {
        printf("Received topic: %.*s, data: %.*s\n", event->topic_len, event->topic, event->data_len, event->data);

        if (strncmp(event->topic, "home/livingroom/temperature/delta", event->topic_len) == 0) {
            char delta_str[16]; 
            snprintf(delta_str, sizeof(delta_str), "%.*s", event->data_len, event->data);
            
            int delta = atoi(delta_str);
            if (delta < 1000) {
                delta = 1000;
            }

            printf("Set delta to: %d\n", delta);
            
            delta_time = delta;
            ssd1306_display_delta(delta_time); 
        }

    }
}

void mqtt_send_temp(float temp) {
    const char *topic = "home/livingroom/temperature";
    char message[10];
    snprintf(message, sizeof(message), " %.2f", temp);
    esp_mqtt_client_publish(mqtt_client, topic, message, 0, 1, 0);
}

void handle_logic() {
    TickType_t last_wake_time = xTaskGetTickCount();
    while (1) {
        ds18b20_convert(ds18b20_info);
        ds18b20_wait_for_conversion(ds18b20_info);

        float temp = 0.0f;
        DS18B20_ERROR err = ds18b20_read_temp(ds18b20_info, &temp);
        if (err == DS18B20_OK) {
            ESP_LOGI(TEMPTAG, "Temperature read: %.2f C", temp);
        } else {
            ESP_LOGE(TEMPTAG, "Failed to read temperature: %d", err);
            temp = -999.0f;
        }

        mqtt_send_temp(temp);
        ssd1306_display_temp(temp);
        vTaskDelayUntil(&last_wake_time, delta_time / portTICK_PERIOD_MS);
    }

    ds18b20_free(&ds18b20_info);
}

void mqtt_app_start() {
    char mqtt_broker_uri[MQTT_URI_BUFFER_SIZE];
    snprintf(mqtt_broker_uri, MQTT_URI_BUFFER_SIZE, "mqtt://%s:%d", MQTT_BROKER_IP, MQTT_BROKER_PORT);
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = mqtt_broker_uri,
        .credentials = {
            .username = MQTT_USERNAME,
            .authentication = {
                .password = MQTT_PASSWORD,
            }
        }
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler_cb, NULL);
    esp_mqtt_client_start(mqtt_client);
}

void owb_ds18b20_init() {
    if (!owb) {
        static owb_rmt_driver_info rmt_driver_info;
        owb = owb_rmt_initialize(&rmt_driver_info, GPIO_DS18B20_0, RMT_CHANNEL_1, RMT_CHANNEL_0);
        owb_use_crc(owb, true);
        ESP_LOGI(TEMPTAG, "OneWire bus initialized");

        // Initialize DS18B20
        ds18b20_info = ds18b20_malloc();
        ds18b20_init_solo(ds18b20_info, owb);
        ds18b20_use_crc(ds18b20_info, true);
        ds18b20_set_resolution(ds18b20_info, DS18B20_RESOLUTION);
        ESP_LOGI(TEMPTAG, "DS18B20 sensor initialized");
    }
}

void spi_ssd1306_init() {
    #if CONFIG_I2C_INTERFACE
	ESP_LOGI(SCREENTAG, "INTERFACE is i2c");
	ESP_LOGI(SCREENTAG, "CONFIG_SDA_GPIO=%d",CONFIG_SDA_GPIO);
	ESP_LOGI(SCREENTAG, "CONFIG_SCL_GPIO=%d",CONFIG_SCL_GPIO);
	ESP_LOGI(SCREENTAG, "CONFIG_RESET_GPIO=%d",CONFIG_RESET_GPIO);
	i2c_master_init(&dev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, CONFIG_RESET_GPIO);
    #endif // CONFIG_I2C_INTERFACE

    #if CONFIG_SPI_INTERFACE
	ESP_LOGI(SCREENTAG, "INTERFACE is SPI");
	ESP_LOGI(SCREENTAG, "CONFIG_MOSI_GPIO=%d",CONFIG_MOSI_GPIO);
	ESP_LOGI(SCREENTAG, "CONFIG_SCLK_GPIO=%d",CONFIG_SCLK_GPIO);
	ESP_LOGI(SCREENTAG, "CONFIG_CS_GPIO=%d",CONFIG_CS_GPIO);
	ESP_LOGI(SCREENTAG, "CONFIG_DC_GPIO=%d",CONFIG_DC_GPIO);
	ESP_LOGI(SCREENTAG, "CONFIG_RESET_GPIO=%d",CONFIG_RESET_GPIO);
	spi_master_init(&dev, CONFIG_MOSI_GPIO, CONFIG_SCLK_GPIO, CONFIG_CS_GPIO, CONFIG_DC_GPIO, CONFIG_RESET_GPIO);
    #endif // CONFIG_SPI_INTERFACE

    ssd1306_init(&dev, 128, 64);
    ssd1306_clear_screen(&dev, false);
    ssd1306_contrast(&dev, 0xff);
    ssd1306_display_text(&dev, 0, " Temperature:", 12, false);
    ssd1306_display_text(&dev, 4, " Delta time:", 12, false);
    ssd1306_display_delta(delta_time);
}

void wifi_connect_with_new_credentials() {
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = SSID,
            .password = PASSWORD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    printf("Connecting to WIFI...\n");

    int retry_count = 0;
    const int max_retries = 5;

    while (retry_count < max_retries) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            printf("Connected to wifi\n");
            return;
        } else {
            retry_count++;
            printf("Attempt %d z %d: Next try to wifi connect\n", retry_count, max_retries);
            esp_wifi_connect();
        }
    }

    printf("Cannot connect to wifi after %d attemps.\n", max_retries);
}

void app_main() {
    esp_err_t ret;

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_connect_with_new_credentials();

    owb_ds18b20_init();
    spi_ssd1306_init();

    xTaskCreate(handle_logic, "handle_logic", 4096, NULL, 10, NULL);
    mqtt_app_start();

    printf("System ready, waiting for events\n");
}
