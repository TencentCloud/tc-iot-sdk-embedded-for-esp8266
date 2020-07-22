# 物联网WiFi设备SmartConfig方式配网

## SmartConfig配网简介及交互流程
#### WiFi配网
WiFi配网指的是，由外部向WiFi设备提供SSID和密码（PSW），让WiFi设备可以连接指定的热点或路由器并加入后者所建立的WiFi网络。对于具备丰富人机界面包括屏幕/键盘的设备比如电脑或者手机，可以直接输入SSID/PSW来进行连接，而对于不具备丰富人机交互界面的物联网WiFi设备比如智能灯、扫地机器人等，则可以借助手机等智能设备，以某种配网方式将SSID/PSW告诉该设备。

#### SmartConfig配网及设备绑定
SmartConfig方式配网的基本原理是先让设备进入WiFi混杂模式（promiscuous mode）以监听捕获周围的WiFi报文，由于设备还没有联网，而WiFi网络的数据帧是通过加密的，设备无法知道payload的内容，但是可以知道报文的某些特征数据比如每个报文的长度，同时对于某些数据帧比如UDP的广播包或多播包，其报文的帧头结构比较固定，可以很容易的识别出来。这个时候在手机app或者小程序测，就可以通过发送UDP的广播包或者多播包，并利用报文的特征比如长度变化来进行编码，将目标WiFi路由器的SSID/PSW字符以约定的编码方式发送出去，设备端在捕获到UDP报文后按约定的方式进行解码就可以得到目标WiFi路由器的相关信息并进行联网。

