#
# Component Makefile
#

COMPONENT_ADD_INCLUDEDIRS := include include/exports sdk_src/internal_inc
COMPONENT_SRCDIRS := sdk_src platform
# 指定编译文件
COMPONENT_OBJS = sdk_src/data_template_action.o \
				sdk_src/data_template_client_common.o \
				sdk_src/data_template_client_json.o 	\
				sdk_src/data_template_client_manager.o 	\
				sdk_src/data_template_client.o 			\
				sdk_src/data_template_event.o 				\
				sdk_src/device_bind.o   					\
				sdk_src/dynreg.o \
				sdk_src/gateway_api.o  \
				sdk_src/gateway_common.o \
				sdk_src/json_parser.o                                        \
				sdk_src/json_token.o                                        \
				sdk_src/kgmusic_client.o                                        \
				sdk_src/log_mqtt.o                                        \
				sdk_src/log_upload.o                                        \
				sdk_src/mqtt_client.o                                        \
				sdk_src/mqtt_client_common.o                                        \
				sdk_src/mqtt_client_connect.o                                        \
				sdk_src/mqtt_client_net.o                                        \
				sdk_src/mqtt_client_publish.o                                        \
				sdk_src/mqtt_client_subscribe.o                                        \
				sdk_src/mqtt_client_unsubscribe.o                                        \
				sdk_src/mqtt_client_yield.o                                        \
				sdk_src/network_interface.o                                        \
				sdk_src/network_socket.o                                        \
				sdk_src/network_tls.o                                        \
				sdk_src/ota_client.o                                        \
				sdk_src/ota_fetch.o                                        \
				sdk_src/ota_lib.o                                        \
				sdk_src/ota_mqtt.o                                        \
				sdk_src/qcloud_iot_ca.o                                        \
				sdk_src/qcloud_iot_device.o                                        \
				sdk_src/qcloud_iot_log.o                                        \
				sdk_src/service_mqtt.o                                        \
				sdk_src/string_utils.o                                        \
				sdk_src/utils_aes.o                                        \
				sdk_src/utils_base64.o                                        \
				sdk_src/utils_getopt.o                                        \
				sdk_src/utils_hmac.o                                        \
				sdk_src/utils_httpc.o                                        \
				sdk_src/utils_list.o                                        \
				sdk_src/utils_md5.o                                        \
				sdk_src/utils_ringbuff.o                                        \
				sdk_src/utils_sha1.o                                        \
				sdk_src/utils_timer.o                                        \
				sdk_src/utils_url_download.o                                        \
				sdk_src/utils_url_upload.o                                        \
				platform/HAL_Device_freertos.o                                        \
				platform/HAL_OS_freertos.o                                        \
				platform/HAL_TCP_lwip.o                                        \
				platform/HAL_TLS_mbedtls.o                                        \
				platform/HAL_Timer_freertos.o                                        \
				platform/HAL_UDP_lwip.o                                   \
				platform/HAL_Airkiss.o  \
				platform/HAL_Soft_ap.o             \
				platform/HAL_Smart_config.o              \
				platform/HAL_Simple_config.o            \
				platform/HAL_BTCombo_config.o           \
				platform/HAL_Wifi_api.o         \
				sdk_src/qcloud_wifi_config.o             \
				sdk_src/qcloud_wifi_config_comm_service.o        \
				sdk_src/qcloud_wifi_config_device_bind.o        \
				sdk_src/qcloud_wifi_config_error_handle.o      \
				sdk_src/qcloud_wifi_config_log_handle.o      