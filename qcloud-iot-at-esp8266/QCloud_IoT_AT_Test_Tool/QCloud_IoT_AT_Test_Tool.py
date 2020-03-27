#!/bin/env python
# -*- coding: utf-8 -*-

'''
Author: Spike Lin(spikelin@tencent.com)
Date： 2019-05-27 (v1.0)
Date： 2019-07-26 (v1.1)
Date： 2019-09-03 (v1.2)
Date： 2020-01-15 (v1.3)
'''

######################## README ###################################
# 腾讯云IoT AT指令模组python测试工具
# 请先阅读使用说明： README.md
# 默认配置文件：Tool_Config.ini
# 使用环境：基于python3和pyserial/paho-mqtt模块
######################## README ###################################

Tool_Version = 'v1.3'

import threading
import serial
import sys
import argparse
import time
import queue
import json
import hmac
import hashlib
import logging
import base64
import random
import string
import paho.mqtt.client as mqtt
import configparser

logging.basicConfig(stream=sys.stdout, level=logging.INFO,  format='%(asctime)s [%(levelname)s] %(message)s')

config_file = configparser.ConfigParser()
Default_config_file_path = "Tool_Config.ini"

############################## CUSTOM PARAMETERS BEGIN ################################

# IoT Hub 测试 ################################################
# IoT Hub 测试可根据需要修改以下设备信息及测试topic和msg内容
# 以下设备信息在配置文件中进行设置
Hub_Product_ID = 'YOUR_PRODUCT_ID'
Hub_Device_Name = 'YOUR_DEVICE_NAME'
Hub_Device_Key = 'YOUR_DEVICE_KEY'
Hub_REG_Product_ID = 'YOUR_PRODUCT_ID'
Hub_REG_Product_Key = "YOUR_PRODUCT_KEY"
Hub_REG_Device_Name = 'YOUR_NEW_DEVICE_NAME'


# JSON format: {"action":"test","time":1234567890,"text":"abcdef"}

def get_hub_test_msg():
    random_text = ''.join(random.choice(string.ascii_uppercase + string.digits) for _ in range(6))
    HubTestMsg = '''{"action":"%s",
    "time":%d,
    "text":"%s"}''' % ('test', int(time.time()), random_text)
    print(">>> publish test msg", HubTestMsg)
    return HubTestMsg


def get_hub_test_long_msg():
    random_long_text = ''.join(random.choice(string.ascii_uppercase + string.digits) for _ in range(300))
    HubTestMsg = '''{"action":"%s",
    "time":%d,
    "text":"%s"}''' % ('test', int(time.time()), random_long_text)
    # print(">>> publish test msg", HubTestMsg)
    return HubTestMsg


def compare_json_string(str_a, str_b):
    try:
        dict1 = json.loads(str_a)
        dict2 = json.loads(str_b)
    except TypeError:
        print("Data type error")

    for src_list, dst_list in zip(sorted(dict1), sorted(dict2)):
        if str(dict1[src_list]) != str(dict2[dst_list]):
            print("NOT consistent: ", src_list, dict1[src_list], dst_list, dict2[dst_list])
            return False

    return True


# IoT Explorer 测试 ############################################
# IoT Explorer 测试可根据需要修改以下设备信息及测试msg内容
# 以下设备信息在配置文件中进行设置
IE_Product_ID = 'YOUR_PRODUCT_ID'
IE_Device_Name = 'YOUR_DEVICE_NAME'
IE_Device_Key = 'YOUR_DEVICE_KEY'

def gen_property_update_msg():
    brightness = random.randint(1, 100)
    color = random.randint(0, 2)
    power_switch = random.randint(0, 1)
    position = random.randint(1, 10000)
    timestamp = int(time.time())
    print(">>> update data >>> brightness:", brightness, "color:", color, "power_switch:", power_switch)
    IEUpdateMsg = '''{
        "method": "report",
        "params": {
                "brightness": %d,
                "color": %d,
                "power_switch":%d,                
                "name":"test-light-position-%d"                            
        },
        "timestamp":%d,
        "clientToken": "%d" }''' \
                  % (brightness, color, power_switch, position, timestamp, timestamp % 1000)
    return IEUpdateMsg.replace('\n', '')


def gen_event_post_msg():
    voltage = random.uniform(1.0, 3.0)
    print(">>> post event >>> voltage:", voltage)
    IEEventMsg = '''{
        "method": "event_post",
        "version": "1.0",
        "eventId": "low_voltage",
        "params": {
            "voltage": %f
        },
        "clientToken": "clientToken-%d"}''' % (voltage, int(time.time()*10)%1000)

    return IEEventMsg.replace('\n', '')

# OTA 测试版本参数 #######################################################
OTA_local_version = '1.0.0'

############################## CUSTOM PARAMETERS END ################################

############################## AT MODULE CODE ################################
# AT模组 配置选项 #####################################################
# 可在配置文件中添加不同模组的参数，并通过程序启动参数进行选择

g_at_module_default_params = {
        'name': 'DEFAULT',
        'add_escapes': 'add_escapes_for_quote',         # 添加转义字符处理方法
        'err_list': ['ERROR', 'FAIL'],                  # AT指令出错提示
        'cmd_timeout': 10,                              # AT指令执行超时限制，单位：秒
        'at_cmd_max_len': 512,                          # 单条AT指令最大长度，单位：字节
        'at_cmd_publ_max_payload': 2048,                # PUBL发送长消息最大长度，单位：字节
}

# MQTT PUB消息payload部分转义字符处理函数，根据模组情况选择
# 双引号和逗号需要转义
def add_escapes_for_quote_comma(raw_str):
    return raw_str.replace('\"', '\\\"').replace(',', '\\,')

# 仅双引号需要转义
def add_escapes_for_quote(raw_str):
    return raw_str.replace('\"', '\\\"')

# 不需要转义
def add_no_escapes(raw_str):
    return raw_str

g_at_escapes_method = {
    'add_escapes_for_quote_comma': add_escapes_for_quote_comma,
    'add_escapes_for_quote': add_escapes_for_quote,
    'add_no_escapes': add_no_escapes
}

# 模组串口配置，可以根据模组具体情况修改，使用的串口名字和波特率可以在程序启动参数指定
g_serial_port = 'COM8'
g_serial_baudrate = 115200
g_serial_bytesize = serial.EIGHTBITS
g_serial_parity = serial.PARITY_NONE
g_serial_stopbits = serial.STOPBITS_ONE
g_serial_timeout_s = 3


################# QCloud python AT cmd framework BEGIN #########################

# 调试选项
g_debug_print = False


class Singleton(type):
    _instances = {}

    def __call__(cls, *args, **kwargs):
        if cls not in cls._instances:
            cls._instances[cls] = super(Singleton, cls).__call__(*args, **kwargs)
        return cls._instances[cls]


class SerialATClient(threading.Thread, metaclass=Singleton):
    def __init__(self, raw_mode=False):
        # thread init
        super(SerialATClient, self).__init__()

        try:
            self.serial_port = serial.Serial(g_serial_port, g_serial_baudrate, g_serial_bytesize,
                                             g_serial_parity, g_serial_stopbits, g_serial_timeout_s, rtscts=False)
        except serial.SerialException:
            print("!!! Serial port", g_serial_port, "open error!!")
            sys.exit(-1)

        self.connected = True
        self.timeout = g_serial_timeout_s
        self.raw_mode = raw_mode
        self.port = g_serial_port
        self.data_handlers = dict()
        self.cmd_queue = queue.Queue()
        self.current_cmd = ""
        self.hex_data_handler = self.default_hexdata_handler
        self.do_at_record = False
        print("Connected to serial port:", self.port)

    def start_record(self):
        self.do_at_record = True
        self.at_record_queue = queue.Queue()

    def at_record(self, at_str):
        if self.do_at_record:
            self.at_record_queue.put(at_str+'\n', block=False)

    def stop_record(self):
        self.do_at_record = False
        del self.at_record_queue

    def output_record(self, file_out):
        if not self.do_at_record or self.at_record_queue.empty():
            return

        if file_out is None:
            while True:
                try:
                    record = self.at_record_queue.get(block=False)
                    continue
                except queue.Empty:
                    break
            return

        file_out.write("```\n")
        while True:
            try:
                record = self.at_record_queue.get(block=False)

                # replace the secret info
                try:
                    if 'AT+TCDEVINFOSET=' in record:
                        cmd_seg = record.split(',')
                        if cmd_seg[0].strip() != 'AT+TCDEVINFOSET=2':
                            record = cmd_seg[0]+','+cmd_seg[1]+','+cmd_seg[2]+',"**************"\n'
                    elif 'AT+TCPRDINFOSET=' in record:
                        cmd_seg = record.split(',')
                        record = cmd_seg[0]+','+cmd_seg[1]+',"**************",'+cmd_seg[3]
                except IndexError:
                    pass

                file_out.write(record)
                continue
            except queue.Empty:
                break
        file_out.write("```\n")

    # start the thread to read data from serial port
    def start_read(self):
        if not self.connected:
            try:
                self.serial_port.open()
            except serial.SerialException:
                print("!!! Serial port", self.port, "open error!!")
                sys.exit(-1)

        self.connected = True
        if not self.is_alive():
            try:
                self.start()
            except RuntimeError:
                print("!!! Start thread failed!!")
                sys.exit(-1)

    def close_port(self):
        if not self.connected:
            return

        self.connected = False
        try:
            self.serial_port.flush()
            self.serial_port.close()
        except serial.SerialException:
            print("Serial port", self.port, "close error!")

        try:
            self.join(self.timeout)
        except RuntimeError:
            print("Join thread failed")

    def add_data_handler(self, key_str, handler_func):
        if not key_str or len(key_str.strip().encode('utf8')) == 0:
            return
        self.data_handlers[key_str] = handler_func

    def remove_data_handler(self, key_str):
        if not key_str or len(key_str.strip().encode('utf8')) == 0:
            return
        if key_str in self.data_handlers:
            del self.data_handlers[key_str]

    def run(self):
        while self.connected:
            try:
                data_raw = self.serial_port.read_until(bytes([13, 10]))  # \r\n
            except serial.SerialException:
                print("Serial port", self.port, "read error!")
                time.sleep(1)
                continue
            else:
                self.handle_recv_data(data_raw)

    def default_hexdata_handler(self, data_raw):
        data_str = "HEXDATA: "
        for i in range(len(data_raw)):
            # print(hex(data_raw[i])[2:])
            data_str += hex(data_raw[i])
            data_str += ' '
        print(data_str)

    def handle_recv_data(self, data_raw):
        if not data_raw:
            return

        try:
            hdr_fixed = data_raw[0:14].decode()
            if hdr_fixed == '+TCREADFWDATA:':
                self.hex_data_handler(data_raw)
                return
        except (TypeError, UnicodeDecodeError):
            pass

        try:
            data_str = data_raw.decode().strip('\r\n')
        except (TypeError, UnicodeDecodeError):
            self.hex_data_handler(data_raw)
            return

        # serial recv data handling
        if len(data_str) == 0:
            return

        if self.raw_mode:
            print(data_str)
            return

        self.at_record(data_str)

        if g_debug_print:
            print(self.port, "RECV:", data_str)

        for i in self.data_handlers:
            if i in data_str:
                self.data_handlers[i](data_str)
                return

        if len(self.current_cmd):
            self.cmd_queue.put(data_str)
            return

        print("Unhandled data:", data_str)

    def send_cmd(self, data):
        if not self.connected and not data:
            return False

        try:
            self.serial_port.write((data+"\r\n").encode('utf8'))
            self.at_record(data)
            return True
        except serial.SerialException:
            print("Serial port", self.port, "write error!")
        except TypeError:
            print("Data type error:", data)

        return False

    def send_cmd_wait_reply(self, cmd_str, ok_str, err_list, timeout=2):
        if not self.connected and not cmd_str:
            return False, ""

        if g_debug_print:
            print("Send CMD:", cmd_str)

        self.current_cmd = cmd_str
        ret = self.send_cmd(cmd_str)
        if not ret:
            self.current_cmd = ''
            return ret, "send cmd error"

        ret = False
        ret_str = ""

        while True:
            try:
                ans = self.cmd_queue.get(True, timeout)
                if ans.replace(' ','') == ok_str:
                    ret = True
                    break

                for i in err_list:
                    if i in ans:
                        ret_str += ans + '\n'
                        if g_debug_print:
                            print("error", i, "happen in", ans)

                        self.current_cmd = ''
                        return ret, ret_str

                if ans != cmd_str:
                    ret_str += ans + "\n"
                continue
            except queue.Empty:
                print("Cmd Queue Timeout")
                break
            except TypeError:
                print("Type error")
                break
            except KeyboardInterrupt:
                print("User interrupt")
                break

        self.current_cmd = ''
        return ret, ret_str

    def do_one_at_cmd(self, cmd_str, ok_str, hint, err_list, timeout):
        ret, reply = self.send_cmd_wait_reply(cmd_str, ok_str, err_list, timeout)
        if ret:
            print(hint, ">> OK")
            return True
        else:
            print(hint, ">> Failed")
            print(reply)
            return False

    def echo_off(self):
        cmd = "ATE0"
        ok_reply = 'OK'
        hint = "Echo off"

        return self.do_one_at_cmd(cmd, ok_reply, hint, ['ERROR', 'FAIL'], timeout=2)

    def wait_at_ready(self):
        cmd = 'AT'
        ok_reply = 'OK'
        hint = "Wait for AT ready"

        for i in range(3):
            if self.do_one_at_cmd(cmd, ok_reply, hint, ['ERROR', 'FAIL'], timeout=5):
                break


