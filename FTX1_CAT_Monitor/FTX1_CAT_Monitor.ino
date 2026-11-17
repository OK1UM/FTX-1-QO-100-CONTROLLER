/*
  ======================================================================
  FTX1_CAT_Monitor.ino  version 1.0
  QO-100 MAIN-to-SUB automatic synchronization controller / monitor for Yaesu FTX-1 and DX Patrol UP/DOWN converter.
  (C) OK1UM, Martin Lipert, ok1um@seznam.cz
  All rights reserved.
  
  CAT-3 -> Nano hardware Serial (D0/D1)
  OLED  -> I2C A4/A5
  Encoder A/B -> D2/D3
  Encoder button -> D4
  Beeper -> D5
  SYNC/CLAR button -> D6
  OWN OFFSET button -> D7
  ANTENNA RELAY PIN -> 8

  The encoder and button logic use the tested custom encoder routine;
  no external encoder library is required.
  EEPROM	2.0	    EEPROM.h
  Wire	    1.0	    Wire.h
  U8glib	1.19.1	U8glib.h
  ======================================================================
*/

#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>
#include <U8glib.h>
#include "FTX1CAT.h"

// ----------------------------------------------------------------------
// OLED
// ----------------------------------------------------------------------
U8GLIB_SSD1306_128X64 u8g(U8G_I2C_OPT_DEV_0 | U8G_I2C_OPT_NO_ACK | U8G_I2C_OPT_FAST);  // Fast I2C / TWI


// ----------------------------------------------------------------------
// Hardware
// ----------------------------------------------------------------------
static const uint8_t ENC_A_PIN   = 2;
static const uint8_t ENC_B_PIN   = 3;
static const uint8_t ENC_BTN_PIN   = 4;
static const uint8_t BEEP_PIN       = 5;
static const uint8_t ANTENNA_RELAY_PIN = 8;
static const uint8_t SYNC_BTN_PIN   = 6;
static const uint8_t OWN_OFFSET_BTN_PIN = 7;

// ----------------------------------------------------------------------
// FTX-1 CAT-3
// ----------------------------------------------------------------------
// FTX-1 CAT-3 -> Nano hardware Serial D0/D1
// D0 = RX
// D1 = TX
// Hardware UART = 38400 Bd, 8N1
//
// USB is used only for firmware upload.
// ----------------------------------------------------------------------
static FTX1CAT rig(Serial);    

// ----------------------------------------------------------------------
// Timing
// ----------------------------------------------------------------------
static const uint32_t CAT_POLL_MS       = 300UL;
static const uint32_t BUTTON_DEBOUNCE   = 20UL;
static const uint32_t LONG_PRESS_MS     = 900UL;
static const uint32_t VFO_SYNC_DELAY_MS = 300UL;
static const uint32_t SPLASH_DURATION_MS = 2500UL;

// ----------------------------------------------------------------------
// QO-100 controller values
// ----------------------------------------------------------------------
static const uint32_t QO100_MAIN_INIT_HZ = 28815000UL;
static const uint32_t QO100_SUB_INIT_HZ  = 432315000UL;

// MAIN frequency range in which automatic MAIN -> SUB synchronization
// is permitted. Outside this range SUB is left untouched.
static const uint32_t QO100_MAIN_MIN_HZ = 28450000UL;
static const uint32_t QO100_MAIN_MAX_HZ = 29050000UL;

// FTX-1 internal MAIN -> QO-100 RX conversion.
// 28.815 MHz + 10.461 GHz = 10.489815 GHz.
static const int64_t QO100_RX_LO_HZ = 10461000000LL;

// FTX-1 internal SUB -> QO-100 TX conversion.
// 432.315 MHz + 1.968 GHz = 2.400315 GHz.
static const int64_t QO100_TX_LO_HZ = 1968000000LL;

// Internal FTX-1 MAIN -> SUB relationship used by this project.
// Standard MAIN -> SUB offset. This is the original project relationship.
static const int32_t STANDARD_OFFSET_HZ = 403500000L;

// Operator-calibrated MAIN -> SUB offset.
static uint32_t ownOffsetHz = (uint32_t)STANDARD_OFFSET_HZ;
static bool ownOffsetActive = false;

// EEPROM layout for the operator offset.
static const uint16_t OWN_OFFSET_EE_MAGIC = 0x4F46;
static const int OWN_OFFSET_EE_ADDR = 0;
static const int OWN_OFFSET_ACTIVE_EE_ADDR = 6;

