#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include <Arduino_JSON.h> // Switched library
#include <SoftwareSerial.h>

SoftwareSerial mySerial(10, 9); // RX, TX
// Semaphores and Queues
SemaphoreHandle_t xSerialMutex;
QueueHandle_t xSensorQueue;

// Struct for sensor data
struct SensorData {
    int analogVal1;
    int analogVal2;
};

// Pin definitions
const int ANALOG_PIN_1 = PA0;
const int ANALOG_PIN_2 = PA1;
const int CONTROL_LED = PH7;

// Task Prototypes
void vTaskSerialRX(void *pvParameters);
void vTaskAnalogRead(void *pvParameters);
void vTaskSerialTX(void *pvParameters);

void setup() {
    //Serial.begin(115200);
    mySerial.begin(115200);
    pinMode(CONTROL_LED, OUTPUT);
    pinMode(ANALOG_PIN_1, INPUT_ANALOG);
    pinMode(ANALOG_PIN_2, INPUT_ANALOG);
    // Create Mutex and Queue
    xSerialMutex = xSemaphoreCreateMutex();
    xSensorQueue = xQueueCreate(5, sizeof(SensorData));
   
    if (xSerialMutex != NULL && xSensorQueue != NULL) {
        // Create Tasks
       // In setup():
       xTaskCreate(vTaskSerialRX, "Serial_RX", 512, NULL, 4, NULL); // Priority 4
       xTaskCreate(vTaskAnalogRead, "Analog_Read", 256, NULL, 3, NULL);
       xTaskCreate(vTaskSerialTX, "Serial_TX", 512, NULL, 1, NULL); // Priority 1

        vTaskStartScheduler();
    }
}

void loop() {
    // Empty when using FreeRTOS
}

// 1. Serial RX Task: Parses incoming JSON commands using Arduino_JSON
void vTaskSerialRX(void *pvParameters) {
    String inputString = "";
    inputString.reserve(64); // Keep this to prevent heap fragmentation

    for (;;) {
        // Only attempt to read if serial data is waiting
        if (mySerial.available() > 0) {
            // Take mutex for the duration of the read process
            if (xSemaphoreTake(xSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                
                while (mySerial.available() > 0) {
                    char inChar = (char)mySerial.read();
                    
                    if (inChar == '\n') {
                        // Safety: Only parse if the string isn't empty
                        if (inputString.length() > 0) {
                            JSONVar myObject = JSON.parse(inputString);
                            
                            // Check for validity AND existence of the "led" key
                            if (JSON.typeof(myObject) != "undefined" && myObject.hasOwnProperty("led")) {
                                // Apply Active-Low logic (PH7: LOW=ON, HIGH=OFF)
                                bool targetState = (bool)myObject["led"];
                                digitalWrite(CONTROL_LED, targetState ? LOW : HIGH);
                            }
                        }
                        inputString = ""; // Reset buffer after processing
                    } else if (inChar != '\r') {
                        inputString += inChar; // Build command string
                    }
                }
                xSemaphoreGive(xSerialMutex);
            }
        }
        // Yield to let other FreeRTOS tasks (TX/Analog) execute
        vTaskDelay(pdMS_TO_TICKS(5)); 
    }
}

// 2. Analog Read Task: High priority deterministic sampling
void vTaskAnalogRead(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(50); // Sample every 50ms (20Hz)
    SensorData currentData;

    for (;;) {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        currentData.analogVal1 = analogRead(ANALOG_PIN_1);
        currentData.analogVal2 = analogRead(ANALOG_PIN_2);

        // Send to queue, overwrite if full to prioritize fresh metrics
        xQueueSend(xSensorQueue, &currentData, 0);
    }
}

// 3. Serial TX Task: Serializes telemetry using Arduino_JSON and streams to Python
// 3. Serial TX Task: Serializes telemetry using Arduino_JSON and streams to Python
// 3. Serial TX Task: Manually formatted JSON telemetry
void vTaskSerialTX(void *pvParameters) {
    SensorData txData;

    for (;;) {
        // Block until data is available in the queue
        if (xQueueReceive(xSensorQueue, &txData, portMAX_DELAY) == pdPASS) {
            
            // Manually build the JSON string to ensure stability and performance
            // Format: {"s1":123,"s2":456}
            String jsonString = "{\"s1\":" + String(txData.analogVal1) + 
                                ",\"s2\":" + String(txData.analogVal2) + "}";

            // Take mutex before writing to shared Serial resource
            if (xSemaphoreTake(xSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                mySerial.println(jsonString); // Sends the single JSON line
                xSemaphoreGive(xSerialMutex);
            }
        }
    }
}