SmartConfig方式配网，在编码方式上面，每个厂商有自己的协议，ESP8266同时支持 **乐鑫 [ESP-TOUCH协议](https://www.espressif.com/zh-hans/products/software/esp-touch/overview)** 和 **微信 [AirKiss协议](https://iot.weixin.qq.com/wiki/new/index.html?page=4-1-1)**。基于这两种协议，设备端在连接WiFi路由器成功之后，会告知手机端自己的IP地址，这个时候手机端可以通过数据通道比如TCP/UDP通讯将后台提供的配网token发送给设备，并由设备转发至物联网后台，依据token可以进行设备绑定。

腾讯连连小程序支持采用 **ESP-TOUCH** 或 **AirKiss** 协议进行SmartConfig配网，并提供了相应的[小程序SDK](https://github.com/tencentyun/qcloud-iotexplorer-appdev-miniprogram-sdk).
下面是SmartConfig方式配网及设备绑定的示例流程图：
![](https://main.qcloudimg.com/raw/60a5a3f9973135430a592bbeb5d591b6.jpg)

## SmartConfig配网设备端与腾讯连连小程序及后台交互的数据协议
1. 腾讯连连小程序进入配网模式后，会从物联网开发平台服务获取到当次配网的token，小程序相关操作可以参考 [生成Wi-Fi设备配网Token](https://cloud.tencent.com/document/product/1081/44044)

2. 使WiFi设备进入SmartConfig配网模式，看到设备有指示灯在快闪，则说明进入配网模式成功。
  
3. 小程序按照提示依次获取WiFi列表，输入家里目标路由器的SSID/PSW，按下一步之后就会通过SmartConfig方式发送报文。

4. 设备端通过监听捕获SmartConfig报文，解析出目标路由器的SSID/PSW并进行联网，联网成功之后设备会告知小程序自己的IP地址，同时开始连接物联网后台。

5. 小程序作为UDP客户端会连接WiFi设备上面的UDP服务（默认端口为**8266**），给设备发送配网token，JSON格式为：
```
   {"cmdType":0,"token":"6ab82618a9d529a2ee777bf6e528a0fd"} 
```
   发送完了之后等待设备UDP回复设备信息及配网协议版本号：
```   
   {"cmdType":2,"productId":"OSPB5ASRWT","deviceName":"dev_01","protoVersion":"2.0"}
```
6. 如果2秒之内没有收到设备回复，则重复步骤5，UDP客户端重复发送配网token。
   如果重复发送5次都没有收到回复，则认为配网失败，WiFi设备有异常。
   
7. 如果步骤5收到设备回复，则说明设备端已经收到token，并准备上报token。这个时候小程序会开始通过token轮询物联网后台来确认配网及设备绑定是否成功。小程序相关操作可以参考 [查询配网Token状态](https://cloud.tencent.com/document/product/1081/44045)

8. 设备端在成功连接WiFi路由器之后，需要通过MQTT连接物联网后台，并将小程序发送来的配网token通过下面MQTT报文上报给后台服务：
```
    topic: $thing/up/service/ProductID/DeviceName
    payload: {"method":"app_bind_token","clientToken":"client-1234","params": {"token":"6ab82618a9d529a2ee777bf6e528a0fd"}}
```
- 设备端可以通过订阅主题 $thing/down/service/ProductID/DeviceName 来获取token上报的结果。
- 注意如果设备需要通过动态注册来创建设备并获取设备密钥，则会先进行动态注册再连接MQTT。

9. 在以上5-7步骤中，如果小程序收到设备UDP服务发送过来的错误日志，且deviceReply字段的值为"Current_Error"，则表示当前配网绑定过程中出错，需要退出配网操作。如果deviceReply字段是"Previous_Error"，则为上一次配网的出错日志，只需要上报，不影响当此操作。
错误日志JSON格式例子：
```
{"cmdType":2,"deviceReply":"Current_Error","log":"ESP WIFI connect error! (10, 2)"} 
```
10. 如果设备成功上报了token，物联网后台服务确认了token有效性，小程序会提示配网完成，设备添加成功。

11. 设备端会记录配网的详细日志，如果配网或者添加设备失败，可以让设备端创建一个特殊的softAP和UDP服务，通过小程序可以从设备端获取更多日志用于错误分析。

## ESP8266使用SmartConfig配网接口
### 腾讯云IoT AT指令ESP8266定制固件
如果ESP8266烧写了腾讯云IoT AT指令ESP8266定制固件，则只要通过指令AT+TCDEVINFOSET配置好设备信息，再通过下面的指令启动SmartConfig配网就可以
```
AT+TCSTARTSMART
```
关于AT指令的详细说明，请参考qcloud-iot-at-esp8266目录文档

### 配网代码示例
在qcloud-iot-esp8266-demo/main/wifi_config目录下，提供了SmartConfig配网在ESP8266上面的参考实现，用户可以使用qcloud-iot-esp8266-demo工程进行体验。

#### 使用示例
配网接口说明请查看wifi_config/qcloud_wifi_config.h，可以按照下面方式使用：

```
    /* to use WiFi config and device binding with Wechat mini program */
    int wifi_config_state;
    int ret = start_smartconfig();
    if (ret) {
        Log_e("start wifi config failed: %d", ret);
    } else {
        /* max waiting: 150 * 2000ms */
        int wait_cnt = 150;
        do {
            Log_d("waiting for wifi config result...");
            HAL_SleepMs(2000);            
            wifi_config_state = query_wifi_config_state();
        } while (wifi_config_state == WIFI_CONFIG_GOING_ON && wait_cnt--);
    }

    wifi_connected = is_wifi_config_successful();
    if (!wifi_connected) {
        Log_e("wifi config failed!");
        // setup a softAP to upload log to mini program
        start_log_softAP();
    }

```

注意如果需要同时支持ESP-TOUCH和AirKiss，设备端启动SmartConfig模式时须按下面方式配置
```
esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_AIRKISS);
```

#### 代码设计说明
配网代码将核心逻辑与平台相关底层操作分离，便于移植到不同的硬件设备上
- qcloud_wifi_config.c：配网相关接口实现，包括UDP服务及MQTT连接及token上报，主要依赖腾讯云物联网C-SDK及FreeRTOS/lwIP运行环境
- wifi_config_esp.c：设备硬件WiFi操作相关接口实现，依赖于ESP8266 RTOS，当使用其他硬件平台时，需要进行移植适配
- wifi_config_error_handle.c：设备错误日志处理，主要依赖于FreeRTOS
- wifi_config_log_handle.c：设备配网日志收集和上报，主要依赖于FreeRTOS

注意如果将SmartConfig移植到不同的芯片平台，需要确保平台支持 **ESP-TOUCH** 或 **AirKiss** 配网协议。同时由于小程序框架限制，小程序通过UDP广播/多播发送ESP-TOUCH协议报文时，会往报文body填入一个固定的IP地址，设备端在回复结果时不应该依赖于该地址，而应当以UDP报文header的源IP地址为准。