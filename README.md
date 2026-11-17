# FTX-1 CAT Monitor

A compact Arduino-based CAT monitor and QO-100 DX controller for the **Yaesu FTX-1**.

This is a reduced version of the original FT-840 monitor project. It focuses on two functions:

- **QO-100 DX** – calculates and synchronizes the SUB transmit frequency from the MAIN receive frequency.
- **CAT Bridge** – provides a transparent CAT connection between the FTX-1 and PC software such as Omni-Rig.

---

## Features

### QO-100 DX

The controller uses the FTX-1 MAIN and SUB VFOs for QO-100 operation:

| Function | Setting |
|---|---|
| MAIN | RX: 28.500–29.000 MHz |
| SUB | TX: 432.000–432.500 MHz |
| Split | ON |
| TX side | SUB |
| Mode | USB |
| RIT | OFF |

The SUB frequency is calculated as:

```text
SUB = MAIN + 403.500.000 Hz + DX offset
```

The adjustable DX offset has the following range and step:

- Range: **−25.0 kHz to +25.0 kHz**
- Step: **2.5 kHz**

### CAT Bridge

The CAT Bridge passes CAT traffic between the FTX-1 and the PC without generating its own polling traffic.

- Radio side: hardware UART (`Serial`), **38400 baud, 8N1**
- PC / Omni-Rig side: `SoftwareSerial`, pins **D10 / D11**, **4800 baud**
- The bridge remains passive and transparent.
- A CAT response is considered received when a complete ASCII response terminated by `;` arrives from the FTX-1.

### CAT / ERR indication

The meaning of the CAT/ERR status depends on the operating mode:

- **QO-100 DX:** based on validated replies returned by `FTX1CAT::readState()`.
- **CAT Bridge:** passive indication; a complete CAT response ending in `;` must be received from the FTX-1.

---

## FTX-1 configuration

For CAT communication, configure the radio as shown below.

1. In **Operation Setting**, select **TUN/LIN PORT SELECT → CAT-3**.
2. Set **CAT-1 RATE → 38400 bps**.
3. In the CAT-3 settings, set **CAT-3 RATE → 38400 bps**.
4. The displayed **CAT-3 TIME OUT TIMER** setting is **100 msec**.

### CAT-3 port selection

![FTX-1 TUN/LIN port selection](FTX1_op_set_CAT3_1.jpg)

### CAT-3 communication rate

![FTX-1 CAT-3 rate settings](FTX1_op_set_CAT3_2.jpg)

---

## Hardware and display

The project is shown running on an Arduino Nano with an OLED display.

### CAT monitor

![FTX-1 CAT monitor showing the CAT-3 connection screen](QO100err.jpg)

### QO-100 DX synchronized

![QO-100 DX screen showing RX, TX and synchronization status](QO100sync.jpg)

### Radio operating screen

![FTX-1 operating screen with MAIN and SUB frequencies](FTX1sync.jpg)

---

## Installation

1. Open the Arduino sketch in the Arduino IDE.
2. Install the **FTX1CAT** library and make sure it is available to the sketch.
3. Connect the controller to the FTX-1 CAT-3 interface.
4. Connect the PC / Omni-Rig serial interface to Arduino pins D10 and D11.
5. Select the correct Arduino board and serial port, then compile and upload the sketch.
6. Configure the FTX-1 CAT settings as described above.

> **Important:** The `FTX1CAT` library must be installed alongside this sketch (in the Arduino libraries folder).

---

## Important notes

- QO-100 DX initialization does **not** use the PC/1W command. The documented FTX-1 CAT power command for the field head does not provide a 1 W value.
- The original project's Omni-Rig virtual status protocol is retained. Only the radio-side state source has been changed to the FTX-1.
- The CAT Bridge does not generate polling commands; it forwards traffic transparently.

---

## Demonstration

[▶ Watch the FTX-1 QO-100 demonstration video](FTX1_QO100_50procent.mp4)

---

## License

See the [`LICENSE`](LICENSE) file for the terms of use.

**73! Martin OK1UM**
