## 腾讯云IoT AT指令ESP8266模组固件使用说明

腾讯云IoT AT指令是一套针对使用通讯模组（2G/4G/NB/WIFI）接入腾讯云物联平台的定制AT指令集，如果通讯模组实现了该指令集，则设备接入和通讯更为简单，所需代码量更少。
ESP8266作为IoT领域使用最广泛的一款WiFi芯片/模组，我们在其通用AT指令基础上，增加了腾讯云IoT AT指令集，形成一个定制的模组固件QCloud_IoT_AT_ESP8266，本文档介绍如何烧写及使用该固件。

关于腾讯云IoT定制AT指令ESP8266版本的详细说明，包括MQTT/OTA/WiFi配网，请参考文档《腾讯云IoT-AT指令集-WiFi-ESP8266》。关于ESP8266通用AT指令，请参考 [ESP-AT](https://github.com/espressif/esp-at)

### 固件说明
腾讯云IoT定制的AT模组固件QCloud_IoT_AT_ESP8266，适用于所有FLASH大小为2MB或者2MB以上的ESP8266模组。

AT串口使用UART0，默认的Tx为GPIO1，Rx为GPIO3。但因为ESP8266的UART0 默认会在上电启动期间输出一些打印，如果打印信息影响设备功能，可在上电期间将 U0TXD(GPIO1)、U0RXD(GPIO3) 分别与 U0RTS (GPIO15)，U0CTS(GPIO13)进行交换，以屏蔽打印。因此我们提供了两个版本的固件：

名称包含**UART_1_3**的固件串口使用的Tx为GPIO1，Rx为GPIO3。

名称包含**UART_15_13**的固件串口使用的Tx为GPIO15，Rx为GPIO13。

用户可根据需要到**QCloud_IoT_AT_ESP8266_FW**目录下载。

### 烧写说明
我们提供了整合的单一固件，只需将固件bin文件烧写到ESP8266模组地址0就可以。

在Windows下面建议使用乐鑫官方下载工具ESPFlashDownloadTool：

![](https://main.qcloudimg.com/raw/4a0950201609be4c0119e75d5ddfce97.png)

>对于有需要单独烧写各个分区bin文件的，可以从压缩包**bin_files**文件夹取相关分区文件并按照对应的flash地址烧写

### GUI体验工具
在目录**QCloud_IoT_AT_GUI_Dev_Tool**，提供了Windows环境的GUI调试工具IoTDevTool，用于体验和测试ESP8266模组定制AT固件，通过简单几步操作就可以完成连接腾讯云物联网服务并进行消息通信，更可以用腾讯连连小程序完成WiFi配网和添加设备操作，直接使用小程序与设备模组互动，具体请看该目录的README说明

![](https://main.qcloudimg.com/raw/7f8f02ac03d638bff9a5e6c832f2f11d.png)

### Python测试工具
在目录**QCloud_IoT_AT_Test_Tool**，提供了python测试工具，对腾讯云IoT AT指令ESP8266定制模组进行完整的功能验证及稳定性测试，比如MQTT通讯以及WiFi配网（和腾讯连连小程序配合）等，具体请查看QCloud_IoT_AT_Test_Tool目录的README说明

### 模组配置
ESP8266模组固件和模组信息存储于不同FLASH分区，模组固件在启动时候会读取模组信息并做相应配置，这样同一版本模组固件可以适配不同的模组硬件。比如FLASH大小以及WiFi状态灯控制的GPIO等等，具体可以参考AT指令`AT+TCMODINFOSET `的说明

### 断线重连
1. 目前版本固件由于乐鑫基础组件限制，在WiFi网络出现断线事件后无法自动重连，比如AP热点关闭或者信号过弱，导致ESP8266模组断开WiFi网络，并上报URC消息 **WIFI DISCONNECT**，这时候需要由MCU通过`AT+CWJAP`指令进行主动重连，或者通过`AT+RST`命令重启模组，重启后如果WiFi网络可用，模组会自动进行重连。
2. 在MQTT层面发生断线之后，模组会自动进行重连，但是重连有超时时间，当模组上报URC消息 **TCMQTTDISCON,-106** 时候，表明MQTT重连超时，这个时候需要由MCU通过`AT+TCMQTTCONN`指令重新发起连接。