// Application-level QO-100 DX offset. This is NOT FTX-1 RIT/XIT.
static const int32_t DX_MIN_HZ  = -25000L;
static const int32_t DX_MAX_HZ  =  25000L;
static const int32_t DX_STEP_HZ =   2500L;

// ----------------------------------------------------------------------
// NORMAL mode
// ----------------------------------------------------------------------
static const uint32_t NORMAL_MAIN_HZ = 14200000UL;

// ----------------------------------------------------------------------
// Controller state
// ----------------------------------------------------------------------
enum ControllerMode {
    CONTROLLER_QO100 = 0,
    CONTROLLER_NORMAL
};

static ControllerMode controllerMode = CONTROLLER_QO100;
static int32_t dxOffsetHz = 0;

// MAIN -> SUB automatic synchronization.
// ON  = SYNC: SUB follows MAIN.
// OFF = CLAR: SUB is left untouched.
static bool syncEnabled = true;

static FTX1CAT::State rigState;
static bool stateValid = false;
static bool writeInProgress = false;
static uint32_t nextPoll = 0;

// Delayed MAIN -> SUB synchronization, retained from the proven FT-840
// controller concept. This prevents SUB from chasing every CAT frequency
// update while the operator is tuning MAIN.
static bool subSyncPending = false;
static uint32_t mainChangedAt = 0;
static uint32_t previousMainHz = 0;
static bool havePreviousMain = false;

// Temporary OLED confirmation after saving/restoring the operator offset.
static uint32_t offsetMessageUntil = 0;

// ----------------------------------------------------------------------
// Tested encoder — no external library
// ----------------------------------------------------------------------
volatile int8_t encDelta = 0;
volatile uint8_t encLast = 0;

void encoderISR()
{
    static const int8_t table[16] = {
         0, -1,  1,  0,
         1,  0,  0, -1,
        -1,  0,  0,  1,
         0,  1, -1,  0
    };

    uint8_t state = encLast & 0x03;

    state = (uint8_t)((state << 2) |
             ((digitalRead(ENC_A_PIN) << 1) |
               digitalRead(ENC_B_PIN)));

    encLast = state & 0x03;
    encDelta += table[state];
}

// ----------------------------------------------------------------------
// Beeper
// ----------------------------------------------------------------------
static void beepKey()
{
    tone(BEEP_PIN, 800, 30);
}

static void beepMode()
{
    tone(BEEP_PIN, 1000, 45);
    delay(55);
    tone(BEEP_PIN, 1200, 45);
}

// ----------------------------------------------------------------------
// Formatting
// ----------------------------------------------------------------------
static void formatInternalFrequency(uint32_t hz, char *out, size_t outSize)
{
    snprintf(out, outSize, "%lu.%03lu.%02lu",
             (unsigned long)(hz / 1000000UL),
             (unsigned long)((hz / 1000UL) % 1000UL),
             (unsigned long)((hz / 10UL) % 100UL));
}

static void formatQO100Frequency(int64_t hz, char *out, size_t outSize)
{
    if (hz < 0) {
        snprintf(out, outSize, "---------");
        return;
    }

    uint64_t f = (uint64_t)hz;

    snprintf(out, outSize, "%lu.%03lu.%02lu",
             (unsigned long)(f / 1000000ULL),
             (unsigned long)((f / 1000ULL) % 1000ULL),
             (unsigned long)((f / 10ULL) % 100ULL));
}

static uint32_t getActiveOffsetHz()
{
    return ownOffsetActive ? ownOffsetHz : (uint32_t)STANDARD_OFFSET_HZ;
}

static void saveOwnOffset()
{
    EEPROM.put(OWN_OFFSET_EE_ADDR, ownOffsetHz);
    EEPROM.put(OWN_OFFSET_EE_ADDR + 4, OWN_OFFSET_EE_MAGIC);
    EEPROM.update(OWN_OFFSET_ACTIVE_EE_ADDR, ownOffsetActive ? 1 : 0);
}

static void loadOwnOffset()
{
    uint32_t storedOffset = 0;
    uint16_t storedMagic = 0;

    EEPROM.get(OWN_OFFSET_EE_ADDR, storedOffset);
    EEPROM.get(OWN_OFFSET_EE_ADDR + 4, storedMagic);

    if (storedMagic == OWN_OFFSET_EE_MAGIC &&
        storedOffset >= 300000000UL &&
        storedOffset <= 500000000UL) {
        ownOffsetHz = storedOffset;
        ownOffsetActive = EEPROM.read(OWN_OFFSET_ACTIVE_EE_ADDR) != 0;
    }
    else {
        ownOffsetHz = (uint32_t)STANDARD_OFFSET_HZ;
        ownOffsetActive = false;
    }
}