class IoTBaseATCmd:
    def __init__(self, at_module='ESP8266'):

        self.serial = SerialATClient()
        # register URC handlers
        self.serial.add_data_handler("+TCMQTTRCVPUB", self.mqtt_msg_handler)
        self.serial.add_data_handler("+TCMQTTDISCON", self.mqtt_state_handler)
        self.serial.add_data_handler("+TCMQTTRECONNECTING", self.mqtt_state_handler)
        self.serial.add_data_handler("+TCMQTTRECONNECTED", self.mqtt_state_handler)
        #self.serial.add_data_handler("+TCREGNET", self.network_state_handler)
        self.topic_handlers = dict()
        self.ota_state = 'off'

        try:
            at_module_conf = config_file['MOD-' + at_module]
            self.at_module = at_module_conf['name'].upper()
            self.cmd_timeout = int(at_module_conf['cmd_timeout'])
            self.at_cmd_max_len = int(at_module_conf['at_cmd_max_len'])
            self.at_cmd_publ_max_payload = int(at_module_conf['at_cmd_publ_max_payload'])
            self.err_list = at_module_conf['err_list'].split(',')
            self.escapes_method = at_module_conf['add_escapes']
        except:
            print("Exceptions when parsing config file. Using default config")
            self.at_module = at_module
            self.cmd_timeout = g_at_module_default_params['cmd_timeout']
            self.at_cmd_max_len = g_at_module_default_params['at_cmd_max_len']
            self.at_cmd_publ_max_payload = g_at_module_default_params['at_cmd_publ_max_payload']
            self.err_list = g_at_module_default_params['err_list']
            self.escapes_method = g_at_module_default_params['add_escapes']

        try:
            self.support_multiline_payload = config_file.get('MOD-' + at_module, 'support_multiline_payload').lower()
        except:
            #print("Exceptions when get support_multiline_payload from config file. Using default: yes")
            self.support_multiline_payload = 'yes'

        try:
            self.conn_mqtt_before_ota = config_file.get('MOD-' + at_module, 'conn_mqtt_before_ota').lower()
        except:
            #print("Exceptions when get conn_mqtt_before_ota from config file. Using default: no")
            self.conn_mqtt_before_ota = 'no'

        print("module:", self.at_module, self.cmd_timeout, self.at_cmd_max_len, self.err_list, self.escapes_method)
        self.mqtt_state = 'INIT'
        self.mqtt_urc_recv = {'+TCMQTTDISCON': False,
                              '+TCMQTTRECONNECTING': False,
                              '+TCMQTTRECONNECTED': False}

    def add_escapes(self, raw_str):
        if self.support_multiline_payload == 'yes':
            return g_at_escapes_method[self.escapes_method](raw_str)
        else:
            return g_at_escapes_method[self.escapes_method](raw_str).replace('\n', '')

    def get_sysram(self):
        if self.at_module != 'ESP8266':
            return

        # AT+SYSRAM?
        cmd = 'AT+SYSRAM?'
        ok_reply = 'OK'

        rc, rep = self.serial.send_cmd_wait_reply(cmd, ok_reply, ['ERROR', 'FAIL'], timeout=2)
        if rc:
            try:
                sysram = int(rep.split(':', 1)[1].strip())
                print("System remain RAM:", sysram)
                return sysram
            except ValueError:
                logging.error("Invalid sysram value: " + rep)
                return 0
        else:
            print("WiFi get sysram failed")
            print(rep)
            return 0

    def mqtt_msg_handler(self, urc_msg):
        try:
            msg = urc_msg.split(':', 1)[1].split(',', 2)
            topic = msg[0].replace(' ', '').lstrip('"').rstrip('"')
            payload = msg[2].lstrip('"').rstrip('"')
        except ValueError:
            print("Invalid MQTT msg:", urc_msg)
            return

        try:
            self.topic_handlers[topic](topic, payload)
            return
        except KeyError:
            pass

        print('''>>>RECV MQTT msg topic: %s\n\t\tpayload: %s:''' % (topic, payload))
        return

    def default_topic_handler(self, topic, payload):
        print('''RECV MQTT msg topic: %s\n\tpayload(len: %d): %s:''' % (topic, len(payload), payload))

    def mqtt_state_handler(self, msg):
        logging.info("MQTT status: " + msg)
        if '+TCMQTTDISCON' in msg:
            self.mqtt_urc_recv['+TCMQTTDISCON'] = True
            self.mqtt_state = 'DISCONNECTED'
        elif '+TCMQTTRECONNECTING' in msg:
            self.mqtt_urc_recv['+TCMQTTRECONNECTING'] = True
            self.mqtt_state = 'RECONNECTING'
        elif '+TCMQTTRECONNECTED' in msg:
            self.mqtt_urc_recv['+TCMQTTRECONNECTED'] = True
            self.mqtt_state = 'CONNECTED'

    def network_state_handler(self, msg):
        logging.info("Network status: " + msg)

    def devinfo_setup(self, pid, dev_name, dev_key, tls=1):
        self.product_id = pid
        self.device_name = dev_name
        self.device_key = dev_key
        self.mqtt_tls_mode = tls

        # AT+TCDEVINFOSET=TLS_mode,"PRODUCTID","DEVICE_NAME","DEVICE_KEY"
        cmd = '''AT+TCDEVINFOSET=%d,"%s","%s","%s"''' % (tls, pid, dev_name, dev_key)
        ok_reply = '+TCDEVINFOSET:OK'
        hint = '''AT+TCDEVINFOSET="%s","%s"''' % (pid, dev_name)

        return self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, self.cmd_timeout)

    def gen_key_bcc(self, key):
        key_bytes = bytes(key, encoding='utf8')
        result = key_bytes[0]
        for i in range(1, len(key_bytes)):
            result ^= key_bytes[i]

        return result

    def devinfo_query(self, product_id, device_name, key):
        # AT+TCDEVINFOSET?
        cmd = 'AT+TCDEVINFOSET?'
        ok_reply = 'OK'

        rc, rep = self.serial.send_cmd_wait_reply(cmd, ok_reply, self.err_list, self.cmd_timeout)
        if rc:
            try:
                devinfo = rep.split(':', 1)[1].split(',')
                if product_id == devinfo[1].strip('"') and device_name == devinfo[2].strip('"'):
                    if len(key) == 0:
                        return True

                    key_bcc = self.gen_key_bcc(key)
                    print("Device Key BCC in PC: {} From module: {}".format(key_bcc, devinfo[3]))
                    if int(devinfo[3].strip()) != key_bcc:
                        return False
                    return True
                else:
                    print("Devinfo inconsistent:", devinfo)
                    return False
            except ValueError:
                print("Invalid devinfo value: " + rep)
                return False
        else:
            print("Query devinfo failed")
            print(rep)
            return False

    def is_mqtt_connected(self):
        # AT+TCMQTTSTATE?
        cmd = 'AT+TCMQTTSTATE?'
        ok_reply = 'OK'

        rc, rep = self.serial.send_cmd_wait_reply(cmd, ok_reply, self.err_list, self.cmd_timeout)
        if rc:
            try:
                status = rep.split(':', 1)[1].strip()
                if status == '1':
                    return True
                else:
                    return False
            except ValueError:
                print("Invalid MQTT state value:", rep)
                return False
        else:
            print("MQTT get state failed")
            print(rep)
            return False

    def mqtt_connect(self, timeout_ms=10000, keepalive=240, clean_session=1, reconnect=1):
        # use the larger timeout value
        if (timeout_ms/1000) > self.cmd_timeout:
            self.cmd_timeout = timeout_ms/1000
        else:
            timeout_ms = self.cmd_timeout * 1000

        self.mqtt_cmd_timeout_ms = timeout_ms
        self.mqtt_keepalive = keepalive
        self.mqtt_clean_session = clean_session
        self.mqtt_reconnect = reconnect

        # AT+TCMQTTCONN=1,5000,240,1,1
        cmd = '''AT+TCMQTTCONN=%d,%d,%d,%d,%d''' % (self.mqtt_tls_mode, timeout_ms, keepalive, clean_session, reconnect)
        ok_reply = '+TCMQTTCONN:OK'
        hint = cmd

        ret = self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, 3*self.cmd_timeout)
        if ret:
            self.mqtt_state = 'CONNECTED'

        return ret

    def mqtt_disconnect(self):
        # AT+TCMQTTDISCONN
        cmd = 'AT+TCMQTTDISCONN'
        ok_reply = 'OK'
        hint = cmd

        ret = self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, self.cmd_timeout)
        if ret:
            self.mqtt_state = 'DISCONNECTED'

        return ret

    def publish_long_msg(self, topic, qos, msg, hint=''):
        # AT+TCMQTTPUBL="topic",QoS,msg_length
        msg = self.add_escapes(msg)
        cmd = '''AT+TCMQTTPUBL="%s",%d,%d''' % (topic, qos, len(msg))
        ok_reply = '>'
        hint = cmd

        if not self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, self.cmd_timeout):
            return False

        hint = "AT+TCMQTTPUBL Send msg"
        ok_reply = '+TCMQTTPUBL:OK'
        if not self.serial.do_one_at_cmd(msg, ok_reply, hint, self.err_list, 20*self.cmd_timeout):
            print('ERR: pub long msg failed: ', msg)
            return False
        else:
            return True

    def publish_msg(self, topic, qos, msg, hint=''):
        # AT+TCMQTTPUB="topic",QoS,"msg"
        cmd = '''AT+TCMQTTPUB="%s",%d,"%s"''' % (topic, qos, self.add_escapes(msg))
        if len(cmd) > self.at_cmd_max_len:
            return self.publish_long_msg(topic, qos, msg, hint)

        ok_reply = '+TCMQTTPUB:OK'
        hint = "AT+TCMQTTPUB="+topic

        if not self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, 2*self.cmd_timeout):
            print('>>cmd failed:', cmd)
            return False
        else:
            return True

    def subscribe_topic(self, topic, qos, topic_callback=print):
        # if topic in self.topic_handlers:
        #     # just update the dict value
        #     self.topic_handlers[topic] = topic_callback
        #     return

        # AT+TCMQTTSUB="topic",QoS
        cmd = '''AT+TCMQTTSUB="%s",%d''' % (topic, qos)
        ok_reply = '+TCMQTTSUB:OK'
        hint = cmd
        self.topic_handlers[topic] = topic_callback

        if not self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, 2*self.cmd_timeout):
            del self.topic_handlers[topic]
            return False
        return True

    def subscribe_topic_query(self):
        # AT+TCMQTTSUB?
        cmd = 'AT+TCMQTTSUB?'
        ok_reply = 'OK'

        rc, rep = self.serial.send_cmd_wait_reply(cmd, ok_reply, self.err_list, self.cmd_timeout)
        if rc:
            try:
                sub_list = rep.splitlines()
                if len(sub_list) != len(self.topic_handlers):
                    print("inconsistent sub list")
                    for i in self.topic_handlers:
                        print("In PC:", i)
                    for i in sub_list:
                        print("In module", i)

                    return False
                else:
                    for i in sub_list:
                        topic = i.split(':', 1)[1].split(',', 1)[0].replace(' ', '').strip('"')
                        if topic in self.topic_handlers:
                            print("Sub Topic consistent", topic)
                        else:
                            print("Sub Topic inconsistent:", topic)
                            return False
                    return True
            except ValueError:
                print("Invalid sub topics list value:", rep)
                return False
        else:
            print("Query TCMQTTSUB failed")
            print(rep)
            return False

    def unsubscribe_topic(self, topic):
        # AT+TCMQTTUNSUB="topic"
        cmd = '''AT+TCMQTTUNSUB="%s"''' % (topic)
        ok_reply = '+TCMQTTUNSUB:OK'
        hint = cmd

        if not self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, self.cmd_timeout):
            return False

        if topic in self.topic_handlers:
            del self.topic_handlers[topic]

        return True

    def unsubscribe_all_topics(self):
        ok_reply = '+TCMQTTUNSUB:OK'
        for i in self.topic_handlers:
            cmd = '''AT+TCMQTTUNSUB="%s"''' % (i)
            hint = cmd
            self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, self.cmd_timeout)

        self.topic_handlers.clear()

    def publish_normal_msg(self, topic_keyword, qos, msg):
        # "ProductID/device_name/keyword"
        topic = '''%s/%s/%s''' % (self.product_id, self.device_name, topic_keyword)

        return self.publish_msg(topic, qos, msg)

    def publish_hidden_msg(self, topic_keyword, qos, msg):
        # "$topic_keyword/operation/ProductID/device_name"
        topic = '''$%s/operation/%s/%s''' \
                % (topic_keyword, self.product_id, self.device_name)

        return self.publish_msg(topic, qos, msg)

    def subscribe_normal_topic(self, topic_keyword, qos, topic_callback=print):
        # "ProductID/device_name/topic_keyword"
        topic = '''%s/%s/%s''' % (self.product_id, self.device_name, topic_keyword)

        return self.subscribe_topic(topic, qos, topic_callback)

    def subscribe_hidden_topic(self, topic_keyword, qos, topic_callback=print):
        # "$topic_keyword/operation/result/ProductID/device_name"
        topic = '''$%s/operation/result/%s/%s''' \
                % (topic_keyword, self.product_id, self.device_name)

        return self.subscribe_topic(topic, qos, topic_callback)

    def unsubscribe_normal_topic(self, topic_keyword):
        # "ProductID/device_name/topic_keyword"
        topic = '''%s/%s/%s''' % (self.product_id, self.device_name, topic_keyword)

        return self.unsubscribe_topic(topic)

    def unsubscribe_hidden_topic(self, topic_keyword):
        # "$topic_keyword/operation/result/ProductID/device_name"
        topic = '''$%s/operation/result/%s/%s''' \
                % (topic_keyword, self.product_id, self.device_name)

        return self.unsubscribe_topic(topic)

    def ota_status_handler(self, msg):
        try:
            status = msg.split(':', 1)[1].replace(' ', '')
        except ValueError:
            print("\nInvalid ota status msg:", msg)
            return

        if status == 'ENTERUPDATE':
            print("\nOTA update start")
            self.ota_state = 'updating'
        elif status == 'UPDATESUCCESS':
            print("\nOTA update success")
            self.ota_state = 'success'
        elif 'UPDATEFAIL' in status:
            print("\nOTA update failed:", status)
            self.ota_state = 'failed'
        else:
            print("\nUnknown OTA update msg:", msg)
            self.ota_state = 'unknown'

        return

    def ota_cmd_test(self):
        cmd = 'AT+TCOTASET=?'
        ok_reply = 'OK'
        hint = 'Test AT+TCOTASET'

        return self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, self.cmd_timeout)

    def ota_update_setup(self, state, version):
        # AT+TCOTASET=state,"version""
        cmd = '''AT+TCOTASET=%d,"%s"''' % (state, version)
        ok_reply = '+TCOTASET:OK'
        hint = cmd

        self.serial.add_data_handler("+TCOTASTATUS", self.ota_status_handler)
        if not self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, 2*self.cmd_timeout):
            return False

        if state == 1:
            self.ota_state = 'updating'
        return True

    def ota_read_fw_info(self):
        # AT+TCFWINFO?
        cmd = 'AT+TCFWINFO?'
        ok_reply = '+TCFWINFO'
        err_list = ['ERROR', '+TCFWINFO']

        rc, rep = self.serial.send_cmd_wait_reply(cmd, ok_reply, err_list, self.cmd_timeout)

        try:
            fw_info = rep.split(':', 1)[1].split(',')
            fw_version = fw_info[0].replace(' ','').strip('"')
            fw_size = int(fw_info[1].strip('"'))
            fw_md5 = fw_info[2].strip('"')
            print('FW version:', fw_version, 'size:', fw_size, 'md5:', fw_md5)
            return fw_version, fw_size, fw_md5
        except (ValueError, IndexError):
            print("Invalid OTA FW info value:", rep.encode())
            return None, 0, None

    def ota_data_save_handler(self, data_raw):
        try:
            hdr_fixed = data_raw[0:14].decode()
        except (TypeError, UnicodeDecodeError):
            print("invalid raw OTA data")
            return

        if hdr_fixed != '+TCREADFWDATA:':
            print("invalid header:", hdr_fixed)
            return

        #print("HEX data len:", len(data_raw))
        self.ota_data_queue.put(data_raw, timeout=2)

    def ota_parse_fw_data(self, data_raw):
        # "+TCREADFWDATA:512,01020AF5......\r\n"
        i = 14
        while i < 22:
            if data_raw[i] == 0x2c:
                i += 1
                break
            i += 1

        if i == 22:
            print("invalid raw OTA data")
            return 0, 0

        # calc the binary data length
        raw_len = len(data_raw)
        if data_raw[raw_len-2] == 0x0d and data_raw[raw_len-1] == 0x0a:
            read_size = len(data_raw) - i - 2
        else:
            read_size = len(data_raw) - i

        # print("OTA data len:", read_size)
        return read_size, i

    def ota_read_fw_data(self, fw_version, fw_size, fw_md5):
        if fw_size <= 0:
            print("Invalid firmware size")
            return False

        file_name = 'OTA_FW_{}_{}.bin'.format(self.at_module, fw_version)
        try:
            fw_file_out = open(file_name, "wb")
        except IOError:
            print("Open file error:", file_name)
            return False

        self.ota_data_queue = queue.Queue()
        self.serial.hex_data_handler = self.ota_data_save_handler

        # AT+TCREADFWDATA=2048
        cmd = 'AT+TCREADFWDATA=2048'
        ok_reply = 'OK'
        err_list = ['ERROR', 'FAIL']

        read_size = 0
        while read_size < fw_size:
            rc, rep = self.serial.send_cmd_wait_reply(cmd, ok_reply, err_list, self.cmd_timeout)

            if not rc:
                print("Invalid OTA FW data reply:", rep)
                return False

            try:
                data_raw = self.ota_data_queue.get(timeout=10)
            except queue.Empty:
                print("OTA data Queue Timeout")
                break

            this_read_size, i = self.ota_parse_fw_data(data_raw)
            if this_read_size == 0:
                print("Invalid FW data size")
                return False

            read_size += this_read_size
            print("Total FW read:", read_size)
            fw_file_out.write(data_raw[i:i+this_read_size])

        self.serial.hex_data_handler = self.serial.default_hexdata_handler
        del self.ota_data_queue
        fw_file_out.close()

        with open(file_name, "rb") as file_read:
            contents = file_read.read()

        md5_calc = hashlib.md5(contents).hexdigest()
        if fw_md5 != md5_calc:
            print("FW corrupted!\n MD5 from module", fw_md5, "\n MD5 from file", md5_calc)
            return False
        else:
            print('''OTA FW data (size %d) has been written to file %s.\n And MD5 (%s) is correct'''
                  % (read_size, file_name, md5_calc))
        return True

    def module_info_exec(self):
        # AT+TCMODULE
        cmd = 'AT+TCMODULE'
        ok_reply = 'OK'
        err_ret_str = '- ####模组信息读取指令测试：指令不存在或返回信息有误'
        rc, rep = self.serial.send_cmd_wait_reply(cmd, ok_reply, self.err_list, self.cmd_timeout)
        if rc:
            try:
                modinfo_list = rep.splitlines()
                if len(modinfo_list) < 3:
                    return False, err_ret_str

                ret_str = ''
                if 'Module HW name' in modinfo_list[0]:
                    ret_str += "- #####模组硬件信息: {}\n".format(modinfo_list[0].split(':')[1])
                else:
                    return False, err_ret_str

                if 'Module FW version' in modinfo_list[1]:
                    ret_str += "- #####模组固件版本: {}\n".format(modinfo_list[1].split(':')[1])
                else:
                    return False, err_ret_str

                return True, ret_str

            except ValueError:
                return False, err_ret_str
        else:
            return False, err_ret_str

    def regnet_test(self):
        cmd = 'AT+TCREGNET=?'
        ok_reply = 'OK'
        hint = 'Test AT+TCREGNET'

        return self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, self.cmd_timeout)

    def regnet_query_state(self):
        # AT+TCREGNET? and expect: +TCREGNET: 0,1,xxx.xxx.xxx.xxx,xx when registed
        cmd = "AT+TCREGNET?"
        ok_reply = 'OK'

        rc, rep = self.serial.send_cmd_wait_reply(cmd, ok_reply, self.err_list, self.cmd_timeout)
        if rc:
            try:
                status = rep.split(',')[1].strip()
                if status == '0':
                    print("Network is NOT registed")
                    return False
                else:
                    print("Network is registed", status)
                    return True

            except ValueError:
                print("Invalid network state value:", rep)
            except IndexError:
                print("Invalid network state value:", rep)

        else:
            print(cmd, "error:", rep)

        return False

    def regnet_setup(self, state, apn="CMNET"):
        # AT+TCREGNET=0,1,"CMNET" and AT+TCREGNET=0,0
        if state == 1:
            cmd = "AT+TCREGNET=0,1," + apn
            ok_reply = '+TCREGNET:0,NET_OK'
        else:
            cmd = "AT+TCREGNET=0,0"
            ok_reply = '+TCREGNET:0,NET_CLOSE'

        err_list = self.err_list + ['NET_FAIL']
        hint = cmd

        return self.serial.do_one_at_cmd(cmd, ok_reply, hint, err_list, self.cmd_timeout)

    def prdinfo_test(self):
        cmd = 'AT+TCPRDINFOSET=?'
        ok_reply = 'OK'
        hint = cmd

        return self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, self.cmd_timeout)

    def prdinfo_setup(self, pid, prd_key, dev_name, tls=1):
        # AT+TCPRDINFOSET=TLS_mode,"PRODUCT_ID","PRODUCT_KEY","DEVICE_NAME"
        cmd = '''AT+TCPRDINFOSET=%d,"%s","%s","%s"''' % (tls, pid, prd_key, dev_name)
        ok_reply = '+TCPRDINFOSET:OK'
        hint = '''AT+TCPRDINFOSET="%s","%s"''' % (pid, dev_name)

        return self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, self.cmd_timeout)

    def devreg_test(self):
        cmd = 'AT+TCDEVREG=?'
        ok_reply = 'OK'
        hint = cmd

        return self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, self.cmd_timeout)

    def devreg_exec(self):
        cmd = 'AT+TCDEVREG'
        ok_reply = '+TCDEVREG:OK'
        hint = cmd

        return self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, 5*self.cmd_timeout)

    def certadd_test(self):
        cmd = 'AT+TCCERTADD=?'
        ok_reply = 'OK'
        hint = cmd

        return self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, self.cmd_timeout)

    def certadd_query(self):
        cmd = 'AT+TCCERTADD?'
        ok_reply = 'OK'
        hint = cmd

        return self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, self.cmd_timeout)

    def certadd_setup(self, cert_name, cert_content):
        # AT+TCCERTADD="cert_name",cert_length
        cmd = '''AT+TCCERTADD="%s",%d''' % (cert_name, len(cert_content))
        ok_reply = '>'
        hint = cmd

        if not self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, self.cmd_timeout):
            return False

        hint = "AT+TCCERTADD Send cert content"
        ok_reply = '+TCCERTADD:OK'
        if not self.serial.do_one_at_cmd(cert_content, ok_reply, hint, self.err_list, 20 * self.cmd_timeout):
            return False
        else:
            return True

    def certcheck_setup(self, cert_name):
        # AT+TCCERTCHECK="cert_name"
        cmd = '''AT+TCCERTCHECK="%s"''' % (cert_name)
        ok_reply = '+TCCERTCHECK:OK'
        hint = cmd

        return self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, self.cmd_timeout)

    def certdel_setup(self, cert_name):
        # AT+TCCERTDEL="cert_name"
        cmd = '''AT+TCCERTDEL="%s"''' % (cert_name)
        ok_reply = '+TCCERTDEL:OK'
        hint = cmd

        return self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, self.cmd_timeout)


