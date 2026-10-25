# power-lora

ESPHome på två ESP32-C3 Super Mini med EByte E220-900T22D: elmätaren
Sagemcom T211 i elrummet → LoRa → Home Assistant i lägenheten.

**Första versionen är ett mocktest.** Den skickar 26 syntetiska mätvärden i ett
76-byte binärt paket. HAN-porten ska inte anslutas för detta steg. Ingen firmware
har flashats av projektets byggkontroller.

## Filer

- `transmitter/transmitter.yaml`: mockgenerator, USB-loggning, utan Wi-Fi,
  Bluetooth, API eller OTA.
- `receiver/receiver.yaml`: mottagare med Wi-Fi, krypterat ESPHome API och OTA.
- `components/power_lora/`: lokal ESPHome-komponent för E220, avkodning och sensorer.
- [Paketformat och radioregister](docs/protocol.md).
- [Testprotokoll](docs/testing.md): matning, räckvidd och airtime.

Mätvärden: importerad/exporterad aktiv och reaktiv energi; total samt fasvis
importerad/exporterad aktiv och reaktiv effekt; spänning och ström per fas.
Mockvärdena är en transporttestsekvens, inte en elektriskt exakt lastsimulering.
Energiräknarna börjar om när transmittern startas om.

## Koppling (båda noderna)

| ESP32-C3 | E220 |
| --- | --- |
| GPIO0 | M0 |
| GPIO1 | M1 |
| GPIO2 (ESP TX) | RXD |
| GPIO3 (ESP RX) | TXD |
| GPIO4 | AUX |
| 5 V | VCC |
| GND | GND |

GPIO2 är avsiktligt kvar som strapping-pin. ESPHomes varning är därför väntad;
verifiera kallstart innan lödning. Loggning går via USB Serial/JTAG, inte via
radions UART. Anslut en lämplig 868 MHz-antenn till båda modulerna före radiotest.

Sändaren: DP100 på **5 V, gemensam strömgräns 250 mA** till ESP32 och E220.
Mottagaren: ESP32 via USB, E220 från ESP32-kortets 5 V och GND.
Mata inte sändarens 5 V samtidigt från DP100 och vanlig USB utan säkerställd
matningsisolering; USB kan annars kringgå strömbegränsningen eller bakmata.
Flasha med USB först och gör sedan strömtestet med enbart DP100. För samtidiga
USB-loggar krävs en lösning där VBUS inte parallellmatar kortet.

GPIO5 är reserverad för senare HAN RX med 4,7 kΩ pull-up till 3,3 V och
inverterad UART. Den aktiveras inte i mockversionen. HAN-kontaktens pinout,
data-request och aktivering hos Ellevio verifieras vid HAN-steget.

## Bygga och flasha

Utvecklingsversion: ESPHome **2026.9.1**, ESP-IDF, 4 MB flash.
ESPHome kan installeras i en venv med `pip install -r requirements.txt`.
Kommandona nedan körs från repots rot.

1. Kopiera `receiver/secrets.yaml.example` till `receiver/secrets.yaml`.
2. Fyll i Wi-Fi, OTA-lösenord och API-nyckel. Skapa en API-nyckel med
   `openssl rand -base64 32`. Filen ignoreras av git.
3. Validera och bygg båda konfigurationerna:

```sh
esphome config transmitter/transmitter.yaml
esphome config receiver/receiver.yaml
esphome compile transmitter/transmitter.yaml
esphome compile receiver/receiver.yaml
```

4. Anslut **ett kort i taget** via USB och kör motsvarande kommando:

```sh
esphome run receiver/receiver.yaml
esphome run transmitter/transmitter.yaml
```

ESPHome låter dig välja port. Vid behov: håll BOOT intryckt, tryck RESET och
släpp BOOT för nedladdningsläge. Transmittern uppdateras via USB även senare.

