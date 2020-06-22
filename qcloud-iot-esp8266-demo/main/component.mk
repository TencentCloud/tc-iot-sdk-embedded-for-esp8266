#
# Component Makefile
#

COMPONENT_ADD_INCLUDEDIRS += ./
COMPONENT_ADD_INCLUDEDIRS += ./wifi_config

COMPONENT_ADD_INCLUDEDIRS += ./ota_esp
COMPONENT_SRCDIRS += ./ota_esp

ifdef CONFIG_SMART_LIGHT_ENABLED
COMPONENT_SRCDIRS += ./samples/data_template_light
endif

ifdef CONFIG_GATEWAY_ENABLED
COMPONENT_SRCDIRS += ./samples/gateway
endif

ifdef CONFIG_RAW_DATA_ENABLED
COMPONENT_SRCDIRS += ./samples/raw_data
endif

ifdef CONFIG_MQTT_ENABLED
COMPONENT_SRCDIRS += ./samples/mqtt
endif

ifndef CONFIG_QCLOUD_IOT_EXPLORER_ENABLED
COMPONENT_SRCDIRS += ./samples/mqtt
endif

ifdef CONFIG_WIFI_CONFIG_ENABLED
COMPONENT_SRCDIRS += ./wifi_config
endif
