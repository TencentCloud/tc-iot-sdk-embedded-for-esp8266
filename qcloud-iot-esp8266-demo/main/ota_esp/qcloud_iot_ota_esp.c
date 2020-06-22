/*
 * Tencent Cloud IoT AT library
 * Copyright (C) 2016 THL A29 Limited, a Tencent company. All rights reserved.

 * Licensed under the MIT License (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://opensource.org/licenses/MIT

 * Unless required by applicable law or agreed to in writing, software distributed under the License is
 * distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>

#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#include "qcloud_iot_ota_esp.h"
#include "qcloud_iot_import.h"
#include "utils_param_check.h"

#ifdef CONFIG_QCLOUD_OTA_ESP_ENABLED

#define OTA_CLIENT_TASK_NAME        "ota_esp_task"
#define OTA_CLIENT_TASK_STACK_BYTES 5120
#define OTA_CLIENT_TASK_PRIO        3

#define ESP_OTA_BUF_LEN   2048
#define MAX_OTA_RETRY_CNT 3
#define MAX_SIZE_OF_FW_VERSION 32

typedef struct _EspOTAHandle {
    esp_partition_t  partition;
    esp_ota_handle_t handle;
} EspOTAHandle;

typedef struct OTAContextData {
    void *ota_handle;
    void *mqtt_client;

    // remote_version means version for the FW in the cloud and to be downloaded
    char     remote_version[MAX_SIZE_OF_FW_VERSION];
    uint32_t fw_file_size;

    // for resuming download
    char     downloading_version[MAX_SIZE_OF_FW_VERSION];
    uint32_t downloaded_size;
    uint32_t ota_fail_cnt;    

    EspOTAHandle *esp_ota;

    TaskHandle_t task_handle;
    char         local_version[MAX_SIZE_OF_FW_VERSION];
} OTAContextData;

static OTAContextData sg_ota_ctx         = {0};
static bool           g_fw_downloading   = false;
static bool           g_ota_task_running = false;


#define SUPPORT_RESUMING_DOWNLOAD

#ifdef SUPPORT_RESUMING_DOWNLOAD

int _read_esp_fw(void *dst_buf, uint32_t read_size_bytes, uint32_t fetched_bytes, OTAContextData *ota_ctx)
{
    esp_err_t ret;
    int       retry_cnt = 0;

    if (fetched_bytes % 4 || read_size_bytes % 4) {
        Log_e("fetched size: %u and read bytes: %u should be word aligned", fetched_bytes, read_size_bytes);
        return -1;
    }

    uint32_t src_addr = ota_ctx->esp_ota->partition.address + fetched_bytes;

    do {
        ret = spi_flash_read(src_addr, dst_buf, read_size_bytes);
        if (ret != ESP_OK) {
            retry_cnt++;
            if (retry_cnt > 3)
                return -1;
            Log_e("read %u bytes from addr %u failed: %u retry: %d", read_size_bytes, src_addr, ret, retry_cnt);
            HAL_SleepMs(100);
        }
    } while (ret != ESP_OK);

    return 0;
}

// calculate left MD5 for resuming download from break point
static int _cal_exist_fw_md5(OTAContextData *ota_ctx)
{
    char * buff;
    size_t rlen, total_read = 0;
    int    ret = QCLOUD_RET_SUCCESS;

    ret = IOT_OTA_ResetClientMD5(ota_ctx->ota_handle);
    if (ret) {
        Log_e("reset MD5 failed: %d", ret);
        return QCLOUD_ERR_FAILURE;
    }

    buff = HAL_Malloc(ESP_OTA_BUF_LEN);
    if (buff == NULL) {
        Log_e("malloc ota buffer failed");
        return QCLOUD_ERR_MALLOC;
    }

    size_t size = ota_ctx->downloaded_size;

    while (total_read < ota_ctx->downloaded_size) {
        rlen = (size > ESP_OTA_BUF_LEN) ? ESP_OTA_BUF_LEN : size;
        ret = _read_esp_fw(buff, rlen, total_read, ota_ctx);        
        if (ret) {
            Log_e("read data from flash error");
            ret = QCLOUD_ERR_FAILURE;
            break;
        }
        IOT_OTA_UpdateClientMd5(ota_ctx->ota_handle, buff, rlen);
        size -= rlen;
        total_read += rlen;
    }

    HAL_Free(buff);
    Log_d("total read: %d", total_read);
    return ret;
}

#endif



static int _save_fw_data(OTAContextData *ota_ctx, char *buf, int len)
{
    if (esp_ota_write(ota_ctx->esp_ota->handle, buf, len) != ESP_OK) {
        Log_e("write esp fw failed");
        return QCLOUD_ERR_FAILURE;
    }

    return 0;
}

static int _init_esp_fw_ota(EspOTAHandle *ota_handle, size_t fw_size)
{
    esp_partition_t *      partition_ptr = NULL;
    esp_partition_t        partition;
    const esp_partition_t *next_partition = NULL;

    // search ota partition
    partition_ptr = (esp_partition_t *)esp_ota_get_boot_partition();
    if (partition_ptr == NULL) {
        Log_e("esp boot partition NULL!");
        return QCLOUD_ERR_FAILURE;
    }

    Log_i("partition type: %d subtype: %d addr: 0x%x label: %s", partition_ptr->type, partition_ptr->subtype,
          partition_ptr->address, partition_ptr->label);

    if (partition_ptr->type != ESP_PARTITION_TYPE_APP) {
        Log_e("esp_current_partition->type != ESP_PARTITION_TYPE_APP");
        return QCLOUD_ERR_FAILURE;
    }

    if (partition_ptr->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
        partition.subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0;
    } else {
        next_partition = esp_ota_get_next_update_partition(partition_ptr);

        if (next_partition) {
            partition.subtype = next_partition->subtype;
        } else {
            partition.subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0;
        }
    }

    partition.type = ESP_PARTITION_TYPE_APP;

    partition_ptr = (esp_partition_t *)esp_partition_find_first(partition.type, partition.subtype, NULL);
    if (partition_ptr == NULL) {
        Log_e("esp app partition NULL!");
        return QCLOUD_ERR_FAILURE;
    }

    Log_i("to use partition type: %d subtype: %d addr: 0x%x label: %s", partition_ptr->type, partition_ptr->subtype,
          partition_ptr->address, partition_ptr->label);
    memcpy(&ota_handle->partition, partition_ptr, sizeof(esp_partition_t));
    if (esp_ota_begin(&ota_handle->partition, fw_size, &ota_handle->handle) != ESP_OK) {
        Log_e("esp_ota_begin failed!");
        return QCLOUD_ERR_FAILURE;
    }

    Log_i("esp_ota_begin done!");

    return 0;
}

static int _pre_ota_download(OTAContextData *ota_ctx)
{
#ifdef SUPPORT_RESUMING_DOWNLOAD
    // re-generate MD5 for resuming download */
    if (ota_ctx->downloaded_size 
            && strncmp(ota_ctx->remote_version, ota_ctx->downloading_version, MAX_SIZE_OF_FW_VERSION) == 0) {
        Log_i("setup local MD5 with offset: %d for version %s", ota_ctx->downloaded_size, ota_ctx->remote_version);
        int ret = _cal_exist_fw_md5(ota_ctx);
        if (ret) {
            Log_e("regen OTA MD5 error: %d", ret);
            ota_ctx->downloaded_size = 0;
            return 0;
        }
        Log_d("local MD5 update done!");
        return 0;
    }
