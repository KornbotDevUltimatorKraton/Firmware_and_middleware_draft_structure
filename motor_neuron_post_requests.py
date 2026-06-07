import requests

# Define the payload to target pin PE7
payload = {
    "mcu_name": "mcu_arm",
    "pin": "PH7",   # The STM32 pin name
    "mode": "gpio", # Can also be "pwm"
    "value": 0 # 0 for LOW, 1 for HIGH
}

# Sending the request to the local FastAPI middleware
response = requests.post("http://127.0.0.1:8095/motor_neuron", json=payload)

# Check the response
print(f"Status Code: {response.status_code}")
print(f"Response: {response.json()}")

# 1. Initialize UART on STM32
#init_payload = {"mcu_name": "mcu_arm", "cmd": "init_uart", "baud": 9600}
#requests.post("http://127.0.0.1:8095/motor_neuron", json=init_payload)

# 2. Write data to the UART device
#write_payload = {"mcu_name": "mcu_arm", "cmd": "uart_write", "data": "Hello UART Device!"}
#requests.post("http://127.0.0.1:8095/motor_neuron", json=write_payload)