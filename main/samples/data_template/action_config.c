#ifdef ACTION_ENABLED

#define TOTAL_ACTION_COUNTS (1)

static TYPE_DEF_TEMPLATE_INT sg_led_blink_in_count     = 1;
static TYPE_DEF_TEMPLATE_INT sg_led_blink_in_interval  = 3;
static TYPE_DEF_TEMPLATE_INT sg_led_blink_in_onTIme    = 200;
static DeviceProperty        g_actionInput_led_blink[] = {

    {.key = "count", .data = &sg_led_blink_in_count, .type = TYPE_TEMPLATE_INT},
    {.key = "interval", .data = &sg_led_blink_in_interval, .type = TYPE_TEMPLATE_INT},
    {.key = "onTIme", .data = &sg_led_blink_in_onTIme, .type = TYPE_TEMPLATE_INT},
};
static TYPE_DEF_TEMPLATE_INT    sg_led_blink_out_code             = 0;
static TYPE_DEF_TEMPLATE_STRING sg_led_blink_out_message[128 + 1] = {0};
static DeviceProperty           g_actionOutput_led_blink[]        = {

    {.key = "code", .data = &sg_led_blink_out_code, .type = TYPE_TEMPLATE_INT},
    {.key = "message", .data = sg_led_blink_out_message, .type = TYPE_TEMPLATE_STRING},
};

static DeviceAction g_actions[] = {

    {
        .pActionId  = "led_blink",
        .timestamp  = 0,
        .input_num  = sizeof(g_actionInput_led_blink) / sizeof(g_actionInput_led_blink[0]),
        .output_num = sizeof(g_actionOutput_led_blink) / sizeof(g_actionOutput_led_blink[0]),
        .pInput     = g_actionInput_led_blink,
        .pOutput    = g_actionOutput_led_blink,
    },
};

#endif
