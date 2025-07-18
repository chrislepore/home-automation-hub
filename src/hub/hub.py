# Objectives

# Keeps track of device status (ex: light(on/off), thermestate(23C), etc) IN MEMORY OOP
# executes scheduled events (ex: turn off all lights at 12:00) OPP
# logic: if motion sensor activates then turn on light on
# communicates via MQTT Broker to other modules
# Logging to track errors



# On startup:

# load config file 
# create device objects
# load schedual config file
# start up other modules 
# connect to MQTT Broker 

#############################################################################

#---Imports---
import paho.mqtt.client as mqtt
import os
import json
from device_manager import DeviceList

#---Setup---

class Event:
    def __init__(self):
        pass

def get_module_topics(config_path='./config/modules_config.json'):
    if not os.path.exists(config_path):
        print("Error: JSON file not found.")
        return
    with open(config_path, 'r') as file:
        data = json.load(file)
        print(data)

#---MQTT Broker Setup---
def on_connect(client, userdata, flags, rc):
    print("Connected with result code " + str(rc))
    #connect to all module topics

    #connect to all MQTT device topics

    client.subscribe("home/lights/#")

def on_message(client, userdata, msg):
    print(f"Topic: {msg.topic}, Message: {msg.payload.decode()}")

#---Run---

#Load other modules

device_list = DeviceList()

#Read device_connfig.json 
#then load into Device class objectes and add to device_list


get_module_topics()



'''
client = mqtt.Client(protocol=mqtt.MQTTv5)
client.on_message = on_message
client.on_connect = on_connect 

client.connect("localhost", 1883, 60)


try:
    client.loop_forever()
except KeyboardInterrupt:
    print("MQTT client loop interrupted, exiting...")
    client.disconnect()
'''