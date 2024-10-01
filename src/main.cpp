#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_AHTX0.h>
#include <Fonts/FreeSans9pt7b.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h> // Додано для OTA

// Константи для Wi-Fi
const char* ssid = "aonline3g";
const char* password = "1qaz2wsx3edc";

// Константи для MQTT
const char* MQTT_BROKER = "greenhouse.net.ua";
const char* MQTT_TOPIC_TEMP = "aparts/temp_in";
const char* MQTT_TOPIC_HUMIDITY = "aparts/humidity_in";
const char* MQTT_USER = "mqtt";
const char* MQTT_PASSWORD = "qwerty";

// Клас для роботи з Wi-Fi
class WiFiConnection {
private:
    unsigned long previousMillis;  // Для перевірки підключення
    const unsigned long interval = 10000;   // Інтервал перевірки підключення (10 секунд)

public:
    // Конструктор
    WiFiConnection() : previousMillis(0) {}

    // Метод для підключення до Wi-Fi
    void connect() {
        Serial.print("Підключення до мережі Wi-Fi: ");
        Serial.println(ssid);

        WiFi.begin(ssid, password);
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);  // Очікуємо підключення
            Serial.print(".");
        }

        Serial.println();
        Serial.println("Підключено до Wi-Fi!");
        Serial.print("IP адреса: ");
        Serial.println(WiFi.localIP());
    }

    // Метод для перевірки підключення та перепідключення при втраті з'єднання
    void checkConnection() {
        unsigned long currentMillis = millis();
        
        // Перевіряємо підключення кожні 10 секунд
        if (currentMillis - previousMillis >= interval) {
            previousMillis = currentMillis;

            // Якщо не підключено до Wi-Fi, спробуємо знову підключитися
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("З'єднання втрачено. Спроба перепідключитися...");
                connect();
            } else {
                Serial.println("Wi-Fi підключення активне.");
            }
        }
    }
};

// Клас для роботи з сенсором AHT20
class AHT20Sensor {
private:
    Adafruit_AHTX0 aht;
    float smoothedTemperature; // Згладжена температура
    float smoothedHumidity;    // Згладжена вологість
    bool initialized;          // Прапорець для перевірки ініціалізації
    unsigned long previousMillis;        // Для зберігання попереднього часу
    const long interval = 2000;          // Інтервал зчитування даних з сенсора (2 секунди)

public:
    // Конструктор
    AHT20Sensor() : smoothedTemperature(0.0), smoothedHumidity(0.0), initialized(false), previousMillis(0) {}

    // Метод для ініціалізації сенсора
    bool begin() {
        if (aht.begin()) {
            Serial.println("AHT20 успішно ініціалізовано!");
            return true;
        } else {
            Serial.println("Помилка ініціалізації AHT20.");
            return false;
        }
    }

    // Метод для зчитування даних з сенсора
    void readData() {
        sensors_event_t humidity_event, temp_event;
        aht.getEvent(&humidity_event, &temp_event);

        // Ініціалізація згладжених значень при першому зчитуванні
        if (!initialized) {
            smoothedTemperature = temp_event.temperature; // ініціалізуємо згладжену температуру
            smoothedHumidity = humidity_event.relative_humidity; // ініціалізуємо згладжену вологість
            initialized = true; // Помічаємо, що ми вже ініціалізували
        } else {
            // Згладжування температури та вологості
            smoothedTemperature = 0.9 * smoothedTemperature + 0.1 * temp_event.temperature;
            smoothedHumidity = 0.9 * smoothedHumidity + 0.1 * humidity_event.relative_humidity;
        }
    }

    // Метод для виведення даних у серійний монітор
    void printData() {
        Serial.print("Температура: ");
        Serial.print(smoothedTemperature);
        Serial.println(" °C");

        Serial.print("Вологість: ");
        Serial.print(smoothedHumidity);
        Serial.println(" %RH");

        Serial.println("---------------------------");
    }

    // Метод для оновлення даних з певним інтервалом
    void update() {
        unsigned long currentMillis = millis();  // Отримуємо поточний час

        // Перевіряємо, чи минув заданий інтервал
        if (currentMillis - previousMillis >= interval) {
            // Оновлюємо час для наступного зчитування
            previousMillis = currentMillis;

            // Зчитуємо та виводимо дані
            readData();
            printData();
        }
    }

    // Геттери для отримання згладжених значень
    float getSmoothedTemperature() {
        return smoothedTemperature;
    }

