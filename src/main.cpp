// main.cpp

#include <Arduino.h>

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <ArduinoJson.h>

#include "index_html.h"

// put function declarations here:
bool tryInitWiFi(const String &, const String &, wifi_mode_t mode = WIFI_STA);
void setAura();
void setWiFiCredentials();
void handleNetworkScan();
void handleNetworkConnect();
void handleRoot();

Preferences preferences;
WebServer server(80);

const String auraEndpoint = "https://api.nikjavor.si/v1/aura";
const unsigned long auraFetchInterval = 15UL * 60UL * 1000UL; // after successful fetch
const unsigned long auraRetryInterval = 30UL * 1000UL;        // after failed fetch
unsigned long auraInterval = 0;                               // either auraFetchInterval or auraRetryInterval
unsigned long lastAuraFetch = 0;

const unsigned long wifiCheckInterval = 30UL * 1000UL;
unsigned long lastWifiCheck = 0;

const unsigned long statusBlinkInterval = 500UL;
unsigned long lastStatusBlink = 0;
bool statusLedOn = false;

bool btnDown = false;
bool longPressHandeled = false;
unsigned long btnDownStart = 0;
const unsigned long longPressInterval = 2UL * 1000UL;

int btnPin = 18;
int statusPin = 19;
int rPin = 25;
int gPin = 26;
int bPin = 27;

int r = 0;
int g = 0;
int b = 0;

void setup()
{
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println("ESP32 setup start.");

  pinMode(btnPin, INPUT_PULLUP);
  pinMode(statusPin, OUTPUT);
  pinMode(rPin, OUTPUT);
  pinMode(gPin, OUTPUT);
  pinMode(bPin, OUTPUT);

  analogWrite(rPin, 0);
  analogWrite(gPin, 0);
  analogWrite(bPin, 0);

  WiFi.mode(WIFI_STA);

  // Check for saved WiFi credentials and try to connect.
  preferences.begin("credentials", true); // opens storage in readonly mode
  String ssid = preferences.getString("ssid", "");
  String pass = preferences.getString("pass", "");
  preferences.end();

  if (!ssid.isEmpty())
    tryInitWiFi(ssid, pass);

  Serial.println("\nESP32 is ready!");
  delay(100);
}

void loop()
{
  // put your main code here, to run repeatedly:
  analogWrite(rPin, r);
  analogWrite(gPin, g);
  analogWrite(bPin, b);

  unsigned long currentMillis = millis();

  if (digitalRead(btnPin) == LOW)
  {
    if (!btnDown) // if it wasnt pressed previous loop
    {
      btnDownStart = currentMillis;
      btnDown = true;
    }

    if (!longPressHandeled && (currentMillis - btnDownStart > longPressInterval))
    {
      longPressHandeled = true;
      WiFi.disconnect();
      setWiFiCredentials();
    }
  }
  else
  {
    btnDown = false;
    longPressHandeled = false;
  }

  if ((WiFi.status() != WL_CONNECTED) && (lastWifiCheck == 0 || currentMillis - lastWifiCheck > wifiCheckInterval))
  {
    lastWifiCheck = currentMillis;

    digitalWrite(statusPin, HIGH);
    statusLedOn = true;

    Serial.println("Reconnecting to WiFi...");
    WiFi.disconnect();
    WiFi.reconnect();
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    digitalWrite(statusPin, LOW);
    statusLedOn = false;

    setAura();
    analogWrite(rPin, r);
    analogWrite(gPin, g);
    analogWrite(bPin, b);
    return;
  }
}

// put function definitions here:
bool tryInitWiFi(const String &ssid, const String &password, wifi_mode_t mode)
{
  WiFi.mode(mode);
  WiFi.begin(ssid, password);
  Serial.print("\nConnecting to WiFi ..");

  int retries = 0;
  while (retries < 30) // x sekund se probava povezat
  {
    wl_status_t status = WiFi.status();

    if (status == WL_CONNECTED)
    {
      Serial.println("\nWiFi connected successfully");
      Serial.print("Local IP: ");
      Serial.println(WiFi.localIP());
      return true;
    }

    if (status == WL_CONNECT_FAILED)
    {
      Serial.println("\nWiFi connection failed (wrong password?)");
      WiFi.disconnect();
      delay(100);
      return false;
    }

    Serial.print('.');
    delay(1000);
    retries++;
  }

  Serial.println("\nWiFi connection timeout");
  WiFi.disconnect();
  delay(100);
  return false;
}

