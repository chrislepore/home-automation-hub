# mock_device.py
from flask import Flask, request
import requests

app = Flask(__name__)

temp = 0
light = "off"


@app.route('/api/light/on', methods=['POST'])
def light_on():
    data = request.json
    print(f"[DEVICE] Turn ON received: {data}")
    light = "on"
    return {"result": "Light turned on"}, 200

@app.route('/api/light/off', methods=['POST'])
def light_off():
    data = request.json
    print(f"[DEVICE] Turn OFF received: {data}")
    light = "off"
    return {"result": "Light turned off"}, 200

@app.route('/api/light/state', methods=['GET'])
def light_state():
    print(f"[DEVICE] Light state: {light}")
    return {"result": light}, 200

@app.route('/api/thermostat/set_temp', methods=['POST'])
def set_temp():
    data = request.json
    temp = data
    print(f"[DEVICE] Set data: {temp}")
    return {"result": "Received set_temp"}, 200

@app.route('/api/thermostat/status', methods=['GET'])
def get_temp():
    print(f"[DEVICE] getting temp: {temp}")
    return {"result": temp}, 200

if __name__ == '__main__':
    app.run(port=8000)

response = requests.get(
                            "http://localhost:5000/api/thermostat/settings",
                            headers={"Content-Type": "application/json"},
                        )

while True:
    response = requests.post(
                            'http://localhost:5000/api/thermostat/update"',
                            headers={"Content-Type": "application/json"},
                            json={"result": temp}
                        )
    sleep 