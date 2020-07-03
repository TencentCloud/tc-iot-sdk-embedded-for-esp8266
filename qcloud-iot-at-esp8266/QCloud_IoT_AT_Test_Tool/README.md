## 腾讯云IoT AT指令模组测试工具使用说明
腾讯云IoT AT指令是一套针对使用通讯模组（2G/4G/NB/WIFI）接入腾讯云物联平台的定制AT指令集，如果通讯模组实现了该指令集，则设备接入和通讯更为简单，所需代码量更少。关于该指令集详细内容请参考文档《腾讯云
IoT MQTT AT指令》
目前腾讯云和主流的模组厂商进行了深度合作，已支持腾讯云定制AT指令的模组列表如下：

| 序号  | 模组商     |   模组型号       |  通信制式      |   固件版本      |
| -------| ------------| -------------------|------------------|----------------|
| 1      | 中移        |   M5311          |      NB-IoT    |  M5311_LV_MOD_BOM_R002_1901232019_0906   |
| 2      | 中移        |   M6315         |          2G    |   CMIOT_M6315_20180901_V10_EXT_20190827_152209     |
| 3     | 中移         |   M8321          |         4G     |   QCloud_AT_v3.0.1_4G_Cellular 20190909_171245     |
| 4     | 中移         |   ML302          |      4G cat1     |   QCloud_AT_v3.0.1_4G_Cellular     |
| 5      | 有方               | N10              |     2G         |     N10_I_1187_PQS63010_TC_V002C   |
| 6      | 有方               | N21                |    NB-IoT      |     N21_RDD0CM_TC_V006A   |
| 7      | 有方               | N720            |    4G         |    N720_EAB0CMF_BZ_V003A_T1    |
| 8      | 移柯               | L206            |    2G         | L206Dv01.04b04.04 |
| 9      | 乐鑫               | ESP8266            |    WIFI         |   QCloud_AT_ESP8266_v2.0.0     |

本工具是一个PC端的测试工具，可以对定制模组进行功能验证和稳定性测试。

### 1. 使用环境：
本工具基于python3和pyserial/paho-mqtt模块，支持Windows/Linux/Mac环境，在安装python3和pip之后，执行下面命令安装模块
```
pip install pyserial
pip install paho-mqtt
```

### 2. 工具配置文件
工具配置文件默认为：Tool_Config.ini
配置文件里面包括了测试使用的设备信息，产品信息，每个模组的配置信息如转义字符处理，AT指令长度限制等等
可以在配置文件里面填写多个设备及多个模组的配置信息，并在工具启动时通过命令行参数指定

### 3. 创建产品和设备
腾讯云IoT AT指令模组支持接入IoT Explorer和IoT Hub平台，使用不同平台时，需要先到对应平台创建产品和设备。

