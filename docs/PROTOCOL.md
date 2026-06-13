# PACKTER protocol format

This document specifies the wire protocol between the PACKTER viewer and a
PACKTER agent. The viewer (a traffic visualizer) listens for UDP datagrams on
**UDP port 11300**. To draw flying objects, show messages, or play sounds, an
agent sends a control UDP datagram to the viewer.

> Adapted and corrected from the original 2010 `ProtocolFormat` wiki. The PACKTER
> 3.0 broker still accepts every command below (lenient parsing), so existing
> agents work unchanged. Boxes marked **3.0:** note where behavior has evolved.

> **3.0 transport notes.** The legacy form sends one record per datagram. The
> 3.0 broker also accepts a *bulk* form — a `PACKTER` header followed by many
> record lines in one datagram — and an optional `PACKTERAGENT` line for HMAC
> authentication. See the Compatibility section of the README for the full list
> of accepted forms (bulk, `PACKTEARTH` GeoIP, normalized/integer coordinates,
> legacy `PACTER`/`PACKTERBALLISTIC`/`PACKTERWITHGATEWAY`).

## PACKTER — draw a flying object

```
PACKTER
SRCIP,DSTIP,SRCPORT,DSTPORT,FLAG,DESCRIPTION
```

- `SRCIP`, `DSTIP` — a dotted-quad IPv4 address or a colon-separated IPv6 address.
- `SRCPORT`, `DSTPORT` — a number greater than 0 and less than 65535.
- `FLAG` — identifies the kind of flying object. With PACKTER's default settings
  the flag is 0, 1, … 9.
- `DESCRIPTION` — a string shown as the packet's description in the viewer. It is
  also used for IP traceback (see below).

Example:

```
PACKTER
192.168.1.1,10.0.0.1,49305,80,5,This is a test
```

A flying object departs from (192.168.1.1, 49305) toward (10.0.0.1, 80); its
kind is 5. When the object is selected, the viewer shows "This is a test".

```
PACKTER
192.168.1.1,10.0.0.1,49305,80,4,0123456789ABCDEF0123456789ABCDEF-172.16.45.1
```

Same as above, but `DESCRIPTION` carries an IP-traceback request. When the object
is selected, the operator can start the traceback: the packet hash
`0123456789ABCDEF0123456789ABCDEF` is sent to 172.16.45.1, which runs PACKTER TC.

> **3.0:** `FLAG` is taken modulo 10 and maps to a color/model:
> 0 = TCP ACK, 1 = TCP SYN, 2 = TCP FIN, 3 = UDP, 4 = ICMP, and 5–9 are the IPv6
> counterparts (TCP ACK/SYN/FIN, UDP, ICMPv6). The coordinate fields also accept
> IPv6 literals, normalized coordinates (0–1), and integers (1–65536), not only
> the IPv4/port forms shown here. Traceback ("PACKTER TC") listens on UDP 11301.

## PACKTERMSG — show a message

```
PACKTERMSG
PICFILE,MESSAGE
```

- `PICFILE` — the file name of an image to display.
- `MESSAGE` — the message text to show in the viewer.

Example:

```
PACKTERMSG
pic01.png,This is a message
```

The viewer draws `pic01.png` and shows "This is a message". The image file
`pic01.png` must be available to the viewer (legacy: in the viewer's working
directory; **3.0:** served under `web/assets/legacy/`).

## PACKTERHTML — show an HTML message

```
PACKTERHTML
HTMLMESSAGE
```

- `HTMLMESSAGE` — a string in HTML format. The viewer renders it with the
  browser.

Example:

```
PACKTERHTML
<html><body><b>ABCDEFG</b></body></html>
```

The viewer shows **ABCDEFG**.

> **3.0:** HTML messages are powerful, so gate this on untrusted networks with
> `--require-auth` (see INSTALL).

## PACKTERSE — play a sound effect

```
PACKTERSE
SOUNDFILE
```

- `SOUNDFILE` — the file name of a sound-effect file.

Example:

```
PACKTERSE
sound01.wav
```

The viewer plays `sound01.wav`. The file must be available to the viewer
(legacy: the viewer's working directory; **3.0:** `web/assets/legacy/`).

## PACKTERSOUND — play background music

```
PACKTERSOUND
TIME,SOUNDFILE
```

- `TIME` — a number; how long (in seconds) to play the BGM.
- `SOUNDFILE` — the file name of a BGM file (same location rules as above).

Example:

```
PACKTERSOUND
60,bgm01.wav
```

The viewer plays `bgm01.wav` until 60 seconds have elapsed. If the track is
shorter than `TIME`, it repeats until the time is up.

## PACKTERVOICE — speak text

```
PACKTERVOICE
SOUNDTEXT
```

- `SOUNDTEXT` — the text to be spoken.

Example:

```
PACKTERVOICE
ABCDEFG
```

The viewer speaks the text.

> **3.0:** the viewer speaks `SOUNDTEXT` with the browser's built-in Web Speech
> API — no external text-to-speech program is configured or launched. Send the
> plain words to speak; the legacy convention of leading
> `/option /parameter …` arguments for an external program no longer applies.

## PACKTERSKYDOMETEXTURE — set the sky-dome texture

```
PACKTERSKYDOMETEXTURE
TEXTUREFILE
```

- `TEXTUREFILE` — the file name of the sky-dome texture.

Example:

```
PACKTERSKYDOMETEXTURE
texture1.png
```

The viewer uses `texture1.png` as the sky-dome texture.

> **3.0:** any web-renderable image works (PNG/JPG/…); the file is loaded from
> `web/assets/legacy/`. (The legacy examples showed a `.bmp`; that was just an
> example, not a requirement.)
