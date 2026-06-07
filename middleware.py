import os
import time
import json
import asyncio
import threading
from concurrent.futures import ThreadPoolExecutor

from fastapi import FastAPI, Request, HTTPException
import uvicorn
import serial

# --- Global Data Stores and Locks ---
app = FastAPI()

# Global dictionary to store the latest sensory data from all sources
store_sensory = {}
store_sensory_lock = threading.Lock()

# Thread pool executor for non-blocking serial writes in FastAPI handlers
executor = ThreadPoolExecutor(max_workers=5) 

# --- Robot Hardware Bridge Class ---
class RobotHardwareBridge:
    def __init__(self, mcu_configs: dict):
        self.mcu_ports = {}
        self.mcu_locks = {}
        self.read_threads = {}
        self.telemetry_stop_events = {}

        print("Initializing RobotHardwareBridge...")
        for mcu_name, config in mcu_configs.items():
            port = config.get("port")
            baudrate = config.get("baudrate", 115200)
            
            try:
                ser = serial.Serial(port, baudrate, timeout=0.1)
                self.mcu_ports[mcu_name] = ser
                self.mcu_locks[mcu_name] = threading.Lock()
                self.telemetry_stop_events[mcu_name] = threading.Event()

                read_thread = threading.Thread(
                    target=self._read_telemetry_loop, 
                    args=(mcu_name, ser, self.mcu_locks[mcu_name], self.telemetry_stop_events[mcu_name]),
                    daemon=True
                )
                self.read_threads[mcu_name] = read_thread
                read_thread.start()
            except Exception as e:
                print(f"ERROR: Could not setup '{mcu_name}': {e}")

    def _read_telemetry_loop(self, mcu_name, ser, lock, stop_event):
        while not stop_event.is_set():
            try:
                line = None
                with lock:
                    if ser.in_waiting > 0:
                        line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    try:
                        data = json.loads(line)
                        with store_sensory_lock:
                            store_sensory[mcu_name] = data
                    except json.JSONDecodeError:
                        pass
                time.sleep(0.01)
            except Exception:
                time.sleep(1)

    def _send_command(self, mcu_name: str, command_json: dict):
        if mcu_name not in self.mcu_ports:
            raise ValueError(f"MCU '{mcu_name}' not connected.")
        
        ser = self.mcu_ports[mcu_name]
        lock = self.mcu_locks[mcu_name]
        command_str = json.dumps(command_json) + '\n'
        
        with lock:
            ser.write(command_str.encode('utf-8'))
            ser.flush()

    def set_gpio(self, mcu_name: str, state: bool):
        self._send_command(mcu_name, {"led": state})
    def set_pin_state(self, mcu_name: str, pin_name: str, mode: str, value: int):
        """
        Sends a command to configure and set a pin dynamically.
        mcu_name: 'mcu_arm'
        pin_name: 'PA0', 'PB8', etc.
        mode: 'gpio' or 'pwm'
        value: 0/1 for GPIO, 0-255 for PWM
        """
        self._send_command(mcu_name, {
            "cmd": "set_pin", 
            "pin": pin_name, 
            "mode": mode, 
            "value": value
        })
    def set_native_servo(self, mcu_name, pin, angle):
        self._send_command(mcu_name, {"cmd": "set_servo", "pin": pin, "value": angle})

    def set_pca_pwm(self, mcu_name, channel, value):
        self._send_command(mcu_name, {"cmd": "set_pwm", "channel": channel, "value": value})

# --- FastAPI Endpoints ---
robot_bridge = None

@app.on_event("startup")
async def startup_event():
    global robot_bridge
    mcu_configs = {"mcu_arm": {"port": "/dev/ttyUSB0", "baudrate": 115200}}
    robot_bridge = RobotHardwareBridge(mcu_configs)

@app.get('/get_totalsense')
async def get_totalsense():
    """Returns the latest sensor data for all connected MCUs."""
    with store_sensory_lock:
        return {"data": store_sensory}

@app.post('/sensory_message')
async def sensory_message(request: Request):
    """Allows external systems to push custom sensory data into the storage."""
    data = await request.json()
    mcu_name = data.get("mcu_name", "external")
    with store_sensory_lock:
        store_sensory[mcu_name] = data
    return {"status": "updated"}

@app.post('/motor_neuron')
async def motor_neuron(request: Request):
    try:
        msg = await request.json()
        mcu_name = msg.get('mcu_name')
        if not mcu_name or mcu_name not in robot_bridge.mcu_ports:
            raise ValueError("Invalid MCU name.")

        # NEW DYNAMIC PIN CONTROL
        # Example Payload: {"mcu_name": "mcu_arm", "pin": "PA0", "mode": "pwm", "value": 128}
        if 'pin' in msg and 'mode' in msg:
            executor.submit(robot_bridge.set_pin_state, mcu_name, msg['pin'], msg['mode'], msg.get('value', 0))
        
        # Keep existing legacy support if needed
        elif 'gpio' in msg:
            executor.submit(robot_bridge.set_gpio, mcu_name, msg['gpio'].get('state', False))
        
        return {'status': 'Command submitted.'}
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))

if __name__ == '__main__':
    uvicorn.run('middleware:app', host='0.0.0.0', port=8095)