#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "driver/uart.h"
#include <string.h>
#include <ctype.h>
#include "esp_efuse.h"
#include "esp_efuse_custom_table.h"

#include "nvs_flash.h"

#include <stdlib.h>


#include "esp_system.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
// #include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
// #include "esp_gatt_common_api.h"

#define EXT_ADV_HANDLE      0
#define NUM_EXT_ADV         1

static const char *EFUSE = "eFUSE msg";
static const char *TAG = "example";
static const char *BLE = "BLE msg";
static SemaphoreHandle_t test_sem = NULL;

#define BUF_SIZE (1024)

#define FUNC_SEND_WAIT_SEM(func, sem) do {\
        esp_err_t __err_rc = (func);\
        if (__err_rc != ESP_OK) { \
            ESP_LOGE(BLE, "%s, message send fail, error = %d", __func__, __err_rc); \
        } \
        xSemaphoreTake(sem, portMAX_DELAY); \
} while(0);

char serial_number[21] = "";

bool initialize(){
    
    // Initialize UART communication
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_param_config(UART_NUM_0, &uart_config);
    uart_driver_install(UART_NUM_0, BUF_SIZE * 2, 0, 0, NULL, 0);

    return true;

}

//  Efuse functions //////////////////

static void read_device_desc_efuse_fields()
{
    ESP_ERROR_CHECK(esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA_SERIAL_NUMBER, &serial_number, 168));
    ESP_LOGI(TAG, "serial_number = %s", serial_number);
}


static void write_efuse_fields()
{
    ESP_LOGI(TAG, "write custom efuse fields");
    ESP_ERROR_CHECK(esp_efuse_batch_write_begin());
    ESP_ERROR_CHECK(esp_efuse_write_field_blob(ESP_EFUSE_USER_DATA_SERIAL_NUMBER, &serial_number, 168));
    ESP_ERROR_CHECK(esp_efuse_batch_write_commit());
}

void efuse_set(){

    // Check if efuse set
    read_device_desc_efuse_fields();
    if (strncmp((char *)serial_number, "1429F", 5) == 0) {
        // Serial number exists in efuse 
        ESP_LOGI(EFUSE, "Serial number exists in efuse");
        return;

    } else {
        // Serial number does not exist in efuse
        // Take in an input from serial monitor
        // Read the serial input and check if its within requirement
        // Configure a temporary buffer for the incoming data
        ESP_LOGI(EFUSE, "No serial number found in efuse");

        uint8_t *data = (uint8_t *) malloc(BUF_SIZE);

        ESP_LOGI(EFUSE, "Enter the serial number");

        while (1){
            // Read data from the UART
            int len = uart_read_bytes(UART_NUM_0, data, (BUF_SIZE - 1), 20 / portTICK_PERIOD_MS);
            data[len] = '\0';

            if (len > 0){
                ESP_LOGI(EFUSE, "Recv str: %s", (char *) data);

                if (len == 20){
                    if (strncmp((char *)data, "1429F", 5) == 0) {
                        bool all_numeric = true;
                        for (int i = 5; i < 20; i++) {
                            if (!isdigit(data[i])) {
                                all_numeric = false;
                            } 
                        }
                        if (all_numeric == true){
                            strncpy(serial_number, (char *)data, len);
                            ESP_LOGI(EFUSE, "Serial number is: %s", (char *) data);
                            // Write data back to the UART
                            // uart_write_bytes(UART_NUM_0, (const char *) data, len);
                            return;
                        } else {
                            ESP_LOGI(EFUSE, "Trailing 15 characters are not numbers.Try again.");
                            len = 0;
                        }
                    } else {
                    ESP_LOGI(EFUSE, "First 5 characters incorrect.Try again.");
                    len = 0;
                }     
                } else {
                    ESP_LOGI(EFUSE, "Wrong message length.Try again.");
                    len = 0;
                }
            }

            if (serial_number[0] != '\0'){
                break;
            }

        }

        free(data);

        esp_efuse_coding_scheme_t coding_scheme = EFUSE_CODING_SCHEME_RS;
        (void) coding_scheme;

        // read_device_desc_efuse_fields(serial_number);

        #if defined(CONFIG_EFUSE_VIRTUAL) 
            ESP_LOGW(TAG, "Write operations in efuse fields are performed virtually");
            // For testing purposes
            // strcpy(serial_number, "aB234567890123456789");
            write_efuse_fields();
            read_device_desc_efuse_fields();

        #else
            
            ESP_LOGW(TAG, "WARNING - Write operations in efuse fields are performed ");
            ESP_LOGW(TAG, "Press 1 to continue: ");
            char efuse_proceed = 0;
            uart_read_bytes(UART_NUM_0, &efuse_proceed, sizeof(efuse_proceed), portMAX_DELAY);
            ESP_LOGW(TAG, "Keyed value: %c", efuse_proceed);

            if (efuse_proceed == '1'){
                ESP_LOGI(TAG, "serial_number = %s", serial_number);
                write_efuse_fields();
                read_device_desc_efuse_fields();
            } else {
                ESP_LOGW(TAG, "Using an example serial number instead: aB234567890123456789");
                strcpy(serial_number, "aB234567890123456789");
            }


        #endif



        ESP_LOGI(TAG, "Done");
    }

}

