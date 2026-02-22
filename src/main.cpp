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

// Variabili per scrittura dati su RFID
char dataToWrite[32] = "";
byte writeMode = 0; // 0=off, 1=waiting to write

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
    Serial.println(F("Connected to MQTT"));
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
void writeDataToCard(String data) {
  // Prepara il buffer con i dati da scrivere (16 byte per blocco)
  byte dataBlock[16];
  memset(dataBlock, 0, 16);
  
  // Copia i dati nel buffer (max 15 caratteri)
  if (data.length() > 15) data = data.substring(0, 15);
  for (size_t i = 0; i < data.length(); i++) {
    dataBlock[i] = data[i];
  }
  
  // Ferma qualsiasi comunicazione precedente
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  delay(50);
  
  // Verifica se c'è di nuovo una carta (rilettura)
  if (!mfrc522.PICC_IsNewCardPresent()) {
    Serial.println(F("Card removed"));
    return;
  }
  
  if (!mfrc522.PICC_ReadCardSerial()) {
    Serial.println(F("Cannot read card again"));
    return;
  }
  
  // Usa la chiave di default FFFFFFFFFFFF
  MFRC522::MIFARE_Key key;
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;
  
  Serial.println(F("Auth attempt on block 1..."));
  
  // Autentica il blocco 1 (settore 0, blocco 1)
  MFRC522::StatusCode status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, 1, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Auth failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    mfrc522.PCD_StopCrypto1();
    mfrc522.PICC_HaltA();
    return;
  }
  
  Serial.println(F("Auth OK, writing to block 1..."));
  
  // Scrive nel blocco 1
  status = mfrc522.MIFARE_Write(1, dataBlock, 16);
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Write failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
  } else {
    Serial.println(F("Data written successfully"));
  }
  
  // Ferma la lettura e cripto
  mfrc522.PCD_StopCrypto1();
  mfrc522.PICC_HaltA();
}

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

  // Se è in modalità scrittura, scrivi i dati
  if (writeMode == 1 && dataToWrite[0] != '\0') {
    Serial.print(F("Writing to card: "));
    Serial.println(dataToWrite);
    writeDataToCard(String(dataToWrite));
    writeMode = 0;
    memset(dataToWrite, 0, sizeof(dataToWrite));
  } else {
    // Altrimenti pubblica l'UID
    if (mqttClient.connected()) {
      mqttClient.publish("arduino/rfid", uidStr.c_str());
    }
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

  // Endpoint per scrivere dati su RFID
  if (requestLine.indexOf("GET /write-rfid") == 0) {
    int start = requestLine.indexOf("data=");
    if (start != -1) {
      start += 5;
      int end = requestLine.indexOf("&", start);
      if (end == -1) end = requestLine.indexOf(" ", start);
      String data = requestLine.substring(start, end);
      // Decodifica URL
      data.replace("%20", " ");
      data.replace("+", " ");
      
      if (data.length() > 0) {
        data.toCharArray(dataToWrite, sizeof(dataToWrite));
        writeMode = 1;
        webMsgCode = 3; // In attesa di scrivere
      }
    }
    
    client.println(F("HTTP/1.1 200 OK"));
    client.println(F("Content-Type: text/plain"));
    client.println(F("Connection: close"));
    client.println();
    if (dataToWrite[0] != '\0') {
      client.println(F("Ready to write. Present an NFC tag."));
    } else {
      client.println(F("No data provided"));
    }
    delay(1);
    client.stop();
    return;
  }

  webMsgCode = 0; // Resetta messaggio
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

    if (srv.length() > 0) {
      srv.toCharArray(mqttConfig.server, sizeof(mqttConfig.server));
      usr.toCharArray(mqttConfig.user, sizeof(mqttConfig.user));
      pw.toCharArray(mqttConfig.pass, sizeof(mqttConfig.pass));
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
  client.println(F("<html><head><style>input,button{padding:4px;margin:3px}</style></head><body style=font-size:14px><h2>RFID</h2>"));
  if (webMsgCode == 1) client.println(F("<p style=color:green>✓ MQTT OK</p>"));
  if (webMsgCode == 2) client.println(F("<p style=color:red>✗ No server</p>"));
  if (webMsgCode == 3) client.println(F("<p style=color:blue>⧐ Write mode ON</p>"));
  
  client.println(F("<h3>MQTT</h3><form action=/mqtt>"));
  client.print(F("<input name=server value=\"")); client.print(mqttConfig.server); client.println(F("\" placeholder=Server size=20>"));
  client.print(F("<input name=user value=\"")); client.print(mqttConfig.user); client.println(F("\" placeholder=User>"));
  client.println(F("<input name=pass type=password placeholder=Pass>"));
  client.print(F("<input name=port value=")); client.print(mqttConfig.port); client.println(F(" size=3>"));
  client.println(F("<input type=submit value=Save></form>"));
  
  client.println(F("<h3>Write NFC</h3><form action=/write-rfid>"));
  client.println(F("<input name=data maxlength=15 placeholder='Text (max 15)' required>"));
  client.println(F("<input type=submit value=Write></form>"));
  
  client.println(F("<h3>Status</h3>"));
  client.print(F("MQTT: ")); client.println(mqttClient.connected() ? F("✓<br>") : F("✗<br>"));
  client.print(F("UID: ")); 
  if (lastRfidUid[0]) {
    client.println(lastRfidUid);
  } else {
    client.println(F("---"));
  }
  client.println(F("<br>"));
  client.print(F("Write: ")); 
  if (writeMode == 1) {
    client.print(F("✓ ")); client.print(dataToWrite);
  } else {
    client.print(F("✗"));
  }
  client.println(F("<br><p><a href=/mqtt-test><button>MQTT Test</button></a></p></body></html>"));

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
