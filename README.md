# Avensys ESP32 Firmware (UART + BLE + Wi-Fi + MQTT + OTA)

[![Build](https://img.shields.io/badge/build-passing-brightgreen)](#)
[![Release](https://img.shields.io/badge/release-v1.0.1-blue)](#)
[![Version](https://img.shields.io/badge/firmware-v1.0.1-blue)](#)
[![License](https://img.shields.io/badge/license-private-lightgrey)](#)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32-orange)](https://platformio.org/)
Firmware ESP32 (ESP-IDF via PlatformIO) per unità Avensys con:

- comunicazione locale con la board unità via **UART**
- telemetria e provisioning via **BLE GATT**
- connettività cloud via **Wi-Fi + MQTT/TLS**
- aggiornamento firmware ESP32 via **HTTPS OTA**
- gestione file firmware unità su **SPIFFS**

Il progetto è pensato per essere compatibile con l'app Android React Native `MyReactNativeApp` (BLE service `0x00FF` e caratteristiche `FF01..FF0B`).

## 1. Caratteristiche principali

- BLE GATT custom service `0x00FF`
- Lettura/scrittura EEPROM via `FF01` con supporto:
  - write standard
  - write chunked/reassembly
  - prepare/exec write
- Dati debug e polling via `FF02` e `FF03`
- Provisioning Wi-Fi via BLE (`FF05` / `FF06`) con sicurezza BLE attiva
- Stato provisioning via `FF0B` (read + notify)
- Connessione Wi-Fi con gestione robusta timeout/retry/cleanup
- MQTT TLS verso AWS IoT (certificati embedded)
- OTA firmware ESP32 via endpoint JSON versione
- Download firmware unità su SPIFFS e invio alla board unità in bootloader

## 2. Hardware target

- Board: `esp32doit-devkit-v1`
- UART unità:
  - `TX`: GPIO17
  - `RX`: GPIO16
  - baudrate: `921600`
- GPIO output:
  - `Wifi_Led1`: GPIO21
  - `Wifi_Ready`: GPIO12

Nota: questo firmware usa **UART** per la comunicazione con la board unità.

## 3. Stack software

- Framework: ESP-IDF
- Build system: PlatformIO
- Platform: `espressif32@6.3.2`
- Partizioni: `partition_one_ota.csv`

## 4. Struttura repository

- `src/main.c`: orchestrazione principale (BLE, UART, MQTT, OTA, SPIFFS, state machine)
- `src/ble.c`: database GATT, eventi BLE, provisioning, write EEPROM
- `src/wifi_connect.c`: lifecycle connessione Wi-Fi (start/stop/timeout/reconnect)
- `src/Uart1.c`: driver/task UART e gestione eventi seriali
- `src/WBM_Serial.c`: protocollo seriale verso board unità
- `include/*.h`: API e strutture condivise
- `partition_one_ota.csv`: tabella partizioni OTA + SPIFFS
- `platformio.ini`: configurazione build/upload

## 5. Configurazione build

`platformio.ini` usa:

- env: `esp32doit-devkit-v1`
- framework: `espidf`
- partizioni: `partition_one_ota.csv`
- embed certificati testo:
  - `src/private.pem.key`
  - `src/certificate.pem.crt`
  - `src/aws-root-ca.pem`
  - `src/Alsaqr.pem`
  - `src/espressif.pem`

## 6. Build e flash

Prerequisiti:

- PlatformIO CLI installato (`pio`)
- toolchain ESP32 installata da PlatformIO
- porta seriale disponibile

Comandi tipici:

```bash
pio run
pio run -t upload
pio device monitor -b 115200
```

Se usi VS Code + PlatformIO, puoi usare i task standard `Build`, `Upload`, `Monitor`.

## 7. BLE API (contratto app)

Service BLE:

- 16-bit: `0x00FF`
- 128-bit canonico: `0000FF00-0000-1000-8000-00805F9B34FB`

Caratteristiche principali:

- `FF01`: EEPROM data (read/write)
- `FF02`: Debug data (read)
- `FF03`: Polling data (read)
- `FF05`: Wi-Fi SSID (encrypted read/write)
- `FF06`: Wi-Fi Password (encrypted read/write)
- `FF07`: Connect-to-cloud trigger
- `FF08`: OTA URL (uso interno/diagnostica)
- `FF09`: Update firmware (uso interno/diagnostica)
- `FF0A`: Device ID (uso interno/diagnostica)
- `FF0B`: Provision status (read + notify)

### 7.1 Provision status (`FF0B`)

Valori stato correnti:

- `0`: IDLE
- `1`: WAIT_SSID
- `2`: WAIT_PASSWORD
- `3`: READY
- `4`: APPLYING
- `5`: DONE
- `6`: ERROR

### 7.2 Sicurezza BLE provisioning

Per `FF05` e `FF06` sono richiesti permessi encrypted read/write.
Il firmware imposta parametri di sicurezza BLE (bonding + security request handling).

## 8. Flusso scrittura EEPROM da app Android

L'app Android scrive `FF01` con payload tipico di `241/242` byte.
Il firmware supporta:

- scrittura diretta `FF01` in singolo frame
- scrittura chunked con reassembly e timeout stream
- prepare-write/exec-write

Dopo apply EEPROM, il firmware imposta i bit di `Read_Eeprom_Request_Index` per innescare la scrittura verso unità e l'aggiornamento dei counter di ACK lato app.

## 9. Wi-Fi e MQTT

Credenziali Wi-Fi:

- salvate in NVS namespace `storage`
- chiavi NVS:
  - `wifi_ssid`
  - `wifi_pass`

MQTT:

- broker configurato in `src/main.c` (`BROKER_URL`)
- TLS con certificati embedded
- topic principali app/esp basati su serial/address unità

## 10. OTA ESP32

Controllo versione:

- URL versione JSON: `VERSION_URL`
- versione firmware locale: `CURRENT_VERSION`

Se disponibile una versione nuova, il firmware esegue `esp_https_ota(...)` e riavvia.

## 11. Firmware unità via SPIFFS

Partizione SPIFFS usata per file firmware unità (`/spiffs/firmware.bin`).
Il file viene scaricato da URL server e, in modalità bootloader unità, inviato via UART con protocollo dedicato.

## 12. Partizioni flash

Da `partition_one_ota.csv`:

- `nvs`: `0x4000`
- `phy_init`: `0x1000`
- `otadata`: `0x2000`
- `ota_0`: `1750K`
- `ota_1`: `1750K`
- `spiffs`: `200K`

## 13. Troubleshooting rapido

### 13.1 L'app legge ma non scrive EEPROM

Verifica:

- scrittura su `FF01` realmente inviata dall'app
- log firmware con messaggi tipo:
  - `EEPROM write accepted from write-direct`
  - `EEPROM write accepted from write-stream`
  - `EEPROM write accepted from prepare-write`
- payload dimensione `>= sizeof(S_EEPROM)`

### 13.2 Provisioning Wi-Fi fallisce

- assicurati pairing BLE completato
- verifica che `FF05` e `FF06` siano accessibili in encrypted mode
- controlla transizioni su `FF0B`
- verifica credenziali NVS (`wifi_ssid`, `wifi_pass`)

### 13.3 Wi-Fi non connette

- controlla RSSI/AP disponibili
- verifica timeout connessione (`WIFI_CONNECT_TIMEOUT_MS`)
- controlla eventuali reason code in `on_wifi_disconnect`

### 13.4 MQTT non si connette

- verifica `BROKER_URL`
- certificati embedded presenti e corretti
- data/ora di sistema e catena TLS

## 14. Parametri da adattare in produzione

Aggiornare almeno:

- `BROKER_URL`
- `VERSION_URL`
- URL firmware unità (`Quarke_URL`)
- credenziali Wi-Fi di default hardcoded (meglio rimuoverle)
- certificati TLS embedded

## 15. Note sicurezza

- Non committare chiavi/certificati reali di produzione in chiaro.
- Evitare credenziali Wi-Fi hardcoded nel firmware finale.
- Usare rotazione certificati e provisioning sicuro in linea con policy prodotto.

## 16. Stato compatibilità app Android

Firmware allineato al contratto BLE dell'app React Native:

- service `0x00FF`
- `FF01/FF02/FF03` presenti
- provisioning su `FF05/FF06`
- status provisioning `FF0B`
- path EEPROM write compatibile con write Android standard

## 17. License

Valutare e aggiungere una licenza esplicita (`LICENSE`) se il repository sarà pubblico.

---

Se vuoi, nel prossimo step posso aggiungere anche:

- sezione "Release process" (tag/versioning/changelog)
- badge GitHub (build, release, license)
- diagramma architettura (BLE <-> ESP32 <-> UART <-> unità)

## 18. Release & Versioning

### 18.1 Strategia versioni consigliata

Usare Semantic Versioning:

- `MAJOR.MINOR.PATCH`
- esempio:
  - `1.0.0`: prima release stabile
  - `1.1.0`: nuova feature compatibile
  - `1.1.1`: bugfix

Allineare sempre:

- `CURRENT_VERSION` in `src/main.c`
- tag git (`vX.Y.Z`)
- release notes GitHub

### 18.2 Flusso release consigliato

1. Aggiorna `CURRENT_VERSION` in `src/main.c`.
2. Verifica build e upload su board di test.
3. Verifica smoke test:
   - connessione BLE + lettura `FF01/FF02/FF03`
   - provisioning `FF05/FF06` e stato `FF0B`
   - scrittura EEPROM via app Android
   - connessione Wi-Fi e MQTT
4. Esegui commit di release:
   - `git add .`
   - `git commit -m "release: vX.Y.Z"`
5. Crea tag annotato:
   - `git tag -a vX.Y.Z -m "Release vX.Y.Z"`
6. Push branch + tag:
   - `git push origin main`
   - `git push origin vX.Y.Z`
7. Crea GitHub Release usando il tag e incolla il changelog.

### 18.3 Template changelog (release notes)

```md
## vX.Y.Z - YYYY-MM-DD

### Added
- ...

### Changed
- ...

### Fixed
- ...

### Security
- ...

### Compatibility
- App Android BLE:
  - [ ] FF01 EEPROM read/write OK
  - [ ] FF02 debug read OK
  - [ ] FF03 polling read OK
  - [ ] FF05/FF06 provisioning write OK
  - [ ] FF0B provisioning status notify OK
- UART unit communication:
  - [ ] polling/debug/eeprom sync OK

### Notes
- OTA:
- MQTT:
- Known limitations:
```

### 18.4 Tag naming policy

- Solo tag release: `vX.Y.Z`
- Evitare tag ambigui (`test`, `latest`, `final`)
- Hotfix urgente:
  - branch `hotfix/vX.Y.Z`
  - merge su `main`
  - tag `vX.Y.Z`

### 18.5 Consigli CI minima

Per il workflow `ci.yml` (badge build), includere almeno:

- checkout repository
- setup PlatformIO
- `pio run` su env `esp32doit-devkit-v1`
- artifact firmware `.bin` in output

### 18.6 Checklist pre-release (rapida)

- [ ] `CURRENT_VERSION` aggiornato
- [ ] build locale OK
- [ ] flash + boot OK
- [ ] BLE contract app OK (`FF01..FF0B`)
- [ ] EEPROM write da app Android confermata
- [ ] Wi-Fi + MQTT OK
- [ ] OTA check senza errori critici
- [ ] changelog compilato
- [ ] tag creato e pushato

## 19. Badge setup

Sostituisci `OWNER/REPO` nei badge in testa al file con il path reale GitHub.

Esempio:

- da `OWNER/REPO`
- a `avensys/avensys-esp32-project`