static void showOffsetSavedMessage()
{
    offsetMessageUntil = millis() + 2000UL;
}

static int32_t calculateSubHz(uint32_t mainHz)
{
    int64_t value = (int64_t)mainHz +
                    (int64_t)getActiveOffsetHz() +
                    (int64_t)dxOffsetHz;

    if (value < 30000LL)
        value = 30000LL;

    if (value > 470000000LL)
        value = 470000000LL;

    return (int32_t)value;
}

// ----------------------------------------------------------------------
// QO-100 initialization
// ----------------------------------------------------------------------
static bool initializeQO100()
{
    bool ok = true;
    writeInProgress = true;

    dxOffsetHz = 0;
    syncEnabled = true;
    subSyncPending = false;
    havePreviousMain = false;

    // Returning to QO-100 always starts from the standard offset.
    // The operator-calibrated offset is not active after initialization.
    ownOffsetActive = false;
    offsetMessageUntil = 0;

    // One-time QO-100 startup configuration.
    // After this initialization has completed, the operator has full control
    // over all transceiver functions. The controller does NOT enforce any
    // of these settings during normal operation.
    //
    // IMPORTANT: enable SPLIT before programming MAIN and SUB.
    // This is required when returning from NORMAL / MONITOR ONLY, where
    // SPLIT has been turned OFF. Otherwise the first initialization can
    // leave SUB equal to MAIN and a second button press is required.
    ok &= rig.setSplit(true);
    ok &= rig.setMainFrequencyHz(QO100_MAIN_INIT_HZ);
    ok &= rig.setSubFrequencyHz(QO100_SUB_INIT_HZ);
    ok &= rig.setMode(FTX1CAT::MAIN, FTX1CAT::MODE_USB);
    ok &= rig.setMode(FTX1CAT::SUB,  FTX1CAT::MODE_USB);
    ok &= rig.setTxSide(FTX1CAT::SUB);

    controllerMode = CONTROLLER_QO100;
    updateAntennaRelay();

    writeInProgress = false;
    return ok;
}

// ----------------------------------------------------------------------
// NORMAL initialization
// ----------------------------------------------------------------------
static bool initializeNormal()
{
    bool ok = true;
    writeInProgress = true;

    subSyncPending = false;
    havePreviousMain = false;
    dxOffsetHz = 0;

    // One-time NORMAL startup configuration.
    // After initialization, the operator has full control.
    ok &= rig.setMainFrequencyHz(NORMAL_MAIN_HZ);
    ok &= rig.setMode(FTX1CAT::MAIN, FTX1CAT::MODE_USB);
    ok &= rig.setSplit(false);

    controllerMode = CONTROLLER_NORMAL;
    updateAntennaRelay();

    writeInProgress = false;
    return ok;
}

