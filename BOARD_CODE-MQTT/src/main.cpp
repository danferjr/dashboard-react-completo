/*
 * Copyright © Paulo Andrés Guerrero Ramírez
 *
 */

#include <Arduino.h>
#include <WiFi.h>
#include "ESPAsyncWebServer.h"
#include <PubSubClient.h>
#include "SPIFFS.h"
#include <ArduinoJson.h>

#define LIGHT_SENSOR_PIN 36 // ESP32 pin GIOP36 (ADC0)

//const char *WIFI_NAME = "KNIGHTS_FORTRESS";
//const char *WIFI_PASS = "covenant";
const char *WIFI_NAME = "nak";
const char *WIFI_PASS = "covenant";

AsyncWebServer server(80);
AsyncWebSocket webSocket("/ws");

// TODO: Finish MQTT configurations --- Check MQTT conect config... almost done MQTT
const char *MQTT_BROKER_ADRESS = "192.168.7.10";
const uint16_t MQTT_PORT = 1883;
const char *MQTT_CLIENT_NAME = "grupo3";

WiFiClient espHost;
PubSubClient espMQTTClient(espHost);
char *subsTopic = "esp32/id/grupo3";
String content = "";
String payload;

static int sensorVal = 0;

void notFound(AsyncWebServerRequest *request)
{
  request->send(404, "text/plain", "Not found");
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  if (type == WS_EVT_CONNECT)
  {
    // client connected
    Serial.println("Client connected....");
    Serial.printf("ws[%s][%u] connect\n", server->url(), client->id());
    client->ping();
  }
  else if (type == WS_EVT_DISCONNECT)
  {
    // client disconnected
    Serial.printf("ws[%s][%u] disconnect: %u\n", server->url(), client->id());
  }
  else if (type == WS_EVT_ERROR)
  {
    // error was received from the other end
    Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t *)arg), (char *)data);
  }
  else if (type == WS_EVT_PONG)
  {
    // pong message was received (in response to a ping request maybe)
    Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len) ? (char *)data : "");
  }
  else if (type == WS_EVT_DATA)
  {
    // do nothing as client is not sending message to server
    Serial.printf("ws[%s][%u] data received\n", server->url(), client->id());
  }
}

void fileSystemCheck()
{
  if (!SPIFFS.begin())
  {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
}

void enableWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_NAME, WIFI_PASS);
  if (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    Serial.printf("WiFi Failed!\n");
    return;
  }
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void webSocketInit()
{
  webSocket.onEvent(onEvent);
  server.addHandler(&webSocket);
}

void webServerInit()
{
  // Route for root index.html
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { 
              AsyncWebServerResponse* response = request->beginResponse(SPIFFS, "/index.html.gz", "text/html");
                response->addHeader("Content-Encoding", "gzip");
                response->addHeader("Cache-Control", "max-age=86400"); // 1 día
                request->send(response); });

  // Route for root index.css
  server.on("/assets/index-2613cd82.css", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              AsyncWebServerResponse* response = request->beginResponse(SPIFFS, "/assets/index-2613cd82.css.gz", "text/css");
              response->addHeader("Content-Encoding", "gzip");
              response->addHeader("Cache-Control", "max-age=86400"); // 1 día
              request->send(response); });

  // Route for root index.js
  server.on("/assets/index-9fbb2826.js", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              AsyncWebServerResponse* response = request->beginResponse(SPIFFS, "/assets/index-9fbb2826.js.gz", "application/javascript");
              response->addHeader("Content-Encoding","gzip");
              response->addHeader("Cache-Control","max-age=86400"); // 1 día
              request->send(response); });

  server.onNotFound(notFound);

  // Start the server
  server.begin();
}

void suscribeMqtt()
{
  espMQTTClient.subscribe("esp32/id/#");
}

void onMqttReceived(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Received on ");
  Serial.print(topic);
  Serial.print(": ");

  content = "";
  for (size_t i = 0; i < length; i++)
  {
    content.concat((char)payload[i]);
  }
  // Serial.print(content);
  // Serial.println();
  StaticJsonDocument<300> doc;
  DeserializationError error = deserializeJson(doc, content);
  if (error)
    return;
  serializeJsonPretty(doc, Serial);
  String json = "";
  serializeJson(doc, json);
  // webSocket.printfAll((json).c_str());
  //  unsigned long data = doc["value"];
  //  Serial.print("Millis:");

}

void enableMQTT()
{
  espMQTTClient.setServer(MQTT_BROKER_ADRESS, MQTT_PORT);
  suscribeMqtt();
  espMQTTClient.setCallback(onMqttReceived);
}

void connectMqtt()
{
  while (!espMQTTClient.connected())
  {
    Serial.print("Starting MQTT connection...");
    if (espMQTTClient.connect(MQTT_CLIENT_NAME))
    {
      suscribeMqtt();
    }
    else
    {
      Serial.print("Failed MQTT connection, rc=");
      Serial.print(espMQTTClient.state());
    }
  }
}

void publisMqtt(unsigned int data)
{
  payload = "";
  StaticJsonDocument<300> jsonDoc;
  // jsonDoc["device_id"] = "parzival";
  jsonDoc["type"] = "light";
  jsonDoc["value"] = data;
  serializeJson(jsonDoc, payload);
  espMQTTClient.publish(subsTopic, (char *)payload.c_str());
}

void handleMqtt()
{
  if (!espMQTTClient.connected())
  {
    connectMqtt();
  }
  espMQTTClient.loop();
}

void readLDR()
{
  // reads the input on analog pin (value between 0 and 4095)
  int analogValue = analogRead(LIGHT_SENSOR_PIN);

  if (analogValue != sensorVal)
  {
    sensorVal = analogValue;
    publisMqtt(sensorVal);
    webSocket.printfAll(std::to_string(sensorVal).c_str());
  }
}

void setup()
{

  Serial.begin(115200);
  Serial.println("Starting system...");

  // Begin LittleFS for ESP8266 or SPIFFS for ESP32
  fileSystemCheck();

  // Connect to WIFI
  enableWiFi();

  // attach AsyncWebSocket
  webSocketInit();

  // Serve files for Web Dashboard
  webServerInit();

  // Stablish conection to MQTT server
  enableMQTT();
}

void loop()
{
  // Read the LDR values continously
  readLDR();

  // MQTT
  handleMqtt();

  delay(5000);
}
