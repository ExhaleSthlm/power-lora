# Power LoRa v1

First stage is **mock only**. It tests binary transport, not the P1 parser. Both
nodes use the local `power_lora` ESPHome external component. No external Arduino
LoRa library is needed: E220 does the modulation and exposes a UART.

## Wire layout

One 76-byte application frame per transmission, little-endian integers, no C++
struct casting or floating-point representation on the wire. E220 is configured
for 128-byte sub-packets, so the complete application frame fits in one packet.
All bytes are submitted in a single UART write at 9600 baud, 8N1.

| Offset | Bytes | Meaning |
| --- | ---: | --- |
| 0 | 2 | Magic `50 4C` (PL) |
| 2 | 1 | Version = 1 |
| 3 | 1 | Flags: bit 0 = mock; other bits must be zero |
| 4 | 2 | Node ID, default 1 |
| 6 | 4 | Random boot/session ID |
| 10 | 4 | Sequence number, starts at 0, wraps modulo 2^32 |
| 14 | 16 | Four energy counters, uint32 each |
| 30 | 44 | Twenty-two measurements, uint16 each |
| 74 | 2 | CRC-16/CCITT-FALSE over bytes 0–73, little-endian |

CRC polynomial 0x1021, init 0xFFFF, no reflection, xorout 0. Check value for
ASCII `123456789` is 0x29B1. CRC is error detection, **not authentication or
encryption**. The radio crypt key is explicitly set to zero for the mock test.

## Measurement order

| Index | Values (in order) | Wire unit | HA unit |
| --- | --- | --- | --- |
| 0–3 | Energy import, export, reactive import, reactive export | Wh / varh | kWh / kvarh |
| 4–7 | Total active import, active export, reactive import, reactive export | W / var | W / var |
| 8–10 | Active import L1, L2, L3 | W | W |
| 11–13 | Active export L1, L2, L3 | W | W |
| 14–16 | Reactive import L1, L2, L3 | var | var |
| 17–19 | Reactive export L1, L2, L3 | var | var |
| 20–22 | Voltage L1, L2, L3 | 0.1 V | V |
| 23–25 | Current L1, L2, L3 | 0.01 A | A |

All-ones denotes missing or out-of-range: uint32 0xFFFFFFFF or uint16 0xFFFF.
Valid energy range is 0–4,294,967,294 Wh/varh; other values 0–65,534 wire units.
Overflowing 16-bit values encode as missing, never wrap to a plausible reading.
Future HAN integration must check conversion/overflow of energy before encoding.
HA stores ESPHome sensor states as floats, so the integer wire precision of a
large energy counter is not necessarily retained in the published float.

## Receiver semantics

E220 appends an RSSI byte to each received sub-packet. It is not part of the
76-byte application frame or its CRC. RSSI dBm = byte − 256. The parser expects
76 frame bytes followed by that suffix, and handles fragmented UART delivery,
noise and multiple consecutive frames. A partial stream expires after 2 seconds.
The suffix itself has no application-level integrity check.

Only matching node ID, supported version/flags, valid CRC and **mock** data are
published. Duplicate/old sequence numbers do not update sensors or link age.
A new boot ID starts a new sequence baseline and increments the restart count.
The first observed sequence after receiver startup is a baseline, not inferred
packet loss. Loss is counted only between valid packets in the same session;
the total loss count is retained across transmitter boots, but not RX reboot.
During a complete outage, age/link status detects the outage; loss is only known
once a later sequence arrives. No acknowledgements or automatic retransmission.

After `stale_after`, all measurements and RSSI become NaN/unknown and link_alive
is false. No fake zero power or zero energy is published. Diagnostic counters
remain available. `frames_rejected` counts failed full frame candidates, not
every noise byte or incomplete frame. `packets_sent` means submitted to UART,
not confirmed delivered. Packet counters in HA are floats as well.

## E220 setup

On every boot, enter mode 3 (M0=M1=high), wait for stable AUX, and send a
temporary register write (`C2 00 08 ...`). Validate its response, then independently
read back registers 0–5 (`C1 00 06`). Only after successful verification enter
normal transparent mode (M0=M1=low). Failed configuration leaves RF data disabled.
No persistent E220 flash writes occur.

Default registers: `00 01 62 43 12 80 00 00`:

- Node address 1, UART 9600/8N1, air rate setting 2.4 kbps.
- 128-byte sub-packets, 10 dBm output.
- Channel 18 (868.125 MHz), RSSI suffix enabled, transparent mode.
- LBT off; WOR unused; crypt key zero. LBT is not used to bypass duty limits.

Normal mode is retained between transmissions in this first test build. ESP32
Wi-Fi/Bluetooth are not initialised on the transmitter, but neither the ESP32
nor E220 is put into deep sleep. That optimisation can follow the power test.

Sources: [EByte manual](https://www.ebyte.com/Uploadfiles/Files/2021-7-6/202176188184448.pdf),
[manufacturer manual mirror](https://pdf.direnc.net/upload/llcc68-ebyte-e220-900t22d-868mhz-915mhz-22dbm-kablosuz-lorawan-modul.pdf).
