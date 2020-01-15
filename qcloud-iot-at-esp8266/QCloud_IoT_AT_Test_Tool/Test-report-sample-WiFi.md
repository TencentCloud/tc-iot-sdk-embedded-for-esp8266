## 腾讯云IoT AT指令模组测试
--------------------------------------------
###1.设备信息(PSK方式)设置/查询指令测试
```
AT+TCDEVINFOSET=1,"S3EUVBRJLB","test_device","**************"
OK
+TCDEVINFOSET:OK
AT+TCDEVINFOSET?
+TCDEVINFOSET:1,"S3EUVBRJLB","test_device",110
OK
```
- ####设备信息(PSK方式)设置/查询指令测试: 通过
--------------------------------------------
--------------------------------------------
###2.MQTT连接/查询状态/断开指令测试
```
AT+TCMQTTCONN=1,10000,240,1,1
OK
+TCMQTTCONN:OK
AT+TCMQTTSTATE?
+TCMQTTSTATE:1
OK
AT+TCMQTTDISCONN
+TCMQTTDISCON,2
OK
AT+TCMQTTSTATE?
+TCMQTTSTATE:0
OK
AT+TCMQTTCONN=1,10000,60,1,1
OK
+TCMQTTCONN:OK
AT+TCMQTTSTATE?
+TCMQTTSTATE:1
OK
```
- ####MQTT连接/查询状态/断开指令测试: 通过
--------------------------------------------
--------------------------------------------
###3.MQTT订阅/取消订阅指令测试
```
AT+TCMQTTSUB="$sys/operation/result/S3EUVBRJLB/test_device",0
OK
+TCMQTTSUB:OK
AT+TCMQTTSUB="$shadow/operation/result/S3EUVBRJLB/test_device",0
OK
+TCMQTTSUB:OK
AT+TCMQTTSUB="S3EUVBRJLB/test_device/control",0
OK
+TCMQTTSUB:OK
AT+TCMQTTSUB?
+TCMQTTSUB:"$sys/operation/result/S3EUVBRJLB/test_device",0
+TCMQTTSUB:"$shadow/operation/result/S3EUVBRJLB/test_device",0
+TCMQTTSUB:"S3EUVBRJLB/test_device/control",0
OK
AT+TCMQTTUNSUB="$shadow/operation/result/S3EUVBRJLB/test_device"
OK
+TCMQTTUNSUB:OK
AT+TCMQTTUNSUB="S3EUVBRJLB/test_device/control"
OK
+TCMQTTUNSUB:OK
AT+TCMQTTSUB?
+TCMQTTSUB:"$sys/operation/result/S3EUVBRJLB/test_device",0
OK
```
- ####MQTT订阅/取消订阅指令测试: 通过
--------------------------------------------
--------------------------------------------
###4.MQTT发布/接收消息指令测试
```
AT+TCMQTTSUB="S3EUVBRJLB/test_device/data",0
OK
+TCMQTTSUB:OK
AT+TCMQTTPUB="S3EUVBRJLB/test_device/data",0,"{\"action\":\"Hello_From_ESP8266\"}"
OK
+TCMQTTPUB:OK
+TCMQTTRCVPUB:"S3EUVBRJLB/test_device/data",31,"{"action":"Hello_From_ESP8266"}"
AT+TCMQTTPUB="$sys/operation/S3EUVBRJLB/test_device",0,"{\"type\": \"get\"\, \"resource\": [\"time\"]}"
OK
+TCMQTTPUB:OK
+TCMQTTRCVPUB:"$sys/operation/result/S3EUVBRJLB/test_device",32,"{"type":"get","time":1579074973}"
AT+TCMQTTPUB="S3EUVBRJLB/test_device/data",1,"{\"action\":\"test\"\,
    \"time\":1579074975\,
    \"text\":\"V7PQBE\"}"
OK
+TCMQTTPUB:OK
+TCMQTTRCVPUB:"S3EUVBRJLB/test_device/data",51,"{"action":"test","text":"V7PQBE","time":1579074975}"
AT+TCMQTTPUBL="S3EUVBRJLB/test_device/data",1,367
OK
>
{\"action\":\"test\"\,
    \"time\":1579074975\,
    \"text\":\"74G6EQZBP969S1YWJU6S4FBJ6803TOLK2RTXSS3X3TA6R9Y96Z8J47NW2Y0T3QGS0ZYA8F0BDLV6URCUDGOFJVN9N7XKNQQRKWG06OIV6O6XLTNFWM14K8HAYB1CLQH7HQMGTNA5VIF4FMHJN40OKHIF6TWUA92TXW27XUNSNPMB9OCAAEA5238LK9417KEIARJU3FXXBUWNTP74VIIYCFAU8J4PUH0QB3I886D9F1EMF36X7NL28ZFNLQMG4HQPSLE99QU5U0SBZJY3PWFTLOG69PQU2UA9YHI7ZN0SJOB6\"}
+TCMQTTRCVPUB:"S3EUVBRJLB/test_device/data",345,"{"action":"test","text":"74G6EQZBP969S1YWJU6S4FBJ6803TOLK2RTXSS3X3TA6R9Y96Z8J47NW2Y0T3QGS0ZYA8F0BDLV6URCUDGOFJVN9N7XKNQQRKWG06OIV6O6XLTNFWM14K8HAYB1CLQH7HQMGTNA5VIF4FMHJN40OKHIF6TWUA92TXW27XUNSNPMB9OCAAEA5238LK9417KEIARJU3FXXBUWNTP74VIIYCFAU8J4PUH0QB3I886D9F1EMF36X7NL28ZFNLQMG4HQPSLE99QU5U0SBZJY3PWFTLOG69PQU2UA9YHI7ZN0SJOB6","time":1579074975}"
+TCMQTTPUBL:OK
```
- ####MQTT发布/接收消息指令测试: 通过
--------------------------------------------
--------------------------------------------
###5.MQTT订阅/发布权限测试
```
AT+TCMQTTSUB="S3EUVBRJLB/test_device/event",0
OK
+TCMQTTSUB:FAIL,-108
AT+TCMQTTSUB?
+TCMQTTSUB:"$sys/operation/result/S3EUVBRJLB/test_device",0
+TCMQTTSUB:"S3EUVBRJLB/test_device/data",0
OK
AT+TCMQTTPUB="S3EUVBRJLB/test_device/control",0,"hello"
OK
+TCMQTTPUB:OK
AT+TCMQTTPUB="S3EUVBRJLB/test_device/control",1,"hello"
OK
+TCMQTTPUB:FAIL,202
```
- ####MQTT订阅/发布权限测试: 通过
--------------------------------------------
--------------------------------------------
###6.MQTT重连测试
```
+TCMQTTDISCON,-103
+TCMQTTRECONNECTING
+TCMQTTRECONNECTED
AT+TCMQTTSTATE?
+TCMQTTSTATE:1
OK
AT+TCMQTTSUB?
+TCMQTTSUB:"$sys/operation/result/S3EUVBRJLB/test_device",0
+TCMQTTSUB:"S3EUVBRJLB/test_device/data",0
OK
AT+TCMQTTPUB="S3EUVBRJLB/test_device/data",0,"{\"action\":\"Hello_From_ESP8266\"}"
OK
+TCMQTTPUB:OK
+TCMQTTRCVPUB:"S3EUVBRJLB/test_device/data",31,"{"action":"Hello_From_ESP8266"}"
```
- ####MQTT重连测试: 通过
--------------------------------------------
--------------------------------------------
###7.OTA升级及固件读取指令测试
```
AT+TCOTASET=?
+TCOTASET:1(ENABLE)/0(DISABLE),"FW_version"
OK
AT+TCMQTTSTATE?
+TCMQTTSTATE:1
OK
AT+TCMQTTDISCONN
+TCMQTTDISCON,2
OK
AT+TCDEVINFOSET=1,"S3EUVBRJLB","test_device","**************"
OK
+TCDEVINFOSET:OK
AT+TCMQTTCONN=1,10000,240,1,1
OK
+TCMQTTCONN:OK
AT+TCOTASET=1,"1.0.0"
OK
+TCOTASET:OK
+TCOTASTATUS:ENTERUPDATE
+TCOTASTATUS:UPDATESUCCESS
AT+TCOTASET=0,"1.0.0"
OK
+TCOTASET:OK
AT+TCMQTTDISCONN
+TCMQTTDISCON,2
OK
AT+TCFWINFO?
OK
+TCFWINFO:"1.3.0",175436,"ad4615b866c13afb8b293a679bfa5dc4",716800
```
- ####OTA升级及固件读取指令测试: 通过
--------------------------------------------
--------------------------------------------
###8.产品信息设置/动态注册指令测试
```
AT+TCPRDINFOSET=?
+TCPRDINFOSET:"TLS_MODE(1)","PRODUCT_ID","PRODUCT_SECRET_BCC","DEVICE_NAME"
OK
AT+TCDEVREG=?
OK
AT+TCMQTTSTATE?
+TCMQTTSTATE:0
OK
AT+TCPRDINFOSET=1,"S3EUVBRJLB","**************","test-dev-reg1"
OK
+TCPRDINFOSET:OK
AT+TCDEVREG
OK
+TCDEVREG:OK
AT+TCDEVINFOSET?
+TCDEVINFOSET:1,"S3EUVBRJLB","test-dev-reg1",3
OK
AT+TCMQTTCONN=1,10000,240,1,1
OK
+TCMQTTCONN:OK
```
- ####产品信息设置/动态注册指令测试: 通过
--------------------------------------------
--------------------------------------------
###9.证书设备指令测试
```
AT+TCCERTADD=?
ERR CODE:0x01090000
ERROR
```
- ####证书设备指令测试: 指令不存在
--------------------------------------------
--------------------------------------------
###10.网络注册指令测试
```
AT+TCREGNET=?
ERR CODE:0x01090000
ERROR
```
- ####网络注册指令测试: 指令不存在
--------------------------------------------
--------------------------------------------
###11.模组信息读取指令测试
```
AT+TCMODULE
Module HW name: ESP-WROOM-02D
Module FW version: QCloud_AT_ESP_WiFi_v1.2.0
Module Mac addr: 3c:71:bf:33:b0:0a
Module FW compiled time: Jan  2 2020 15:37:39
Module Flash size: 2MB
OK
```
- ####模组信息读取指令测试: 通过
--------------------------------------------
--------------------------------------------
--------------------------------------------
## 腾讯云IoT AT指令模组测试
#### 测试时间： 2020-01-15 15:57:58
#### 模组名称： ESP8266
- #####模组硬件信息:  ESP-WROOM-02D
- #####模组固件版本:  QCloud_AT_ESP_WiFi_v1.2.0

--------------------------------------------
#### MQTT AT指令测试: 通过
- ####设备信息(PSK方式)设置/查询指令测试: 通过
- ####MQTT连接/查询状态/断开指令测试: 通过
- ####MQTT订阅/取消订阅指令测试: 通过
- ####MQTT发布/接收消息指令测试: 通过
- ####MQTT订阅/发布权限测试: 通过
- ####MQTT重连测试: 通过
--------------------------------------------
#### 其他AT指令测试:
- ####OTA升级及固件读取指令测试: 通过
- ####产品信息设置/动态注册指令测试: 通过
- ####证书设备指令测试: 指令不存在
- ####网络注册指令测试: 指令不存在
