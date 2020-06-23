/*
 * Copyright (c) 2020 Tencent Cloud. All rights reserved.

 * Licensed under the MIT License (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://opensource.org/licenses/MIT

 * Unless required by applicable law or agreed to in writing, software distributed under the License is
 * distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <string.h>
#include <time.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "cJSON.h"
#include "spi_flash.h"

#include "qcloud_iot_export_log.h"
#include "qcloud_iot_import.h"
#include "wifi_config_internal.h"

/************** WiFi config error msg collect and post feature ******************/

/* FreeRTOS msg queue */
static void *g_dev_log_queue = NULL;
#define LOG_QUEUE_SIZE 10
#define LOG_ITEM_SIZE  128

int init_dev_log_queue(void)
{
#ifdef WIFI_LOG_UPLOAD
    if (g_dev_log_queue) {
        Log_d("re-enter, reset queue");
        xQueueReset(g_dev_log_queue);
        return 0;
    }

    g_dev_log_queue = xQueueCreate(LOG_QUEUE_SIZE, LOG_ITEM_SIZE);
    if (g_dev_log_queue == NULL) {
        Log_e("xQueueCreate failed");
        return ERR_OS_QUEUE;
    }
#endif
    return 0;
}

void delete_dev_log_queue(void)
{
#ifdef WIFI_LOG_UPLOAD
    vQueueDelete(g_dev_log_queue);
    g_dev_log_queue = NULL;
#endif
}

int push_dev_log(const char *func, const int line, const char *fmt, ...)
{
#ifdef WIFI_LOG_UPLOAD
    if (g_dev_log_queue == NULL) {
        Log_e("log queue not initialized!");
        return ERR_OS_QUEUE;
    }

    char log_buf[LOG_ITEM_SIZE];
    memset(log_buf, 0, LOG_ITEM_SIZE);

    // only keep the latest LOG_QUEUE_SIZE log
    uint32_t log_cnt = (uint32_t)uxQueueMessagesWaiting(g_dev_log_queue);
    if (log_cnt == LOG_QUEUE_SIZE) {
        // pop the oldest one
        xQueueReceive(g_dev_log_queue, log_buf, 0);
        HAL_Printf("<<< POP LOG: %s", log_buf);
    }

    char *o = log_buf;
    memset(log_buf, 0, LOG_ITEM_SIZE);
    o += HAL_Snprintf(o, LOG_ITEM_SIZE, "%u|%s(%d): ", HAL_GetTimeMs(), func, line);

    va_list ap;
    va_start(ap, fmt);
    HAL_Vsnprintf(o, LOG_ITEM_SIZE - 3 - strlen(log_buf), fmt, ap);
    va_end(ap);

    strcat(log_buf, "\r\n");

    /* unblocking send */
    int ret = xQueueGenericSend(g_dev_log_queue, log_buf, 0, queueSEND_TO_BACK);
    if (ret != pdPASS) {
        Log_e("xQueueGenericSend failed: %d", ret);
        return ERR_OS_QUEUE;
    }

    // HAL_Printf(">>> PUSH LOG: %s\n", log_buf);
#endif
    return 0;
}


int app_send_dev_log(comm_peer_t *peer)
{
    int ret = 0;
#ifdef WIFI_LOG_UPLOAD

    if (g_dev_log_queue == NULL) {
        Log_e("log queue not initialized!");
        return ERR_OS_QUEUE;
    }

    uint32_t log_cnt = (uint32_t)uxQueueMessagesWaiting(g_dev_log_queue);
    if (log_cnt == 0)
        return 0;

    size_t max_len  = (log_cnt * LOG_ITEM_SIZE) + 32;
    char * json_buf = HAL_Malloc(max_len);
    if (json_buf == NULL) {
        Log_e("malloc failed!");
        return -1;
    }

    memset(json_buf, 0, max_len);

    char                 log_buf[LOG_ITEM_SIZE];
    signed portBASE_TYPE rc;
    do {
        memset(log_buf, 0, LOG_ITEM_SIZE);
        rc = xQueueReceive(g_dev_log_queue, log_buf, 0);
        if (rc == pdPASS) {
            strcat(json_buf, log_buf);
        }
    } while (rc == pdPASS);

    HAL_Printf("to reply: %s\r\n", json_buf);

    int i = 0;
    for (i = 0; i < 2; i++) {
        ret = sendto(peer->socket_id, json_buf, strlen(json_buf), 0, peer->socket_addr, peer->addr_len);
        if (ret < 0) {
            Log_e("send error: %s", strerror(errno));
            break;
        }
        HAL_SleepMs(500);
    }

    // vQueueDelete(g_dev_log_queue);
#endif
    return ret;
}