#endif

    // new download, erase partition first
    ota_ctx->downloaded_size = 0;
    if (_init_esp_fw_ota(ota_ctx->esp_ota, ota_ctx->fw_file_size)) {
        Log_e("init esp ota failed");
        return QCLOUD_ERR_FAILURE;
    }

    return 0;
}

static int _post_ota_download(OTAContextData *ota_ctx)
{
    if (esp_ota_end(ota_ctx->esp_ota->handle) != ESP_OK) {
        Log_e("esp_ota_end failed!");
        return QCLOUD_ERR_FAILURE;
    }
    Log_i("esp_ota_end done!");

    if (esp_ota_set_boot_partition(&ota_ctx->esp_ota->partition) != ESP_OK) {
        Log_e("esp_ota_set_boot_partition failed!");
        return QCLOUD_ERR_FAILURE;
    }
    Log_i("esp_ota_set_boot_partition done!");
    return 0;
}


// main OTA cycle
static void _ota_update_task(void *pvParameters)
{
    OTAContextData *ota_ctx               = (OTAContextData *)pvParameters;
    bool            upgrade_fetch_success = true;
    char *          buf_ota               = NULL;
    int             rc;
    void *          h_ota               = ota_ctx->ota_handle;
    int             mqtt_disconnect_cnt = 0;
    EspOTAHandle    esp_ota             = {0};

    if (h_ota == NULL) {
        Log_e("mqtt ota not ready");
        goto end_of_ota;
    }

    ota_ctx->esp_ota = &esp_ota;

    Log_i("start ota update task!");

begin_of_ota:    

    while (g_ota_task_running) {

        // recv the upgrade cmd
        if (IOT_OTA_IsFetching(h_ota)) {
            g_fw_downloading = true;
            Log_i(">>>>>>>>>> start firmware download!");

            IOT_OTA_Ioctl(h_ota, IOT_OTAG_FILE_SIZE, &ota_ctx->fw_file_size, 4);
            memset(ota_ctx->remote_version, 0, MAX_SIZE_OF_FW_VERSION);
            IOT_OTA_Ioctl(h_ota, IOT_OTAG_VERSION, ota_ctx->remote_version, MAX_SIZE_OF_FW_VERSION);

            rc = _pre_ota_download(ota_ctx);
            if (rc) {
                Log_e("pre ota download failed: %d", rc);
                upgrade_fetch_success = false;
                goto end_of_ota;
            }

            buf_ota = HAL_Malloc(ESP_OTA_BUF_LEN + 1);
            if (buf_ota == NULL) {
                Log_e("malloc ota buffer failed");
                upgrade_fetch_success = false;
                goto end_of_ota;
            }

            /*set offset and start http connect*/
            rc = IOT_OTA_StartDownload(h_ota, ota_ctx->downloaded_size, ota_ctx->fw_file_size);
            if (QCLOUD_RET_SUCCESS != rc) {
                Log_e("OTA download start err,rc:%d", rc);
                upgrade_fetch_success = false;
                goto end_of_ota;
            }

            // download and save the fw
            while (!IOT_OTA_IsFetchFinish(h_ota)) {
                if (!g_ota_task_running) {
                    Log_e("OTA task stopped during downloading!");
                    upgrade_fetch_success = false;
                    goto end_of_ota;
                }

                memset(buf_ota, 0, ESP_OTA_BUF_LEN + 1);
                int len = IOT_OTA_FetchYield(h_ota, buf_ota, ESP_OTA_BUF_LEN + 1, 20);
                if (len > 0) {
                    // Log_i("save fw data %d from addr %u", len, ota_ctx->downloaded_size);
                    rc = _save_fw_data(ota_ctx, buf_ota, len);
                    if (rc) {
                        Log_e("write data to file failed");
                        upgrade_fetch_success = false;
                        goto end_of_ota;
                    }
                } else if (len < 0) {
                    Log_e("download fail rc=%d, size_downloaded=%u", len, ota_ctx->downloaded_size);
                    upgrade_fetch_success = false;
                    goto end_of_ota;
                } else {
                    Log_e("OTA download timeout! size_downloaded=%u", ota_ctx->downloaded_size);
                    upgrade_fetch_success = false;
                    goto end_of_ota;
                }

                // get OTA downloaded size
                IOT_OTA_Ioctl(h_ota, IOT_OTAG_FETCHED_SIZE, &ota_ctx->downloaded_size, 4);
                // delay is needed to avoid TCP read timeout
                HAL_SleepMs(500);

                if (!IOT_MQTT_IsConnected(ota_ctx->mqtt_client)) {
                    mqtt_disconnect_cnt++;
                    Log_e("MQTT disconnect %d during OTA download!", mqtt_disconnect_cnt);
                    if (mqtt_disconnect_cnt > 3) {
                        upgrade_fetch_success = false;
                        goto end_of_ota;
                    }
                    HAL_SleepMs(2000);
                } else {
                    mqtt_disconnect_cnt = 0;
                }
            }

            /* Must check MD5 match or not */
            if (upgrade_fetch_success) {
                uint32_t firmware_valid = 0;
                IOT_OTA_Ioctl(h_ota, IOT_OTAG_CHECK_FIRMWARE, &firmware_valid, 4);
                if (0 == firmware_valid) {
                    Log_e("The firmware is invalid");
                    ota_ctx->downloaded_size = 0;
                    upgrade_fetch_success = false;
                    // don't retry for this error
                    ota_ctx->ota_fail_cnt = MAX_OTA_RETRY_CNT + 1;
                    goto end_of_ota;
                } else {
                    Log_i("The firmware is valid");

                    rc = _post_ota_download(ota_ctx);
                    if (rc) {
                        Log_e("post ota handling failed: %d", rc);
                        upgrade_fetch_success = false;
                        // don't retry for this error
                        ota_ctx->ota_fail_cnt = MAX_OTA_RETRY_CNT + 1;
                        goto end_of_ota;
                    }
                    upgrade_fetch_success = true;
                    break;
                }
            }
        } else if (IOT_OTA_GetLastError(h_ota)) {
            Log_e("OTA update failed! last error: %d", IOT_OTA_GetLastError(h_ota));
            upgrade_fetch_success = false;
            goto end_of_ota;
        }

        HAL_SleepMs(1000);
    }

end_of_ota:

    if (!upgrade_fetch_success && g_fw_downloading) {
        IOT_OTA_ReportUpgradeFail(h_ota, NULL);
        ota_ctx->ota_fail_cnt++;
    }

    // do it again
    if (g_ota_task_running && IOT_MQTT_IsConnected(ota_ctx->mqtt_client) && !upgrade_fetch_success &&
        ota_ctx->ota_fail_cnt <= MAX_OTA_RETRY_CNT) {
        HAL_Free(buf_ota);
        buf_ota               = NULL;
        g_fw_downloading      = false;
        upgrade_fetch_success = true;

        Log_e("OTA failed! downloaded %u. retry %d time!", ota_ctx->downloaded_size, ota_ctx->ota_fail_cnt);
        HAL_SleepMs(1000);

        if (0 > IOT_OTA_ReportVersion(ota_ctx->ota_handle, ota_ctx->local_version)) {
            Log_e("report OTA version %s failed", ota_ctx->local_version);
        }

#ifdef SUPPORT_RESUMING_DOWNLOAD
        // resuming download
        if (ota_ctx->downloaded_size) {
            memset(ota_ctx->downloading_version, 0, MAX_SIZE_OF_FW_VERSION);
            strncpy(ota_ctx->downloading_version, ota_ctx->remote_version, MAX_SIZE_OF_FW_VERSION);
        }
#else
        // fresh restart
        ota_ctx->downloaded_size = 0; 
#endif

        goto begin_of_ota;
    }

    if (upgrade_fetch_success && g_fw_downloading) {
        IOT_OTA_ReportUpgradeSuccess(h_ota, NULL);
        Log_w("OTA update success! Reboot after a while");

        HAL_SleepMs(2000);
        esp_restart();

    } else if (g_fw_downloading) {
        Log_e("OTA failed! Downloaded: %u. Quit the task and reset", ota_ctx->downloaded_size);
    }

    g_fw_downloading = false;
    Log_w(">>>>>>>>>> OTA task going to be deleted");

    if (buf_ota) {
        HAL_Free(buf_ota);
        buf_ota = NULL;
    }

    IOT_OTA_Destroy(ota_ctx->ota_handle);
    memset(ota_ctx, 0, sizeof(OTAContextData));

    vTaskDelete(NULL);
    return;
}

