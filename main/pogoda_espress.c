/** 
 *  @file       pogoda_espress.c
 *
 *  @brief      PoC for Yandex.Pogoda API based ESP32weather station 
 *
 *  @author     Mikhail Zaytsev
 */

/********** INCLUDES **********/

#include "sdkconfig.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_netif.h"
#include "esp_tls.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "cJSON.h"

#include "time_sync.h"

/******************** DEFINES ********************/

#define API_YANDEX_HOST "api.weather.yandex.ru"                 /**< Host URL */
#define API_YANDEX_URL  "https://" API_YANDEX_HOST "/"          /**< Host URL full */
#define API_YANDEX_PORT "443"                                   /**< TLS port */
#define API_YANDEX_PATH "/v2/informers?lat=59.9386&lon=30.3141" /**< Host path */
#define API_YANDEX_KEY  "822a9b7c-bfdf-4f43-93b8-ac085bb84c1d"  /**< Yandex API key */
/**< Yandex API weather GET request */
#define API_YANDEX_GET_REQ \
    "GET " API_YANDEX_PATH " HTTP/1.1\r\n" \
    "Host: " API_YANDEX_HOST "\r\n"  \
    "X-Yandex-API-Key: " API_YANDEX_KEY "\r\n" \
    "\r\n"


#define APP_WIFI_SSID          "coreofbear" /**< WiFi SSID */
#define APP_WIFI_PASS          "12344321"   /**< WiFi password */
#define APP_WIFI_CONN_TRY_QTY  20           /**< WiFi connection try quantity */

#define WEATHER_GET_TASK_NAME       "Weather get task"  /**< Weather task stack size */
#define WEATHER_GET_TASK_STACK_SIZE 8192                /**< Weather task stack size */
#define WEATHER_GET_TASK_PRIORITY   5                   /**< Weather task priority */
#define WEATHER_GET_RX_BUF_SIZE     1536                /**< Receiving buffer size for weather task */
#define WEATHER_GET_RX_TIMEOUT_S    10                  /**< Receiving timeout in seconds */

#define APP_DELAY_COMMON_MS         5000                /**< Common used delay in milliseconds */

/**< Time update period (1 day) */
#define APP_TIME_UPDATE_PERIOD  (86400000000ULL)

/**< Yandex Weather API root certificate */
#define API_YANDEX_ROOT_CERT \
"-----BEGIN CERTIFICATE-----\n" \
"MIIETjCCAzagAwIBAgINAe5fIh38YjvUMzqFVzANBgkqhkiG9w0BAQsFADBMMSAw\n" \
"HgYDVQQLExdHbG9iYWxTaWduIFJvb3QgQ0EgLSBSMzETMBEGA1UEChMKR2xvYmFs\n" \
"U2lnbjETMBEGA1UEAxMKR2xvYmFsU2lnbjAeFw0xODExMjEwMDAwMDBaFw0yODEx\n" \
"MjEwMDAwMDBaMFAxCzAJBgNVBAYTAkJFMRkwFwYDVQQKExBHbG9iYWxTaWduIG52\n" \
"LXNhMSYwJAYDVQQDEx1HbG9iYWxTaWduIFJTQSBPViBTU0wgQ0EgMjAxODCCASIw\n" \
"DQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAKdaydUMGCEAI9WXD+uu3Vxoa2uP\n" \
"UGATeoHLl+6OimGUSyZ59gSnKvuk2la77qCk8HuKf1UfR5NhDW5xUTolJAgvjOH3\n" \
"idaSz6+zpz8w7bXfIa7+9UQX/dhj2S/TgVprX9NHsKzyqzskeU8fxy7quRU6fBhM\n" \
"abO1IFkJXinDY+YuRluqlJBJDrnw9UqhCS98NE3QvADFBlV5Bs6i0BDxSEPouVq1\n" \
"lVW9MdIbPYa+oewNEtssmSStR8JvA+Z6cLVwzM0nLKWMjsIYPJLJLnNvBhBWk0Cq\n" \
"o8VS++XFBdZpaFwGue5RieGKDkFNm5KQConpFmvv73W+eka440eKHRwup08CAwEA\n" \
"AaOCASkwggElMA4GA1UdDwEB/wQEAwIBhjASBgNVHRMBAf8ECDAGAQH/AgEAMB0G\n" \
"A1UdDgQWBBT473/yzXhnqN5vjySNiPGHAwKz6zAfBgNVHSMEGDAWgBSP8Et/qC5F\n" \
"JK5NUPpjmove4t0bvDA+BggrBgEFBQcBAQQyMDAwLgYIKwYBBQUHMAGGImh0dHA6\n" \
"Ly9vY3NwMi5nbG9iYWxzaWduLmNvbS9yb290cjMwNgYDVR0fBC8wLTAroCmgJ4Yl\n" \
"aHR0cDovL2NybC5nbG9iYWxzaWduLmNvbS9yb290LXIzLmNybDBHBgNVHSAEQDA+\n" \
"MDwGBFUdIAAwNDAyBggrBgEFBQcCARYmaHR0cHM6Ly93d3cuZ2xvYmFsc2lnbi5j\n" \
"b20vcmVwb3NpdG9yeS8wDQYJKoZIhvcNAQELBQADggEBAJmQyC1fQorUC2bbmANz\n" \
"EdSIhlIoU4r7rd/9c446ZwTbw1MUcBQJfMPg+NccmBqixD7b6QDjynCy8SIwIVbb\n" \
"0615XoFYC20UgDX1b10d65pHBf9ZjQCxQNqQmJYaumxtf4z1s4DfjGRzNpZ5eWl0\n" \
"6r/4ngGPoJVpjemEuunl1Ig423g7mNA2eymw0lIYkN5SQwCuaifIFJ6GlazhgDEw\n" \
"fpolu4usBCOmmQDo8dIm7A9+O4orkjgTHY+GzYZSR+Y0fFukAj6KYXwidlNalFMz\n" \
"hriSqHKvoflShx8xpfywgVcvzfTO3PYkz6fiNJBonf6q8amaEsybwMbDqKWwIX7e\n" \
"SPY=\n" \
"-----END CERTIFICATE-----\n\n\0"
    