// Bluetooth communication

uint8_t addr_coded[6] = {0xc0, 0xde, 0x52, 0x00, 0x00, 0x04};

esp_ble_gap_ext_adv_params_t ext_adv_params_lr_coded = {
    .type = ESP_BLE_GAP_SET_EXT_ADV_PROP_SCANNABLE,
    .interval_min = 0x50,
    .interval_max = 0x50,
    .channel_map = ADV_CHNL_ALL,
    .filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    .primary_phy = ESP_BLE_GAP_PHY_1M,
    .max_skip = 0,
    .secondary_phy = ESP_BLE_GAP_PHY_CODED,
    .sid = 0,
    .scan_req_notif = false,
    .own_addr_type = BLE_ADDR_TYPE_RANDOM,
    .tx_power = ESP_PWR_LVL_P18
};

static uint8_t raw_ext_adv_data_coded[] = {
        0x02, 0x01, 0x06,
        0x02, 0x0a, 0xeb,
        0x15, 0x09, 'E', 'S', 'P', '_', 'M', 'U', 'L', 'T', 'I', '_', 'A',
        'D', 'V', '_', 'C', 'O', 'D', 'E', 'D', '1'
};

static esp_ble_gap_ext_adv_t ext_adv[1] = {
    // instance, duration, peroid
    [0] = {EXT_ADV_HANDLE, 0, 0},
};

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_EXT_ADV_SET_RAND_ADDR_COMPLETE_EVT:
        xSemaphoreGive(test_sem);
        ESP_LOGI(BLE, "ESP_GAP_BLE_EXT_ADV_SET_RAND_ADDR_COMPLETE_EVT, status %d", param->ext_adv_set_rand_addr.status);
        break;
    case ESP_GAP_BLE_EXT_ADV_SET_PARAMS_COMPLETE_EVT:
        xSemaphoreGive(test_sem);
        ESP_LOGI(BLE, "ESP_GAP_BLE_EXT_ADV_SET_PARAMS_COMPLETE_EVT, status %d", param->ext_adv_set_params.status);
        break;
    case ESP_GAP_BLE_EXT_ADV_DATA_SET_COMPLETE_EVT:
        xSemaphoreGive(test_sem);
        ESP_LOGI(BLE, "ESP_GAP_BLE_EXT_ADV_DATA_SET_COMPLETE_EVT, status %d", param->ext_adv_data_set.status);
        break;
    case ESP_GAP_BLE_EXT_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        xSemaphoreGive(test_sem);
        ESP_LOGI(BLE, "ESP_GAP_BLE_EXT_SCAN_RSP_DATA_SET_COMPLETE_EVT, status %d", param->scan_rsp_set.status);
        break;
    case ESP_GAP_BLE_EXT_ADV_START_COMPLETE_EVT:
        xSemaphoreGive(test_sem);
        ESP_LOGI(BLE, "ESP_GAP_BLE_EXT_ADV_START_COMPLETE_EVT, status %d", param->ext_adv_start.status);
        break;
    case ESP_GAP_BLE_EXT_ADV_STOP_COMPLETE_EVT:
        xSemaphoreGive(test_sem);
        ESP_LOGI(BLE, "ESP_GAP_BLE_EXT_ADV_STOP_COMPLETE_EVT, status %d", param->ext_adv_stop.status);
        break;
    case ESP_GAP_BLE_PERIODIC_ADV_SET_PARAMS_COMPLETE_EVT:
        xSemaphoreGive(test_sem);
        ESP_LOGI(BLE, "ESP_GAP_BLE_PERIODIC_ADV_SET_PARAMS_COMPLETE_EVT, status %d", param->peroid_adv_set_params.status);
        break;
    case ESP_GAP_BLE_PERIODIC_ADV_DATA_SET_COMPLETE_EVT:
        xSemaphoreGive(test_sem);
        ESP_LOGI(BLE, "ESP_GAP_BLE_PERIODIC_ADV_DATA_SET_COMPLETE_EVT, status %d", param->period_adv_data_set.status);
        break;
    case ESP_GAP_BLE_PERIODIC_ADV_START_COMPLETE_EVT:
        xSemaphoreGive(test_sem);
        ESP_LOGI(BLE, "ESP_GAP_BLE_PERIODIC_ADV_START_COMPLETE_EVT, status %d", param->period_adv_start.status);
        break;
    default:
        break;
    }
}

