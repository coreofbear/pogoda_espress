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
#include "esp_tls.h"
#include "esp_sntp.h"
#include "esp_netif.h"

#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "cJSON.h"

/******************** DEFINES ********************/

#define API_YANDEX_HOST "api.weather.yandex.ru"                 /**< Host URL */
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
#define APP_WIFI_CONN_TRY_QTY  10           /**< WiFi connection try quantity */

#define WEATHER_GET_TASK_NAME       "Weather get task"  /**< Weather task stack size */
#define WEATHER_GET_TASK_STACK_SIZE 8192                /**< Weather task stack size */
#define WEATHER_GET_TASK_PRIORITY   5                   /**< Weather task priority */
#define WEATHER_GET_RX_BUF_SIZE     1536                /**< Receiving buffer size for weather task */
#define WEATHER_GET_RX_TIMEOUT_S    10                  /**< Receiving timeout in seconds */

#define APP_DELAY_COMMON_MS         5000                /**< Common used delay in milliseconds */

/**< Server root CA */
#define API_YANDEX_ROOT_CERT \
    "-----BEGIN CERTIFICATE-----\r\n" \
    "MIIETjCCAzagAwIBAgINAe5fFp3/lzUrZGXWajANBgkqhkiG9w0BAQsFADBXMQsw\r\n" \
    "CQYDVQQGEwJCRTEZMBcGA1UEChMQR2xvYmFsU2lnbiBudi1zYTEQMA4GA1UECxMH\r\n" \
    "Um9vdCBDQTEbMBkGA1UEAxMSR2xvYmFsU2lnbiBSb290IENBMB4XDTE4MDkxOTAw\r\n" \
    "MDAwMFoXDTI4MDEyODEyMDAwMFowTDEgMB4GA1UECxMXR2xvYmFsU2lnbiBSb290\r\n" \
    "IENBIC0gUjMxEzARBgNVBAoTCkdsb2JhbFNpZ24xEzARBgNVBAMTCkdsb2JhbFNp\r\n" \
    "Z24wggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDMJXaQeQZ4Ihb1wIO2\r\n" \
    "hMoonv0FdhHFrYhy/EYCQ8eyip0EXyTLLkvhYIJG4VKrDIFHcGzdZNHr9SyjD4I9\r\n" \
    "DCuul9e2FIYQebs7E4B3jAjhSdJqYi8fXvqWaN+JJ5U4nwbXPsnLJlkNc96wyOkm\r\n" \
    "DoMVxu9bi9IEYMpJpij2aTv2y8gokeWdimFXN6x0FNx04Druci8unPvQu7/1PQDh\r\n" \
    "BjPogiuuU6Y6FnOM3UEOIDrAtKeh6bJPkC4yYOlXy7kEkmho5TgmYHWyn3f/kRTv\r\n" \
    "riBJ/K1AFUjRAjFhGV64l++td7dkmnq/X8ET75ti+w1s4FRpFqkD2m7pg5NxdsZp\r\n" \
    "hYIXAgMBAAGjggEiMIIBHjAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0TAQH/BAUwAwEB\r\n" \
    "/zAdBgNVHQ4EFgQUj/BLf6guRSSuTVD6Y5qL3uLdG7wwHwYDVR0jBBgwFoAUYHtm\r\n" \
    "GkUNl8qJUC99BM00qP/8/UswPQYIKwYBBQUHAQEEMTAvMC0GCCsGAQUFBzABhiFo\r\n" \
    "dHRwOi8vb2NzcC5nbG9iYWxzaWduLmNvbS9yb290cjEwMwYDVR0fBCwwKjAooCag\r\n" \
    "JIYiaHR0cDovL2NybC5nbG9iYWxzaWduLmNvbS9yb290LmNybDBHBgNVHSAEQDA+\r\n" \
    "MDwGBFUdIAAwNDAyBggrBgEFBQcCARYmaHR0cHM6Ly93d3cuZ2xvYmFsc2lnbi5j\r\n" \
    "b20vcmVwb3NpdG9yeS8wDQYJKoZIhvcNAQELBQADggEBACNw6c/ivvVZrpRCb8RD\r\n" \
    "M6rNPzq5ZBfyYgZLSPFAiAYXof6r0V88xjPy847dHx0+zBpgmYILrMf8fpqHKqV9\r\n" \
    "D6ZX7qw7aoXW3r1AY/itpsiIsBL89kHfDwmXHjjqU5++BfQ+6tOfUBJ2vgmLwgtI\r\n" \
    "fR4uUfaNU9OrH0Abio7tfftPeVZwXwzTjhuzp3ANNyuXlava4BJrHEDOxcd+7cJi\r\n" \
    "WOx37XMiwor1hkOIreoTbv3Y/kIvuX1erRjvlJDKPSerJpSZdcfL03v3ykzTr1Eh\r\n" \
    "kluEfSufFT90y1HonoMOFm8b50bOI7355KKL0jlrqnkckSziYSQtjipIcJDEHsXo\r\n" \
    "4HA=\r\n" \
    "-----END CERTIFICATE-----\r\n"

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
    char recv_buf[WEATHER_GET_RX_BUF_SIZE];

    esp_tls_t * ptr_tls = esp_tls_init();
    esp_tls_cfg_t tls_cfg = 
    {
        .cacert_buf = (const unsigned char *)API_YANDEX_ROOT_CERT,
        .cacert_bytes = strlen(API_YANDEX_ROOT_CERT),
        .timeout_ms = 1000
    };
    esp_tls_conn_http_new_sync(API_YANDEX_HOST, &tls_cfg, ptr_tls);
    esp_tls_conn_write(ptr_tls, API_YANDEX_GET_REQ, strlen(API_YANDEX_GET_REQ));
    printf("\n\n%s\n\n\n", API_YANDEX_GET_REQ);

    int32_t ret = 0;
    for (;;)
    {
        memset(recv_buf, 0x00, sizeof(recv_buf));
        ret = esp_tls_conn_read(ptr_tls, recv_buf, sizeof(recv_buf) - sizeof('\0'));

        if (ret == ESP_TLS_ERR_SSL_WANT_WRITE  || ret == ESP_TLS_ERR_SSL_WANT_READ) {
            vTaskDelay(300 / portTICK_PERIOD_MS);
            continue;
        } else if (ret <= 0) {
            break;
        }

        weather_display(strchr(recv_buf, '{'));

        for(int32_t idx = 0; idx < ret; idx++) {
            putchar(recv_buf[idx]);
        }
        putchar('\n');
    }

    esp_tls_conn_destroy(ptr_tls);
    
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
    
    printf("Current weather in Saint-Petersburg:\n");
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

