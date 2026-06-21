#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define SS_PIN 5
#define RST_PIN 27

#define SDA_PIN 21
#define SCL_PIN 22

#define LCD_ADDRESS 0x27
#define LCD_COLUMNS 16
#define LCD_ROWS 2

LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);
MFRC522 rfid(SS_PIN, RST_PIN);

const char* WIFI_SSID = "NPITI-IoT";
const char* WIFI_PASSWORD = "NPITI-IoT";

const char* GOOGLE_SCRIPT_URL = "https://script.google.com/macros/s/AKfycbzGZqUycTxg7yhNn7iV7nr5aW-A0dJb1TOmoPBR6NwVPF7gf1aoOms8xnEbgBqFZC1w/exec";

// =======================
// ADAFRUIT IO MQTT
// =======================

const char* MQTT_BROKER = "io.adafruit.com";
const int MQTT_PORT = 1883;

const char* ADAFRUIT_USERNAME = "maria_santos_121";
const char* ADAFRUIT_KEY = "aio_kXok27Vh2dtJ04yQ2qf7G7DxKuMH";

const char* TOPIC_UID =
"maria_santos_121/feeds/uid-usuarios";

const char* TOPIC_NOME =
"maria_santos_121/feeds/nomes-usuarios";

const char* TOPIC_STATUS =
"maria_santos_121/feeds/status-usuarios";

const char* TOPIC_HORA =
"maria_santos_121/feeds/usuario-entradahora";

const char* TOPIC_DURACAO =
"maria_santos_121/feeds/usuario-duracao";

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

unsigned long lastMQTTReconnect = 0;
const unsigned long mqttReconnectInterval = 5000;

String lastUid = "";
unsigned long lastReadTime = 0;
const unsigned long messageDuration = 4000;

bool showingResultMessage = false;

struct User {
  bool success;
  bool found;
  bool active;

  String uid;
  String name;
  String error;

  String action;
  String hour;
  String duration;
  String alert;
};

void showWelcomeMessage() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Bem vindo!");
  lcd.setCursor(0, 1);
  lcd.print("Aproxime card");
}

void showConnectingWifiMessage() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Conectando");
  lcd.setCursor(0, 1);
  lcd.print("ao WiFi...");
}

void showWifiConnectedMessage() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi conectado");
  lcd.setCursor(0, 1);
  lcd.print("Sistema pronto");
}

void showConsultingMessage(const String& uid) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Consultando...");

  lcd.setCursor(0, 1);

  if (uid.length() <= LCD_COLUMNS) {
    lcd.print(uid);
  } else {
    lcd.print(uid.substring(0, LCD_COLUMNS));
  }
}

void showWelcomeUser(const String& name) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Bem vindo:");

  lcd.setCursor(0, 1);

  if (name.length() <= LCD_COLUMNS) {
    lcd.print(name);
  } else {
    lcd.print(name.substring(0, LCD_COLUMNS));
  }
}

void showNotFoundMessage() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Cartao nao");
  lcd.setCursor(0, 1);
  lcd.print("cadastrado");
}

void showInactiveMessage() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Cadastro");
  lcd.setCursor(0, 1);
  lcd.print("inativo");
}

void showErrorMessage(const String& message) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Erro consulta");

  lcd.setCursor(0, 1);

  if (message.length() == 0) {
    lcd.print("Sem detalhes");
  } else if (message.length() <= LCD_COLUMNS) {
    lcd.print(message);
  } else {
    lcd.print(message.substring(0, LCD_COLUMNS));
  }
}

void showWifiFailedMessage() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Falha WiFi");
  lcd.setCursor(0, 1);
  lcd.print("Verifique rede");
}

void connectWifi() {
  showConnectingWifiMessage();

  Serial.print("Connecting to WiFi");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;

  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    showWifiConnectedMessage();
    delay(1500);
  } else {
    Serial.println("WiFi connection failed");

    showWifiFailedMessage();
    delay(2000);
  }
}

String getUidAsString() {
  String uid = "";

  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) {
      uid += "0";
    }

    uid += String(rfid.uid.uidByte[i], HEX);

    if (i < rfid.uid.size - 1) {
      uid += " ";
    }
  }

  uid.toUpperCase();
  return uid;
}

void printCardInfoToSerial(const String& uid) {
  MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);

  Serial.println("--------------------");
  Serial.print("Card type: ");
  Serial.println(rfid.PICC_GetTypeName(piccType));
  Serial.print("UID: ");
  Serial.println(uid);
  Serial.println("--------------------");
}

String urlEncode(String value) {
  value.replace(" ", "%20");
  value.replace(":", "%3A");
  value.replace("-", "%2D");
  return value;
}

////////////////////////////////////////////////////////

