import serial
import json
import threading
import queue

# Configuration
# Ensure this matches the port connected to your STM32
SERIAL_PORT = '/dev/ttyUSB0' 
BAUD_RATE = 115200

data_queue = queue.Queue()

def serial_reader(ser):
    """Continuously reads from serial and puts valid JSON into the queue."""
    while True:
        try:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8').strip()
                if line:
                    # Only put valid JSON telemetry data into the queue
                    data = json.loads(line)
                    data_queue.put(data)
        except (json.JSONDecodeError, UnicodeDecodeError):
            continue 

def send_led_command(ser, state):
    """Sends a JSON command to the STM32 to toggle the LED."""
    # Ensure state is boolean for the JSON command
    command = {"led": bool(state)}
    # Convert dict to JSON string and add newline character
    # The newline is crucial for the STM32 to know the command has ended
    ser.write((json.dumps(command) + "\n").encode('utf-8'))
    print(f"Sent command: {command}")

def main():
    # Initialize Serial connection
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    
    # Start the reader thread to process telemetry in background
    reader_thread = threading.Thread(target=serial_reader, args=(ser,), daemon=True)
    reader_thread.start()

    print("Middleware started. Type '1' for LED ON, '0' for LED OFF.")

    try:
        while True:
            # 1. Process incoming telemetry from the queue
            if not data_queue.empty():
                payload = data_queue.get()
                print(f"Received Telemetry: S1={payload.get('s1')}, S2={payload.get('s2')}")
            
            # 2. Check for user input to send LED command
            # Using input() here is blocking, but for simple command-line testing it works.
            user_input = input("Command > ")
            if user_input == '1':
                send_led_command(ser, True)
            elif user_input == '0':
                send_led_command(ser, False)
                
    except KeyboardInterrupt:
        print("\nClosing connection...")
        ser.close()

if __name__ == "__main__":
    main()
