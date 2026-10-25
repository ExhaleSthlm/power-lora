# Testprotokoll

## 1. Bordstest

- Montera antenner och kontrollera gemensam jord samt UART-korsningen.
- Flasha korten ett i taget via USB. E220-konfigurationen görs automatiskt;
  ingen separat EByte-programmerare behövs.
- Kör sändaren med DP100 5 V / 250 mA och mottagaren med USB.
- Kontrollera `Radio ready`/logg på båda noderna. Vid fel hålls radiotrafiken
  avstängd; kontrollera matning, AUX och RX/TX och starta om.
- Första paketet kommer 120 sekunder efter att radions konfiguration är klar.
  Börja med flera meters avstånd; låt inte antennerna ligga mot varandra.
- Kontrollera att 26 MOCK-värden, RSSI och paketantal kommer in i HA.
- Stäng av transmittern: paketåldern ska öka, och efter 360 sekunder ska
  `Transmitter link` bli frånkopplad och värdena bli okända.
- Starta om transmittern: vänta ett helt intervall. Omstartsräknaren ska öka
  utan att en enorm sekvenslucka registreras.
- Stäng av Wi-Fi/HA tillfälligt: mottagaren ska fortsätta läsa radion och
  sedan återansluta. Diagnostik lagras i RAM och nollställs vid RX-omstart.

## 2. Strömbudget

Gör minst tio kallstarter med DP100-gränsen 250 mA. Notera om den går över i
konstantströmsläge, om 5 V sjunker, eller om kortet startar om. Ett DMM eller
DP100:s display kan missa snabba toppar; använd oscilloskop om beteendet är
oklart. Logga både start och sändning. Vid stabil drift i testet kvarstår ett
separat test med HAN-portens verkliga strömförsörjning.

USB får inte mata sändaren parallellt och därmed dölja strömproblem. Att enbart
stänga av Wi-Fi och Bluetooth innebär inte att ESP32/E220 sover.

## 3. Airtime och frekvens

AUX:s höga nivå betyder inte säkert att sista RF-paketet är färdigsänt. Använd
inte UART-tiden, AUX-pulsbredden eller `bytes * 8 / air_rate` som exakt airtime.
Verifiera radioinställningens upptagna bandbredd och totala RF-sändningstid för
76-bytepaketet med tillverkardata för rätt revision eller RF-mätning (t.ex.
SDR/spektrumanalysator med lämplig tidsupplösning).

Den provisoriska budgeten är 1 s per paket. Vid 120 s intervall är gränsen
för 1 % i jämn drift 1,2 s per paket, men sikta på marginal. Om mätningen visar
att budgeten inte räcker: öka `airtime_budget` och `transmit_interval` innan
fortsatt regelbunden trafik. Räckviddstestet får inte bygga på högre effekt än
vad band, antenn och förluster medger.

## 4. Räckvidd

Flytta först sändaren till elrummets utsida, sedan till avsedd plats med dörren
stängd. Behåll mottagaren på avsedd plats i lägenheten. Jämför ett par
antennplaceringar och orienteringar. Vid 120 s intervall ger en timme cirka
30 paket; minst 100 intervall ger en mer användbar observation.

Anteckna för varje test:

| Datum/plats | Antenner/placering | Intervall | RX start/slut | Saknade start/slut | RSSI-intervall | Omstarter/strömproblem |
| --- | --- | --- | --- | --- | --- | --- |
| | | | | | | |

Förlust mellan observerade paket kan uppskattas som
`delta_saknade / (delta_mottagna + delta_saknade)`. Räkna inom en sammanhängande
sändarsession och efter första baslinjepaketet. En utebliven sista sändning
syns inte i sekvensluckor förrän nästa paket kommer; använd också paketåldern.
RSSI är användbar för jämförelser, men är inte SNR eller ett garanterat mått på
länkmarginal. Kontrollera framför allt paketförlust och långa avbrott.

## 5. HAN efter godkänt mocktest

Verifiera aktiverad port hos Ellevio, RJ12-orientering/pinout och data-request
innan anslutning. Bekräfta 4,7 kΩ (inte 47 kΩ) till 3,3 V på GPIO5.
Läs ett verkligt telegram och kontrollera fält och enheter innan vi ersätter
mockgeneratorn. Prova därefter kallstart och sändning med verklig HAN-matning.