User getUserByUid(String uid) {
  User user;

  user.success = false;
  user.found = false;
  user.active = false;

  user.uid = uid;
  user.name = "";
  user.error = "";

  user.action = "";
  user.hour = "";
  user.duration = "";
  user.alert = "";

  if (WiFi.status() != WL_CONNECTED) {
    user.error = "WiFi offline";
    Serial.println("WiFi disconnected");
    return user;
  }

  String url = String(GOOGLE_SCRIPT_URL) + "?uid=" + urlEncode(uid);

  Serial.println();
  Serial.println("===== Searching google sheet =====");
  Serial.print("URL: ");
  Serial.println(url);

  HTTPClient http;

  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  if (!http.begin(url)) {
    user.error = "HTTP begin";
    Serial.println("HTTP begin failed");
    return user;
  }

  int httpCode = http.GET();

  Serial.print("HTTP code: ");
  Serial.println(httpCode);

  if (httpCode <= 0) {
    String httpError = http.errorToString(httpCode);

    user.error = httpError;

    Serial.print("HTTP error: ");
    Serial.println(httpError);

    http.end();
    return user;
  }

  String payload = http.getString();
  http.end();

  payload.trim();

  Serial.println("Raw response:");
  Serial.println(payload);

  int jsonStart = payload.indexOf('{');
  int jsonEnd = payload.lastIndexOf('}');

  if (jsonStart < 0 || jsonEnd < 0 || jsonEnd <= jsonStart) {
    user.error = "Invalid JSON";
    Serial.println("No JSON object found in response");
    return user;
  }

  String jsonPayload = payload.substring(jsonStart, jsonEnd + 1);

  Serial.println("JSON:");
  Serial.println(jsonPayload);

  StaticJsonDocument<1024> doc;
  DeserializationError jsonError = deserializeJson(doc, jsonPayload);

  if (jsonError) {
    user.error = "JSON error";
    Serial.print("JSON parse error: ");
    Serial.println(jsonError.c_str());
    return user;
  }

  user.success = doc["success"] | false;

user.found =
    doc["found"] |
    doc["encontrado"] |
    false;

user.active =
    doc["ativo"] |
    false;

user.uid =
    doc["uid"] |
    uid;

user.name =
    doc["nome"] |
    "";

user.action =
    doc["acao"] |
    "";

user.hour =
    doc["hora"] |
    "";

user.duration =
    doc["duracao"] |
    "";

user.alert =
    doc["alerta"] |
    "";

if (doc["error"]) {
  user.error = doc["error"].as<String>();
} 

  Serial.println("===== DADOS RECEBIDOS =====");

  Serial.print("Nome: ");
  Serial.println(user.name);

  Serial.print("Acao: ");
  Serial.println(user.action);

  Serial.print("Hora: ");
  Serial.println(user.hour);

  Serial.print("Duracao: ");
  Serial.println(user.duration);

  Serial.println("===========================");
  Serial.println("Search finished");
  Serial.println("========================");

  return user;
}

void publishAttendance(
    String uid,
    String nome,
    String status,
    String hora,
    String duracao
) {

  if (!mqttClient.connected()) {
    return;
  }

  mqttClient.publish(TOPIC_UID,uid.c_str(),true);

  mqttClient.publish(TOPIC_NOME,nome.c_str(),true);

  mqttClient.publish(TOPIC_STATUS,status.c_str(),true);

  mqttClient.publish(TOPIC_HORA,hora.c_str(),true);

  mqttClient.publish(TOPIC_DURACAO,duracao.c_str(),true);

  Serial.println("Evento enviado ao Adafruit");
} 

void processCard(String uid) {
  printCardInfoToSerial(uid);

  showConsultingMessage(uid);

  User user = getUserByUid(uid);

  if (!user.success) {
    showErrorMessage(user.error);

    Serial.println("Query failed");
    Serial.print("Error: ");
    Serial.println(user.error);
  } else if (!user.found) {
    showNotFoundMessage();
    publishAttendance(
      user.uid,
      user.name,
      user.action,
      user.hour,
      user.duration
    );

    Serial.println("Card not registered");
    Serial.print("Unknown UID: ");
    Serial.println(uid);
  } else if (!user.active) {
    showInactiveMessage();
    publishAttendance(
       uid,
      user.name,
      "INATIVO",
      user.hour,
      user.duration
    );

    Serial.println("User found but inactive");
    Serial.print("Name: ");
    Serial.println(user.name);
  } else {
    showWelcomeUser(user.name);
    publishAttendance(
      user.uid,
      user.name,
      user.action,
      user.hour,
      user.duration
    );

    Serial.println("Attendance registered");
    Serial.print("Name: ");
    Serial.println(user.name);
    Serial.print("UID: ");
    Serial.println(user.uid);
  }

  lastUid = uid;
  lastReadTime = millis();
  showingResultMessage = true;
}

void connectMQTT() {

  if (mqttClient.connected()) {
    return;
  }

  Serial.println("Conectando MQTT...");

  if (
      mqttClient.connect(
        "ESP32RFID",
        ADAFRUIT_USERNAME,
        ADAFRUIT_KEY
      )
     )
  {
    Serial.println("MQTT conectado");
  }
  else
  {
    Serial.print("Falha MQTT. Estado: ");
    Serial.println(mqttClient.state());
  }
}

void setup() {
  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN);

  lcd.init();
  lcd.backlight();

  SPI.begin();
  rfid.PCD_Init();

  connectWifi();

  delay(1000);

  mqttClient.setServer(
  MQTT_BROKER,
  MQTT_PORT
);

connectMQTT();

  showWelcomeMessage();

  Serial.println("System started");
}

void loop() {
  if (!mqttClient.connected()) {

  unsigned long now = millis();

  if (now - lastMQTTReconnect > mqttReconnectInterval) {

    lastMQTTReconnect = now;

    connectMQTT();
  }
}

mqttClient.loop();
  
  if (showingResultMessage && millis() - lastReadTime >= messageDuration) {
    showWelcomeMessage();
    showingResultMessage = false;
    lastUid = "";
  }

  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }

  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  String uid = getUidAsString();

  if (uid != lastUid) {
    processCard(uid);
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}