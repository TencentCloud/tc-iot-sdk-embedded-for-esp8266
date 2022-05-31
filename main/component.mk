#
# Component Makefile
#

COMPONENT_ADD_INCLUDEDIRS += ./

COMPONENT_ADD_INCLUDEDIRS += ./ota_esp
COMPONENT_SRCDIRS += ./ota_esp

ifdef CONFIG_SMART_LIGHT_ENABLED
COMPONENT_SRCDIRS += ./samples/scenarized
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

ifdef CONFIG_DYNREG_ENABLED
COMPONENT_SRCDIRS += ./samples/dynreg_dev
endif

ifdef CONFIG_DATA_TEMPLATE_ENABLED
COMPONENT_SRCDIRS += ./samples/data_template/
endif

ifdef CONFIG_WIFI_CONFIG_ENABLED
COMPONENT_SRCDIRS += ./samples/wifi_config
endif