// ----------------------------------------------------------------------
// QO-100 maintenance
// MAIN is master; SUB follows MAIN after the tuning delay.
// ----------------------------------------------------------------------
static void maintainQO100(uint32_t now)
{
    if (!stateValid || writeInProgress)
        return;

    // CLAR mode: automatic MAIN -> SUB synchronization is disabled.
    // The SUB frequency is deliberately left untouched.
    if (!syncEnabled) {
        subSyncPending = false;
        return;
    }

    // ABSOLUTE RULE: the operator has 100% control of the transceiver.
    // Do not enforce SPLIT, TX side, MODE, CLAR/RIT, XIT or any other
    // radio function here.
    //
    // The ONLY radio write allowed in this project is FBxxxx; to synchronize
    // SUB after a real MAIN-frequency change. RIT/CLAR is never written and
    // never participates in the MAIN -> SUB calculation.

    // Automatic MAIN -> SUB synchronization is allowed only while MAIN
    // is inside the QO-100 operating window.
    const bool mainInQO100Range =
        rigState.mainFrequencyHz >= QO100_MAIN_MIN_HZ &&
        rigState.mainFrequencyHz <= QO100_MAIN_MAX_HZ;

    // Detect operator/PC changes of MAIN.
    if (!havePreviousMain) {
        previousMainHz = rigState.mainFrequencyHz;
        havePreviousMain = true;

        // First poll is only a baseline acquisition. It is NOT a MAIN change.
        // Therefore SUB must not be written merely because Arduino started.
        subSyncPending = false;
    }
    else if (rigState.mainFrequencyHz != previousMainHz) {
        previousMainHz = rigState.mainFrequencyHz;

        if (mainInQO100Range) {
            // Restart the settling timer on every MAIN frequency change.
            mainChangedAt = now;
            subSyncPending = true;
        } else {
            // MAIN left the permitted range: cancel any pending SUB update.
            subSyncPending = false;
        }
    }
    else if (!mainInQO100Range) {
        // Keep SUB completely untouched while MAIN is outside the range.
        subSyncPending = false;
    }

    // Do not move SUB until MAIN has remained stable for the full delay.
    // Re-check the frequency range immediately before writing SUB so a
    // tuning excursion outside the permitted range can never cause a
    // delayed SUB write.
    if (subSyncPending &&
        mainInQO100Range &&
        (uint32_t)(now - mainChangedAt) >= VFO_SYNC_DELAY_MS) {

        int32_t desiredSub = calculateSubHz(rigState.mainFrequencyHz);

        if ((int32_t)rigState.subFrequencyHz != desiredSub) {
            writeInProgress = true;
            bool ok = rig.setSubFrequencyHz((uint32_t)desiredSub);
            writeInProgress = false;

            if (ok) {
                rigState.subFrequencyHz = (uint32_t)desiredSub;
            }
        }

        subSyncPending = false;
    }
}

// ----------------------------------------------------------------------
// Encoder
// ----------------------------------------------------------------------
static void updateEncoder()
{
    static int8_t encAcc = 0;

    noInterrupts();
    int8_t delta = encDelta;
    encDelta = 0;
    interrupts();

    encAcc += delta;

    int8_t step = 0;

    // Proven encoder: four transitions per mechanical detent.
    if (encAcc >= 4) {
        step = +1;
        encAcc = 0;
    }
    else if (encAcc <= -4) {
        step = -1;
        encAcc = 0;
    }

    if (step == 0)
        return;

    // NORMAL = monitor only.
    if (controllerMode != CONTROLLER_QO100)
        return;

    dxOffsetHz += (int32_t)step * DX_STEP_HZ;

    // Preserve the original controller's wrap-around behavior:
    // +25 kHz -> next step returns to 0 kHz;
    // -25 kHz -> next step returns to 0 kHz.
    if (dxOffsetHz > DX_MAX_HZ)
        dxOffsetHz = 0;

    if (dxOffsetHz < DX_MIN_HZ)
        dxOffsetHz = 0;

    // DX changes are applied to SUB immediately when SYNC is enabled.
    // No 300 ms settling delay is used for DX changes.
    if (syncEnabled &&
        rigState.mainFrequencyHz >= QO100_MAIN_MIN_HZ &&
        rigState.mainFrequencyHz <= QO100_MAIN_MAX_HZ) {

        int32_t desiredSub = calculateSubHz(rigState.mainFrequencyHz);

        writeInProgress = true;
        bool ok = rig.setSubFrequencyHz((uint32_t)desiredSub);
        writeInProgress = false;

        if (ok) {
            rigState.subFrequencyHz = (uint32_t)desiredSub;
            subSyncPending = false;
        } else {
            // Keep a pending request so the normal synchronization path
            // can retry it on the next maintenance cycle.
            subSyncPending = true;
            mainChangedAt = millis();
        }
    } else {
        // In CLAR mode, or outside the allowed MAIN range, SUB is untouched.
        subSyncPending = false;
    }

    beepKey();
}

// ----------------------------------------------------------------------
// SYNC / CLAR button
// Short press:
//   SYNC -> CLAR : disable automatic MAIN -> SUB synchronization.
//   CLAR -> SYNC : enable synchronization and immediately align SUB.
//
// Long press:
//   Read actual MAIN and SUB from FTX-1, calculate:
//       OWN OFFSET = SUB - MAIN
//   Save the result to EEPROM and activate OWN OFFSET.
//
// The button is active LOW and uses the internal pull-up.
// ----------------------------------------------------------------------
static bool syncButtonStable = HIGH;
static bool syncButtonRawLast = HIGH;
static uint32_t syncButtonRawChangedAt = 0;
static uint32_t syncButtonPressedAt = 0;
static bool syncButtonLongPressHandled = false;

