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

#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "cJSON.h"

/******************** DEFINES ********************/

#define API_YANDEX_HOST "api.weather.yandex.ru"                 /**< Host URL */
#define API_YANDEX_PORT "80"                                    /**< TLS port */
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
    const struct addrinfo hints = 
    {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo * ptr_res = NULL;
    char recv_buf[WEATHER_GET_RX_BUF_SIZE];

    int32_t err = getaddrinfo(API_YANDEX_HOST, API_YANDEX_PORT, &hints, &ptr_res);
    if (0 != err || ptr_res == NULL) 
    {
        ESP_LOGE("Get", "getaddrinfo()");
        return;
    }

    int32_t sock = socket(ptr_res->ai_family, ptr_res->ai_socktype, 0);
    if (sock < 0) 
    {
        freeaddrinfo(ptr_res);
        ESP_LOGE("Get", "socket()");
        return;
    }

    if (connect(sock, ptr_res->ai_addr, ptr_res->ai_addrlen) != 0) 
    {
        ESP_LOGE("Get", "connect()");
        close(sock);
        return;
    }

    freeaddrinfo(ptr_res);

    printf("\n%s\n\n", API_YANDEX_GET_REQ);

    if (write(sock, API_YANDEX_GET_REQ, strlen(API_YANDEX_GET_REQ)) < 0) 
    {
        ESP_LOGE("Get", "write()");
        close(sock);
        return;
    }

    struct timeval receiving_timeout;
    receiving_timeout.tv_sec = WEATHER_GET_RX_TIMEOUT_S;
    receiving_timeout.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout, sizeof(receiving_timeout)) < 0) 
    {
        close(sock);
        return;
    }

    int32_t rx_bytes = 0;
    do {
        bzero(recv_buf, sizeof(recv_buf));
        rx_bytes = read(sock, recv_buf, sizeof(recv_buf) - sizeof('\0'));

        for(int32_t idx = 0; idx < rx_bytes; idx++) {
            putchar(recv_buf[idx]);
        }
        putchar('\n');

        if (rx_bytes <= 0)
        {
            break;
        }

        weather_display(strchr(recv_buf, '{'));
    } while (rx_bytes > 0);

    close(sock);
    
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

