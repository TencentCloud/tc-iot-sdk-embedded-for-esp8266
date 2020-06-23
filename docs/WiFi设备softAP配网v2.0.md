# 物联网WiFi设备SoftAP方式配网v2.0

## SoftAP配网简介及交互流程
#### WiFi配网
WiFi配网指的是，由外部向WiFi设备提供SSID和密码（PSW），让WiFi设备可以连接指定的热点或路由器并加入后者所建立的WiFi网络。对于具备丰富人机界面包括屏幕/键盘的设备比如电脑或者手机，可以直接输入SSID/PSW来进行连接，而对于不具备丰富人机交互界面的物联网WiFi设备比如智能灯、扫地机器人等，则可以借助手机等智能设备，以某种配网方式将SSID/PSW告诉该设备。

配网有多种方式，包括WPS、smartconfig、softAP等等，其中WPS存在安全性问题，而smartconfig虽然比较便捷，但是一般都是各个厂商采用私有协议，兼容性和互操作性较差，而softAP方式配网的优点是适配性兼容性都比较好，对手机或者设备只需要标准的WiFi连接和TCP/UDP操作。缺点是手机端需要做两次WiFi连接设置的切换，步骤稍微复杂。

#### SoftAP配网及设备绑定
SoftAP方式配网的基本原理是让设备通过softAP方式创建一个WiFi热点，手机连接到该设备热点，再通过数据通道比如TCP/UDP通讯将目标WiFi路由器的SSID/PSW告诉设备，设备获取到之后就可以连接目标WiFi路由器从而连接互联网。同时，为了对设备进行绑定，手机app可以利用该TCP/UDP数据通道将后台提供的配网token发送给设备，并由设备转发至物联网后台，依据token可以进行设备绑定。

腾讯连连小程序已经支持softAP配网，并提供了相应的[小程序SDK](https://github.com/tencentyun/qcloud-iotexplorer-appdev-miniprogram-sdk).
下面是基于token的softAP方式配网及设备绑定的示例流程图：
![](https://main.qcloudimg.com/raw/a146b79d88299a59507d81eaad99137c.jpg)

## SoftAP配网设备端与腾讯连连小程序及后台交互的数据协议
1. 腾讯连连小程序进入配网模式后，会从物联网开发平台服务获取到当次配网的token。小程序相关操作可以参考 [生成Wi-Fi设备配网Token](https://cloud.tencent.com/document/product/1081/44044)

2. 使WiFi设备进入softAP配网模式，看到设备有指示灯在快闪，则说明进入配网模式成功。
    
3. 小程序按照提示依次获取WiFi列表，输入家里目标路由器的SSID/PSW，再选择设备softAP热点的SSID/PSW。

4. 手机连接设备softAP热点成功之后，小程序作为UDP客户端会连接WiFi设备上面的UDP服务（默认IP为**192.168.4.1**，端口为**8266**）

5. 小程序给设备UDP服务发送目标WiFi路由器的SSID/PSW以及配网token，JSON格式为：
```
   {"cmdType":1,"ssid":"Home-WiFi","password":"abcd1234","token":"6ab82618a9d529a2ee777bf6e528a0fd"} 
```
   发送完了之后等待设备UDP回复设备信息及配网协议版本号：
```   
   {"cmdType":2,"productId":"OSPB5ASRWT","deviceName":"dev_01","protoVersion":"2.0"}
```   
6. 如果2秒之内没有收到设备回复，则重复步骤5，UDP客户端重复发送目标WiFi路由器的SSID/PSW及配网token。
   如果重复发送5次都没有收到回复，则认为配网失败，WiFi设备有异常。
    
7. 如果步骤5收到设备回复，则说明设备端已经收到WiFi路由器的SSID/PSW及token，正在连接WiFi路由器，并上报token。这个时候小程序会提示手机也去连接WiFi路由器，并通过token轮询物联网后台来确认配网及设备绑定是否成功。小程序相关操作可以参考 [查询配网Token状态](https://cloud.tencent.com/document/product/1081/44045)

8. 设备端在成功连接WiFi路由器之后，需要通过MQTT连接物联网后台，并将小程序发送来的配网token通过下面MQTT报文上报给后台服务：
```
    topic: $thing/up/service/ProductID/DeviceName
    payload: {"method":"app_bind_token","clientToken":"client-1234","params": {"token":"6ab82618a9d529a2ee777bf6e528a0fd"}}
```
    设备端也可以通过订阅主题 $thing/down/service/ProductID/DeviceName 来获取token上报的结果
    
9. 在以上5-7步骤中，如果小程序收到设备UDP服务发送过来的错误日志，且deviceReply字段的值为"Current_Error"，则表示当前配网绑定过程中出错，需要退出配网操作。如果deviceReply字段是"Previous_Error"，则为上一次配网的出错日志，只需要上报，不影响当此操作。
错误日志JSON格式例子：
```
{"cmdType":2,"deviceReply":"Current_Error","log":"ESP WIFI connect error! (10, 2)"} 
```
10. 如果设备成功上报了token，物联网后台服务确认了token有效性，小程序会提示配网完成，设备添加成功。

11. 设备端会记录配网的详细日志，如果配网或者添加设备失败，还可以让设备端创建一个特殊的softAP和UDP服务，通过小程序可以从设备端获取更多日志用于错误分析

## ESP8266使用softAP配网接口
### 腾讯云IoT AT指令ESP8266定制固件
如果ESP8266烧写了腾讯云IoT AT指令ESP8266定制固件，则只要通过指令AT+TCDEVINFOSET配置好设备信息，再通过下面的指令启动softAP配网就可以
```
AT+TCSAP="ESP8266-SAP","12345678"
```
关于AT指令的详细说明，请参考qcloud-iot-at-esp8266目录文档

### 配网代码示例
在qcloud-iot-esp8266-demo/main/wifi_config目录下，提供了softAP配网v2.0在ESP8266上面的参考实现，用户可以使用qcloud-iot-esp8266-demo工程进行体验。

#### 使用示例
配网接口说明请查看wifi_config/qcloud_wifi_config.h，可以按照下面方式使用：

```
    /* to use WiFi config and device binding with Wechat mini program */
    int wifi_config_state;
    int ret = start_softAP("ESP8266-SAP", "12345678", 0);
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

#### 代码设计说明
配网代码将核心逻辑与平台相关底层操作分离，便于移植到不同的硬件设备上
- qcloud_wifi_config.c：配网相关接口实现，包括UDP服务及MQTT连接及token上报，主要依赖腾讯云物联网C-SDK及FreeRTOS/lwIP运行环境
- wifi_config_esp.c：设备硬件WiFi操作相关接口实现，依赖于ESP8266 RTOS，当使用其他硬件平台时，需要进行移植适配
- wifi_config_error_handle.c：设备错误日志处理，主要依赖于FreeRTOS
- wifi_config_log_handle.c：设备配网日志收集和上报，主要依赖于FreeRTOS
