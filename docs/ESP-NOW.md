# FluidDial ESP-NOW *(experimental)*

This is my implementation of ESP-NOW for the FluidDial. ESP-NOW is a new, optional connection mode for FluidDial that lets the pendant communicate directly with your FluidNC controller over a peer-to-peer encrypted radio link - no router, no WiFi network, no IP address required.

> **Flash the firmware:** Use the **[ESP-NOW Preview Installer](https://figamore.github.io/FluidDial)** to flash this experimental build onto your M5Dial or CYD pendant.

---

## Why ESP-NOW?

This feature was added in response to user interest in a simpler, potentially more power-efficient wireless option:

- **User demand** - some users want wireless control without setting up a WiFi network or dealing with router configuration.
- **Potentially lower power consumption** - ESP-NOW is a lightweight MAC-layer protocol that may reduce radio-on time compared to a full WiFi/WebSocket stack, which could benefit battery-powered pendant builds.
- **No router required** - the pendant talks directly to the FluidNC controller over a point-to-point encrypted radio link, so it works in any shop environment regardless of network availability.

---

## How it works

ESP-NOW uses the ESP32's built-in peer-to-peer radio protocol. After a one-time pairing step, the pendant and the FluidNC controller communicate directly by MAC address using hardware AES-128 encryption - no IP address, no router, no WebSocket.

Pairing uses a **8-digit numeric code**: the pendant displays a countdown screen while broadcasting a discovery beacon. FluidNC matches the code, derives the encryption key, and completes the handshake. Once paired, the credentials are stored in NVS on both sides and pairing does not need to be repeated.

<img src="images/espNowPairingScene.png" width="240" alt="ESP-NOW pairing screen showing 8-digit code and countdown">

---

## Requirements

### FluidNC

> **Note:** ESP-NOW requires a matching FluidNC build with ESP-NOW channel support. Because this has not yet been merged upstream, you must currently flash a temporary fork:
>
> **FluidNC fork:** [`figamore/FluidNC` - `feature/esp-now` branch](https://github.com/figamore/FluidNC/tree/feature/esp-now)
>
> This is temporary. Once the ESP-NOW channel implementation is accepted into the main FluidNC repository, a standard FluidNC release will work without any special build.

---

## Pairing steps

1. Flash the [ESP-NOW preview firmware](https://figamore.github.io/FluidDial) onto your pendant.
1. On first boot (or from Connection Settings), select **ESP-NOW** as the connection mode.
1. The pendant displays a 8-digit code. Add an entry at the bottom of your config.yaml file in the following format:
```yaml
espnow:
    pairing_code: XXXXXXXX
```

4. The controller matches the code, completes the handshake, and both sides store the credentials.
5. From this point on the pendant connects automatically on boot - no re-pairing needed.

---

## WiFi vs ESP-NOW - which should I choose?

| | WiFi | ESP-NOW |
|---|---|---|
| **Router required** | Yes | No |
| **Range** | Limited by your network | ~100 m line-of-sight (ESP32 radio) |
| **Setup complexity** | Moderate (captive portal) | Low (one-time 8-digit pairing) |
| **Network interference** | Shares band with other devices | Dedicated peer channel |
| **Power consumption** | Higher (full WiFi stack) | Potentially lower |

**Choose WiFi if:**
- You already have a reliable network in your shop and want the pendant on it alongside other devices.
- You use FluidNC's built-in web UI or other networked tools alongside the pendant.

**Choose ESP-NOW if:**
- You have no router near your machine, or want to avoid network setup entirely.
- You are running the pendant on battery and want to explore lower power consumption.
- You want the simplest possible wireless setup with minimal configuration.