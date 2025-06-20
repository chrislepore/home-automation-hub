#mosquitto_sub -h localhost -t home/lights/home/handlers/rest/output | jq

/bin/python3 /home/reco1swave/extradrive1/Projects/home-automation-hub/src/handlers/rest_api__handler/api.py

sleep 5

mosquitto_pub -h localhost -p 1883 -t home/lights/home/handlers/rest/input -m '
{
  "addDevice": {
    "light_1": {
      "commands": {
        "turn_on": {
          "method": "POST",
          "endpoint": "http://localhost:8000/api/light/on",
          "headers": {
            "Content-Type": "application/json"
          },
          "payload": { "state": "on" },
          "ack": { },
          "listen": false
        },
        "turn_off": {
          "method": "POST",
          "endpoint": "http://localhost:8000/api/light/off",
          "headers": {
            "Content-Type": "application/json"
          },
          "payload": { "state": "off" },
          "ack": { },
          "listen": false
        },
        "get_state": {
          "method": "GET",
          "endpoint": "http://localhost:8000/api/light/state",
          "headers": {
            "Content-Type": "application/json"
          },
          "payload": { },
          "ack": { },
          "listen": false
        }
      }
    },
    "thermostat_1": {
      "commands": {
        "set_temperature": {
          "method": "POST",
          "endpoint": "http://localhost:8000/api/thermostat/set_temp",
          "headers": {
            "Content-Type": "application/json"
          },
          "payload": { },
          "ack": { },
          "listen": false
        },
        "get_temperature": {
          "method": "GET",
          "endpoint": "http://localhost:8000/api/thermostat/status",
          "headers": {
            "Content-Type": "application/json"
          },
          "ack": {},
          "listen": false
        },
        "update_temperature": {
          "method": "POST",
          "endpoint": "http://localhost:5000/api/thermostat/update",
          "headers": {
            "Content-Type": "application/json"
          },
          "ack": {"status": "status received", "code": 200},
          "listen": true
        },
        "get_settings": {
          "method": "GET",
          "endpoint": "http://localhost:5000/api/thermostat/settings",
          "headers": {
            "Content-Type": "application/json"
          },
          "ack": {"status": "status received", "mode": 20},
          "listen": true
        }
      }
    }
  }
}'

sleep 5

/bin/python3 /home/reco1swave/extradrive1/Projects/home-automation-hub/tests/rest_api_test.py

sleep 5

mosquitto_pub -h localhost -p 1883 -t home/lights/home/handlers/rest/input -m '
{
  "action": {
    "device_id": "light_2",
    "command_id": "turn_on"
  }
}'

sleep 5

curl -X POST http://localhost:5000/api/light/on -H "Content-Type: application/json" -d '{}'
