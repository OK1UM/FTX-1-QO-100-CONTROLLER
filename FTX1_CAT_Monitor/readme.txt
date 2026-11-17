FTX-1 QO-100 CONTROLLER / MONITOR

Information and function reference Based on:
FTX1_CAT_Monitor.ino

Purpose

This Arduino Nano application controls and monitors a Yaesu FTX-1
through CAT-3. It provides a dedicated QO-100 operating mode, a NORMAL /
MONITOR ONLY mode, OLED status display, encoder-based DX offset control,
MAIN-to-SUB automatic synchronization, operator-calibrated offset
support, and an antenna relay output.

The controller uses the FTX1CAT library for all FTX-1 CAT communication.
The main sketch does not implement the low-level CAT protocol itself.

HARDWARE

Arduino Nano / ATmega328P

FTX-1 CAT-3: D0 / RX <- FTX-1 CAT-3 TX D1 / TX -> FTX-1 CAT-3 RX 38400
baud, 8N1

OLED SSD1306: I2C SDA = A4 I2C SCL = A5

Controls: D2 Encoder A D3 Encoder B D4 Encoder push button D5 Beeper D6
SYNC / CLAR button D7 OWN OFFSET button D8 Antenna relay

BUTTONS AND CONTROLS

1.  ENCODER + PUSH BUTTON (D4)

Short press: - In QO-100 mode: re-initializes QO-100. - In NORMAL /
MONITOR ONLY mode: returns to QO-100 mode and performs the complete
QO-100 initialization.

Long press (900 ms): - In QO-100 mode: switches to NORMAL / MONITOR
ONLY. - In NORMAL / MONITOR ONLY mode: switches to QO-100.

Encoder rotation: - Active only in QO-100 mode. - Changes the
application-level DX offset in 2.5 kHz steps. - Range: -25 kHz to +25
kHz. - After +25 kHz or -25 kHz, the next step wraps to 0 kHz. - When
SYNC is enabled, the new SUB frequency is applied immediately. - The
encoder DX offset is NOT FTX-1 RIT/XIT.

2.  SYNC / CLAR BUTTON (D6)

Short press: - Toggles SYNC and CLAR behavior.

SYNC ON: - MAIN follows the operator’s MAIN tuning. - After MAIN has
been stable for 300 ms, SUB is calculated and updated. - Synchronization
is allowed only when MAIN is within 28.450 to 29.050 MHz. - The active
MAIN-to-SUB offset and DX offset are used.

CLAR: - Automatic MAIN-to-SUB synchronization is disabled. - SUB is
deliberately left untouched.

When SYNC is enabled again: - The current SUB frequency becomes the
reference. - MAIN is reconstructed as: MAIN = SUB - active offset - DX
offset - SUB itself is not rewritten during this restoration.

Long press: - Calibrates the operator’s MAIN-to-SUB offset from the
current radio frequencies. - The calculated value can then be activated
with the OWN OFFSET control.

3.  OWN OFFSET BUTTON (D7)

Short press: - Toggles the operator-calibrated offset active/inactive. -
The setting is stored in EEPROM. - SUB is immediately recalculated from
MAIN using the newly selected offset. - MAIN is never changed.

Long press: - Restores the original project standard offset: 403.500
MHz - Deactivates the operator offset. - Stores the restored setting in
EEPROM. - Immediately restores SUB according to the standard offset. -
MAIN remains unchanged.

QO-100 MODE

Startup and QO-100 initialization use:

MAIN = 28.815.000 MHz SUB = 432.315.000 MHz SPLIT = ON TX side = SUB
MAIN mode = USB SUB mode = USB DX offset = 0 Hz operator offset =
inactive synchronization = ON

Important initialization detail: SPLIT is enabled BEFORE MAIN and SUB
are programmed. This is required when returning from NORMAL / MONITOR
ONLY, where SPLIT is OFF.

After initialization, the operator has full control of the FTX-1. The
controller does not continuously enforce SPLIT, TX side, modes,
RIT/CLAR, XIT, or other radio settings.

The only normal operating radio write performed by automatic QO-100
maintenance is a SUB frequency update used for MAIN-to-SUB
synchronization.

NORMAL / MONITOR ONLY MODE

Initialization uses:

MAIN = 14.200.000 MHz MAIN mode = USB SPLIT = OFF

After initialization the operator again has full control.

