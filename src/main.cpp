#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <MFRC522.h>

// MAC address (modifica se necessario)
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
// Static fallback IP se DHCP fallisce (modifica secondo la tua rete)
IPAddress ip(192, 168, 188, 177); // Aggiornato alla tua subnet probabile
EthernetServer server(80);

EthernetClient ethClient;
PubSubClient mqttClient(ethClient);

// RFID RC522 Configuration
#define SS_PIN 9  // SDA pin for RC522
#define RST_PIN 8 // Reset pin for RC522
MFRC522 mfrc522(SS_PIN, RST_PIN);

// Struttura per salvare le impostazioni MQTT in EEPROM
struct MqttConfig {
  unsigned long magic; // Numero magico per verificare validità dati
  char server[64];
  char user[32];
  char pass[32];
  char topic[64];
  int port;
};
MqttConfig mqttConfig;
const unsigned long CONFIG_MAGIC = 0xCAFEBABF; // Firma di validità (cambiata per reset struttura)
const int EEPROM_ADDR = 0;


// Variabili per Messaggio Scorrevole
char lastMqttMessage[32] = "";
unsigned long lastReconnectAttempt = 0;
byte webMsgCode = 0;
char lastRfidUid[16] = "";
unsigned long lastRfidTime = 0;

void writeConfig() {
  mqttConfig.magic = CONFIG_MAGIC;
  EEPROM.put(EEPROM_ADDR, mqttConfig);
}

void readConfig() {
  EEPROM.get(EEPROM_ADDR, mqttConfig);
  if (mqttConfig.magic != CONFIG_MAGIC) {
    memset(&mqttConfig, 0, sizeof(mqttConfig));
    mqttConfig.magic = CONFIG_MAGIC;
    mqttConfig.port = 1883;
    strcpy(mqttConfig.topic, "casa/sensor/potenza_totale");
  }
}