static void calibrateOwnOffsetFromRadio()
{
    if (controllerMode != CONTROLLER_QO100 || !stateValid)
        return;

    uint32_t mainHz = 0;
    uint32_t subHz = 0;

    // Use the FTX1CAT library for both frequency reads.
    if (!rig.readMainFrequencyHz(mainHz))
        return;

    if (!rig.readSubFrequencyHz(subHz))
        return;

    if (subHz <= mainHz)
        return;

    uint32_t calculatedOffset = subHz - mainHz;

    // Accept only a physically sensible MAIN -> SUB relationship.
    if (calculatedOffset < 300000000UL ||
        calculatedOffset > 500000000UL)
        return;

    ownOffsetHz = calculatedOffset;
    ownOffsetActive = true;
    saveOwnOffset();
    showOffsetSavedMessage();

    // Keep the current radio state as the reference. The calibration itself
    // must never write a frequency back to the FTX-1.
    rigState.mainFrequencyHz = mainHz;
    rigState.subFrequencyHz = subHz;
    previousMainHz = mainHz;
    havePreviousMain = true;
    subSyncPending = false;
}

static void updateSyncButton()
{
    uint32_t now = millis();
    bool raw = digitalRead(SYNC_BTN_PIN);

    if (raw != syncButtonRawLast) {
        syncButtonRawLast = raw;
        syncButtonRawChangedAt = now;
    }

    if ((now - syncButtonRawChangedAt) >= BUTTON_DEBOUNCE &&
        raw != syncButtonStable) {

        syncButtonStable = raw;

        if (syncButtonStable == LOW) {
            syncButtonPressedAt = now;
            syncButtonLongPressHandled = false;
        }
        else if (!syncButtonLongPressHandled) {
            // Short press keeps the original SYNC / CLAR function.
            syncEnabled = !syncEnabled;

            tone(BEEP_PIN, 800, 30);

            if (syncEnabled) {
                // When SYNC is enabled again, SUB is the reference.
                // MAIN = SUB - active offset - DX offset.
                subSyncPending = false;

                if (controllerMode == CONTROLLER_QO100 && stateValid) {
                    uint32_t actualSubHz = 0;

                    if (rig.readSubFrequencyHz(actualSubHz)) {
                        int64_t restoredMainHz =
                            (int64_t)actualSubHz -
                            (int64_t)getActiveOffsetHz() -
                            (int64_t)dxOffsetHz;

                        // Do not accept an invalid reconstructed MAIN.
                        if (restoredMainHz >=
                                (int64_t)QO100_MAIN_MIN_HZ &&
                            restoredMainHz <=
                                (int64_t)QO100_MAIN_MAX_HZ) {

                            writeInProgress = true;
                            bool ok = rig.setMainFrequencyHz(
                                (uint32_t)restoredMainHz);
                            writeInProgress = false;

                            if (ok) {
                                // SUB is deliberately NOT written here.
                                // It is the reference from which MAIN was
                                // restored.
                                rigState.subFrequencyHz = actualSubHz;
                                rigState.mainFrequencyHz =
                                    (uint32_t)restoredMainHz;

                                // Prevent maintainQO100() from interpreting
                                // the restored MAIN as a fresh operator
                                // change and writing FB again.
                                previousMainHz =
                                    (uint32_t)restoredMainHz;
                                havePreviousMain = true;
                                subSyncPending = false;
                            }
                        }
                    }
                }
            }
            else {
                // CLAR: never change SUB.
                subSyncPending = false;
            }
        }
    }

    // Long press is handled once, at the same threshold used by the
    // controller's other long-press button.
    if (syncButtonStable == LOW &&
        !syncButtonLongPressHandled &&
        (now - syncButtonPressedAt >= LONG_PRESS_MS)) {

        syncButtonLongPressHandled = true;

        calibrateOwnOffsetFromRadio();
        beepMode();
    }
}

// ----------------------------------------------------------------------
// OWN OFFSET button
// Short press: toggle operator-calibrated offset ON/OFF.
// Long press : restore the standard 403.500.000 Hz offset,
//              save it to EEPROM and turn OWN OFFSET OFF.
//
// The button is active LOW and uses the internal pull-up.
// ----------------------------------------------------------------------
static bool ownOffsetButtonStable = HIGH;
static bool ownOffsetButtonRawLast = HIGH;
static uint32_t ownOffsetButtonRawChangedAt = 0;
static uint32_t ownOffsetButtonPressedAt = 0;
static bool ownOffsetButtonLongPressHandled = false;