void broadcast_bluetooth(){
    esp_err_t ret;

    // Initialize NVS.
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(BLE, "%s initialize controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(BLE, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }
    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    if (ret) {
        ESP_LOGE(BLE, "%s init bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(BLE, "%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret){
        ESP_LOGE(BLE, "gap register error, error code = %x", ret);
        return;
    }


    vTaskDelay(200 / portTICK_PERIOD_MS);
    test_sem = xSemaphoreCreateBinary();
    memcpy(&raw_ext_adv_data_coded[8], serial_number, 20);


    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P18 );

    FUNC_SEND_WAIT_SEM(esp_ble_gap_ext_adv_set_params(EXT_ADV_HANDLE, &ext_adv_params_lr_coded), test_sem);
    FUNC_SEND_WAIT_SEM(esp_ble_gap_ext_adv_set_rand_addr(EXT_ADV_HANDLE, addr_coded), test_sem);
    FUNC_SEND_WAIT_SEM(esp_ble_gap_config_ext_scan_rsp_data_raw(EXT_ADV_HANDLE, sizeof(raw_ext_adv_data_coded), &raw_ext_adv_data_coded[0]), test_sem);
    // FUNC_SEND_WAIT_SEM(esp_ble_gap_config_ext_adv_data_raw(EXT_ADV_HANDLE, sizeof(raw_ext_adv_data_coded), &raw_ext_adv_data_coded[0]), test_sem);


    // start all adv
    FUNC_SEND_WAIT_SEM(esp_ble_gap_ext_adv_start(NUM_EXT_ADV, &ext_adv[0]), test_sem);

    int i = esp_ble_tx_power_get(ESP_BLE_PWR_TYPE_CONN_HDL0);
    ESP_LOGI(BLE, "TX power level is: %d", i);
}


void app_main(void)
{
    // Initialize UART communication 
    initialize();
    
    // Get the serial number
    efuse_set();

    // Broadcast the serial number 
    broadcast_bluetooth();
    

}
