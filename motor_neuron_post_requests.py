import requests

# Define the payload to target pin PE7
payload = {
    "mcu_name": "mcu_arm",
    "pin": "PB8",   # The STM32 pin name
    "mode": "pwm", # Can also be "pwm"
    "value": 100 # 0 for LOW, 1 for HIGH
}

# Sending the request to the local FastAPI middleware
response = requests.post("http://127.0.0.1:8095/motor_neuron", json=payload)

# Check the response
print(f"Status Code: {response.status_code}")
print(f"Response: {response.json()}")