static void applyActiveOffsetImmediately()
{
    // OWN OFFSET changes the SUB frequency only.
    // MAIN must NEVER be touched here: the operator is listening on MAIN.
    //
    // This is intentionally immediate; the normal 300 ms MAIN->SUB
    // synchronization delay is not used for a direct OWN OFFSET action.
    if (controllerMode != CONTROLLER_QO100 ||
        !stateValid ||
        !syncEnabled)
        return;

    if (rigState.mainFrequencyHz < QO100_MAIN_MIN_HZ ||
        rigState.mainFrequencyHz > QO100_MAIN_MAX_HZ)
        return;

    int32_t desiredSub = calculateSubHz(rigState.mainFrequencyHz);

    if ((int32_t)rigState.subFrequencyHz == desiredSub) {
        subSyncPending = false;
        return;
    }

    writeInProgress = true;
    bool ok = rig.setSubFrequencyHz((uint32_t)desiredSub);
    writeInProgress = false;

    if (ok) {
        rigState.subFrequencyHz = (uint32_t)desiredSub;
        subSyncPending = false;
    }
    else {
        // Let the normal synchronization path retry the SUB update.
        subSyncPending = true;
        mainChangedAt = millis();
    }
}

static void updateOwnOffsetButton()
{
    uint32_t now = millis();
    bool raw = digitalRead(OWN_OFFSET_BTN_PIN);

    if (raw != ownOffsetButtonRawLast) {
        ownOffsetButtonRawLast = raw;
        ownOffsetButtonRawChangedAt = now;
    }

    if ((now - ownOffsetButtonRawChangedAt) >= BUTTON_DEBOUNCE &&
        raw != ownOffsetButtonStable) {

        ownOffsetButtonStable = raw;

        if (ownOffsetButtonStable == LOW) {
            ownOffsetButtonPressedAt = now;
            ownOffsetButtonLongPressHandled = false;
        }
        else if (!ownOffsetButtonLongPressHandled) {
            // Short press: toggle the operator offset.
            //
            // Immediately move SUB according to the newly selected offset.
            // MAIN is never written or changed.
            ownOffsetActive = !ownOffsetActive;
            saveOwnOffset();
            applyActiveOffsetImmediately();
            showOffsetSavedMessage();
            beepKey();
        }
    }

    if (ownOffsetButtonStable == LOW &&
        !ownOffsetButtonLongPressHandled &&
        (now - ownOffsetButtonPressedAt >= LONG_PRESS_MS)) {

        ownOffsetButtonLongPressHandled = true;

        // Long press: restore the original project standard.
        // This also immediately restores SUB according to 403.500.000 Hz.
        // MAIN remains untouched.
        ownOffsetHz = (uint32_t)STANDARD_OFFSET_HZ;
        ownOffsetActive = false;
        saveOwnOffset();
        applyActiveOffsetImmediately();
        showOffsetSavedMessage();

        beepMode();
    }
}

// ----------------------------------------------------------------------
// Button
// Short press: QO-100 reset / return to QO-100
// Long press : QO-100 <-> NORMAL
// ----------------------------------------------------------------------
static bool buttonStable = HIGH;
static bool buttonRawLast = HIGH;
static uint32_t buttonRawChangedAt = 0;
static uint32_t buttonPressedAt = 0;
static bool longPressHandled = false;

static void updateButton()
{
    uint32_t now = millis();
    bool raw = digitalRead(ENC_BTN_PIN);

    if (raw != buttonRawLast) {
        buttonRawLast = raw;
        buttonRawChangedAt = now;
    }

    if ((now - buttonRawChangedAt) >= BUTTON_DEBOUNCE &&
        raw != buttonStable) {

        buttonStable = raw;

        if (buttonStable == LOW) {
            buttonPressedAt = now;
            longPressHandled = false;
        }
        else if (!longPressHandled) {
            beepKey();

            if (controllerMode == CONTROLLER_QO100) {
                initializeQO100();
            }
            else {
                initializeQO100();
                beepMode();
            }
        }
    }

    if (buttonStable == LOW &&
        !longPressHandled &&
        (now - buttonPressedAt >= LONG_PRESS_MS)) {

        longPressHandled = true;

        if (controllerMode == CONTROLLER_QO100)
            initializeNormal();
        else
            initializeQO100();

        beepMode();
    }
}