class IoTHubATTest(IoTBaseATCmd):
    def __init__(self, at_module='ESP8266'):
        super(IoTHubATTest, self).__init__(at_module)
        self.sys_time = 0
        self.report_file = None
        self.hub_payload_queue = queue.Queue()

    def parse_sys_time(self, topic, payload):
        try:
            obj = json.loads(payload)
            self.sys_time = int(obj["time"])
        except KeyError:
            print("Invalid time JSON：", topic, payload)
            self.sys_time = -1
            return

        print("IoT Hub system time is", self.sys_time)

    def get_sys_time(self):
        msg = '{"type": "get", "resource": ["time"]}'
        self.sys_time = 0
        if not self.publish_hidden_msg('sys', 0, msg):
            return False

        for i in range(3):
            time.sleep(1)
            if self.sys_time != 0:
                break

        if self.sys_time <= 0:
            return False

        return True

    def payload_queue_handler(self, topic, payload):
        try:
            self.hub_payload_queue.put(payload)
        except AttributeError:
            print("Recv msg", payload, "after loop test finished!")

    def do_loop_test(self, loop_cnt):
        self.hub_payload_queue = queue.Queue()
        self.subscribe_normal_topic('data', 1, self.payload_queue_handler)

        send_cnt = 0
        fail_cnt = 0
        recv_cnt = 0
        recv_err = 0
        recv_timeout_err = 0
        start_time = time.time()
        start_ram = self.get_sysram()
        max_send_recv_time = 0.0
        while send_cnt < loop_cnt:
            logging.info("------IoT Hub MQTT QoS1 loop test cnt: %d" % send_cnt)
            send_time = time.time()
            loop_test_last_msg = get_hub_test_long_msg()

            ret = self.publish_normal_msg('data', 1, loop_test_last_msg)
            send_cnt += 1
            if not ret:
                fail_cnt += 1
                continue

            recv_timeout_cnt = 0
            while True:
                try:
                    recv_payload = self.hub_payload_queue.get(timeout=2 * self.cmd_timeout)
                    recv_timeout_cnt = 0
                    if recv_payload == "{}":
                        continue
                    print("RECV MQTT loop test msg:", recv_payload)
                    if compare_json_string(recv_payload, loop_test_last_msg):
                        recv_cnt += 1
                        send_recv_time = round(time.time() - send_time, 2) * 100
                        if send_recv_time > max_send_recv_time:
                            max_send_recv_time = send_recv_time
                    else:
                        print("*****Differ with last msg:", loop_test_last_msg)
                        recv_err += 1
                    break
                except queue.Empty:
                    print("*****RECV MQTT timeout!!", loop_test_last_msg)
                    recv_timeout_cnt += 1
                    if recv_timeout_cnt > 3:
                        recv_timeout_cnt = 0
                        recv_timeout_err += 1
                        break
                    continue
                except KeyboardInterrupt:
                    print("Test interrupted")
                    return

        end_time = time.time()
        end_ram = self.get_sysram()
        print("---------IoT Hub MQTT QoS1 loop test result:--------")
        print("Test AT module:", self.at_module)
        print("Test start time:", time.ctime(start_time), " End time:", time.ctime(end_time))
        print("Test duration:", round(end_time - start_time, 1), "seconds")
        print("MQTT Msg Send count:", send_cnt)
        print("MQTT Msg Send failed count:", fail_cnt)
        print("MQTT Msg Recv success count:", recv_cnt)
        print("MQTT Msg Recv error total count:", recv_err, "timeout:", recv_timeout_err)
        print('''MQTT Publish success rate %.2f%%''' % (round(((send_cnt - fail_cnt) / send_cnt) * 100, 2)))
        print('''MQTT Send/Recv success rate %.2f%%''' % (round((recv_cnt / send_cnt) * 100, 2)))
        print('''MQTT Msg Send/Recv Ave time: %.2f seconds Max time: %.2f seconds'''
              % (round((end_time - start_time) / send_cnt, 2), round(max_send_recv_time / 100, 2)))
        print("Test start RAM:", start_ram, " End RAM:", end_ram)
        print("---------IoT Hub MQTT QoS1 loop test end------------")

        del self.hub_payload_queue

    def iot_hub_loop_test(self, loop=True, set_loop_cnt=0):

        while True:
            if self.is_mqtt_connected():
                self.mqtt_disconnect()

            if not self.devinfo_setup(Hub_Product_ID, Hub_Device_Name, Hub_Device_Key):
                break

            if not self.mqtt_connect():
                break

            # loop test
            while loop:
                if set_loop_cnt == 0:
                    cmd = input("---------IoT Hub MQTT QoS1 loop test:--------\n"
                                "Input:\n"
                                "1. 'quit' to break\n"
                                "2. pub/sub loop count times [1..3000]\n"                            
                                "Your choice:\n").strip('\n')

                    if cmd.lower() == 'quit':
                        break

                    try:
                        loop_cnt = int(cmd)
                    except ValueError:
                        print("Invalid loop times:", cmd)
                        continue

                    if loop_cnt < 1 or loop_cnt > 3000:
                        loop_cnt = 10
                        print("loop times out of range, set to ", loop_cnt)

                    self.do_loop_test(loop_cnt)

                    # do it again
                    continue
                else:
                    self.do_loop_test(set_loop_cnt)
                    break
            break

        self.unsubscribe_all_topics()
        time.sleep(0.5)
        self.mqtt_disconnect()
        return

    def wait_for_pub_msg_return(self, sent_payload):
        recv_timeout_cnt = 0
        while True:
            try:
                recv_payload = self.hub_payload_queue.get(timeout=2 * self.cmd_timeout)
                recv_timeout_cnt = 0
                if recv_payload == "{}":
                    continue
                # compare two JSON string: {"action":"test","time":1234567890,"text":"abc"}
                if compare_json_string(sent_payload, recv_payload):
                    print("Correct return MQTT msg:", recv_payload)
                    return True
                else:
                    print("Recv msg <"+recv_payload+"> is differ with sent msg:", sent_payload)
                    return False
            except queue.Empty:
                recv_timeout_cnt += 1
                if recv_timeout_cnt > 3:
                    print("*****RECV MQTT timeout!!")
                    return False
                continue
            except KeyboardInterrupt:
                print("Test interrupted")
                return False

    def log_record(self, log_str):
        print(log_str)
        if self.report_file is None:
            return
        self.report_file.write(log_str+'\n')

    def set_report_file(self, file_out):
        self.report_file = file_out

    def test_case_devinfo_psk(self, test_case_cnt):
        test_item = "设备信息(PSK方式)设置/查询指令测试"
        self.log_record("###{}.{}".format(test_case_cnt, test_item))
        cmd_err_cnt = 0

        if not self.devinfo_setup(Hub_Product_ID, Hub_Device_Name, Hub_Device_Key):
            self.serial.output_record(self.report_file)
            self.log_record("##### 设备信息设置指令执行失败")
            cmd_err_cnt += 1

        if not self.devinfo_query(Hub_Product_ID, Hub_Device_Name, Hub_Device_Key):
            self.serial.output_record(self.report_file)
            self.log_record("##### 查询设备信息指令执行失败")
            cmd_err_cnt += 1

        self.serial.output_record(self.report_file)
        if cmd_err_cnt == 0:
            test_result = "- ####" + test_item + ": 通过"
        else:
            test_result = "- ####" + test_item + ": 失败"

        self.log_record(test_result)
        self.log_record("--------------------------------------------")
        return cmd_err_cnt, test_result

    def test_case_mqtt_connection(self, test_case_cnt):
        test_item = "MQTT连接/查询状态/断开指令测试"
        self.log_record("###{}.{}".format(test_case_cnt, test_item))
        cmd_err_cnt = 0

        if not self.mqtt_connect():
            self.serial.output_record(self.report_file)
            self.log_record("##### MQTT连接指令执行失败")
            cmd_err_cnt += 1

        if not self.is_mqtt_connected():
            self.serial.output_record(self.report_file)
            self.log_record("##### 查询MQTT连接状态指令执行失败")
            cmd_err_cnt += 1

        time.sleep(1)

        if not self.mqtt_disconnect():
            self.serial.output_record(self.report_file)
            self.log_record("##### MQTT断开连接指令执行失败")
            cmd_err_cnt += 1

        if self.is_mqtt_connected():
            self.serial.output_record(self.report_file)
            self.log_record("##### 查询MQTT断开状态指令执行失败")
            cmd_err_cnt += 1

        time.sleep(5)

        if not self.mqtt_connect(keepalive=60):
            self.serial.output_record(self.report_file)
            self.log_record("##### MQTT连接指令执行失败")
            cmd_err_cnt += 1

        if not self.is_mqtt_connected():
            self.serial.output_record(self.report_file)
            self.log_record("##### 查询MQTT连接状态指令执行失败")
            cmd_err_cnt += 1

        self.serial.output_record(self.report_file)
        if cmd_err_cnt == 0:
            test_result = "- ####" + test_item + ": 通过"
        else:
            test_result = "- ####" + test_item + ": 失败"

        self.log_record(test_result)
        self.log_record("--------------------------------------------")
        return cmd_err_cnt, test_result

    def test_case_mqtt_sub_unsub(self, test_case_cnt):
        test_item = "MQTT订阅/取消订阅指令测试"
        self.log_record("###{}.{}".format(test_case_cnt, test_item))
        cmd_err_cnt = 0

        if not self.subscribe_hidden_topic("sys", 0, self.parse_sys_time):
            self.serial.output_record(self.report_file)
            self.log_record("##### MQTT订阅指令执行失败")
            cmd_err_cnt += 1

        if not self.subscribe_hidden_topic("shadow", 0, self.parse_sys_time):
            self.serial.output_record(self.report_file)
            self.log_record("##### MQTT订阅指令执行失败")
            cmd_err_cnt += 1

        if not self.subscribe_normal_topic("control", 0):
            self.serial.output_record(self.report_file)
            self.log_record("##### MQTT订阅指令执行失败")
            cmd_err_cnt += 1

        if not self.subscribe_topic_query():
            self.serial.output_record(self.report_file)
            self.log_record("##### 查询MQTT已订阅主题指令执行失败或结果不正确")
            cmd_err_cnt += 1

        if not self.unsubscribe_hidden_topic("shadow"):
            self.serial.output_record(self.report_file)
            self.log_record("##### 取消MQTT订阅指令执行失败")
            cmd_err_cnt += 1

        if not self.unsubscribe_normal_topic("control"):
            self.serial.output_record(self.report_file)
            self.log_record("##### 取消MQTT订阅指令执行失败")
            cmd_err_cnt += 1

        if not self.subscribe_topic_query():
            self.serial.output_record(self.report_file)
            self.log_record("##### 查询MQTT已订阅主题指令执行失败或结果不正确")
            cmd_err_cnt += 1

        self.serial.output_record(self.report_file)
        if cmd_err_cnt == 0:
            test_result = "- ####" + test_item + ": 通过"
        else:
            test_result = "- ####" + test_item + ": 失败"

        self.log_record(test_result)
        self.log_record("--------------------------------------------")
        return cmd_err_cnt, test_result

    def test_case_mqtt_pub_recv(self, test_case_cnt):
        test_item = "MQTT发布/接收消息指令测试"
        self.log_record("###{}.{}".format(test_case_cnt, test_item))
        cmd_err_cnt = 0

        if not self.subscribe_normal_topic('data', 0, self.payload_queue_handler):
            self.serial.output_record(self.report_file)
            self.log_record("##### MQTT订阅指令执行失败")
            cmd_err_cnt += 1

        send_payload = '''{"action":"Hello_From_%s"}''' % self.at_module
        if not self.publish_normal_msg('data', 0, send_payload):
            self.serial.output_record(self.report_file)
            self.log_record("##### MQTT QoS0发布消息指令执行失败")
            cmd_err_cnt += 1
        elif not self.wait_for_pub_msg_return(send_payload):
            self.serial.output_record(self.report_file)
            self.log_record("##### MQTT 接收消息失败")
            cmd_err_cnt += 1

        if not self.get_sys_time():
            self.serial.output_record(self.report_file)
            self.log_record("##### MQTT获取Hub系统时间指令执行失败")
            cmd_err_cnt += 1

        send_payload = get_hub_test_msg()
        if not self.publish_normal_msg('data', 1, send_payload):
            self.serial.output_record(self.report_file)
            self.log_record("##### MQTT QoS1发布JSON消息指令执行失败")
            cmd_err_cnt += 1
        elif not self.wait_for_pub_msg_return(send_payload):
            self.serial.output_record(self.report_file)
            self.log_record("##### MQTT 接收JSON消息失败")
            cmd_err_cnt += 1

        HubTestTopic = '''%s/%s/data''' % (self.product_id, self.device_name)
        send_payload = get_hub_test_long_msg()
        if not self.publish_long_msg(HubTestTopic, 1, send_payload):
            self.serial.output_record(self.report_file)
            self.log_record("##### MQTT QoS1发布长消息指令执行失败")
            cmd_err_cnt += 1
        elif not self.wait_for_pub_msg_return(send_payload):
            self.serial.output_record(self.report_file)
            self.log_record("##### MQTT 接收长消息失败")
            cmd_err_cnt += 1

        self.serial.output_record(self.report_file)
        if cmd_err_cnt == 0:
            test_result = "- ####" + test_item + ": 通过"
        else:
            test_result = "- ####" + test_item + ": 失败"

        self.log_record(test_result)
        self.log_record("--------------------------------------------")
        return cmd_err_cnt, test_result

    def test_case_mqtt_sub_pub_auth(self, test_case_cnt):
        test_item = "MQTT订阅/发布权限测试"
        self.log_record("###{}.{}".format(test_case_cnt, test_item))
        cmd_err_cnt = 0

        # event主题没有订阅权限
        if self.subscribe_normal_topic("event", 0):
            self.serial.output_record(self.report_file)
            self.log_record("##### MQTT订阅无权限主题指令执行失败")
            cmd_err_cnt += 1

        if not self.subscribe_topic_query():
            self.serial.output_record(self.report_file)
            self.log_record("##### 查询MQTT已订阅主题指令执行失败或结果不正确")
            cmd_err_cnt += 1

        # control主题没有发布权限，不过QoS0消息不会超时
        if not self.publish_normal_msg("control", 0, "hello"):
            self.serial.output_record(self.report_file)
            self.log_record("##### MQTT QoS0发布无权限主题消息指令执行失败")
            cmd_err_cnt += 1

        # control主题没有发布权限，QoS1消息会超时
        if self.publish_normal_msg("control", 1, "hello"):
            self.serial.output_record(self.report_file)
            self.log_record("##### MQTT QoS1发布无权限主题消息指令执行失败")
            cmd_err_cnt += 1

        self.serial.output_record(self.report_file)
        if cmd_err_cnt == 0:
            test_result = "- ####" + test_item + ": 通过"
        else:
            test_result = "- ####" + test_item + ": 失败"

        self.log_record(test_result)
        self.log_record("--------------------------------------------")
        return cmd_err_cnt, test_result

    def test_case_mqtt_reconnect(self, test_case_cnt):
        test_item = "MQTT重连测试"
        self.log_record("###{}.{}".format(test_case_cnt, test_item))
        cmd_err_cnt = 0

        # 这里使用同名设备通过PAHO-MQTT客户端接入后台，会导致AT模组的设备连接被踢掉，模拟设备断线重连
        if not IoTMQTTClient().do_one_connect(Hub_Product_ID, Hub_Device_Name, Hub_Device_Key):
            test_result = "- ####" + test_item + ": 模拟设备断线失败"
            cmd_err_cnt += 1
        else:
            self.mqtt_state = 'DISCONNECTED'

            # 等待状态改变
            time.sleep(self.cmd_timeout)
            while not self.is_mqtt_connected() and self.mqtt_state != 'CONNECTED':
                time.sleep(self.cmd_timeout)

            if not self.subscribe_topic_query():
                self.serial.output_record(self.report_file)
                self.log_record("##### 重连后查询MQTT已订阅主题指令执行失败或结果不正确")
                cmd_err_cnt += 1

            send_payload = '''{"action":"Hello_From_%s"}''' % self.at_module
            if not self.publish_normal_msg('data', 0, send_payload):
                self.serial.output_record(self.report_file)
                self.log_record("##### 重连后MQTT发布消息指令执行失败")
                cmd_err_cnt += 1
            elif not self.wait_for_pub_msg_return(send_payload):
                self.serial.output_record(self.report_file)
                self.log_record("##### 重连后MQTT接收消息失败")
                cmd_err_cnt += 1

            self.serial.output_record(self.report_file)

            for key, value in self.mqtt_urc_recv.items():
                if not value:
                    self.log_record("##### MQTT URC缺失： " + key)
                    cmd_err_cnt += 1

            if cmd_err_cnt == 0:
                test_result = "- ####" + test_item + ": 通过"
            else:
                test_result = "- ####" + test_item + ": 失败"

        self.log_record(test_result)
        self.log_record("--------------------------------------------")
        return cmd_err_cnt, test_result

    def test_case_get_modinfo(self, test_case_cnt):
        test_item = '模组信息读取指令测试'
        self.log_record("###{}.{}".format(test_case_cnt, test_item))

        ret, ret_str = self.module_info_exec()
        self.serial.output_record(self.report_file)
        if ret:
            cmd_err_cnt = 0
            test_result = "- ####" + test_item + ": 通过"
        else:
            cmd_err_cnt = 1
            test_result = "- ####" + test_item + ": 失败"

        self.log_record(test_result)
        self.log_record("--------------------------------------------")
        return cmd_err_cnt, ret_str

    def test_case_ota(self, test_case_cnt):
        test_item = 'OTA升级及固件读取指令测试'
        self.log_record("###{}.{}".format(test_case_cnt, test_item))
        cmd_err_cnt = 0

        if not self.ota_cmd_test():
            cmd_err_cnt = 1
            self.serial.output_record(self.report_file)
            test_result = "- ####" + test_item + ": 指令不存在"
        else:
            if not self.ota_update_test(OTA_local_version):
                self.serial.output_record(self.report_file)
                self.log_record("##### OTA升级指令执行失败")
                cmd_err_cnt += 1

            fw_version, fw_size, fw_md5 = self.ota_read_fw_info()
            self.serial.output_record(self.report_file)

            if fw_size != 0:
                if not self.ota_read_fw_data(fw_version, fw_size, fw_md5):
                    self.log_record("##### OTA读取固件指令执行失败")
                    cmd_err_cnt += 1
                # 不保存固件读取期间的串口数据
                self.serial.output_record(None)

            if cmd_err_cnt == 0:
                test_result = "- ####" + test_item + ": 通过"
            else:
                test_result = "- ####" + test_item + ": 失败"

        self.log_record(test_result)
        self.log_record("--------------------------------------------")
        return cmd_err_cnt, test_result

    def test_case_regnet(self, test_case_cnt):
        test_item = '网络注册指令测试'
        self.log_record("###{}.{}".format(test_case_cnt, test_item))
        cmd_err_cnt = 0

        if not self.regnet_test():
            cmd_err_cnt = 1
            test_result = "- ####" + test_item + ": 指令不存在"
        else:
            ret = self.regnet_query_state()

            if ret:
                self.regnet_setup(0)
                time.sleep(1)
                self.regnet_setup(1)
            else:
                self.regnet_setup(1)

            for i in range(5):
                time.sleep(1)
                ret = self.regnet_query_state()
                if ret:
                    break

            if ret:
                cmd_err_cnt = 0
                test_result = "- ####" + test_item + ": 通过"
            else:
                cmd_err_cnt = 1
                test_result = "- ####" + test_item + ": 失败"

        self.serial.output_record(self.report_file)
        self.log_record(test_result)
        self.log_record("--------------------------------------------")
        return cmd_err_cnt, test_result

    def test_case_prdinfo_devreg(self, test_case_cnt):
        test_item = "产品信息设置/动态注册指令测试"
        self.log_record("###{}.{}".format(test_case_cnt, test_item))
        cmd_err_cnt = 0

        while True:
            if not self.prdinfo_test() or not self.devreg_test():
                cmd_err_cnt = -1
                test_result = "- ####" + test_item + ": 指令不存在"
                break

            if self.is_mqtt_connected():
                self.mqtt_disconnect()

            if not self.prdinfo_setup(Hub_REG_Product_ID, Hub_REG_Product_Key, Hub_REG_Device_Name):
                self.serial.output_record(self.report_file)
                self.log_record("##### 产品信息设置指令执行失败")
                cmd_err_cnt += 1
                break

            if not self.devreg_exec():
                self.serial.output_record(self.report_file)
                self.log_record("##### 动态注册指令执行失败")
                cmd_err_cnt += 1
                break

            if not self.devinfo_query(Hub_REG_Product_ID, Hub_REG_Device_Name, ''):
                self.serial.output_record(self.report_file)
                self.log_record("##### 查询设备信息指令执行失败")
                cmd_err_cnt += 1

            if not self.mqtt_connect():
                self.serial.output_record(self.report_file)
                self.log_record("##### 设备注册后MQTT连接指令执行失败")
                cmd_err_cnt += 1

            break

        self.serial.output_record(self.report_file)
        if cmd_err_cnt == 0:
            test_result = "- ####" + test_item + ": 通过"
        elif cmd_err_cnt > 0:
            test_result = "- ####" + test_item + ": 失败"

        self.log_record(test_result)
        self.log_record("--------------------------------------------")
        return cmd_err_cnt, test_result

    def test_case_cert(self, test_case_cnt):
        test_item = '证书设备指令测试'
        self.log_record("###{}.{}".format(test_case_cnt, test_item))
        cmd_err_cnt = 0

        while True:
            if not self.certadd_test():
                cmd_err_cnt = -1
                test_result = "- ####" + test_item + ": 指令不存在"
                break

            self.serial.output_record(self.report_file)

            try:
                cert_devinfo = 'HUB-CERT1'
                cert_devinfo = config_file[cert_devinfo]
                CERT_Product_ID = cert_devinfo['CERT_Product_ID']
                CERT_Device_Name = cert_devinfo['CERT_Device_Name']
                CERT_Crt_File = cert_devinfo['CERT_Crt_File']
                CERT_Key_File = cert_devinfo['CERT_Key_File']
            except:
                self.log_record("##### 从配置文件解析证书设备信息失败")
                cmd_err_cnt += 1
                break

            self.mqtt_disconnect()

            with open(CERT_Crt_File, "rt", encoding='utf8') as crt_file:
                crt_content = crt_file.read()
                if not self.certadd_setup(CERT_Crt_File, crt_content):
                    self.serial.output_record(self.report_file)
                    self.log_record("##### 添加设备公钥证书指令执行失败")
                    cmd_err_cnt += 1
                    break

            with open(CERT_Key_File, "rt", encoding='utf8') as crt_file:
                crt_content = crt_file.read()
                if not self.certadd_setup(CERT_Key_File, crt_content):
                    self.serial.output_record(self.report_file)
                    self.log_record("##### 添加设备私钥证书指令执行失败")
                    cmd_err_cnt += 1
                    break

            self.serial.output_record(None)

            if not self.certadd_query():
                self.serial.output_record(self.report_file)
                self.log_record("##### 查询设备证书指令执行失败")
                cmd_err_cnt += 1
                break

            if not self.certcheck_setup(CERT_Crt_File):
                self.serial.output_record(self.report_file)
                self.log_record("##### 校验设备公钥证书指令执行失败")
                cmd_err_cnt += 1
                break

            if not self.certcheck_setup(CERT_Key_File):
                self.serial.output_record(self.report_file)
                self.log_record("##### 校验设备私钥证书指令执行失败")
                cmd_err_cnt += 1
                break

            if not self.devinfo_setup(CERT_Product_ID, CERT_Device_Name, CERT_Crt_File, tls=2):
                self.serial.output_record(self.report_file)
                self.log_record("##### 证书设备信息设置指令执行失败")
                cmd_err_cnt += 1
                break

            if not self.mqtt_connect():
                self.serial.output_record(self.report_file)
                self.log_record("##### 证书设备MQTT连接指令执行失败")
                cmd_err_cnt += 1
                break

            if not self.is_mqtt_connected():
                self.serial.output_record(self.report_file)
                self.log_record("##### 证书设备查询MQTT连接状态指令执行失败")
                cmd_err_cnt += 1

            if not self.subscribe_normal_topic('data', 0, self.payload_queue_handler):
                self.serial.output_record(self.report_file)
                self.log_record("##### 证书设备MQTT订阅指令执行失败")
                cmd_err_cnt += 1

            send_payload = get_hub_test_msg()
            if not self.publish_normal_msg('data', 1, send_payload):
                self.serial.output_record(self.report_file)
                self.log_record("##### 证书设备MQTT QoS1发布JSON消息指令执行失败")
                cmd_err_cnt += 1
            elif not self.wait_for_pub_msg_return(send_payload):
                self.serial.output_record(self.report_file)
                self.log_record("##### 证书设备MQTT 接收JSON消息失败")
                cmd_err_cnt += 1

            if not self.mqtt_disconnect():
                self.serial.output_record(self.report_file)
                self.log_record("##### 证书设备MQTT断开连接指令执行失败")
                cmd_err_cnt += 1

            if self.is_mqtt_connected():
                self.serial.output_record(self.report_file)
                self.log_record("##### 证书设备查询MQTT断开状态指令执行失败")
                cmd_err_cnt += 1

            if not self.certdel_setup(CERT_Crt_File):
                self.serial.output_record(self.report_file)
                self.log_record("##### 删除设备公钥证书指令执行失败")
                cmd_err_cnt += 1
                break

            if not self.certdel_setup(CERT_Key_File):
                self.serial.output_record(self.report_file)
                self.log_record("##### 删除设备私钥证书指令执行失败")
                cmd_err_cnt += 1
                break

            break

        self.serial.output_record(self.report_file)
        if cmd_err_cnt == 0:
            test_result = "- ####" + test_item + ": 通过"
        elif cmd_err_cnt > 0:
            test_result = "- ####" + test_item + ": 失败"

        self.log_record(test_result)
        self.log_record("--------------------------------------------")
        return cmd_err_cnt, test_result

    def iot_hub_verify_test(self, file_out, entire_test=False):
        if self.is_mqtt_connected():
            self.mqtt_disconnect()

        self.set_report_file(file_out)
        self.serial.start_record()

        total_err_cnt = 0
        test_result_list = []
        extra_result_list = []
        modinfo_result = ''
        modinfo_err_cnt = 0
        start_time = time.time()
        print(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>")
        self.log_record("## 腾讯云IoT AT指令模组测试")
        while True:
            test_case_cnt = 1
            self.log_record("--------------------------------------------")
            cmd_err_cnt, test_result = self.test_case_devinfo_psk(test_case_cnt)
            test_result_list.append(test_result)
            total_err_cnt += cmd_err_cnt

            test_case_cnt += 1
            self.log_record("--------------------------------------------")
            cmd_err_cnt, test_result = self.test_case_mqtt_connection(test_case_cnt)
            test_result_list.append(test_result)
            total_err_cnt += cmd_err_cnt

            # 该项测试如果失败则退出
            #if cmd_err_cnt != 0:
                # break

            test_case_cnt += 1
            self.log_record("--------------------------------------------")
            cmd_err_cnt, test_result = self.test_case_mqtt_sub_unsub(test_case_cnt)
            test_result_list.append(test_result)
            total_err_cnt += cmd_err_cnt

            test_case_cnt += 1
            self.log_record("--------------------------------------------")
            cmd_err_cnt, test_result = self.test_case_mqtt_pub_recv(test_case_cnt)
            test_result_list.append(test_result)
            total_err_cnt += cmd_err_cnt

            test_case_cnt += 1
            self.log_record("--------------------------------------------")
            cmd_err_cnt, test_result = self.test_case_mqtt_sub_pub_auth(test_case_cnt)
            test_result_list.append(test_result)
            total_err_cnt += cmd_err_cnt

            test_case_cnt += 1
            self.log_record("--------------------------------------------")
            cmd_err_cnt, test_result = self.test_case_mqtt_reconnect(test_case_cnt)
            test_result_list.append(test_result)
            total_err_cnt += cmd_err_cnt

            if entire_test:
                test_case_cnt += 1
                self.log_record("--------------------------------------------")
                cmd_err_cnt, test_result = self.test_case_ota(test_case_cnt)
                extra_result_list.append(test_result)

                test_case_cnt += 1
                self.log_record("--------------------------------------------")
                cmd_err_cnt, test_result = self.test_case_prdinfo_devreg(test_case_cnt)
                extra_result_list.append(test_result)

                test_case_cnt += 1
                self.log_record("--------------------------------------------")
                cmd_err_cnt, test_result = self.test_case_cert(test_case_cnt)
                extra_result_list.append(test_result)

                test_case_cnt += 1
                self.log_record("--------------------------------------------")
                cmd_err_cnt, test_result = self.test_case_regnet(test_case_cnt)
                extra_result_list.append(test_result)

                test_case_cnt += 1
                self.log_record("--------------------------------------------")
                modinfo_err_cnt, modinfo_result = self.test_case_get_modinfo(test_case_cnt)

            # 结束测试
            self.log_record("--------------------------------------------")
            self.log_record("--------------------------------------------")
            break

        self.log_record("## 腾讯云IoT AT指令模组测试")
        self.log_record("#### 测试时间： " + time.strftime("%Y-%m-%d %H:%M:%S", time.localtime()))
        self.log_record("#### 模组名称： " + self.at_module)
        if modinfo_err_cnt == 0:
            self.log_record(modinfo_result)

        self.log_record("--------------------------------------------")
        if total_err_cnt == 0:
            self.log_record("#### MQTT AT指令测试: 通过")
        else:
            self.log_record("#### MQTT AT指令测试: 失败")

        for i in test_result_list:
            self.log_record(i)

        if entire_test:
            if modinfo_err_cnt == 1:
                self.log_record(modinfo_result)

            self.log_record("--------------------------------------------")
            self.log_record("#### 其他AT指令测试:")

            for i in extra_result_list:
                self.log_record(i)

        print("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<")
        end_time = time.time()
        print("Test duration:", round(end_time - start_time, 1), "seconds")

        self.serial.stop_record()
        del self.hub_payload_queue
        self.mqtt_disconnect()
        return

    def ota_update_test(self, version):

        if self.is_mqtt_connected():
            self.mqtt_disconnect()

        if not self.devinfo_setup(Hub_Product_ID, Hub_Device_Name, Hub_Device_Key):
            return False

        if self.conn_mqtt_before_ota == 'yes':
            if not self.mqtt_connect():
                return False

        if not self.ota_update_setup(1, version):
            return False

        update_timeout_cnt = 1
        while self.ota_state == 'updating' and update_timeout_cnt <= 200:
            print('Wait for OTA update completed...', update_timeout_cnt)
            time.sleep(3)
            update_timeout_cnt += 1

        self.ota_update_setup(0, version)

        if self.at_module == 'ESP8266':
            self.mqtt_disconnect()

        print("OTA update state:", self.ota_state)

        if self.ota_state != 'success':
            print("OTA update failed!")
            return False

        return True

    def ota_read_test(self):

        fw_version, fw_size, fw_md5 = self.ota_read_fw_info()

        if fw_size != 0:
            self.ota_read_fw_data(fw_version, fw_size, fw_md5)
        else:
            print("null OTA FW info")

        return


class IoTExplorerATTest(IoTBaseATCmd):
    def __init__(self, at_module='ESP8266'):
        super(IoTExplorerATTest, self).__init__(at_module)
        self.property_result_err_cnt = 0
        self.event_result_err_cnt = 0
        self.property_result_ok_cnt = 0
        self.event_result_ok_cnt = 0

    def property_msg_handler(self, topic, payload):
        try:
            obj = json.loads(payload)
            method = obj["method"]
        except KeyError:
            print("Invalid template JSON：", topic, payload)
            return

        try:
            if method == 'report_reply':
                code = obj["code"]
                status = obj["status"]

                print("----- property report reply ---------")
                if code != 0:
                    print("error status: ", status)
                    self.property_result_err_cnt += 1
                else:
                    self.property_result_ok_cnt += 1
                print("----- result code %d ---------" % code)

            elif method == 'control':
                params = obj["params"]

                print("----- property downlink control msg ---------")
                print(json.dumps(params, indent=2))

            elif method == 'get_status_reply':
                code = obj["code"]
                status_type = obj["type"]
                data = obj["data"]

                print("----- property get reply code %d type %s ---------" % (code, status_type))
                print(json.dumps(data, indent=2))

        except KeyError:
            print("Invalid property JSON：", topic, payload)
            self.property_result_err_cnt += 1

        return

    def event_msg_handler(self, topic, payload):
        try:
            obj = json.loads(payload)
            code = obj["code"]

            print("----- event result msg ---------")
            print(json.dumps(obj, indent=2))
            print("----- event result_code %d ---------" % code)
            if code != 0:
                self.event_result_err_cnt += 1
            else:
                self.event_result_ok_cnt += 1
        except KeyError:
            print("Invalid event JSON：", topic, payload)
            self.event_result_err_cnt += 1

        return

    def action_msg_handler(self, topic, payload):
        try:
            obj = json.loads(payload)

            print("----- action msg start ---------")
            print(json.dumps(obj, indent=2))
            print("----- action msg end ---------")

        except KeyError:
            print("Invalid action JSON：", topic, payload)
            self.event_result_err_cnt += 1

        return

    def subscribe_property_topic(self, qos=0):
        # "$thing/down/property/ProductID/DeviceName"
        topic = '''$thing/down/property/%s/%s''' % (self.product_id, self.device_name)
        return self.subscribe_topic(topic, qos, self.property_msg_handler)

    def get_property_status(self, qos=1):
        # "$thing/up/property/ProductID/DeviceName"
        topic = "$thing/up/property/%s/%s" % (self.product_id, self.device_name)
        msg = '{"method":"get_status", "type":"report", "showmeta": 0,' \
              '"clientToken":"clientToken-%d"}' % ( int(time.time()*10)%1000 )
        return self.publish_msg(topic, qos, msg)

    def publish_property_msg(self, msg, qos=0):
        # "$thing/up/property/ProductID/DeviceName"
        topic = "$thing/up/property/%s/%s" % (self.product_id, self.device_name)
        return self.publish_msg(topic, qos, msg)

    def subscribe_event_topic(self, qos=0):
        # "$thing/down/event/ProductID/DeviceName
        topic = '''$thing/down/event/%s/%s''' % (self.product_id, self.device_name)
        return self.subscribe_topic(topic, qos, self.event_msg_handler)

    def post_event_msg(self, msg, qos=0):
        # "$thing/up/event/ProductID/DeviceName"
        topic = '''$thing/up/event/%s/%s''' % (self.product_id, self.device_name)
        return self.publish_msg(topic, qos, msg)

    def subscribe_action_topic(self, qos=0):
        # "$thing/down/action/ProductID/DeviceName
        topic = '''$thing/down/action/%s/%s''' % (self.product_id, self.device_name)
        return self.subscribe_topic(topic, qos, self.action_msg_handler)

    def reply_action_msg(self, qos=0):
        # "$thing/up/action/ProductID/DeviceName"
        topic = "$thing/up/action/%s/%s" % (self.product_id, self.device_name)
        msg = '{"desired":null, "clientToken":"clientToken-%d"}' % ( int(time.time()*10)%1000 )
        return self.publish_msg(topic, qos, msg)

    def do_loop_test(self, loop_cnt):
        self.property_result_err_cnt = 0
        self.event_result_err_cnt = 0
        self.property_result_ok_cnt = 0
        self.event_result_ok_cnt = 0
        test_cnt = 0
        property_send_err_cnt = 0
        property_send_ok_cnt = 0
        event_post_ok_cnt = 0
        event_post_err_cnt = 0
        start_time = time.time()
        start_ram = self.get_sysram()
        while test_cnt < loop_cnt:
            logging.info("------IoT Explorer property/event loop test cnt: %d" % test_cnt)
            if self.publish_property_msg(gen_property_update_msg(), qos=1):
                property_send_ok_cnt += 1
            else:
                property_send_err_cnt += 1
                logging.error("publish long msg failed")

            time.sleep(1)
            if self.post_event_msg(gen_event_post_msg(), qos=1):
                event_post_ok_cnt += 1
            else:
                event_post_err_cnt += 1

            time.sleep(1)
            test_cnt += 1

        time.sleep(1)
        end_time = time.time()
        end_ram = self.get_sysram()
        print("---------IoT Explorer property/event loop test result:--------")
        print("Test AT module:", self.at_module)
        print("Test start time:", time.ctime(start_time), " End time:", time.ctime(end_time))
        print("Test duration:", round(end_time - start_time, 1), "seconds")
        print("Test count:", test_cnt)
        print("Property send OK count:", property_send_ok_cnt, " send error count:", property_send_err_cnt)
        print('''Property result OK count: %d  error count: %d''' % (self.property_result_ok_cnt, self.property_result_err_cnt))
        print('''Property Publish success rate %.2f%%''' % (round((self.property_result_ok_cnt / test_cnt) * 100, 2)))
        print("Event post OK count:", event_post_ok_cnt, " post error count:", event_post_err_cnt)
        print('''Event result OK count: %d  error count: %d''' % (self.event_result_ok_cnt, self.event_result_err_cnt))
        print('''Event Post success rate %.2f%%''' % (round((self.event_result_ok_cnt / test_cnt) * 100, 2)))
        print("Test start RAM:", start_ram, " End RAM:", end_ram)
        print("---------IoT Explorer property/event loop test end--------")

    def iot_explorer_test(self, loop=False, loop_cnt=0):

        while True:
            if self.is_mqtt_connected():
                self.mqtt_disconnect()

            ret = self.devinfo_setup(IE_Product_ID, IE_Device_Name, IE_Device_Key)
            if not ret:
                break

            ret = self.mqtt_connect()
            if not ret:
                break

            ret = self.subscribe_property_topic()
            if not ret:
                break

            ret = self.subscribe_event_topic()
            if not ret:
                break

            ret = self.subscribe_action_topic()
            if not ret:
                break

            ret = self.post_event_msg(gen_event_post_msg())
            if not ret:
                break

            time.sleep(0.5)

            ret = self.get_property_status()
            if not ret:
                break

            time.sleep(0.5)

            ret = self.publish_property_msg(gen_property_update_msg())
            if not ret:
                break

            while loop:
                time.sleep(0.5)

                if loop_cnt > 0:
                    self.do_loop_test(loop_cnt)
                    break

                cmd = input("---------IoT Explorer template/event loop test:--------\n"
                            "Input:\n"
                            "1. 'quit' to break\n"
                            "2. 'update' to update property msg\n"
                            "3. 'event' to post event msg\n"
                            "Your choice:\n").strip('\n')
                if cmd.lower() == 'quit':
                    break

                if cmd.lower() == 'update':
                    self.publish_property_msg(gen_property_update_msg())

                if cmd.lower() == 'event':
                    self.post_event_msg(gen_event_post_msg())

            time.sleep(0.5)

            self.unsubscribe_all_topics()

            time.sleep(0.5)

            ret = self.mqtt_disconnect()
            if not ret:
                break

            # one shot test
            return

################# QCloud python AT cmd framework END #########################


def on_connect(mqttc, obj, flags, rc):
    logging.info("PAHO-MQTT Connected with result code " + str(rc))


class IoTMQTTClient:
    def do_one_connect(self, ProductId, DeviceName, DevicePsk):
        ret = self.IotHmac_sha1(ProductId, DeviceName, DevicePsk)
        logging.info("PAHO-MQTT prepare client " + ret["clientid"])
        try:
            mqttc = mqtt.Client(client_id=ret["clientid"])
            mqttc.on_connect = on_connect
            mqttc.username_pw_set(ret["username"], ret["password"])

            logging.info("PAHO-MQTT start connection with " + ret["host"])
            mqttc.connect(ret["host"], 1883, 60)
            mqttc.loop_start()
            time.sleep(2)
            mqttc.loop_stop()  # stop the loop
            logging.info("PAHO-MQTT stop")
            return True
        except TimeoutError:
            logging.info("PAHO-MQTT connect with " + ret["host"] + " timeout!")
            return False


    def IotHmac_sha1(self, productid, devicename, devicekey):
        # 1. 生成connid为一个随机字符串,方便后台定位问题
        connid = ''.join(random.choice(string.ascii_uppercase + string.digits) for _ in range(5))
        # 2. 生成过期时间,表示签名的过期时间,从纪元1970年1月1日 00:00:00 UTC 时间至今秒数的 UTF8 字符串
        expiry = int(time.time()) + 24 * 3600
        # 3. 生成MQTT的clientid部分
        clientid = "{}{}".format(productid, devicename)
        # 4. 生成mqtt的username部分
        username = "{};12010126;{};{}".format(clientid, connid, expiry)
        # 5. 对username进行签名,生成token
        token = hmac.new(base64.b64decode(devicekey), username.encode('utf8'), digestmod=hashlib.sha1).hexdigest()
        # 6. 根据物联云通信平台规则生成password字段
        password = "{};{}".format(token, "hmacsha1")
        return {
            "host": "%s.iotcloud.tencentdevices.com" % productid,
            # "host": mqtt_host,
            "product": productid,
            "device": devicename,
            "clientid": clientid,
            "username": username,
            "password": password
        }


class ESPWiFiATCmd:
    def __init__(self, cmd_timeout=3):
        self.serial = SerialATClient()
        self.cmd_timeout = cmd_timeout  # unit: seconds
        self.err_list = ['ERROR', 'busy']
        self.boarding_state = 'off'

    def do_network_connection(self, ssid, psw):
        for i in range(5):
            if self.is_network_connected():
                return True

            print("Connecting to wifi", ssid, psw)
            self.set_wifi_mode(1)
            self.join_wifi_network(ssid, psw)

        return False

    def join_wifi_network(self, ssid, psw):
        # AT+CWJAP="SSID","PSW"
        cmd = '''AT+CWJAP="%s","%s"''' % (ssid, psw)
        ok_reply = 'OK'
        hint = cmd

        return self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, 10)        

    def set_tc_log_debug(self, level):
        # AT+TCLOG=level
        cmd = "AT+TCLOG=86013388,"+level
        ok_reply = 'OK'
        hint = cmd

        return self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, self.cmd_timeout)

    def is_network_connected(self):
        # AT+CWJAP?
        cmd = 'AT+CWJAP?'
        ok_reply = 'OK'

        ret, rep = self.serial.send_cmd_wait_reply(cmd, ok_reply, self.err_list, self.cmd_timeout)
        if ret:
            ap_str = rep

        # do this by checking STA IP value
        # AT+CIPSTA?
        cmd = 'AT+CIPSTA?'
        ok_reply = 'OK'

        ret, rep = self.serial.send_cmd_wait_reply(cmd, ok_reply, self.err_list, self.cmd_timeout)
        if ret:
            try:
                ip_str = rep.split('\n', 1)[0]
                status = ip_str.find('+CIPSTA:ip:"0.0.0.0"')
                if status == -1:
                    print("Network is connected")
                    print(ap_str+ip_str)
                    return True
                else:
                    print("WiFi network is not connected")
                    return False
            except ValueError:
                print("Invalid WiFi IP status value:", rep)
                return False

        else:
            print("WiFi get IP status failed")
            print(rep)
            return False

    def get_wifi_status(self):
        # AT+CIPSTATUS
        cmd = 'AT+CIPSTATUS'
        ok_reply = 'OK'

        rc, rep = self.serial.send_cmd_wait_reply(cmd, ok_reply, self.err_list, self.cmd_timeout)
        if rc:
            try:
                status = rep.split(':', 1)[1].strip()
                if status == '2':
                    print("WiFi is connected")
                    return True
                else:
                    return False
            except ValueError:
                print("Invalid WiFi state value:", rep)
                return False
        else:
            print("WiFi get state failed")
            print(rep)
            return False

    def set_wifi_mode(self, mode):
        '''
        :param mode:
         0: WIFI OFF
         1: Station
         2: SoftAP
         3: SoftAP+Station
        '''
        # AT+CWMODE=mode
        cmd = '''AT+CWMODE=%d''' % (mode)
        ok_reply = 'OK'
        hint = cmd

        return self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, self.cmd_timeout)
    
    def smartconfig_msg_handler(self, msg):
        try:
            status = msg.split(':', 1)[1]
        except ValueError:
            print("\nInvalid smartconfig msg:", msg)
            return

        if status == 'WIFI_CONNECT_SUCCESS':
            print("\nsmartconfig boarding and connection success")
            self.boarding_state = 'success'
        elif 'WIFI_CONNECT_FAILED' in status:
            print("\nsmartconfig boarding and connection failed")
            self.boarding_state = 'failed'
        else:
            print("\nUnknown smartconfig msg:", msg)
            self.boarding_state = 'unknown'
        return

    def start_smartconfig(self):
        # AT+TCSTARTSMART"
        cmd = 'AT+TCSTARTSMART'
        ok_reply = '+TCSTARTSMART:OK'
        hint = cmd

        if not self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, 10):
            return False

        self.serial.add_data_handler("+TCSTARTSMART", self.smartconfig_msg_handler)
        self.boarding_state = 'boarding'
        return True

    def stop_smartconfig(self):
        # AT+TCSTOPSMART"
        cmd = 'AT+TCSTOPSMART'
        ok_reply = '+TCSTOPSMART:OK'
        hint = cmd

        return self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, 10)

    def softap_msg_handler(self, msg):
        try:
            status = msg.split(':', 1)[1]
        except ValueError:
            print("\nInvalid softAP msg:", msg)
            return

        if status == 'WIFI_CONNECT_SUCCESS':
            print("\nsoftAP boarding and connection success")
            self.boarding_state = 'success'
        elif 'WIFI_CONNECT_FAILED' in status:
            print("\nsoftAP boarding and connection failed")
            self.boarding_state = 'failed'
        else:
            print("\nUnknown softAP msg:", msg)
            self.boarding_state = 'unknown'
        return

    def start_softAP(self):
        # AT+TCSAP="ESP-SoftAP","12345678"
        ssid = config_file.get('WIFI', 'SAP_SSID')
        psw = config_file.get('WIFI', 'SAP_PSWD')
        cmd = '''AT+TCSAP="%s","%s"''' % (ssid, psw)
        ok_reply = '+TCSAP:OK'
        hint = cmd

        if not self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, 10):
            return False

        self.serial.add_data_handler("+TCSAP", self.softap_msg_handler)
        self.boarding_state = 'boarding'
        return True

    def stop_softAP(self):
        # AT+TCSTOPSAP"
        cmd = 'AT+TCSTOPSAP'
        ok_reply = '+TCSTOPSAP:OK'
        hint = cmd

        return self.serial.do_one_at_cmd(cmd, ok_reply, hint, self.err_list, 10)

    def wifi_boarding_test(self):

        while True:
            qcloud = IoTBaseATCmd('ESP8266')

            if qcloud.is_mqtt_connected():
                qcloud.mqtt_disconnect()

            ret = qcloud.devinfo_setup(IE_Product_ID, IE_Device_Name, IE_Device_Key)
            if not ret:
                break

            ret = self.start_softAP()
            if not ret:
                break

            while self.boarding_state == 'boarding':
                print('Wait for WiFi boarding completed...')
                time.sleep(1)
                cmd = input("Input 'quit' to break:").strip('\n')
                if cmd.lower() == 'quit':
                    break
                else:
                    continue

            if self.boarding_state == 'success':
                IoTExplorerATTest('ESP8266').iot_explorer_test(loop=True)

            break

        return