void connectToMqtt() {
  if (strlen(mqttConfig.server) == 0) return;
  mqttClient.disconnect();
  mqttClient.setServer(mqttConfig.server, mqttConfig.port);
  const char* user = (strlen(mqttConfig.user) > 0) ? mqttConfig.user : NULL;
  const char* pass = (strlen(mqttConfig.pass) > 0) ? mqttConfig.pass : NULL;
  if (mqttClient.connect("ArduinoDisplayClient", user, pass)) {
    mqttClient.subscribe(mqttConfig.topic);
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (length >= sizeof(lastMqttMessage)) length = sizeof(lastMqttMessage) - 1;
  memcpy(lastMqttMessage, payload, length);
  lastMqttMessage[length] = '\0';
  Serial.print(F("MQTT Recv: "));
  Serial.println(lastMqttMessage);
}

// RFID Functions
void handleRfidCard() {
  // Verifica se c'è una nuova card
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }

  // Verifica se il UID è stato letto
  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  // Costruisce la stringa dell'UID in formato esadecimale
  String uidStr = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) {
      uidStr += "0";
    }
    uidStr += String(mfrc522.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();

  // Copia l'UID per visualizzazione web
  uidStr.toCharArray(lastRfidUid, sizeof(lastRfidUid));
  lastRfidTime = millis();

  if (mqttClient.connected()) {
    mqttClient.publish("arduino/rfid", uidStr.c_str());
  }

  // Ferma la lettura
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

void setup() {
  Serial.begin(9600);
  delay(1000);
  unsigned long dhcpStart = millis();
  bool dhcpOk = false;
  while (millis() - dhcpStart < 10000) {
    if (Ethernet.begin(mac) != 0) {
      dhcpOk = true;
      break;
    }
    delay(2000);
  }
  if (!dhcpOk) Ethernet.begin(mac, ip);
  readConfig();
  SPI.begin();
  mfrc522.PCD_Init(SS_PIN, RST_PIN);
  mqttClient.setCallback(mqttCallback);
  connectToMqtt();
  server.begin();
}

void handleClient() {
  EthernetClient client = server.available();
  if (!client) return;

  // Attendi che arrivino i dati della richiesta
  delay(5);

  String requestLine = client.readStringUntil('\r');
  client.read();
  while (client.available()) {
    String headerLine = client.readStringUntil('\n');
    if (headerLine == "\r") break;
  }
  webMsgCode = 0; // Resetta messaggio

  // Endpoint AJAX per leggere solo il valore MQTT
  if (requestLine.indexOf("GET /read-mqtt") == 0) {
    client.println(F("HTTP/1.1 200 OK"));
    client.println(F("Content-Type: text/plain"));
    client.println(F("Connection: close"));
    client.println();
    client.print(lastMqttMessage[0] != '\0' ? lastMqttMessage : "Nessun messaggio ancora ricevuto");
    delay(1);
    client.stop();
    return;
  }

  // /mqtt-test sends a test message
  if (requestLine.indexOf("GET /mqtt-test") == 0) {
    if (mqttClient.connected()) {
      mqttClient.publish("casa/sensor/arduino", "0");
      Serial.println(F("MQTT Test Sent"));
    }
    client.println(F("HTTP/1.1 200 OK"));
    client.println(F("Content-Type: text/plain"));
    client.println(F("Connection: close"));
    client.println();
    client.println(F("OK"));
    delay(1);
    client.stop();
    return;
  }

  // /mqtt?server=...&user=...&pass=...
  if (requestLine.indexOf("GET /mqtt") == 0) {
    String srv = "";
    String usr = "";
    String pw = "";
    String portStr = "";
    String topicStr = "";

    int start = requestLine.indexOf("server=");
    if (start != -1) {
      start += 7;
      int end = requestLine.indexOf("&", start);
      if (end == -1) end = requestLine.indexOf(" ", start);
      srv = requestLine.substring(start, end);
      srv.replace("%20", " ");
      srv.replace("+", " ");
    }

    start = requestLine.indexOf("user=");
    if (start != -1) {
      start += 5;
      int end = requestLine.indexOf("&", start);
      if (end == -1) end = requestLine.indexOf(" ", start);
      usr = requestLine.substring(start, end);
      usr.replace("%20", " ");
      usr.replace("+", " ");
    }

    start = requestLine.indexOf("pass=");
    if (start != -1) {
      start += 5;
      int end = requestLine.indexOf("&", start);
      if (end == -1) end = requestLine.indexOf(" ", start);
      pw = requestLine.substring(start, end);
      pw.replace("%20", " ");
      pw.replace("+", " ");
    }

    start = requestLine.indexOf("port=");
    if (start != -1) {
      start += 5;
      int end = requestLine.indexOf("&", start);
      if (end == -1) end = requestLine.indexOf(" ", start);
      portStr = requestLine.substring(start, end);
    }

    start = requestLine.indexOf("topic=");
    if (start != -1) {
      start += 6;
      int end = requestLine.indexOf("&", start);
      if (end == -1) end = requestLine.indexOf(" ", start);
      topicStr = requestLine.substring(start, end);
      topicStr.replace("%20", " ");
      topicStr.replace("+", " ");
      topicStr.replace("%2F", "/");
      topicStr.replace("%2f", "/");
    }

    if (srv.length() > 0) {
      srv.toCharArray(mqttConfig.server, sizeof(mqttConfig.server));
      usr.toCharArray(mqttConfig.user, sizeof(mqttConfig.user));
      pw.toCharArray(mqttConfig.pass, sizeof(mqttConfig.pass));
      if (topicStr.length() > 0) topicStr.toCharArray(mqttConfig.topic, sizeof(mqttConfig.topic));
      int port = 1883;
      if (portStr.length() > 0) port = portStr.toInt();
      mqttConfig.port = port;
      writeConfig();
      connectToMqtt();
      webMsgCode = 1;
    } else {
      webMsgCode = 2;
    }
  }

  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html"));
  client.println(F("Connection: close\n"));
  client.println(F("<html><body><h2>Setup</h2>"));
  if (webMsgCode == 1) client.println(F("<p style=color:green>Saved</p>"));
  if (webMsgCode == 2) client.println(F("<p style=color:red>No server</p>"));
  client.println(F("<form action=/mqtt method=get>"));
  client.print(F("<input name=server value=\"")); client.print(mqttConfig.server); client.println(F("\" placeholder=Server>"));
  client.print(F("<input name=user value=\"")); client.print(mqttConfig.user); client.println(F("\" placeholder=User>"));
  client.println(F("<input name=pass type=password placeholder=Pass>"));
  client.print(F("<input name=port value=")); client.print(mqttConfig.port); client.println(F(" size=4>"));
  client.println(F("<input type=submit></form>"));
  client.print(F("<p>Status: "));
  client.println(mqttClient.connected() ? F("OK</p>") : F("Disc</p>"));
  client.print(F("<p>UID: "));
  if (lastRfidUid[0]) {
    client.print(lastRfidUid);
  } else {
    client.print(F("---"));
  }
  client.println(F("</p><a href=/mqtt-test><button>Test</button></a></body></html>"));

  delay(1);
  client.stop();
}

void loop() {
  // Gestisci eventuali client HTTP
  handleClient();

  // Rinnova il lease DHCP se necessario
  Ethernet.maintain();

  // Gestione lettura RFID
  handleRfidCard();

  // Gestione riconnessione MQTT
  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > 5000) { // Riprova ogni 5 secondi
      lastReconnectAttempt = now;
      if (strlen(mqttConfig.server) > 0) {
        connectToMqtt();
      }
    }
  } else {
    mqttClient.loop();
  }
}
