# --- Imports ---
from flask import Flask, request, jsonify
import requests
import paho.mqtt.client as mqtt
import json

app = Flask(__name__)

# --- Device Classes ---
class RESTDevice:
    def __init__(self, device_id):
        self.device_id = device_id
        self.commands = {}

class RESTList:
    def __init__(self):
        self.devices = []

    def add_device(self, new_device):
        for device in self.devices:
            if device.device_id == new_device.device_id:
                device.commands.update(new_device.commands)
                return
        self.devices.append(new_device)

    def send(self, device_id, command_id):
        for device in self.devices:
            if device.device_id == device_id:
                command = device.commands.get(command_id)
                if command is None:
                    print("Command not found")
                    return
                if command.get('method') == "POST":
                    requests.post(command.get('endpoint'),
                                  headers=command.get('headers', {}),
                                  json=command.get('payload'))
                elif command.get('method') == "GET":
                    requests.get(command.get('endpoint'),
                                 headers=command.get('headers', {}))

# --- MQTT Setup ---
def on_connect(client, userdata, flags, rc, properties=None):
    print("Connected with result code " + str(rc))
    client.subscribe("home/lights/home/handlers/rest/input")

def on_message(client, userdata, msg):
    data = json.loads(msg.payload.decode())

    if data.get('addDevice') is not None:
        for device_id, details in data.get('addDevice').items():
            device = RESTDevice(device_id)
            device.commands = details['commands']
            list.add_device(device)

    elif data.get('action') is not None:
        list.send(data['action']['device_id'], data['action']['command_id'])

client = mqtt.Client(protocol=mqtt.MQTTv5)
client.on_connect = on_connect
client.on_message = on_message
client.connect("localhost", 1883, 60)
client.loop_start()

list = RESTList()

# --- Dynamic Flask Route ---
@app.route('/<path:endpoint>', methods=["GET", "POST"])
def handle_dynamic(endpoint):
    method = request.method
    print(f"Incoming request: /{endpoint} [{method}]")

    for device in list.devices:
        for command, details in device.commands.items():
            # Normalize both sides to ensure match
            expected_endpoint = details.get("endpoint", "").lstrip("/")
            if expected_endpoint == endpoint and details.get("method") == method:
                print(f"Matched command '{command}' for device '{device.device_id}'")
                data = request.get_json(force=True, silent=True) or {}
                data["location"] = {
                    "device_id": device.device_id,
                    "command_id": command
                }
                client.publish("home/lights/home/handlers/rest/output", json.dumps(data))
                ack = details.get("ack")
                return jsonify(ack) if ack else ("", 204)
    return "Not found", 404

# --- Flask Run ---
if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)
