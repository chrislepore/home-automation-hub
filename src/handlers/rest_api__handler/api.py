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
    
    def send(self, device_id, command_id, payload=None):
        for device in self.devices:
            if device.device_id == device_id:
                command = device.commands.get(command_id)
                if command is None:
                    print("Command not found")
                    return {"error": "Command not found"}

                try:
                    if command.get('method') == "POST":
                        output = payload if payload is not None else command.get('payload')
                        response = requests.post(
                            command.get('endpoint'),
                            headers=command.get('headers', {}),
                            json=output
                        )
                    elif command.get('method') == "GET":
                        response = requests.get(
                            command.get('endpoint'),
                            headers=command.get('headers', {})
                        )
                    else:
                        return {"error": "Unsupported method"}

                    # Try to return response from device or fallback to configured ack
                    if response.ok:
                        try:
                            return response.json()
                        except ValueError:
                            return {"message": "No JSON in response", "status_code": response.status_code}
                    else:
                        return {"error": "Request failed", "status_code": response.status_code}

                except requests.RequestException as e:
                    return {"error": str(e)}

        return {"error": "Device not found"}


# --- MQTT Setup ---
def on_connect(client, userdata, flags, rc, properties=None):
    print("Connected with result code " + str(rc))
    client.subscribe("home/lights/home/handlers/rest/input")

def on_message(client, userdata, msg):
    payload_str = msg.payload.decode()

    # Validate JSON
    try:
        data = json.loads(payload_str)
    except json.JSONDecodeError:
        print(f"Invalid JSON received: {payload_str}")
        return  # Skip processing

    if data.get('addDevice') is not None:
        for device_id, details in data.get('addDevice').items():
            device = RESTDevice(device_id)
            device.commands = details['commands']
            rest_list.add_device(device)
            print(f"Device {device_id} added")
            print(f"{device.commands}")

    elif data.get('action') is not None:
        payload = data['action'].get('payload', None)
        result = rest_list.send(data['action']['device_id'], data['action']['command_id'], payload)
        client.publish("home/lights/home/handlers/rest/output", json.dumps(result))
    else:
        print("Unknown MQTT message from Hub")


client = mqtt.Client(protocol=mqtt.MQTTv5)
client.on_connect = on_connect
client.on_message = on_message
client.connect("localhost", 1883, 60)
client.loop_start()

rest_list = RESTList()

# --- Dynamic Flask Route ---
@app.route('/<path:endpoint>', methods=["GET", "POST"])
def handle_dynamic(endpoint):
    method = request.method
    print(f"Incoming request: /{endpoint} [{method}]")

    for device in rest_list.devices:
        for command, details in device.commands.items():
            # Normalize both sides to ensure match
            expected_endpoint = details.get("endpoint", "").lstrip("/")
            print(expected_endpoint)
            if expected_endpoint == endpoint and details.get("method", "").upper() == method and details.get("listen", True):
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
    app.run(host='0.0.0.0', port=5000, debug=True, use_reloader=False)