def cell_network_connect(at_module):
    IoTATCmd = IoTBaseATCmd(at_module)
    if not IoTATCmd.regnet_test():
        print("AT+TCREGNET Not exist")
        return False

    if IoTATCmd.regnet_query_state():
        return True

    IoTATCmd.regnet_setup(1)

    for i in range(10):
        time.sleep(1)
        if IoTATCmd.regnet_query_state():
            return True

    return False


def at_module_network_connect(at_module, WiFi_SSID, WiFi_PSWD):
    if at_module == 'ESP8266':
        return ESPWiFiATCmd().do_network_connection(WiFi_SSID, WiFi_PSWD)
    elif at_module in ['N21', 'N10', 'M6315']:
        return cell_network_connect(at_module)
    else:
        return True


def interactive_test():
    ser = SerialATClient(raw_mode=True)
    ser.start_read()
    while True:
        cmd = input().strip('\n')
        if cmd.lower() == 'quit':
            ser.close_port()
            sys.exit(0)
        elif len(cmd) == 0:
            continue

        ser.send_cmd(cmd)
        time.sleep(1)


def main():
    parser = argparse.ArgumentParser(description="QCloud IoT AT commands test tool",
                                     epilog="e.g.  python QCloud_IoT_AT_Test_Tool.py -p COM5 -a N21 -m MQTT")
    parser.add_argument('--version', '-v', action='version', version='%(prog)s: '+Tool_Version)
    test_mode_group = parser.add_argument_group('AT commands mode parameters')
    test_mode_group.add_argument(
            "--mode", "-m", required=True,
            help="Test mode: CLI/MQTT/IOT/WIFI/HUB/IE/OTA/CERT")

    test_mode_group.add_argument(
        "--at_module", "-a",
        help='AT module HW: ESP8266/N21/M5311(default: ESP8266)',
        default="ESP8266")

    test_mode_group.add_argument(
        "--loop", '-l', type=str,
        help='To do loop test or not',
        default="False")

    test_mode_group.add_argument(
        "--loop_cnt", '-n', type=int,
        help='loop test times count',
        default=0)

    test_mode_group.add_argument(
        "--debug", '-d',
        help='To print debug message or not',
        default="False")

    test_config_group = parser.add_argument_group('Test config file parameters')
    test_config_group.add_argument(
        "--config_file_path", '-c',
        help='test config file path',
        default=Default_config_file_path)

    test_config_group.add_argument(
        "--devinfo", '-x',
        help='devinfo select from config file',
        default="DEV1")

    test_config_group.add_argument(
        "--prdinfo", '-y',
        help='proinfo select from config file',
        default="PRD1")

    serial_port_group = parser.add_argument_group('Serial port parameters')
    serial_port_group.add_argument(
        "--port", '-p', required=True,
        help='which serial port. e.g. COM5 or /dev/ttyUSB0')

    serial_port_group.add_argument(
        "--baudrate", '-b',
        help='serial port baudrate(default: 115200)',
        default=115200)

    args = parser.parse_args()

    global g_serial_port
    g_serial_port = args.port
    global g_serial_baudrate
    g_serial_baudrate = args.baudrate

    at_module = args.at_module.upper()
    test_mode = args.mode.upper()
    devinfo = args.devinfo.upper()
    prdinfo = args.prdinfo.upper()
    config_file_path = args.config_file_path

    if not test_mode in ['CLI', 'MQTT', 'IOT', 'WIFI', 'HUB', 'OTA', 'IE', 'CERT']:
        print("Invalid test mode", test_mode)
        return

    if args.loop.upper() == "TRUE" or args.loop_cnt > 0:
        loop = True
        loop_cnt = args.loop_cnt
    else:
        loop = False
        loop_cnt = 0

    global g_debug_print
    log_level = '1'
    if args.debug.upper() == "TRUE":
        g_debug_print = True
        log_level = '4'
    elif args.debug.upper() == "FALSE":
        g_debug_print = False
    else:
        log_level = args.debug

    try:
        config_file.read(config_file_path, encoding="utf-8")

        if test_mode in ['MQTT', 'IOT', 'HUB', 'OTA']:
            devinfo = 'HUB-'+devinfo
            prdinfo = 'HUB-'+prdinfo
            devinfo_conf = config_file[devinfo]
            prdinfo_conf = config_file[prdinfo]
            global Hub_Product_ID
            global Hub_Device_Name
            global Hub_Device_Key
            global Hub_REG_Product_ID
            global Hub_REG_Product_Key
            global Hub_REG_Device_Name
            Hub_Product_ID = devinfo_conf['Product_ID']
            Hub_Device_Name = devinfo_conf['Device_Name']
            Hub_Device_Key = devinfo_conf['Device_Key']
            Hub_REG_Product_ID = prdinfo_conf['REG_Product_ID']
            Hub_REG_Product_Key = prdinfo_conf['REG_Product_Key']
            Hub_REG_Device_Name = prdinfo_conf['REG_Device_Name']

        elif test_mode in ['WIFI', 'IE']:
            devinfo = 'IE-' + devinfo
            devinfo_conf = config_file[devinfo]
            global IE_Product_ID
            global IE_Device_Name
            global IE_Device_Key
            IE_Product_ID = devinfo_conf['Product_ID']
            IE_Device_Name = devinfo_conf['Device_Name']
            IE_Device_Key = devinfo_conf['Device_Key']

        WiFi_SSID = config_file.get('WIFI', 'WiFi_SSID')
        WiFi_PSWD = config_file.get('WIFI', 'WiFi_PSWD')

    except:
        print("Exceptions when parsing config file. Exit!")
        return

    # Let's Rock'n'Roll

    # interactive command line test
    if test_mode == 'CLI':
        interactive_test()
        return

    SerialATClient().start_read()
    SerialATClient().wait_at_ready()
    SerialATClient().echo_off()

    if at_module == 'ESP8266':
        ESPWiFiATCmd().set_tc_log_debug(log_level)

    while True:
        # WiFi boarding test
        if test_mode == 'WIFI':
            ESPWiFiATCmd().wifi_boarding_test()
            break

        # For other tests, connect network first
        if not at_module_network_connect(at_module, WiFi_SSID, WiFi_PSWD):
            break

        # IoT MQTT AT commands test and output test report:
        if test_mode == 'MQTT':
            output_file_name = at_module+"-AT指令测试报告.md"
            with open(output_file_name, "wt", encoding='utf8') as file_out:
                IoTHubATTest(at_module).iot_hub_verify_test(file_out, entire_test=False)
            break

        # IoT entire AT commands test and output test report:
        if test_mode == 'IOT':
            output_file_name = at_module + "-AT指令测试报告.md"
            with open(output_file_name, "wt", encoding='utf8') as file_out:
                IoTHubATTest(at_module).iot_hub_verify_test(file_out, entire_test=True)
            break

        # IoT Hub test
        if test_mode == 'HUB':
            IoTHubATTest(at_module).iot_hub_loop_test(loop, loop_cnt)
            break

        # IoT Hub OTA test
        if test_mode == 'OTA':
            if IoTHubATTest(at_module).ota_update_test(OTA_local_version):
                IoTHubATTest(at_module).ota_read_test()
            break

        # IoT Hub cert device test
        if test_mode == 'CERT':
            IoTHubATTest(at_module).test_case_cert(0)
            break

        # IoT Explorer test
        if test_mode == 'IE':
            IoTExplorerATTest(at_module).iot_explorer_test(loop, loop_cnt)
            break

    SerialATClient().close_port()
    return


if __name__ == "__main__":
    main()