import json
import os

class Device:
    def __init__(self, device_id, device_name, protocol, event_based):
        self.device_id = device_id
        self.device_name = device_name
        self.protocol = protocol
        self.event_based = event_based
        self.state = {}
        self.commands = {}  # Will be set by subclass

    def has_command(self, command_name):
        return command_name in self.commands

    def get_command_info(self, command_name):
        return self.commands.get(command_name)

class MQTTDevice(Device):
    def __init__(self, device_id, device_name, protocol, event_based, address, commands, state_topic):
        super().__init__(device_id, device_name, protocol, event_based)
        self.address = address
        self.commands = commands
        self.state_topic = state_topic

    def convert_command(self, command_name):
        command = self.get_command_info(command_name)
        if command:
            payload = {
                "topic": self.address,
                "payload": command
            }
            return payload
        


class RESTDevice(Device):
    def __init__(self, device_id, device_name, protocol, event_based, base_url, commands):
        super().__init__(device_id, device_name, protocol, event_based)
        self.base_url = base_url
        self.commands = commands
    
    def convert_command(self, command_name):
        command = self.get_command_info(command_name)
        payload = {
            "method": command.get("method"),
            "url": self.base_url,
            "headers": "?",
            "body": "?"
        }
        return payload


class BLEDevice(Device):
    def __init__(self, device_id, device_name, protocol, event_based, ble_address, commands):
        super().__init__(device_id, device_name, protocol, event_based)
        self.ble_address = ble_address
        self.commands = commands
    
    def convert_command(self, command_name):
        command = self.get_command_info(command_name)
        payload = {
            "topic": self.address,
            "payload": command
        }
        return payload


class ZigbeeDevice(Device):
    def __init__(self, device_id, device_name, protocol, event_based, zigbee_id, state_topic):
        super().__init__(device_id, device_name, protocol, event_based)
        self.zigbee_id = zigbee_id
        self.state_topic = state_topic
        self.commands = {}  # No commands for Zigbee sensors in your config
    
    def convert_command(self, command_name):
        command = self.get_command_info(command_name)
        payload = {
            "topic": self.address,
            "payload": command
        }
        return payload


class RTSPDevice(Device):
    def __init__(self, device_id, device_name, protocol, event_based, stream_url, commands):
        super().__init__(device_id, device_name, protocol, event_based)
        self.stream_url = stream_url
        self.commands = commands
    
    def convert_command(self, command_name):
        command = self.get_command_info(command_name)
        payload = {
            "topic": self.address,
            "payload": command
        }
        return payload


class DeviceList:
    def __init__(self, config_path='./config/devices_config.json'):
        self.devices = []
        self.config_path = config_path
        self.devices_dict = {}
        self.load_devices()

    def load_devices(self):
        if not os.path.exists(self.config_path):
            print("Error: JSON file not found.")
            return

        with open(self.config_path, 'r') as file:
            data = json.load(file)
            self.devices_dict = data.get('devices', {})
            for device_id, details in self.devices_dict.items():
                protocol = details.get('protocol')
                device_name = details.get('device_name', 'Unknown')
                event_based = details.get('event_based', False)

                # Match protocol to proper subclass
                if protocol == 'MQTT':
                    device = MQTTDevice(
                        device_id=device_id,
                        device_name=device_name,
                        protocol=protocol,
                        event_based=event_based,
                        address=details['address'],
                        commands=details['commands'],
                        state_topic=details['state_topic']
                    )
                elif protocol == 'REST':
                    device = RESTDevice(
                        device_id=device_id,
                        device_name=device_name,
                        protocol=protocol,
                        event_based=event_based,
                        base_url=details['base_url'],
                        commands=details['commands']
                    )
                elif protocol == 'BLE':
                    device = BLEDevice(
                        device_id=device_id,
                        device_name=device_name,
                        protocol=protocol,
                        event_based=event_based,
                        ble_address=details['ble_address'],
                        commands=details['commands']
                    )
                elif protocol == 'Zigbee':
                    device = ZigbeeDevice(
                        device_id=device_id,
                        device_name=device_name,
                        protocol=protocol,
                        event_based=event_based,
                        zigbee_id=details['zigbee_id'],
                        state_topic=details['state_topic']
                    )
                elif protocol == 'RTSP':
                    device = RTSPDevice(
                        device_id=device_id,
                        device_name=device_name,
                        protocol=protocol,
                        event_based=event_based,
                        stream_url=details['stream_url'],
                        commands=details['commands']
                    )
                else:
                    print(f"Unknown protocol '{protocol}' for device {device_id}, skipping...")
                    continue

                self.devices.append(device)

    def save_devices(self):
        devices_to_save = {}

        for device in self.devices:
            common = {
                "device_name": device.device_name,
                "protocol": device.protocol,
                "event_based": device.event_based
            }

            # Add protocol-specific fields
            if isinstance(device, MQTTDevice):
                common.update({
                    "address": device.address,
                    "commands": device.commands,
                    "state_topic": device.state_topic
                })
            elif isinstance(device, RESTDevice):
                common.update({
                    "base_url": device.base_url,
                    "commands": device.commands
                })
            elif isinstance(device, BLEDevice):
                common.update({
                    "ble_address": device.ble_address,
                    "commands": device.commands
                })
            elif isinstance(device, ZigbeeDevice):
                common.update({
                    "zigbee_id": device.zigbee_id,
                    "state_topic": device.state_topic
                })
            elif isinstance(device, RTSPDevice):
                common.update({
                    "stream_url": device.stream_url,
                    "commands": device.commands
                })

            devices_to_save[device.device_id] = common

        with open(self.config_path, 'w') as file:
            json.dump({"devices": devices_to_save}, file, indent=2)

    def add_device(self, device):
        self.devices.append(device)
        self.save_devices()

    def remove_device(self, device_id):
        self.devices = [d for d in self.devices if d.device_id != device_id]
        self.save_devices()
    
    def get_device(self, device_id):
        for device in self.devices:
            if device.device_id == device_id:
                return device
        
    def command_Payload(self, device_id, command_name):
        device = self.get_device(device_id)
        #command = self.get_command_info(device, command)
        if device:
            return device.convert_command(command_name)
        else:
            print("Device not found")

    def get_all_MQTTtopics(self):
        topics = []
        for device in self.devices:
            if isinstance(device, MQTTDevice):
                topic = device.get('address')
                topics.append(topic)
        return topics
        

    def print_all_devices(self):
        if not self.devices:
            print("No devices found.")
            return

        for device in self.devices:
            print(f"\nDevice ID: {device.device_id}")
            print(f"  Name: {device.device_name}")
            print(f"  Protocol: {device.protocol}")
            print(f"  Event-Based: {device.event_based}")
            print(f"  Commands: {json.dumps(device.commands, indent=4)}")

            # Print protocol-specific fields
            if isinstance(device, MQTTDevice):
                print(f"  Address: {device.address}")
                print(f"  State Topic: {device.state_topic}")
            elif isinstance(device, RESTDevice):
                print(f"  Base URL: {device.base_url}")
            elif isinstance(device, BLEDevice):
                print(f"  BLE Address: {device.ble_address}")
            elif isinstance(device, ZigbeeDevice):
                print(f"  Zigbee ID: {device.zigbee_id}")
                print(f"  State Topic: {device.state_topic}")
            elif isinstance(device, RTSPDevice):
                print(f"  Stream URL: {device.stream_url}")

#test section

#device_list = DeviceList()

#payload = device_list.command_Payload("light_1", "set_brightness")

#print(payload)