#endif

int disable_ota_task(void)
{
#ifdef CONFIG_QCLOUD_OTA_ESP_ENABLED
    if (g_ota_task_running) {
        g_ota_task_running = false;
        Log_w(">>>>>>>>>> Disable OTA upgrade!");

        do {
            HAL_SleepMs(1000);
        } while (is_fw_downloading());

        HAL_SleepMs(500);
    }
#endif
    return 0;
}

int enable_ota_task(DeviceInfo *dev_info, void *mqtt_client, char *version)
{
#ifdef CONFIG_QCLOUD_OTA_ESP_ENABLED
    POINTER_SANITY_CHECK(dev_info, QCLOUD_ERR_INVAL);
    POINTER_SANITY_CHECK(mqtt_client, QCLOUD_ERR_INVAL);
    POINTER_SANITY_CHECK(version, QCLOUD_ERR_INVAL);

    /* to enable FW update */
    g_ota_task_running = true;
    if (sg_ota_ctx.ota_handle == NULL) {
        void *ota_handle  = IOT_OTA_Init(dev_info->product_id, dev_info->device_name, mqtt_client);
        if (NULL == ota_handle) {
            Log_e("initialize OTA failed");
            return QCLOUD_ERR_FAILURE;
        }

        memset(&sg_ota_ctx, 0, sizeof(sg_ota_ctx));
        sg_ota_ctx.mqtt_client = mqtt_client;
        sg_ota_ctx.ota_handle  = ota_handle;

        int ret = xTaskCreate(_ota_update_task, OTA_CLIENT_TASK_NAME, OTA_CLIENT_TASK_STACK_BYTES, (void *)&sg_ota_ctx,
                              OTA_CLIENT_TASK_PRIO, &sg_ota_ctx.task_handle);
        if (ret != pdPASS) {
            Log_e("create ota task failed: %d", ret);
            IOT_OTA_Destroy(sg_ota_ctx.ota_handle);
            memset(&sg_ota_ctx, 0, sizeof(sg_ota_ctx));
            return QCLOUD_ERR_FAILURE;
        }
    }

    /* report current user version */
    if (0 > IOT_OTA_ReportVersion(sg_ota_ctx.ota_handle, version)) {
        Log_e("report OTA version %s failed", version);
        g_ota_task_running = false;
        HAL_SleepMs(1000);
        IOT_OTA_Destroy(sg_ota_ctx.ota_handle);
        memset(&sg_ota_ctx, 0, sizeof(sg_ota_ctx));
        return QCLOUD_ERR_FAILURE;
    }
    memset(sg_ota_ctx.local_version, 0, MAX_SIZE_OF_FW_VERSION + 4);
    strncpy(sg_ota_ctx.local_version, version, strlen(version));
#else
    Log_w("OTA on ESP is not enabled!");
#endif
    return 0;
}

bool is_fw_downloading(void)
{
#ifdef CONFIG_QCLOUD_OTA_ESP_ENABLED
    return g_fw_downloading;
#else
    return false;
#endif
}