In NORMAL mode: - The encoder does not change the DX offset. - Automatic
MAIN-to-SUB synchronization is disabled. - The antenna relay is switched
to the NORMAL state.

QO-100 FREQUENCY RELATIONSHIP

Standard internal MAIN-to-SUB relationship:

SUB = MAIN + 403.500 MHz

Example:

MAIN = 28.815 MHz SUB = 432.315 MHz

The application-level DX offset is then added:

SUB = MAIN + active_offset + DX_offset

The allowed automatic synchronization window is:

MAIN = 28.450 MHz … 29.050 MHz

Outside this range, SUB is not modified automatically.

LO CONSTANTS

QO-100 RX conversion reference:

QO100_RX_LO_HZ = 10.461 GHz

QO-100 TX conversion reference:

QO100_TX_LO_HZ = 1.968 GHz

These constants document the internal QO-100 conversion frequencies used
by the project.

MAIN .INO CONSTANTS

Hardware pins: ENC_A_PIN = 2 Encoder A input.

ENC_B_PIN = 3 Encoder B input.

ENC_BTN_PIN = 4 Encoder push button.

BEEP_PIN = 5 Audible feedback output.

SYNC_BTN_PIN = 6 SYNC / CLAR button.

OWN_OFFSET_BTN_PIN = 7 OWN OFFSET button.

ANTENNA_RELAY_PIN = 8 QO-100 / NORMAL antenna relay control.

Timing: CAT_POLL_MS = 300 ms CAT state polling interval.

BUTTON_DEBOUNCE = 20 ms Button debounce time.

LONG_PRESS_MS = 900 ms Long-press recognition threshold.

VFO_SYNC_DELAY_MS = 300 ms Required MAIN stability time before automatic
SUB synchronization.

QO-100 initialization: QO100_MAIN_INIT_HZ = 28,815,000 Hz Initial MAIN
frequency.

QO100_SUB_INIT_HZ = 432,315,000 Hz Initial SUB frequency.

QO100_MAIN_MIN_HZ = 28,450,000 Hz Lower limit for automatic
synchronization.

QO100_MAIN_MAX_HZ = 29,050,000 Hz Upper limit for automatic
synchronization.

QO100_RX_LO_HZ = 10,461,000,000 Hz RX LO reference.

QO100_TX_LO_HZ = 1,968,000,000 Hz TX LO reference.

STANDARD_OFFSET_HZ = 403,500,000 Hz Standard MAIN-to-SUB relationship.

DX offset: DX_MIN_HZ = -25,000 Hz DX_MAX_HZ = +25,000 Hz DX_STEP_HZ =
2,500 Hz

These values define the application-level DX offset controlled by the
encoder.

EEPROM: OWN_OFFSET_EE_MAGIC = 0x4F46 Validation marker for stored
operator offset data.

OWN_OFFSET_EE_ADDR = 0 EEPROM address of the operator offset data.

OWN_OFFSET_ACTIVE_EE_ADDR = 6 EEPROM address storing whether the
operator offset is active.

NORMAL mode: NORMAL_MAIN_HZ = 14,200,000 Hz Initial MAIN frequency for
NORMAL / MONITOR ONLY mode.

RUNTIME STATE VARIABLES

controllerMode Current controller mode: QO-100 or NORMAL.

dxOffsetHz Current application-level DX offset.

ownOffsetHz Stored operator-calibrated MAIN-to-SUB offset.

ownOffsetActive Indicates whether the operator-calibrated offset is
currently active.

syncEnabled TRUE = SYNC; FALSE = CLAR behavior.

rigState Latest complete FTX-1 state read from CAT.

stateValid Indicates whether the latest CAT state is valid.

writeInProgress Prevents conflicting controller operations during a CAT
write.

nextPoll Scheduling point for the next CAT poll.

subSyncPending Indicates that a delayed MAIN-to-SUB synchronization is
waiting.

mainChangedAt Timestamp of the latest detected MAIN frequency change.

previousMainHz Previous MAIN frequency used for change detection.

havePreviousMain Indicates that a valid MAIN baseline has already been
acquired.

offsetMessageUntil End time for temporary OLED offset confirmation text.

MAIN .INO FUNCTIONS

encoderISR() Interrupt service routine for the rotary encoder. Detects
encoder transitions and accumulates the direction/count.

beepKey() Generates the short key/action confirmation beep.

beepMode() Generates the mode-change confirmation beep.

formatInternalFrequency() Formats an internal FTX-1 frequency for OLED
presentation.

