# 文档说明
本文档介绍如何将腾讯云物联 **IoT C-SDK** 移植到乐鑫 **ESP8266 RTOS** 平台，并提供可运行的demo。
同时介绍了在代码级别如何使用WiFi配网API，可结合腾讯连连小程序进行softAP方式WiFi配网及设备绑定。

本项目适用于**ESP8266 Launcher**开发板，如果使用其他板子，需要修改main/board_ops.h里面的GPIO配置。

示例中使用了两个GPIO，一个控制WiFi连接及配网的蓝色LED状态灯，一个在IoT Explorer demo中用于控制light sample的灯开关（绿色LED）。
如果编译选择了配网模式和light sample默认选项），则程序正常启动之后，会先进入配网模式（蓝色LED灯处于闪烁状态），可使用腾讯连连小程序进行配网，成功之后可以用小程序控制开发板上面绿色LED的亮灭。

### 1. 获取 ESP8266_RTOS_SDK 以及编译器
本项目基于**Linux(ubuntu)**环境进行开发，关于ESP8266开发的基础知识，请参考其 [开发指南](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/get-started/linux-setup.html)

在当前目录下获取ESP8266 RTOS SDK 3.1
```
git clone --single-branch -b release/v3.1 https://github.com/espressif/ESP8266_RTOS_SDK.git
```

