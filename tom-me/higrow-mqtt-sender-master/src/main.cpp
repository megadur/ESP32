#include <Arduino.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <DHT_U.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "device_id.h"
#include "mqtt.h"
#include "wifi1.h"

const int DHT_PIN = 22;
const int DHT_TYPE = DHT11;
const int SOIL_PIN = 32;
const int LIGHT_PIN = 33;
const int INFO_LED_PIN = 16;

const String TOPIC_PREFIX = "higrow_plant_monitor/";
const String TOPIC_SUFFIX = "/state";
const int DEEP_SLEEP_SECONDS = 60 * 20;

const int WATER_MEASUREMENTS = 10;
const int WATER_MEASURE_INTERVAL_MS = 200;
// The following values were calibrated using a glass of water
const int NO_WATER = 3323;
const int FULL_WATER = 1389;

DHT_Unified dht(DHT_PIN, DHT_TYPE);

String device_id;
String client_id;
String topic;

WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);

void print_info() {
  Serial.println(F("------------------------------------------------------------"));
  Serial.print(F("Device id:      "));
  Serial.println(device_id);
  Serial.print(F("Mqtt broker:    "));
  Serial.print(MQTT_BROKER_HOST);
  Serial.print(F(":"));
  Serial.println(MQTT_BROKER_PORT);
  Serial.print(F("Mqtt topic:     "));
  Serial.println(topic);
  Serial.print(F("Mqtt client id: "));
  Serial.println(client_id);
  Serial.println(F("------------------------------------------------------------"));
}

void setup() {
  Serial.begin(115200);
  Serial.println("");
  Serial.println(F("Hello :)\nHigrow MQTT sender\nhttps://github.com/tom-mi/higrow-mqtt-sender"));

  pinMode(INFO_LED_PIN, OUTPUT);

  device_id = get_device_id();
  client_id = "higrow_" + device_id;
  topic = TOPIC_PREFIX + device_id + TOPIC_SUFFIX;

  print_info();

  connect_wifi();

  mqtt_client.setServer(MQTT_BROKER_HOST, String(MQTT_BROKER_PORT).toInt());

  dht.begin();

  digitalWrite(INFO_LED_PIN, LOW);
}

float read_sensor(int pin, int zero_value, int one_value) {
  int raw = analogRead(pin);
  float level = map(raw, zero_value, one_value, 0, 10000) / 10000.;
  return constrain(level, 0., 1.);
}

float read_water_smoothed() {
  float value = 0.;
  for (int i=0; i<WATER_MEASUREMENTS; i++) {
    value += read_sensor(SOIL_PIN, NO_WATER, FULL_WATER);
    delay(WATER_MEASURE_INTERVAL_MS);
  }
  return value / WATER_MEASUREMENTS;
}

float read_temperature() {
  sensors_event_t event;
  dht.temperature().getEvent(&event);
  return event.temperature;
}

float read_relative_humidity() {
  sensors_event_t event;
  dht.humidity().getEvent(&event);
  return event.relative_humidity;
}

void read_and_send_data() {
  DynamicJsonBuffer buffer;
  JsonObject& root = buffer.createObject();
  root["device_id"] = device_id;
  root["temperature_celsius"] = read_temperature();
  root["humidity_percent"] = read_relative_humidity();
  root["water"] = read_water_smoothed();
  root["light"] = read_sensor(LIGHT_PIN, 0, 4095);
  Serial.print(F("Sending payload: "));
  root.printTo(Serial);
  Serial.println("");
  if(reconnect_mqtt(mqtt_client, client_id.c_str())) {
    String payload;
    root.printTo(payload);
    if (mqtt_client.beginPublish(topic.c_str(), payload.length(), true)) {
      mqtt_client.print(payload);
      mqtt_client.endPublish();
      mqtt_client.disconnect();
      Serial.println("Success sending message");
    } else {
        Serial.println("Error sending message");
    }
  }
}

void loop() {
  read_and_send_data();
  Serial.println(F("Flushing wifi client"));
  wifi_client.flush();
  Serial.println(F("Disconnecting wifi"));
  WiFi.disconnect(true, false);
  delay(500);  // Wait a bit to ensure message sending is complete
  Serial.print(F("Going to sleep after "));
  Serial.print(millis());
  Serial.println(F("ms"));
  Serial.flush();
  ESP.deepSleep(DEEP_SLEEP_SECONDS * 1000000);
}
