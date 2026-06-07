#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include <Arduino_JSON.h>
#include <SoftwareSerial.h>
#include <Wire.h>

SoftwareSerial mySerial(10, 9);
SemaphoreHandle_t xSerialMutex;
QueueHandle_t xSensorQueue;

struct SensorData {
    int analogVal1;
    int analogVal2;
    float i2cVal;
};

// Map names to pins
uint32_t getPinFromName(String name) {
    if (name == "PA0") return PA0;
    if (name == "PA1") return PA1;
    if (name == "PB8") return PB8;
    if (name == "PH7") return PH7;
    if (name == "PA9") return PA9;   // TX1
    if (name == "PA10") return PA10; // RX1
    return 0xFFFFFFFF; 
}

// Global UART pointer
HardwareSerial* dynamicSerial = nullptr;

// Task Prototypes
void vTaskSerialRX(void *pvParameters);
void vTaskAnalogRead(void *pvParameters);
void vTaskI2CRead(void *pvParameters);
void vTaskSerialTX(void *pvParameters);
void vTaskUARTRead(void *pvParameters);

void setup() {
    mySerial.begin(115200);
    Wire.begin();
    xSerialMutex = xSemaphoreCreateMutex();
    xSensorQueue = xQueueCreate(5, sizeof(SensorData));
    
    xTaskCreate(vTaskSerialRX, "Serial_RX", 1024, NULL, 4, NULL);
    xTaskCreate(vTaskAnalogRead, "Analog_Read", 256, NULL, 3, NULL);
    xTaskCreate(vTaskI2CRead, "I2C_Read", 256, NULL, 3, NULL);
    xTaskCreate(vTaskSerialTX, "Serial_TX", 512, NULL, 1, NULL);
    xTaskCreate(vTaskUARTRead, "UART_Read", 512, NULL, 2, NULL); // Critical for UART telemetry
    
    vTaskStartScheduler();
}

void loop() {}

// 1. Serial RX Task
void vTaskSerialRX(void *pvParameters) {
    String inputString = "";
    for (;;) {
        if (mySerial.available() > 0) {
            char inChar = (char)mySerial.read();
            if (inChar == '\n') {
                JSONVar myObject = JSON.parse(inputString);
                if (JSON.typeof(myObject) != "undefined" && myObject.hasOwnProperty("cmd")) {
                    String cmd = (String)myObject["cmd"];
                    
                    if (cmd == "set_pin") {
                        uint32_t pin = getPinFromName((String)myObject["pin"]);
                        String mode = (String)myObject["mode"];
                        int val = (int)myObject["value"];
                        if (pin != 0xFFFFFFFF) {
                            if (mode == "gpio") { pinMode(pin, OUTPUT); digitalWrite(pin, val ? HIGH : LOW); }
                            else if (mode == "pwm") { pinMode(pin, OUTPUT); analogWrite(pin, val); }
                        }
                    } 
                    else if (cmd == "init_uart") {
                        long baud = (long)myObject["baud"];
                        if (dynamicSerial) delete dynamicSerial;
                        dynamicSerial = new HardwareSerial(PA10, PA9);
                        dynamicSerial->begin(baud);
                    }
                    else if (cmd == "uart_write" && dynamicSerial) {
                        dynamicSerial->print((String)myObject["data"]);
                    }
                }
                inputString = "";
            } else if (inChar != '\r') { inputString += inChar; }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// 2. UART Read Task
void vTaskUARTRead(void *pvParameters) {
    SensorData d;
    for (;;) {
        if (dynamicSerial && dynamicSerial->available() > 0) {
            String incoming = dynamicSerial->readStringUntil('\n');
            d.analogVal1 = incoming.toInt();
            d.analogVal2 = 0;
            d.i2cVal = 0.0;
            xQueueSend(xSensorQueue, &d, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// 3. Analog Read Task
void vTaskAnalogRead(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    SensorData d;
    for (;;) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(50));
        d.analogVal1 = analogRead(PA0);
        d.analogVal2 = analogRead(PA1);
        xQueueSend(xSensorQueue, &d, 0);
    }
}

// 4. I2C Read Task
void vTaskI2CRead(void *pvParameters) {
    for (;;) { vTaskDelay(pdMS_TO_TICKS(100)); }
}

// 5. Serial TX Task
void vTaskSerialTX(void *pvParameters) {
    SensorData d;
    for (;;) {
        if (xQueueReceive(xSensorQueue, &d, portMAX_DELAY) == pdPASS) {
            String s = "{\"s1\":" + String(d.analogVal1) + ",\"s2\":" + String(d.analogVal2) + ",\"i2c\":" + String(d.i2cVal) + "}";
            if (xSemaphoreTake(xSerialMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                mySerial.println(s);
                xSemaphoreGive(xSerialMutex);
            }
        }
    }
}