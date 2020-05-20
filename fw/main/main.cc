
#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>
#include <wifi_provisioning/scheme_softap.h>

#include "tcp_server.h"
#include "rotator.h"

static const char *TAG = "app_main";
#define WIFI_CONNECTED_EVENT BIT0
static EventGroupHandle_t wifi_event_group;
#define SERV_NAME_PREFIX "PROV_"


/* Global event handler */
static void event_handler(void *arg,
                               esp_event_base_t event_base,
                               int event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            esp_wifi_connect();
            break;

        default:
            ESP_LOGI(TAG, "Unknown WiFi event: %d", event_id);
            break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
        case IP_EVENT_STA_GOT_IP:
            {
                ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                ESP_LOGI(TAG, "Connected with IP Address:" IPSTR,
                    IP2STR(&event->ip_info.ip));
                /* Signal main application to continue execution */
                xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_EVENT);
                break;
            }

        default:
            ESP_LOGI(TAG, "Unknown IP event");
            break;
        }
    } else if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
        case WIFI_PROV_START:
            ESP_LOGI(TAG, "Provisioning started");
            break;

        case WIFI_PROV_CRED_RECV:
            {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "Received Wi-Fi credentials"
                         "\n\tSSID     : %s\n\tPassword : %s",
                         (const char *) wifi_sta_cfg->ssid,
                         (const char *) wifi_sta_cfg->password);
            }
            break;

        case WIFI_PROV_CRED_FAIL:
            {
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s"
                         "\n\tPlease reset to factory and retry provisioning",
                         (*reason == WIFI_PROV_STA_AUTH_ERROR) ?
                         "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
            }
            break;
        case WIFI_PROV_CRED_SUCCESS:
            ESP_LOGI(TAG, "Provisioning successful");
            break;
        case WIFI_PROV_END:
            /* De-initialize manager once provisioning is finished */
            wifi_prov_mgr_deinit();
            break;
        default:
            ESP_LOGI(TAG, "Unknown provisioning event");
            break;
        }

    } else {
        ESP_LOGI(TAG, "Unknown event");
    }

}

static void wifi_init_sta()
{
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void init_network()
{
    /* Register our event handler for Wi-Fi, IP and Provisioning related events */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    /* Initialize Wi-Fi including netif with default config */
    {
        esp_netif_create_default_wifi_sta();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    }

    /* Configure the provisioning manager */
    {
        wifi_prov_mgr_config_t cfg =
        {
            .scheme = wifi_prov_scheme_ble,
            .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
            .app_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
        };
        ESP_ERROR_CHECK(wifi_prov_mgr_init(cfg));
    }

    /* Check if the device has been provisioned */
    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
    if (!provisioned)
    {
        ESP_LOGI(TAG, "Starting provisioning");

        char service_name[12];
        uint8_t eth_mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
        snprintf(service_name, sizeof service_name, "%s%02x%02x%02x",
            SERV_NAME_PREFIX, eth_mac[3], eth_mac[4], eth_mac[5]);

        wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
        const char *proof_of_possession = "gurka";

        const char *service_key = NULL;

        uint8_t custom_service_uuid[] = {
            /* LSB <---------------------------------------
             * ---------------------------------------> MSB */
            0x21, 0x43, 0x65, 0x87, 0x09, 0xba, 0xdc, 0xfe,
            0xef, 0xcd, 0xab, 0x90, 0x78, 0x56, 0x34, 0x12
        };
        wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);

        /* Start the provisioning service */
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security,
            proof_of_possession, service_name, service_key));
    } else {
        ESP_LOGI(TAG, "Using previously provisioned WiFi credentials");

        wifi_prov_mgr_deinit();
        wifi_init_sta();
    }

}

extern "C"
void app_main()
{
    ESP_LOGI(TAG, "Starting");

    /* Initialize NVS partition */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        err = nvs_flash_erase();
        err |= nvs_flash_init();
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize NVS");
    }

    /* Initialize TCP/IP */
    ESP_ERROR_CHECK(esp_netif_init());

    /* Initialize the event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    init_network();

    wifi_event_group = xEventGroupCreate();

    RotatorScale azimuth_scale(550*OVERSAMPLING, 4010*OVERSAMPLING, 0, 36000);
    RotatorScale elevation_scale(500*OVERSAMPLING, 4010*OVERSAMPLING, 0, 18000);
    Rotator rotator(azimuth_scale, elevation_scale);

    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_EVENT, false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "Init done - starting app");

    xTaskCreate(tcp_server_task, "tcpsrv", 4096, &rotator, 5, NULL);

    // Make sure not to exit from this function as that would mean destroying
    // objects created
    while(1) {
        vTaskDelay(1000 / (portTICK_PERIOD_MS));
    }

    return;
}