5. Lägg till mottagaren via ESPHome-integrationen i HA, med API-nyckeln från
   secrets-filen. Alla mätvärden heter **MOCK**. Lägg inte dessa energisensorer
   i den riktiga energipanelen. Live-HAN får egna sensornamn i nästa steg.

Efter uppstart konfigurerar båda noderna E220 och verifierar registervärdena.
Loggen ska visa `E220 registers verified; ready`. Sändaren väntar sedan ett helt
sändningsintervall före första paketet. Mottagaren visar RSSI, mottagna/saknade
paket, avvisade ramar, dubbletter, observerade sändaromstarter och paketålder.
Efter 360 sekunder utan nytt giltigt paket blir länken frånkopplad och mätvärdena
okända. Wi-Fi-/API-avbrott ska inte starta om mottagaren automatiskt.

## Radio och duty cycle

Startprofil: **868,125 MHz, 2,4 kbit/s, 10 dBm, 120 sekunders intervall**.
Kanalen och radiohastigheten är fasta i komponenten i detta första steg.
`tx_power` kan vara 10 eller 13 dBm; anta inte att modulens 22 dBm är tillåtet
på denna kanal. Antennförstärkning och kabelförlust påverkar utstrålad effekt.

I 868,0–868,6 MHz anger PTS högst 25 mW ERP och alternativet högst 1 %
sändningscykel. E220:s kanaler ligger med 1 MHz steg och når inte 869,525 MHz
inne i 10 %-bandet. Även utsänd bandbredd måste rymmas i valt delband.
Se [PTSFS 2025:1, §141 och §146](https://pts.se/contentassets/b05c8a7d01a64783aafc6a19e1b78590/ptsfs-2025-1-foreskrifter-om-undantag-fran-tillstandsplikt-for-anvandning-av-vissa-radiosandare.pdf).

`airtime_budget: 1000ms` är en **provisorisk konstruktionsbudget, inte uppmätt
sändningstid eller ett bevis på regelefterlevnad**. Valideringen kräver
`transmit_interval >= 100 × airtime_budget`. Med 1 sekund som budget och
120 sekunders intervall blir den budgeterade andelen 0,83 %.
RF-airtime och bandbredd för den faktiska modulrevisionen behöver verifieras
med tillverkardata eller RF-mätning innan kontinuerlig installation eller
kortare intervall. AUX är inte en exakt RF-tidsindikator.

Det finns inga automatiska omsändningar eller ACK-paket. Den första sändningen
sker efter ett helt intervall även vid omstart. Båda YAML-filerna ska ha samma
node-ID och tidsinställningar. När airtime har verifierats kan budget och
intervall ändras uttryckligen. Exempel: 80 ms verifierad budget kräver minst
8 sekunder enligt 1 %-räkningen; ett intervall på 10 sekunder ger marginal.
Ändra även `stale_after` till lämpligen minst tre gånger intervallet.

Första versionen håller E220 i normalläge mellan paketen. Wi-Fi och Bluetooth
startas inte på transmittern, men detta är inte en deep-sleep-implementation.
När matning och länk fungerar kan vi optimera viloström och eventuellt dela upp
snabba och långsamma mätvärden i olika paket.

## Programvarutester

Pakettestet körs utan ESP32 och kontrollerar CRC mot en känd referens,
bitfel i varje position, avklippta paket, brus, synkronisering, saknade värden,
sekvensluckor, dubbletter, omstart och sekvensräknarens varvning:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -I. \
  tests/protocol_test.cpp -o /tmp/power-lora-protocol-test
/tmp/power-lora-protocol-test
```

## Nästa steg: riktig HAN

Efter godkänt mocktest integrerar vi
[psvanstrom/esphome-p1reader](https://github.com/psvanstrom/esphome-p1reader)
på transmitterns andra UART (GPIO5, normalt 115200 med inverterad RX).
Det kräver ett verkligt telegram från T211 för att kontrollera tillgängliga
fält, enheter och datagiltighet. Värden som saknas ska markeras saknade,
inte ersättas med noll. Vid övergången skiljs live-entiteter från mockdata.
