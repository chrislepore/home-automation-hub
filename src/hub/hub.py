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

# Plan:

# read all the info from the json file : Devices(protocol )

import paho.mqtt.client as mqtt

class Device:
    def __init__(self, device_id, device_name, protocol, event_based):
        self.device_id = device_id
        self.device_name = device_name
        self.protocol = protocol
        self.state = {}
        self.event_based = event_based

class Event:
    def __init__(self):
        pass


def add_device(device_list, device_id, device_name, protocol, event_based):
    device = Device(device_id, device_name, protocol, event_based)
    device_list.append(device)
    # save device to devices_config file
        
def remove_device(device_list, device_id):
    for device in device_list:
        if device.device_id == device_id:
            device_list.remove(device)
            break
    # Remove device from devices_config file


device_list = []

add_device(device_list, "light_1", "Living Room Light", "MQTT", True)
add_device(device_list, "light_2", "Bedroom Light", "MQTT", True)

for device in device_list:
    print(device.device_id)

remove_device(device_list, "light_1")

for device in device_list:
    print(device.device_id)



# mqtt start

def on_connect(client, userdata, flags, rc):
    print("Connected with result code " + str(rc))
    #connect to all topics
    client.subscribe("home/lights/#")

def on_message(client, userdata, msg):
    print(f"Topic: {msg.topic}, Message: {msg.payload.decode()}")

client = mqtt.Client(protocol=mqtt.MQTTv5)
client.on_message = on_message
client.on_connect = on_connect 

client.connect("localhost", 1883, 60)


try:
    client.loop_forever()
except KeyboardInterrupt:
    print("MQTT client loop interrupted, exiting...")
    client.disconnect()