void setWiFiCredentials()
{
  // will switch to ap mode or ap_sta mode
  // to scan available networks and open html page
  // where user will be able to connect to wifi
  // with their phone through browser

  WiFi.mode(WIFI_AP_STA);

  IPAddress local_ip(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);

  WiFi.softAPConfig(local_ip, gateway, subnet);
  WiFi.softAP("AURA_WIFI", "setup1234");

  Serial.println("\nAccess Point Started");
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/networks", HTTP_POST, handleNetworkScan);
  server.on("/connect", HTTP_POST, handleNetworkConnect);

  server.begin();

  while (WiFi.status() != WL_CONNECTED)
  {
    unsigned long currentMillis = millis();
    if (currentMillis - lastStatusBlink > statusBlinkInterval)
    {
      lastStatusBlink = currentMillis;

      statusLedOn = !statusLedOn;
      digitalWrite(statusPin, statusLedOn ? HIGH : LOW);
    }

    server.handleClient();
    delay(2);
  }

  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
}

void handleNetworkScan()
{
  Serial.println("\nNetwork scan start");
  int n = WiFi.scanNetworks();

  JsonDocument doc;
  JsonArray networks = doc.to<JsonArray>();

  if (n == 0)
  {
    Serial.println("No networks found");
  }
  else
  {
    Serial.print("Found ");
    Serial.print(n);
    Serial.println(" networks");
    for (int i = 0; i < n; i++)
    {
      Serial.print(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "*" : "");
      Serial.print(WiFi.SSID());
      Serial.print(" - ");
      Serial.println(WiFi.RSSI());
      JsonObject entry = networks.add<JsonObject>();
      entry["ssid"] = WiFi.SSID(i);
      entry["protected"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
  }

  String jsonString;
  serializeJson(doc, jsonString);

  WiFi.scanDelete();

  server.send(200, "application/json", jsonString);
}

void handleNetworkConnect()
{
  String body = server.arg("plain");

  Serial.println("Received body:");
  Serial.println(body);

  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);

  String response = "";

  if (error)
  {
    doc.clear();
    doc["success"] = false;

    serializeJson(doc, response);
    server.send(400, "application/json", response);
    return;
  }

  String ssid = doc["ssid"] | "";
  String pass = doc["pass"] | "";

  if (ssid.isEmpty())
  {
    doc.clear();
    doc["success"] = false;
    doc["error"] = "Missing SSID";

    serializeJson(doc, response);
    server.send(400, "application/json", response);
    return;
  }

  Serial.println(ssid);

  bool result = tryInitWiFi(ssid, pass, WIFI_AP_STA);

  if (result)
  {
    doc.clear();
    doc["success"] = true;

    serializeJson(doc, response);
    server.send(200, "application/json", response);

    preferences.begin("credentials", false); // opens storage in read&write mode
    preferences.putString("ssid", ssid);
    preferences.putString("pass", pass);
    preferences.end();

    return;
  }

  doc.clear();
  doc["success"] = false;

  serializeJson(doc, response);
  server.send(400, "application/json", response);
}

void handleRoot()
{
  server.send_P(200, "text/html", html);
}

void setAura()
{
  unsigned long currentMillis = millis();

  if (lastAuraFetch != 0 && currentMillis - lastAuraFetch < auraInterval)
    return;

  auraInterval = auraRetryInterval;
  lastAuraFetch = currentMillis;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(5 * 1000);

  if (!http.begin(client, auraEndpoint))
  {
    Serial.println("\nHTTP begin failed");
    return;
  }

  int responseCode = http.GET();

  if (responseCode != HTTP_CODE_OK)
  {
    Serial.print("Aura request failed. Code: ");
    Serial.println(responseCode);

    http.end();
    return;
  }

  String response = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, response);

  if (error)
  {
    Serial.print("\ndeserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  int newR = doc["r"] | -1;
  int newG = doc["g"] | -1;
  int newB = doc["b"] | -1;

  if (newR < 0 || newR > 255 || newG < 0 || newG > 255 || newB < 0 || newB > 255)
  {
    Serial.println("Invalid RGB values received");
    return;
  }

  r = newR;
  g = newG;
  b = newB;

  auraInterval = auraFetchInterval;

  Serial.print("Aura updated: (");
  Serial.print(r);
  Serial.print(", ");
  Serial.print(g);
  Serial.print(", ");
  Serial.print(b);
  Serial.println(")");
}