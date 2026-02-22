# Arduino MQTT RFID Client

Progetto Arduino Duemilanove con Ethernet Shield che legge carte RFID (RC522) e invia i dati via MQTT. Include interfaccia web per la configurazione.

## Caratteristiche

- 🔖 Lettura carte RFID RC522
- 📡 Invio dati MQTT via Ethernet
- 🌐 Interfaccia web HTTP per la configurazione
- 💾 Configurazione persistente in EEPROM
- 🔄 Riconnessione automatica MQTT
- 🔧 DHCP con fallback IP statico

## Requisiti Hardware

### Componenti
- **Arduino Duemilanove** (ATmega328)
- **Ethernet Shield W5100**
- **Lettore RFID RC522** (13.56 MHz)
- Carte RFID/NFC compatibili Mifare Classic

### Connessioni RC522

| RC522 | Arduino |
|-------|---------|
| 3.3V | 3.3V |
| GND | GND |
| MISO | Pin 12 (SPI) |
| MOSI | Pin 11 (SPI) |
| SCK | Pin 13 (SPI) |
| SDA/CS | Pin 9 |
| RST | Pin 8 |

**Nota:** L'Ethernet Shield utilizza il pin 10 per il CS, il pin 9 è libero per l'RFID.

## Librerie Richieste

Installare via PlatformIO o Arduino IDE:
- [Ethernet](https://github.com/arduino-libraries/Ethernet) v2.0.2
- [PubSubClient](https://github.com/knolleary/pubsubclient) v2.8.0
- [MFRC522](https://github.com/miguelbalboa/MFRC522) v1.4.12

## Installazione

### PlatformIO
```bash
git clone https://github.com/tuousername/arduino-mqtt-client-rfid.git
cd arduino-mqtt-client-rfid
pio run --target upload
```

### Arduino IDE
1. Clonare il repository
2. Installare le librerie tramite Gestione librerie
3. Aprire `src/main.cpp` in Arduino IDE
4. Compilare e caricare

## Configurazione

### Parametri Principali

Modifica in `src/main.cpp`:

```cpp
// MAC address Ethernet Shield
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

// IP statico di fallback
IPAddress ip(192, 168, 188, 177);

// Pin RFID
#define SS_PIN 9
#define RST_PIN 8
```

### Accesso all'Interfaccia Web

1. Aprire il browser: `http://<IP_ARDUINO>`
2. Compilare i campi:
   - **Server**: Indirizzo IP o hostname del broker MQTT
   - **User**: Username MQTT (opzionale)
   - **Pass**: Password MQTT (opzionale)
   - **Port**: Porta MQTT (default 1883)
3. Cliccare **Save**

La configurazione viene salvata in EEPROM e persiste anche dopo il riavvio.

## Utilizzo

### Lettura Carte RFID

Quando una carta RFID viene rilevata:
1. L'UID viene letto in formato esadecimale (es: `A1B2C3D4`)
2. L'UID viene inviato al topic MQTT: `arduino/rfid`
3. L'UID è visibile nell'interfaccia web

### Test MQTT

Cliccare il bottone "Test" on the web interface per inviare un messaggio di test al topic `casa/sensor/arduino`.

## Integrazione Home Assistant

### Configuration

Aggiungere al `configuration.yaml`:

```yaml
input_text:
  rfid_uid:
    name: "Ultimo RFID letto"
    max: 16
```

### Automazione

Creare un'automazione per catturare gli UID RFID:

```yaml
alias: RFID to MQTT lettura carte
description: Legge UID RFID da Arduino tramite MQTT
triggers:
  - trigger: mqtt
    topic: arduino/rfid
conditions: []
actions:
  - choose:
      - conditions:
          - condition: template
            value_template: "{{ trigger.payload == 'XXXXXXXX' }}"
        sequence:
          - action: input_text.set_value
            data:
              value: "UNO"
            target:
              entity_id: input_text.rfid_uid
      - conditions:
          - condition: template
            value_template: "{{ trigger.payload == 'YYYYYYYY' }}"
        sequence:
          - action: input_text.set_value
            data:
              value: "DUE"
            target:
              entity_id: input_text.rfid_uid
      - conditions:
          - condition: template
            value_template: "{{ trigger.payload == 'ZZZZZZZZ' }}"
        sequence:
          - action: input_text.set_value
            data:
              value: "TRE"
            target:
              entity_id: input_text.rfid_uid
    default:
      - action: input_text.set_value
        data:
          value: "{{ trigger.payload }}"
        target:
          entity_id: input_text.rfid_uid
mode: single
```

Sostituire `XXXXXXXX`, `YYYYYYYY`, `ZZZZZZZZ` con i veri UID delle carte RFID.

## Arduino Duemilanove Specifiche

- **Flash**: 30 KB (~30KB utilizzati, compilazione ottimizzata)
- **RAM**: 2 KB
- **Velocità**: 16 MHz
- **EEPROM**: 1024 bytes

## Topic MQTT

| Topic | Payload | Descrizione |
|-------|---------|-------------|
| `arduino/rfid` | `A1B2C3D4` | UID RFID letto |
| `casa/sensor/arduino` | `0` | Messaggio di test |
| (configurabile) | (varia) | Topic sottoscritto per ricevere messaggi |

## Troubleshooting

### Arduino non si connette
- Verificare il cavo Ethernet
- Controllare il broker MQTT è raggiungibile
- Controllare username/password MQTT
- Verificare firewall

### RFID non legge le carte
- Controllare i collegamenti (soprattutto pin 8, 9, 11, 12, 13)
- Verificare il voltaggio 3.3V sul sensore
- Assicurarsi di avere carte Mifare Classic compatibili
- Avvicinare la carta al lettore

### Memoria insufficiente
Se il codice non compila per mancanza di memoria:
- Rimuovere messaggi Serial di debug
- Ridurre la dimensione dei buffer
- Compilare in modalità release (-Os)

## Serial Output

Collegare alla porta seriale (9600 baud) per debug:
```
MQTT Recv: <messaggio ricevuto>
MQTT Test Sent
```

## Licenza

MIT License - vedi LICENSE.md

## Autore

Progetto per Arduino MQTT RFID Reader

## Contributi

Pull request benvenute! Per modifiche significative, aprire prima un issue per discussione.

## Note Importanti

⚠️ **Memoria limitata**: Arduino Duemilanove ha solo 30KB di Flash. Il codice è ottimizzato per stare nei limiti.

⚠️ **Alimentazione**: Assicurarsi che l'alimentazione sia stabile. L'Ethernet Shield richiede più corrente che uno sketch Arduino standard.

⚠️ **RFID Spacing**: Non leggere troppe carte rapidamente - il sensore ha un refresh rate limitato.