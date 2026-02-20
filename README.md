# atis_decoder

A lightweight C program that decodes **ATIS (Automatic Transmitter Identification System)** signals from a RTL-SDR dongle and outputs the decoded ATIS number over UDP.

ATIS is used in European inland and coastal waterways. When a ship releases its PTT button, the radio automatically transmits a short digital burst containing the vessel's unique ATIS identification number. Bridges, locks, and harbours use this to identify which vessel is calling.

---

## How it works

The pipeline is:

```
RTL-SDR dongle → rtl_fm → atis_decoder → UDP → Node-RED (or any UDP listener)
```

`atis_decoder` reads raw signed 16-bit PCM audio from stdin, performs FSK demodulation using the Goertzel algorithm at 1300 Hz (mark) and 2100 Hz (space), and decodes ITU-R M.493 compliant 10-bit DSC symbols to extract the ATIS number.

### Technical details

- **Standard**: ITU-R M.493 (Digital Selective Calling)
- **Modulation**: FSK, mark = 1300 Hz, space = 2100 Hz
- **Baud rate**: 1200 Bd
- **Sample rate**: 24000 Hz (gives exactly 20 samples per bit — no drift)
- **Symbol format**: 10 bits (7 info bits LSB-first + 3 checkbits MSB-first)
- **Sync**: Locks on DX phasing symbol (125), then alternates read/skip to handle ITU-R M.493 time diversity (each symbol transmitted twice)
- **Output**: ATIS number as plain ASCII string to stdout and over UDP

---

## Requirements

- Linux (tested on Ubuntu)
- [rtl-sdr](https://osmocom.org/projects/rtl-sdr/wiki) (`sudo apt install rtl-sdr`)
- GCC and make (`sudo apt install build-essential`)
- An RTL-SDR USB dongle

---

## Build

```bash
git clone https://github.com/yourusername/atis_decoder
cd atis_decoder
make
```

---

## Usage

```bash
rtl_fm -f 156.500M -M fm -s 24000 -g 40 -l 50 | ./atis_decoder <udp_host> <udp_port>
```

Example sending to localhost port 5005:

```bash
rtl_fm -f 156.500M -M fm -s 24000 -g 40 -l 50 | ./atis_decoder 127.0.0.1 5005
```

The decoded ATIS number is printed to stdout and sent as a plain ASCII UDP datagram:

```
ATIS: 9224044408
```

### Parameters

| Parameter | Description |
|-----------|-------------|
| `-f 156.500M` | Tune to 156.500 MHz (VHF maritime channel 10) |
| `-M fm` | Narrowband FM demodulation |
| `-s 24000` | Output sample rate — must be 24000 |
| `-g 40` | Tuner gain in dB, adjust for your environment |
| `-l 50` | Squelch level, increase to suppress noise, decrease if signal is cut |

---

## Run as a systemd service

Copy the included service file:

```bash
sudo cp atis-decoder.service /etc/systemd/system/
```

Edit `atis-decoder.service` to match your username and paths, then:

```bash
sudo systemctl daemon-reload
sudo systemctl enable atis-decoder
sudo systemctl start atis-decoder
```

Check status:

```bash
sudo systemctl status atis-decoder
journalctl -u atis-decoder -f
```

---

## Node-RED integration

Add a **UDP input node** listening on the configured port (e.g. 5005). The payload will arrive as a plain string containing the ATIS number followed by a newline.

To control the service from Node-RED exec nodes:

```
/usr/bin/systemctl start atis-decoder
/usr/bin/systemctl stop atis-decoder
```

Allow passwordless sudo for these commands by adding to `/etc/sudoers` via `visudo`:

```
youruser ALL=(ALL) NOPASSWD: /usr/bin/systemctl start atis-decoder, /usr/bin/systemctl stop atis-decoder
```

---

## Frequency reference

| Channel | Frequency | Use |
|---------|-----------|-----|
| 10 | 156.500 MHz | ATIS (inland waterways, Netherlands/Belgium/Germany) |
| 70 | 156.525 MHz | DSC distress and calling |

---

## License

MIT