/******************** STRUCTURES, ENUMS, UNIONS ********************/

typedef struct pogoda_ctx_s
{
    EventGroupHandle_t wifi_event_group;
    uint32_t try_num;
} pogoda_ctx_t;

/******************** GLOBAL VARIABLES ********************/

static pogoda_ctx_t global_ctx = {0};

/******************** PRIVATE FUNCTION PROTOTYPES ********************/

static void net_event_handler(void * ptr_arg, 
                                esp_event_base_t event_base, 
                                int32_t event_id, 
                                void * ptr_event_data);
static void wifi_init(void);
static void weather_get_task(void * ptr_params);
static void weather_display(const char * ptr_str);

/******************** PRIVATE FUNCTIONS ********************/

/**
 *  @brief      Network operations handler
 *
 *  @param[in]  ptr_arg         Argument pointer (don't used)
 *  @param[in]  event_base      Event base
 *  @param[in]  event_id        Event ID
 *  @param[in]  ptr_event_data  Event data pointer (don't used)
 */  
static void net_event_handler(void * ptr_arg, 
                                esp_event_base_t event_base, 
                                int32_t event_id, 
                                void * ptr_event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) 
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        if (global_ctx.try_num < APP_WIFI_CONN_TRY_QTY) 
        {
            esp_wifi_connect();
            global_ctx.try_num++;
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        global_ctx.try_num = 0;
    }
}

/**
 *  @brief WiFi initializaiton and connecting function
 */
static void wifi_init(void)
{
    global_ctx.wifi_event_group = xEventGroupCreate();
    global_ctx.try_num = 0;

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &net_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &net_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = APP_WIFI_SSID,
            .password = APP_WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start());
}

/**
 *  @brief      Weather getting task handler
 *
 *  @param[in]  ptr_param   Parameter pointer (don't used)
 */
