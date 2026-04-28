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

unsigned long lastAuraFetch = 0;
unsigned long auraInterval = 0;

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

  WiFi.mode(WIFI_STA);

  pinMode(rPin, OUTPUT);
  pinMode(gPin, OUTPUT);
  pinMode(bPin, OUTPUT);

  Serial.println("\nESP32 is ready!");
  delay(100);
}

void loop()
{
  // put your main code here, to run repeatedly:
  analogWrite(rPin, r);
  analogWrite(gPin, g);
  analogWrite(bPin, b);

  if (WiFi.status() == WL_CONNECTED)
  {
    setAura();
    return;
  }

  preferences.begin("credentials", true); // opens storage in readonly mode
  String ssid = preferences.getString("ssid", "");
  String pass = preferences.getString("pass", "");
  preferences.end();

  if (!ssid.isEmpty() && tryInitWiFi(ssid, pass))
  {
    setAura();
    return;
  }

  setWiFiCredentials();
}

// put function definitions here:
bool tryInitWiFi(const String &ssid, const String &password, wifi_mode_t mode)
{
  WiFi.mode(mode);
  WiFi.begin(ssid, password);
  Serial.print("\nConnecting to WiFi ..");

  int retries = 0;
  while (retries < 60) // x sekund se probava povezat
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