// ----------------------------------------------------------------------
// OLED
// ----------------------------------------------------------------------
static void showSplashScreen()
{
    // U8glib standard fonts used by this project do not reliably provide
    // Unicode symbols such as © and →. Therefore use the portable ASCII
    // equivalents (C) and -> in the splash screen.
    u8g.firstPage();

    do {
        u8g.setFont(u8g_font_7x13);
        u8g.drawStr(4, 16, "FTX-1 QO-100 SYNC");
        u8g.drawHLine(0, 19, 128);
        u8g.setFont(u8g_font_6x10);
        u8g.drawStr(0, 35, "DXpatrol UP/DOWN CONV.");
        u8g.drawStr(0, 47, "MAIN 28MHz>SUB 432MHz");
        u8g.setFont(u8g_font_6x10);
        u8g.drawStr(12, 60, "v1.0  (C) OK1UM");

    } while (u8g.nextPage());

    delay(SPLASH_DURATION_MS);
}

// ----------------------------------------------------------------------
// OLED
// ----------------------------------------------------------------------
static void drawDisplay()
{
    char rxText[20];
    char txText[20];

    u8g.firstPage();

    do {
        u8g.setFont(u8g_font_6x12);

        if (!stateValid) {
            u8g.drawStr(0, 12, "FTX-1 CAT");
            u8g.drawStr(0, 30, "WAIT / ERR");
            u8g.drawStr(0, 48, "CAT-3 38400 8N1");
            continue;
        }

        if (controllerMode == CONTROLLER_QO100) {
            // Display actual QO-100 RF frequencies, not FTX-1 internal
            // MAIN/SUB frequencies.
            int64_t rxHz = (int64_t)rigState.mainFrequencyHz +
                           QO100_RX_LO_HZ;

            // While synchronization is pending, show the target TX
            // frequency. Once synchronized, it equals rigState.SUB + LO.
            uint32_t txInternalHz = rigState.subFrequencyHz;

            if (subSyncPending)
                txInternalHz = (uint32_t)calculateSubHz(
                    rigState.mainFrequencyHz);

            int64_t txHz = (int64_t)txInternalHz + QO100_TX_LO_HZ;

            formatQO100Frequency(rxHz, rxText, sizeof(rxText));
            formatQO100Frequency(txHz, txText, sizeof(txText));

            // First two rows:
            // RX/TX labels use a small normal font and are vertically
            // centered in the same row. Frequencies remain large and
            // dynamically aligned to the right edge.
            u8g.setFont(u8g_font_6x12);

            u8g.drawStr(0, 12, "RX:");

            u8g.setFont(u8g_font_helvB12);
            int rxWidth = u8g.getStrWidth(rxText);
            u8g.drawStr(128 - 2 - rxWidth, 13, rxText);

            u8g.setFont(u8g_font_6x12);
            u8g.drawStr(0, 28, "TX:");

            u8g.setFont(u8g_font_helvB12);
            int txWidth = u8g.getStrWidth(txText);
            u8g.drawStr(128 - 2 - txWidth, 29, txText);

            // Restore the smaller font for the remaining information.
            u8g.setFont(u8g_font_6x12);

            u8g.drawHLine(0, 34, 128);

            char dxText[24];

            if (dxOffsetHz == 0) {
                snprintf(dxText, sizeof(dxText),
                         "QO-100  DX 0.0 kHz");
            }
            else {
                int32_t absHz =
                    dxOffsetHz < 0 ? -dxOffsetHz : dxOffsetHz;

                snprintf(dxText, sizeof(dxText),
                         "QO-100 DX %c%ld.%01ld kHz",
                         dxOffsetHz > 0 ? '+' : '-',
                         (long)(absHz / 1000L),
                         (long)((absHz % 1000L) / 100L));
            }

            if ((int32_t)(offsetMessageUntil - millis()) > 0) {
                char offsetText[24];
                snprintf(offsetText, sizeof(offsetText),
                         "OFFSET %lu",
                         (unsigned long)ownOffsetHz);
                u8g.drawStr(0, 49, offsetText);
            }
            else {
                u8g.drawStr(0, 49, dxText);
            }

            if (rigState.txState == 2) {
                u8g.drawBox(0, 55, 19, 11);
                u8g.setColorIndex(0);
                u8g.drawStr(3, 63, "TX");
                u8g.setColorIndex(1);
            } else {
                u8g.drawStr(3, 63, "RX");
            }

            // SYNC is displayed inverted; CLAR is displayed normally.
            if (syncEnabled) {
                u8g.setColorIndex(1);
                u8g.drawBox(26, 55, 25, 11);
                u8g.setColorIndex(0);
                u8g.drawStr(27, 63, "SYNC");
                u8g.setColorIndex(1);
            } else {
                u8g.drawStr(27, 63, "CLAR");
            }

            // OWN OFFSET is displayed inverted when active.
            if (ownOffsetActive) {
                u8g.setColorIndex(1);
                u8g.drawBox(55, 55, 38, 11);
                u8g.setColorIndex(0);
                u8g.drawStr(56, 63, "OFFSET");
                u8g.setColorIndex(1);
            }
        }
        else {
            formatInternalFrequency(rigState.mainFrequencyHz,
                                    rxText, sizeof(rxText));

            u8g.drawStr(0, 11, "NORMAL");
            u8g.drawStr(0, 29, "MAIN");
            u8g.drawStr(30, 29, rxText);

            if (rigState.mainMode == FTX1CAT::MODE_USB)
                u8g.drawStr(0, 47, "USB");
            else
                u8g.drawStr(0, 47, "MODE");

            if (rigState.txState == 2)
                u8g.drawStr(94, 47, "TX");
            else
                u8g.drawStr(94, 47, "RX");

            u8g.drawStr(0, 63, "MONITOR ONLY");
        }

    } while (u8g.nextPage());
}