    float getSmoothedHumidity() {
        return smoothedHumidity;
    }
};

// Клас для роботи з MQTT
class MQTTConnection {
private:
    WiFiClient espClient;        // Клієнт для Wi-Fi
    PubSubClient client;         // Об'єкт для роботи з MQTT
    const char* mqttBroker;      // Адреса брокера MQTT
    const char* mqttUser;        // Логін для MQTT
    const char* mqttPassword;    // Пароль для MQTT
    const char* tempTopic;       // Топік для температури
    const char* humidityTopic;   // Топік для вологості
    unsigned long previousMillis; // Час останньої передачі даних
    const unsigned long publishInterval = 2000; // Інтервал передачі даних до MQTT (2 секунди)
    unsigned long lastPublishTime; // Для зберігання часу останньої передачі

public:
    // Конструктор класу
    MQTTConnection(const char* broker, const char* user, const char* password, const char* tempTopic, const char* humidityTopic) 
      : mqttBroker(broker), mqttUser(user), mqttPassword(password), tempTopic(tempTopic), humidityTopic(humidityTopic), client(espClient), previousMillis(0), lastPublishTime(0) {}

    // Метод для підключення до MQTT-брокера
    void connect() {
        client.setServer(mqttBroker, 1883); // Встановлюємо брокера і порт

        // Підключення до брокера MQTT
        if (!client.connected()) {
            Serial.println("Підключення до MQTT...");
            if (client.connect("ESP8266Client", mqttUser, mqttPassword)) {
                Serial.println("Підключено до MQTT!");
            } else {
                Serial.print("Не вдалося підключитися до MQTT. Помилка: ");
                Serial.println(client.state());
                delay(2000); // Затримка перед наступною спробою підключення
            }
        }
    }

    // Метод для перевірки підключення до MQTT
    void checkConnection() {
        if (!client.connected()) {
            connect(); // Спробувати перепідключитися, якщо втрачено з'єднання
        }
        client.loop(); // Обробка вхідних повідомлень
    }

    // Метод для публікації даних у MQTT
    void publishData(float temperature, float humidity) {
        unsigned long currentMillis = millis(); // Отримуємо поточний час

        // Публікуємо дані з певним інтервалом
        if (currentMillis - lastPublishTime >= publishInterval) {
            lastPublishTime = currentMillis; // Оновлюємо час останньої публікації

            // Формуємо повідомлення та публікуємо
            String tempString = String(temperature);
            String humidityString = String(humidity);
            client.publish(tempTopic, tempString.c_str());
            client.publish(humidityTopic, humidityString.c_str());
            Serial.println("Дані опубліковані в MQTT.");
        }
    }
};

// Створюємо об'єкти класів
WiFiConnection wifi;
AHT20Sensor sensor;
MQTTConnection mqtt(MQTT_BROKER, MQTT_USER, MQTT_PASSWORD, MQTT_TOPIC_TEMP, MQTT_TOPIC_HUMIDITY);

// Функція налаштування
void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10); // Чекаємо на підключення серійного монітора

    // Підключення до Wi-Fi
    wifi.connect();

    // Ініціалізація сенсора
    if (!sensor.begin()) {
        while (1) delay(10); // Зупиняємо програму, якщо не вдалося ініціалізувати сенсор
    }

    // Підключення до MQTT
    mqtt.connect();

    // Налаштування OTA
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
            type = "flash";
        } else {
            type = "sketch";
        }
        Serial.println("Початок оновлення " + type);
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("\nЗавершено оновлення");
    });

    // Оновлення прогресу
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Прогрес: %u%%\r", (progress / (total / 100)));
    });

    // Обробка помилки
    ArduinoOTA.onError([](unsigned int error) {
        Serial.printf("Помилка [%u]: ", error);
        if (error == OTA_END_ERROR) {
            Serial.println("Відключення від живлення під час оновлення");
        }
    });
    ArduinoOTA.begin(); // Ініціалізація OTA
}

// Основний цикл
void loop() {
    // Оновлення OTA
    ArduinoOTA.handle(); // Обробка OTA

    // Перевірка з'єднання з Wi-Fi
    wifi.checkConnection();
    
    // Перевірка з'єднання з MQTT
    mqtt.checkConnection();
    
    // Оновлення даних з сенсора
    sensor.update();

    // Публікація даних у MQTT
    mqtt.publishData(sensor.getSmoothedTemperature(), sensor.getSmoothedHumidity());
}
