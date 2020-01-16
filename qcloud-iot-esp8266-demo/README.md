# 文档说明
本文档介绍如何将腾讯云物联 **IoT C-SDK** 移植到乐鑫 **ESP8266 RTOS** 平台，并提供可运行的demo。
同时介绍了在代码级别如何使用WiFi配网API，可结合腾讯连连小程序进行softAP方式WiFi配网及设备绑定。

本项目适用于**ESP8266 Launcher**开发板，如果使用其他板子，需要修改main/board_ops.h里面的GPIO配置。

示例中使用了两个GPIO，一个控制WiFi连接及配网的蓝色LED状态灯，一个在IoT Explorer demo中用于控制light sample的灯开关（绿色LED）。

### 1. 获取 ESP8266_RTOS_SDK 以及编译器
在当前目录下获取ESP8266 RTOS SDK 3.1
```
git clone --single-branch -b release/v3.1 https://github.com/espressif/ESP8266_RTOS_SDK.git
```

编译toolchain请参考ESP8266_RTOS_SDK/README.md，推荐使用
* [Linux(64) GCC 5.2.0](https://dl.espressif.com/dl/xtensa-lx106-elf-linux64-1.22.0-92-g8facf4c-5.2.0.tar.gz)

在Linux安装toolchain之后，需要将toolchain的bin目录添加到PATH环境变量中

### 2.从腾讯云物联 C-SDK 中抽取相关代码
根据使用的平台下载腾讯云物联 C-SDK
```
# 腾讯云物联网开发平台 IoT Explorer
git clone https://github.com/tencentyun/qcloud-iot-explorer-sdk-embedded-c.git

# 腾讯云物联网通信 IoT Hub
git clone https://github.com/tencentyun/qcloud-iot-sdk-embedded-c.git
```

在根目录的 CMakeLists.txt 中配置为freertos平台，并开启代码抽取功能。其他配置选项可以根据需要修改：
```
set(BUILD_TYPE                  "release")
set(PLATFORM 	                "freertos")
set(EXTRACT_SRC ON)
set(FEATURE_AT_TCP_ENABLED OFF)
```
Linux环境运行以下命令
```
mkdir build
cd build
cmake ..
```
即可在output/qcloud_iot_c_sdk中找到相关代码文件，目录介绍如下：
include目录为SDK供用户使用的API及可变参数，其中config.h为根据编译选项生成的编译宏。API具体介绍请参考C-SDK文档**C-SDK_API及可变参数说明**。
platform目录为平台相关的代码，可根据设备的具体情况进行修改适配。具体的函数说明请参考C-SDK文档**C-SDK_Porting跨平台移植概述**
sdk_src为SDK的核心逻辑及协议相关代码，一般不需要修改，其中internal_inc为SDK内部使用的头文件。

将output/qcloud_iot_c_sdk 文件夹拷贝到当前目录的components/qcloud_iot/目录下
默认包含了一个基于IoT Explorer C-SDK v3.1.0的代码

### 3.工程目录结构
在下载了ESP8266 RTOS SDK以及拷贝qcloud_iot_c_sdk之后，应该具有以下目录结构（部分文件没有展示出来）
```
QCloud_IoT_ESP8266_RTOS/
├── components
│   └── qcloud_iot
│       ├── component.mk
│       └── qcloud_iot_c_sdk
│           ├── include
│           │   ├── config.h
│           │   ├── exports
│           ├── platform
│           │   ├── HAL_Device_freertos.c
│           │   ├── HAL_DTLS_mbedtls.c
│           │   ├── HAL_OS_freertos.c
│           │   ├── HAL_TCP_lwip.c
│           │   ├── HAL_Timer_freertos.c
│           │   ├── HAL_TLS_mbedtls.c
│           │   └── HAL_UDP_lwip.c
│           └── sdk_src
│               └── internal_inc
├── ESP8266_RTOS_SDK
│   ├── components
│   ├── docs
│   ├── examples
│   ├── make
│   └── tools
├── main
│   ├── component.mk
│   └── main.c
├── Makefile
├── partitions_qcloud_demo.csv
├── README.md
├── sdkconfig
└── sdkconfig.qcloud
```

### 4.修改WiFi热点信息和腾讯云设备信息
到components/qcloud_iot/qcloud_iot_c_sdk/platform/HAL_Device_freertos.c里面修改在腾讯云物联网平台注册的设备信息：

```
/* Product Id */
#define QCLOUD_IOT_PRODUCT_ID          CONFIG_QCLOUD_IOT_PRODUCT_ID
/* Device Name */
#define QCLOUD_IOT_DEVICE_NAME         CONFIG_QCLOUD_IOT_DEVICE_NAME
/* Device Secret */
#define QCLOUD_IOT_DEVICE_SECRET       CONFIG_QCLOUD_IOT_DEVICE_SECRET
```

### 5.编译及烧写
执行make menuconfig可进行功能配置，顶层菜单里面有对本示例的配置（QCloud IoT demo Configuration）
```
    [*] To demo IoT Explorer (y) or IoT Hub (n)   
    [*] To use WiFi boarding (softAP) or not                 
    (YOUR_WIFI) WiFi SSID                         
    (12345678) WiFi PASSWORD                      
```
第一项可选择演示IoT Explorer示例或者IoT Hub示例
第二项可选择在找不到目标WiFi路由器的时候是否进入softAP配网模式，需要与腾讯连连小程序配合
后面两项可以用于配置以STA模式（即不进行配网环节）要连接的WiFi热点信息

再执行make就可以在build目录下面生成镜像。

烧写镜像可以在Linux下面执行make flash命令，或者使用乐鑫在Windows下面的FLASH_DOWNLOAD_TOOLS工具

![](https://main.qcloudimg.com/raw/c9afd85e46ca0acb00f67f6d64e07f5c.png)

### 6. WiFi配网说明
工程里面包含了WiFi配网及设备绑定的代码，关于softAP配网协议请看docs目录文档《物联网WiFi设备softAP方式配网》
##### qcloud_wifi_boarding.c 说明
- 代码包含了softAP+UDP配网（面向微信小程序）和smartconfig+TCP配网（面向app）两种方式及接口，可分别调用
- 公用部分包括socket服务与app/小程序按照协议进行通信完成配网，设备绑定及错误信息上报的操作
- 平台函数依赖于ESP8266 RTOS，MQTT函数依赖于腾讯云物联网C-SDK
- 目前微信小程序仅支持softAP+UDP配网方式，故smartconfig+TCP部分可以忽略。

##### 使用示例
```
    int ret = start_softAP("ESP8266-SAP", "12345678", 0);
    if (ret) {
        Log_e("softAP start failed: %d", ret);
    } else {
        int wait_cnt = 150;
        do {
            Log_d("waiting for boarding result...");
            HAL_SleepMs(2000);
            wifi_connected = is_wifi_boarding_succesful();
        } while (!wifi_connected && wait_cnt--);
    }

```