// ----------------------------------------------------------------------
// CAT polling / recovery
// A failed read only marks communication invalid. The next poll retries
// the same state acquisition; no blocking recovery loop is used.
// ----------------------------------------------------------------------
static void pollCAT()
{
    uint32_t now = millis();

    if ((int32_t)(now - nextPoll) < 0)
        return;

    nextPoll = now + CAT_POLL_MS;

    FTX1CAT::State newState;

    if (rig.readState(newState)) {
        rigState = newState;
        stateValid = true;

        if (controllerMode == CONTROLLER_QO100)
            maintainQO100(now);
    }
    else {
        stateValid = false;
    }

    drawDisplay();
}

// ----------------------------------------------------------------------
// SETUP
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// Antenna relay control
// The relay follows controllerMode directly.
// QO-100 = HIGH (+5 V), NORMAL = LOW (0 V).
// OLED display is not involved in this control path.
// ----------------------------------------------------------------------
static void updateAntennaRelay()
{
    digitalWrite(
        ANTENNA_RELAY_PIN,
        controllerMode == CONTROLLER_QO100 ? HIGH : LOW
    );
}

void setup()
{
    pinMode(ENC_A_PIN, INPUT_PULLUP);
    pinMode(ENC_B_PIN, INPUT_PULLUP);
    pinMode(ENC_BTN_PIN, INPUT_PULLUP);
    pinMode(BEEP_PIN, OUTPUT);
    pinMode(ANTENNA_RELAY_PIN, OUTPUT);
    updateAntennaRelay();
    pinMode(SYNC_BTN_PIN, INPUT_PULLUP);
    pinMode(OWN_OFFSET_BTN_PIN, INPUT_PULLUP);

    digitalWrite(BEEP_PIN, LOW);

// ----------------------------------------------------------------------
// USB SERIAL MONITOR
// ----------------------------------------------------------------------
Serial.begin(FTX1CAT::DEFAULT_BAUD);

// ----------------------------------------------------------------------
// FTX-1 CAT-3 -> Nano D10/D11
//
// D10 = RX
// D11 = TX
//
//  = 8N1
// ----------------------------------------------------------------------


    encLast = (uint8_t)((digitalRead(ENC_A_PIN) << 1) |
                         digitalRead(ENC_B_PIN));

    attachInterrupt(digitalPinToInterrupt(ENC_A_PIN),
                    encoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC_B_PIN),
                    encoderISR, CHANGE);

    Wire.begin();
    delay(100);
    u8g.begin();

    // Show the project identification once at startup.
    showSplashScreen();

    rig.begin();

    // Restore the operator offset settings before the QO-100 controller
    // starts its normal synchronization logic.
    loadOwnOffset();

    delay(250);

    // Startup always enters QO-100 mode.
    initializeQO100();

    delay(100);
    drawDisplay();
}

// ----------------------------------------------------------------------
// LOOP
// ----------------------------------------------------------------------
void loop()
{
    updateEncoder();
    updateButton();
    updateSyncButton();
    updateOwnOffsetButton();
    pollCAT();
}