#### IoT Explorer 产品及数据模板
如果需要对IoT Explorer平台设备进行测试，需要参考 [物联网开发平台](https://cloud.tencent.com/document/product/1081/34744) 创建一个密钥设备，并到配置文件里面`[IE-DEV1]`部分配置下列字段：
```
Product_ID / Device_Name / Device_Key
```
产品需要配置数据模板，本工具使用的数据模板基于智能城市-公共事业-路灯照明，请参考`test_data_template_light.json`，可将其导入到创建的产品中

#### IoT Hub 产品及Topic权限
如果需要对IoT Hub平台设备进行测试，需要参考 [物联网通信](https://cloud.tencent.com/document/product/634/38258) 到控制台创建一个密钥设备，并到工具配置文件里面`[HUB-DEV1]`部分配置下列字段：
```
Product_ID / Device_Name / Device_Key
```
IoT Hub平台设备测试还需要到控制台权限列表创建一个具备发布和订阅权限的“data” topic
并确保“control” topic只有订阅权限，“event” topic只有发布权限

| MQTT Topic       | 操作权限                                  |
| ---------------- | --------------------------------------- |
| control          | 订阅权限 |
| event            | 发布权限 |
| data             | 订阅和发布权限 |

测试工具基于data topic进行数据的自发自收测试，数据为JSON格式
```
{"action":"test","time":1234567890,"text":"abc"}
```
测试之前需要到物联网通信平台的规则引擎按照下图创建转发规则
![](https://main.qcloudimg.com/raw/de36394fa07c07512cc147dfab9f41aa.png)

#### 证书设备和动态注册测试
对于支持证书设备指令的模组测试，还需要创建证书设备，并到工具配置文件里面`[HUB-CERT1]`部分进行配置
对于支持动态注册指令的模组测试，还需要将设备的产品密钥及新设备名字配置到文件里面`[HUB-PRD1]`部分

### 4. 测试模式说明
#### CLI: 原始AT命令行模式
以原始命令行交互方式进行手动测试，类似于一个串口测试工具

#### MQTT: MQTT指令验证测试
对腾讯云IoT MQTT相关包括设备信息的AT命令进行自动化测试，并输出测试报告
测试内容包括设备信息设置和读取，MQTT连接/状态查询/断开连接，MQTT订阅/取消订阅/订阅查询，MQTT发布和接收（包括JSON格式数据，多行数据），MQTT发布订阅权限（QoS0/1）测试，断线自动重连测试。
该项测试为模组必须通过的测试，可以验证模组对腾讯云IoT MQTT相关AT指令的实现在功能上和规范上是否合格，如果有测试项失败，需要检查AT指令的格式以及返回值是否符合《腾讯云IoT AT指令集》文档规定，也可以与文件“Test-report-sample.md”进行参照对比

#### IOT: 腾讯云IoT AT指令验证测试
在MQTT测试模式基础上，增加了对其他腾讯云IoT AT命令包括（OTA，动态注册，证书设备，入网指令等）的自动化测试，并输出测试报告
如果模组实现了相关的功能指令，则对应的测试项也需要通过

#### HUB: IoT Hub测试
对在IoT Hub创建的设备进行测试，在循环模式下，会以QoS1循环收发指定数量的报文，并统计成功率和测试时间，可以依此进行稳定性测试

#### IE: IoT Explorer测试
对在IoT Explorer创建的设备进行测试，在循环模式下，可以循环进行发送接收property和event消息的测试，并统计成功率和测试时间，可以依此进行稳定性测试

#### WIFI: WIFI配网测试
对WiFi模组进行softAP配网及绑定测试。须结合腾讯连连小程序进行。
可以在配置文件`[WIFI]`部分配置所要创建的softAP热点名称和密码
```
; WiFi模组创建softAP热点信息
SAP_SSID = ESP8266-SoftAP
SAP_PSWD = 12345678
```
配网成功之后，会进入IoT Explorer测试模式

#### OTA: 固件下载及读取测试
对IoT Hub设备进行OTA升级测试，并将成功获取到的固件保存到本地文件

#### CERT: 证书设备测试
对IoT Hub创建的证书设备进行证书添加校验删除等测试，并连接MQTT服务进行收发包测试

### 5. AT模组配置:
配置文件里面包括了部分已验证模组的配置信息，如新的模组在转义字符处理及AT指令长度限制等等与不一致，则需要在配置文件中增加模组配置
对于转义字符处理，建议的处理是对payload部分的双引号进行转义，如按以下AT指令方式进行MQTT消息发布
AT+TCMQTTPUB="S3EUVBRJLB/device1/data",0,"{\"action\":\"test\",\"time\":1234567890}"
模组须确保云端收到的payload为{"action":"test","time":1234567890}

工具目前支持三种转义字符处理方式

| 转义字符方式                   | 含义                                          |
| --------------------------- | --------------------------------------------- |
| add_no_escapes              | 对源数据不加任何转义处理 |
| add_escapes_for_quote       | 对源数据里面的双引号“前面添加转义字符\ |
| add_escapes_for_quote_comma | 对源数据里面的双引号“和逗号,前面添加转义字符\ |

如果模组由于实现限制，在PUB消息时不支持多行的payload（以'\n'分隔），可以在配置文件中将support_multiline_payload设为no（默认为yes）
如果模组由于实现限制，在进行OTA时需要先连接MQTT，可以在配置文件中将conn_mqtt_before_ota设为yes（默认为no）

### 6. 使用示例：
工具帮助信息：
  python QCloud_IoT_AT_Test_Tool.py -h

对连接到串口COM5的N720模组进行IoT AT全指令集自动化测试：
  python QCloud_IoT_AT_Test_Tool.py -p COM5 -a N720 -m IOT

对连接到串口COM5的M5311模组进行MQTT AT指令集自动化测试：
  python QCloud_IoT_AT_Test_Tool.py -p COM5 -a M5311 -m MQTT  

对连接到串口/dev/ttyUSB0的L206模组进行IoT Hub平台100次MQTT收发消息循环测试：
  python QCloud_IoT_AT_Test_Tool.py -p /dev/ttyUSB0 -a L206 -m HUB -n 100

对连接到串口COM5的ESP8266模组进行WiFi配网及IoT Explorer设备绑定测试：
  python QCloud_IoT_AT_Test_Tool.py -p COM5 -a ESP8266 -m WIFI

### 7. 注意事项
1. 本工具可以覆盖大部分指令的自动化测试，但是对于部分指令功能，仍需要通过CLI模式进行手动测试，比如AT命令参数的及返回数据的格式校验等，以及设备信息设置成功之后，需保存在FLASH中，在断电后仍然可以读取到，该项操作需要手动进行测试
2. 对本工具有使用疑问，可以直接阅读修改python源代码，或者联系Spike Lin(spikelin@tencent.com)