formatQO100Frequency() Formats a QO-100 frequency for OLED presentation.

getActiveOffsetHz() Returns the currently active MAIN-to-SUB offset:
standard offset or operator-calibrated offset.

saveOwnOffset() Stores the operator offset and its active/inactive state
in EEPROM.

loadOwnOffset() Loads the operator offset configuration from EEPROM at
startup.

showOffsetSavedMessage() Activates the temporary OLED confirmation
message for offset changes.

calculateSubHz() Calculates the required SUB frequency from MAIN, the
active offset, and the current DX offset.

initializeQO100() Performs the complete one-time QO-100
initialization: - resets DX offset to zero - enables synchronization -
clears pending synchronization state - deactivates the operator offset -
clears the temporary offset message - enables SPLIT first - sets MAIN to
28.815 MHz - sets SUB to 432.315 MHz - sets MAIN and SUB to USB -
selects SUB as TX side - selects QO-100 controller mode - updates the
antenna relay

initializeNormal() Performs the one-time NORMAL / MONITOR ONLY
initialization: - clears pending synchronization - resets DX offset -
sets MAIN to 14.200 MHz - sets MAIN to USB - turns SPLIT OFF - selects
NORMAL controller mode - updates the antenna relay

maintainQO100() Implements the automatic MAIN-to-SUB synchronization
logic. It detects real MAIN frequency changes, waits for the configured
300 ms settling time, checks the allowed MAIN range, calculates the
required SUB frequency, and writes SUB only when necessary.

It deliberately does NOT enforce: - SPLIT state - TX side - radio mode -
RIT / CLAR - XIT - other operator-controlled radio functions

updateEncoder() Processes the accumulated encoder movement. In QO-100
mode it changes the DX offset and, when SYNC is active, immediately
updates SUB.

calibrateOwnOffsetFromRadio() Calculates the operator’s MAIN-to-SUB
offset from the current radio frequencies.

updateSyncButton() Handles the SYNC / CLAR button: - debounce - short
press toggle - restoration of MAIN from SUB when SYNC is re-enabled -
long-press offset calibration

applyActiveOffsetImmediately() Immediately recalculates and applies SUB
using the current MAIN, active offset, and DX offset.

updateOwnOffsetButton() Handles the OWN OFFSET button: - short press
activates/deactivates the stored operator offset - long press restores
the standard 403.500 MHz offset

updateButton() Handles the main encoder push button: - short press =
QO-100 initialization - long press = QO-100 / NORMAL mode change

drawDisplay() Updates the OLED display with controller status,
frequencies, mode information, synchronization state, offsets, and CAT
status.

pollCAT() Periodically reads the complete FTX-1 CAT state. On a valid
state it updates the internal state and, in QO-100 mode, calls
maintainQO100(). It then refreshes the OLED display.

updateAntennaRelay() Sets the antenna relay according to controller
mode: QO-100 = relay HIGH NORMAL = relay LOW

setup() Initializes: - Arduino I/O - serial communication - encoder
interrupts - I2C - OLED - FTX1CAT library - EEPROM-stored operator
offset - initial QO-100 mode

Startup always enters QO-100 mode.

loop() Main application loop. It continuously processes: - encoder
input - main button - SYNC / CLAR button - OWN OFFSET button - CAT
polling and OLED updates

CONTROL LOGIC SUMMARY

The controller has two application modes:

QO-100 - Full QO-100 initialization available. - Encoder controls DX
offset. - Optional automatic MAIN -> SUB synchronization. - MAIN
synchronization limited to 28.450-29.050 MHz. - Operator retains control
of all other FTX-1 functions.

NORMAL / MONITOR ONLY - MAIN initialized to 14.200 MHz. - SPLIT OFF. -
No automatic MAIN -> SUB synchronization. - Encoder DX control disabled.

The most important design principle is that the controller assists the
operator but does not continuously take control of the transceiver.
During normal QO-100 operation, the only automatic CAT write is the
SUB-frequency update required by the MAIN-to-SUB synchronization logic.

RELATED FTX1CAT LIBRARY

The main sketch uses the FTX1CAT class for CAT communication. The
library provides MAIN/SUB frequency control, SPLIT and TX-side control,
mode control, RIT control, TX status, IF reading, and complete
radio-state reading. The library default CAT speed is 38400 baud. The
library’s public interface and state structure are defined in FTX1CAT.h.