编译toolchain请参考ESP8266_RTOS_SDK/README.md，推荐使用
* [Linux(64) GCC 5.2.0](https://dl.espressif.com/dl/xtensa-lx106-elf-linux64-1.22.0-92-g8facf4c-5.2.0.tar.gz)

在Linux安装toolchain之后，需要将toolchain的bin目录添加到PATH环境变量中

ESP8266_RTOS_SDK编译需要python及pip，并且需要安装以下python库及软件
```
sudo apt-get install git wget flex bison gperf python python-pip python-setuptools cmake ninja-build ccache libffi-dev libssl-dev
pip install pyserial
pip install xlrd
```

### 2.腾讯云物联 C-SDK 代码
项目默认已经包含了一个腾讯云IoT Explorer C-SDK v3.1.2的代码。**如需要更新可参考文档底部说明**

### 3.工程目录结构
在下载了ESP8266 RTOS SDK之后，应该具有以下目录结构（部分文件没有展示出来）
```
qcloud-iot-esp8266-demo/
├── components
│   └── qcloud_iot
│       ├── component.mk
│       └── qcloud_iot_c_sdk
│           ├── include
│           │   ├── config.h
│           │   ├── exports
│           ├── platform
│           │   ├── HAL_Device_freertos.c
│           │   ├── HAL_OS_freertos.c
│           │   ├── HAL_TCP_lwip.c
│           │   ├── HAL_Timer_freertos.c
│           │   ├── HAL_TLS_mbedtls.c
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
|   └── wifi_config
├── Makefile
├── partitions_qcloud_demo.csv
├── README.md
├── sdkconfig
└── sdkconfig.qcloud
```

### 4.修改设备三元组信息
到components/qcloud_iot/qcloud_iot_c_sdk/platform/HAL_Device_freertos.c里面修改在腾讯云物联网平台注册的设备信息（目前仅支持密钥设备）：

```
/* Product Id */
static char sg_product_id[MAX_SIZE_OF_PRODUCT_ID + 1]    = "PRODUCT_ID";
/* Device Name */
static char sg_device_name[MAX_SIZE_OF_DEVICE_NAME + 1]  = "YOUR_DEV_NAME";
/* Device Secret */
static char sg_device_secret[MAX_SIZE_OF_DEVICE_SECRET + 1] = "YOUR_IOT_PSK";
```

### 5.编译及烧写
执行make menuconfig可进行功能配置，顶层菜单里面有对本示例的配置（QCloud IoT demo Configuration）
```
   [*] To demo IoT Explorer (y) or IoT Hub (n)                      
         Select explorer demo example (Smart light example)  --->  
   [*] To use WiFi config or not                                  
   (YOUR_WIFI) WiFi SSID                                          
   (12345678) WiFi PASSWORD                                       
   [*] To enable OTA support on ESP or not   
```

第一项可选择演示IoT Explorer示例（勾选）或者IoT Hub示例（不勾选），勾选IoT Explorer示例，则可通过示例选择子菜单进一步选择需要demo的示例，支持的示例有智能灯、网关、二进制、MQTT

```
		   Select explorer demo example 
  Use the arrow keys to navigate this window or press the      
  hotkey of the item you wish to select followed by the <SPACE 
  BAR>. Press <?> for additional information about this        
 ┌───────────────────────────────────────────────────────────┐ 
 │                  (X) Smart light example                  │ 
 │                  ( ) Gateway example                      │ 
 │                  ( ) Raw data example                     │ 
 │                  ( ) Mqtt example                         │ 
 │                                                           │ 
 └───────────────────────────────────────────────────────────┘ 
```

第二项可选择是先进入WiFi配网模式（勾选）或者直接连接目标WiFi路由器（不勾选），配网模式需要与腾讯连连小程序进行配合
如果选择直接连接WiFi目标路由器，则后面两项可以用于配置要连接的WiFi路由器热点信息

再执行make就可以在build目录下面生成镜像。

烧写镜像可以在Linux下面执行make flash命令，或者使用乐鑫在Windows下面的FLASH_DOWNLOAD_TOOLS工具

![](https://main.qcloudimg.com/raw/1aa4edc684b35208ff78fc0266c5f1c2.png)

烧写成功之后可以重启开发板运行程序

### 6. WiFi配网说明
工程里面包含了WiFi配网及设备绑定的代码，关于softAP配网协议及接口使用请看 [WiFi设备softAP配网](https://github.com/tencentyun/qcloud-iot-esp-wifi/blob/master/docs/WiFi%E8%AE%BE%E5%A4%87softAP%E9%85%8D%E7%BD%91v2.0.md)，关于SmartConfig配网协议及接口使用请看 [WiFi设备SmartConfig配网](https://github.com/tencentyun/qcloud-iot-esp-wifi/blob/master/docs/WiFi%E8%AE%BE%E5%A4%87SmartConfig%E9%85%8D%E7%BD%91.md)。对于支持乐鑫ESP-TOUCH协议的设备，建议优先选择SmartConfig配网。

>注意：配网参考代码的函数get_reg_dev_info()里面包含了动态注册部分，简单演示了进入动态注册的条件，用户可根据自己情况调整。

### 7. ESP8266固件OTA
工程里面包含了对ESP8266进行完整固件OTA的参考代码，编译时首先需要打开
```
CONFIG_PARTITION_TABLE_TWO_OTA=y
CONFIG_QCLOUD_OTA_ESP_ENABLED=y
```
在代码里面，只需要在应用的sample比如light_data_template_sample.c里面调用enable_ota_task接口函数就会启动OTA后台任务。

首先使用flash工具更新具备OTA功能的固件（分区信息需要使用partitions_two_ota.bin），后续就可以将更新编译出来的esp8266-qcloud-iot.bin上传到腾讯云物联网平台进行固件升级的操作。
如果想恢复到初始状态，除了更新bootloader.bin/esp8266-qcloud-iot.bin/partitions_two_ota.bin，还需要将ota_data_initial.bin烧写到flash的0xD000位置


### 8. 更新腾讯云物联 C-SDK
如果有需要更新SDK，可根据使用的平台按下面步骤下载更新：
>注意：本项目使用的C-SDK在原始代码基础中进行了部分优化，替换之前需要仔细对比相同版本C-SDK并将修改集成进新代码

##### 从GitHub下载C-SDK代码
```
# 腾讯云物联网开发平台 IoT Explorer
git clone https://github.com/tencentyun/qcloud-iot-explorer-sdk-embedded-c.git

# 腾讯云物联网通信 IoT Hub
git clone https://github.com/tencentyun/qcloud-iot-sdk-embedded-c.git
```
##### 配置CMake并执行代码抽取
在C-SDK根目录的 CMakeLists.txt 中配置为freertos平台，并开启代码抽取功能。其他配置选项可以根据需要修改：
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
即可在output/qcloud_iot_c_sdk中找到相关代码文件。

##### 拷贝替换项目文件
将output/qcloud_iot_c_sdk 文件夹拷贝替换本项目目录的components/qcloud_iot/qcloud_iot_c_sdk 文件夹
qcloud_iot_c_sdk 目录介绍如下：
include目录为SDK供用户使用的API及可变参数，其中config.h为根据编译选项生成的编译宏。API具体介绍请参考C-SDK文档**C-SDK_API及可变参数说明**。
platform目录为平台相关的代码，可根据设备的具体情况进行修改适配。具体的函数说明请参考C-SDK文档**C-SDK_Porting跨平台移植概述**
sdk_src为SDK的核心逻辑及协议相关代码，一般不需要修改，其中internal_inc为SDK内部使用的头文件。


