#mosquitto_sub -h localhost -t home/lights/home/handlers/rest/output | jq

/bin/python3 /home/reco1swave/extradrive1/Projects/home-automation-hub/src/handlers/rest_api__handler/api.py

sleep 5

mosquitto_pub -h localhost -p 1883 -t home/lights/home/handlers/rest/input -m '
{
  "addDevice": {
    "light_2": {
      "commands": {
        "turn_on": {
          "method": "POST",
          "endpoint": "http://localhost:8000/api/light/on",
          "headers": {
            "Content-Type": "application/json"
          },
          "payload": { "state": "on" },
          "ack": { "status": "light on", "code": 200 },
          "listen": false
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
