from flask import Flask

#---Flask run---
app = Flask(__name__)

@app.route('/device-update', methods=['POST'])
def device_update():
    pass

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)