static void weather_get_task(void * ptr_params)
{
    char buf[WEATHER_GET_RX_BUF_SIZE];

    esp_tls_cfg_t cfg = {
        .cacert_buf = (const unsigned char *) API_YANDEX_ROOT_CERT,
        .cacert_bytes = strlen(API_YANDEX_ROOT_CERT) + sizeof('\0'),
    };

    esp_tls_t * ptr_tls = esp_tls_init();
    if (NULL == ptr_tls)
    {
        ESP_LOGE("Get", "esp_tls_init()");
        return;
    }

    if (esp_tls_conn_http_new_sync(API_YANDEX_URL, &cfg, ptr_tls) == 1) {
        ESP_LOGI("Get", "Connection established...");
    } else {
        ESP_LOGE("Get", "Connection failed...");
        esp_tls_conn_destroy(ptr_tls);
        return;
    }    
    
    size_t written_bytes = 0;
    int32_t ret = 0;
    do {
        ret = esp_tls_conn_write(ptr_tls,
                                 API_YANDEX_GET_REQ + written_bytes,
                                 strlen(API_YANDEX_GET_REQ) - written_bytes);
        if (ret >= 0) {
            ESP_LOGI("Get", "%d bytes written", ret);
            written_bytes += ret;
        } else if (ret != ESP_TLS_ERR_SSL_WANT_READ  && ret != ESP_TLS_ERR_SSL_WANT_WRITE) {
            ESP_LOGE("Get", "esp_tls_conn_write  returned: [0x%02X](%s)", ret, esp_err_to_name(ret));
            esp_tls_conn_destroy(ptr_tls);
            return;
        }
    } while (written_bytes < strlen(API_YANDEX_GET_REQ));

    ESP_LOGI("Get", "Reading HTTP response...");
    do {
        int32_t len = sizeof(buf) - 1;
        memset(buf, 0x00, sizeof(buf));
        ret = esp_tls_conn_read(ptr_tls, (char *)buf, len);

        if (ret == ESP_TLS_ERR_SSL_WANT_WRITE  || ret == ESP_TLS_ERR_SSL_WANT_READ) {
            continue;
        } else if (ret <= 0) {
            break;
        }

        len = ret;
        ESP_LOGD("Get", "%d bytes read", len);
        /* Print response directly to stdout as it is read */
        for (int i = 0; i < len; i++) {
            putchar(buf[i]);
        }
        putchar('\n'); // JSON output doesn't have a newline at end

        weather_display(strchr(buf, '{'));
    } while (1);    

    esp_tls_conn_destroy(ptr_tls);
    
    printf("Exit\n");
    vTaskDelay(APP_DELAY_COMMON_MS / portTICK_PERIOD_MS);
}

/**
 *  @brief      Weather parse and display function
 *
 *  @param[in]  ptr_str     NULL-terminater string pointer
 */
static void weather_display(const char * ptr_str)
{
    if (NULL == ptr_str)
    {
        ESP_LOGE("Display", "NULL string pointer");
        return;
    }

    cJSON * ptr_json_root = cJSON_Parse(ptr_str);
    cJSON * ptr_json_fact = cJSON_GetObjectItem(ptr_json_root, "fact");
    cJSON * ptr_json_condition = cJSON_GetObjectItem(ptr_json_fact, "condition");
    cJSON * ptr_json_temp = cJSON_GetObjectItem(ptr_json_fact, "temp");
    if ((NULL == ptr_json_condition) ||
        (NULL == ptr_json_temp))
    {
        ESP_LOGE("Display", "Cannot parse weather response");
        return;
    }
    
    printf("\nCurrent weather in Saint-Petersburg:\n");
    printf("\tCondition: %s\n", ptr_json_condition->valuestring);
    printf("\tTemperature: %d\n", ptr_json_temp->valueint);

    cJSON_Delete(ptr_json_root);
}


/******************** PUBLIC FUNCTIONS ********************/

/**
 *  @brief  Application entry point
 */
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) 
    {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init();
    vTaskDelay(APP_DELAY_COMMON_MS / portTICK_PERIOD_MS);

    if (esp_reset_reason() == ESP_RST_POWERON) {
        ESP_LOGI("Get", "Updating time from NVS");
        ESP_ERROR_CHECK(update_time_from_nvs());
    }

    const esp_timer_create_args_t nvs_update_timer_args = {
            .callback = &fetch_and_store_time_in_nvs,
    };

    esp_timer_handle_t nvs_update_timer;
    ESP_ERROR_CHECK(esp_timer_create(&nvs_update_timer_args, &nvs_update_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(nvs_update_timer, APP_TIME_UPDATE_PERIOD));

    vTaskDelay(APP_DELAY_COMMON_MS / portTICK_PERIOD_MS);
    vTaskDelay(APP_DELAY_COMMON_MS / portTICK_PERIOD_MS);
    vTaskDelay(APP_DELAY_COMMON_MS / portTICK_PERIOD_MS);
    
    xTaskCreate(&weather_get_task, 
                WEATHER_GET_TASK_NAME, 
                WEATHER_GET_TASK_STACK_SIZE, 
                NULL, 
                WEATHER_GET_TASK_PRIORITY, 
                NULL);
    for (;;)
    {
        vTaskDelay(APP_DELAY_COMMON_MS / portTICK_PERIOD_MS);
    }
}

