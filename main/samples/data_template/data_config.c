/*-----------------data config start  -------------------*/
#include "qcloud_iot_export.h"

#define TOTAL_PROPERTY_COUNT 5

static sDataPoint sg_DataTemplate[TOTAL_PROPERTY_COUNT];

typedef struct _ProductDataDefine {
    TYPE_DEF_TEMPLATE_BOOL   m_power_switch;
    TYPE_DEF_TEMPLATE_INT    m_brightness;
    TYPE_DEF_TEMPLATE_ENUM   m_color;
    TYPE_DEF_TEMPLATE_INT    m_color_temp;
    TYPE_DEF_TEMPLATE_STRING m_name[64 + 1];
} ProductDataDefine;

static ProductDataDefine sg_ProductData;

static void _init_data_template(void)
{
    sg_ProductData.m_power_switch         = 0;
    sg_DataTemplate[0].data_property.data = &sg_ProductData.m_power_switch;
    sg_DataTemplate[0].data_property.key  = "power_switch";
    sg_DataTemplate[0].data_property.type = TYPE_TEMPLATE_BOOL;
    sg_DataTemplate[0].state              = eCHANGED;

    sg_ProductData.m_brightness           = 1;
    sg_DataTemplate[1].data_property.data = &sg_ProductData.m_brightness;
    sg_DataTemplate[1].data_property.key  = "brightness";
    sg_DataTemplate[1].data_property.type = TYPE_TEMPLATE_INT;
    sg_DataTemplate[1].state              = eCHANGED;

    sg_ProductData.m_color                = 0;
    sg_DataTemplate[2].data_property.data = &sg_ProductData.m_color;
    sg_DataTemplate[2].data_property.key  = "color";
    sg_DataTemplate[2].data_property.type = TYPE_TEMPLATE_ENUM;
    sg_DataTemplate[2].state              = eCHANGED;

    sg_ProductData.m_color_temp           = 0;
    sg_DataTemplate[3].data_property.data = &sg_ProductData.m_color_temp;
    sg_DataTemplate[3].data_property.key  = "color_temp";
    sg_DataTemplate[3].data_property.type = TYPE_TEMPLATE_INT;
    sg_DataTemplate[3].state              = eCHANGED;

    sg_ProductData.m_name[0]                       = '\0';
    sg_DataTemplate[4].data_property.data          = sg_ProductData.m_name;
    sg_DataTemplate[4].data_property.data_buff_len = sizeof(sg_ProductData.m_name) / sizeof(sg_ProductData.m_name[0]);
    sg_DataTemplate[4].data_property.key           = "name";
    sg_DataTemplate[4].data_property.type          = TYPE_TEMPLATE_STRING;
    sg_DataTemplate[4].state                       = eCHANGED;
};
