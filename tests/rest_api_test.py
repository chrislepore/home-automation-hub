# mock_device.py
from flask import Flask, request

app = Flask(__name__)

@app.route('/api/light/on', methods=['POST'])
def light_on():
    data = request.json
    print(f"[DEVICE] Turn ON received: {data}")
    return {"result": "Light turned on"}, 200

if __name__ == '__main__':
    app.run(port=8000)