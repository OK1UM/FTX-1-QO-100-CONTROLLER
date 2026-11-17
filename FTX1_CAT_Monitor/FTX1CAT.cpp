#include "FTX1CAT.h"
#include <string.h>
#include <stdlib.h>

FTX1CAT::FTX1CAT(Stream &catPort)
    : _port(catPort), _status(CAT_ERR) {
}

void FTX1CAT::begin() {
    clearRx();

    // Polling is intentional. Do not enable Auto Information.
    sendCommand("AI0;");
    clearRx();

    _status = CAT_ERR;
}

FTX1CAT::CATStatus FTX1CAT::status() const {
    return _status;
}

bool FTX1CAT::isCATConnected() const {
    return _status == CAT_OK;
}

void FTX1CAT::clearRx() {
    while (_port.available() > 0) {
        _port.read();
    }
}

bool FTX1CAT::sendCommand(const char *command) {
    if (command == nullptr || command[0] == '\0') {
        return false;
    }

    size_t len = strlen(command);
    size_t written = _port.write(
        reinterpret_cast<const uint8_t *>(command), len);

    return written == len;
}

bool FTX1CAT::readReply(char *reply, uint8_t maxLen, uint16_t timeoutMs) {
    if (reply == nullptr || maxLen < 3) {
        return false;
    }

    uint8_t index = 0;
    unsigned long start = millis();

    while ((unsigned long)(millis() - start) < timeoutMs) {
        while (_port.available() > 0) {
            int value = _port.read();
            if (value < 0) {
                continue;
            }

            if (index >= (uint8_t)(maxLen - 1)) {
                reply[0] = '\0';
                return false;
            }

            reply[index++] = (char)value;

            if (value == ';') {
                reply[index] = '\0';
                return true;
            }
        }
    }

    reply[0] = '\0';
    return false;
}

bool FTX1CAT::transact(const char *command, char *reply,
                       uint8_t maxLen, uint16_t timeoutMs) {
    clearRx();

    if (!sendCommand(command)) {
        _status = CAT_ERR;
        return false;
    }

    if (!readReply(reply, maxLen, timeoutMs)) {
        _status = CAT_ERR;
        return false;
    }

    _status = CAT_OK;
    return true;
}

bool FTX1CAT::validateReply(const char *reply, const char *prefix,
                            uint8_t expectedLength) const {
    if (reply == nullptr || prefix == nullptr) {
        return false;
    }

    if (strlen(reply) != expectedLength) {
        return false;
    }

    return reply[0] == prefix[0] &&
           reply[1] == prefix[1] &&
           reply[expectedLength - 1] == ';';
}

bool FTX1CAT::validFrequency(uint32_t frequencyHz) {
    return frequencyHz >= 30000UL &&
           frequencyHz <= 470000000UL;
}

bool FTX1CAT::parseFrequency(const char *reply, const char *prefix,
                             uint32_t &frequencyHz) const {
    if (!validateReply(reply, prefix, 12)) {
        return false;
    }

    for (uint8_t i = 2; i <= 10; ++i) {
        if (reply[i] < '0' || reply[i] > '9') {
            return false;
        }
    }

    char value[10];
    memcpy(value, reply + 2, 9);
    value[9] = '\0';

    unsigned long parsed = strtoul(value, nullptr, 10);

    if (parsed > 470000000UL) {
        return false;
    }

    frequencyHz = (uint32_t)parsed;
    return validFrequency(frequencyHz);
}

bool FTX1CAT::setMainFrequencyHz(uint32_t frequencyHz) {
    if (!validFrequency(frequencyHz)) {
        return false;
    }

    char command[16];
    snprintf(command, sizeof(command), "FA%09lu;",
             (unsigned long)frequencyHz);

    return sendCommand(command);
}

bool FTX1CAT::readMainFrequencyHz(uint32_t &frequencyHz) {
    char reply[MAX_REPLY_LENGTH];

    if (!transact("FA;", reply, sizeof(reply))) {
        return false;
    }

    return parseFrequency(reply, "FA", frequencyHz);
}

bool FTX1CAT::setSubFrequencyHz(uint32_t frequencyHz) {
    if (!validFrequency(frequencyHz)) {
        return false;
    }

    char command[16];
    snprintf(command, sizeof(command), "FB%09lu;",
             (unsigned long)frequencyHz);

    return sendCommand(command);
}

bool FTX1CAT::readSubFrequencyHz(uint32_t &frequencyHz) {
    char reply[MAX_REPLY_LENGTH];

    if (!transact("FB;", reply, sizeof(reply))) {
        return false;
    }

    return parseFrequency(reply, "FB", frequencyHz);
}

bool FTX1CAT::setSplit(bool enabled) {
    return sendCommand(enabled ? "ST1;" : "ST0;");
}

bool FTX1CAT::readSplit(bool &enabled) {
    char reply[MAX_REPLY_LENGTH];

    if (!transact("ST;", reply, sizeof(reply))) {
        return false;
    }

    if (!validateReply(reply, "ST", 4)) {
        _status = CAT_ERR;
        return false;
    }

    if (reply[2] == '0') {
        enabled = false;
    } else if (reply[2] == '1') {
        enabled = true;
    } else {
        _status = CAT_ERR;
        return false;
    }

    return true;
}

bool FTX1CAT::setTxSide(Side side) {
    return sendCommand(side == SUB ? "FT1;" : "FT0;");
}

bool FTX1CAT::readTxSide(Side &side) {
    char reply[MAX_REPLY_LENGTH];

    if (!transact("FT;", reply, sizeof(reply))) {
        return false;
    }

    if (!validateReply(reply, "FT", 4)) {
        _status = CAT_ERR;
        return false;
    }

    if (reply[2] == '0') {
        side = MAIN;
    } else if (reply[2] == '1') {
        side = SUB;
    } else {
        _status = CAT_ERR;
        return false;
    }

    return true;
}

bool FTX1CAT::setMode(Side side, Mode mode) {
    if (mode == MODE_UNKNOWN) {
        return false;
    }

    char command[8];
    snprintf(command, sizeof(command), "MD%c%c;",
             side == MAIN ? '0' : '1',
             (char)mode);

    return sendCommand(command);
}

bool FTX1CAT::decodeModeChar(char c, Mode &mode) {
    switch (c) {
        case '1': mode = MODE_LSB; break;
        case '2': mode = MODE_USB; break;
        case '3': mode = MODE_CW_U; break;
        case '4': mode = MODE_FM; break;
        case '5': mode = MODE_AM; break;
        case '6': mode = MODE_RTTY_L; break;
        case '7': mode = MODE_CW_L; break;
        case '8': mode = MODE_DATA_L; break;
        case '9': mode = MODE_RTTY_U; break;
        case 'A': mode = MODE_DATA_FM; break;
        case 'B': mode = MODE_FM_N; break;
        case 'C': mode = MODE_DATA_U; break;
        case 'D': mode = MODE_AM_N; break;
        case 'E': mode = MODE_PSK; break;
        case 'F': mode = MODE_DATA_FM_N; break;
        default:
            mode = MODE_UNKNOWN;
            return false;
    }

    return true;
}

bool FTX1CAT::readMode(Side side, Mode &mode) {
    char reply[MAX_REPLY_LENGTH];
    const char *command = side == MAIN ? "MD0;" : "MD1;";

    if (!transact(command, reply, sizeof(reply))) {
        return false;
    }

    if (!validateReply(reply, "MD", 5)) {
        _status = CAT_ERR;
        return false;
    }

    if (reply[2] != (side == MAIN ? '0' : '1')) {
        _status = CAT_ERR;
        return false;
    }

    return decodeModeChar(reply[3], mode);
}

bool FTX1CAT::setRIT(bool enabled) {
    // MAIN RX clarifier. TX clarifier remains OFF.
    return sendCommand(enabled ? "CF00010000;" : "CF00000000;");
}

bool FTX1CAT::readRIT(bool &enabled) {
    char reply[MAX_REPLY_LENGTH];

    // CF read format: CF P1 P2 P3 ;
    // For MAIN clarifier setting: CF000;
    // Answer: CF P1 P2 P3 P4 P5 P6 P7 P8 ;
    // P4 = RX CLAR, P5 = TX CLAR.
    if (!transact("CF000;", reply, sizeof(reply))) {
        return false;
    }

    if (!validateReply(reply, "CF", 11)) {
        _status = CAT_ERR;
        return false;
    }

    if (reply[2] != '0' || reply[3] != '0' || reply[4] != '0') {
        _status = CAT_ERR;
        return false;
    }

    if (reply[5] == '0') {
        enabled = false;
    } else if (reply[5] == '1') {
        enabled = true;
    } else {
        _status = CAT_ERR;
        return false;
    }

    return true;
}

bool FTX1CAT::setRITOffset(int16_t offsetHz) {
    // FTX-1 CAT clarifier offset: -9990 .. +9990 Hz.
    if (offsetHz < -9990 || offsetHz > 9990) {
        return false;
    }

    char sign = offsetHz < 0 ? '-' : '+';
    uint16_t magnitude =
        (uint16_t)(offsetHz < 0 ? -offsetHz : offsetHz);

    char command[16];
    snprintf(command, sizeof(command), "CF001%c%04u;",
             sign, (unsigned)magnitude);

    return sendCommand(command);
}

bool FTX1CAT::readRITOffset(int16_t &offsetHz) {
    char reply[MAX_REPLY_LENGTH];

    if (!transact("CF001;", reply, sizeof(reply))) {
        return false;
    }

    if (!validateReply(reply, "CF", 11)) {
        _status = CAT_ERR;
        return false;
    }

    if (reply[2] != '0' || reply[3] != '0' || reply[4] != '1') {
        _status = CAT_ERR;
        return false;
    }

    if (reply[5] != '+' && reply[5] != '-') {
        _status = CAT_ERR;
        return false;
    }

    for (uint8_t i = 6; i <= 9; ++i) {
        if (reply[i] < '0' || reply[i] > '9') {
            _status = CAT_ERR;
            return false;
        }
    }

    char value[5];
    memcpy(value, reply + 6, 4);
    value[4] = '\0';

    int magnitude = atoi(value);
    offsetHz = (int16_t)(reply[5] == '-' ? -magnitude : magnitude);

    return true;
}

bool FTX1CAT::readTX(uint8_t &txState) {
    char reply[MAX_REPLY_LENGTH];

    if (!transact("TX;", reply, sizeof(reply))) {
        return false;
    }

    if (!validateReply(reply, "TX", 4)) {
        _status = CAT_ERR;
        return false;
    }

    if (reply[2] < '0' || reply[2] > '2') {
        _status = CAT_ERR;
        return false;
    }

    txState = (uint8_t)(reply[2] - '0');
    return true;
}

bool FTX1CAT::readIF(State &state) {
    char reply[MAX_REPLY_LENGTH];

    if (!transact("IF;", reply, sizeof(reply))) {
        state.valid = false;
        return false;
    }

    if (!validateReply(reply, "IF", 30)) {
        _status = CAT_ERR;
        state.valid = false;
        return false;
    }

    // IF positions (including IF at 0..1):
    //  2..6  P1 VFO/memory selector
    //  7..15 P2 frequency
    //  16    P3 sign
    //  17..20 P3 offset
    //  21    P4 RX clarifier
    //  22    P5 TX clarifier
    //  23    P6 mode
    //  24    P7 VFO/memory state
    //  25    P8 tone
    //  26..27 P9
    //  28    P10 repeater shift
    //  29    ';'

    for (uint8_t i = 7; i <= 15; ++i) {
        if (reply[i] < '0' || reply[i] > '9') {
            _status = CAT_ERR;
            state.valid = false;
            return false;
        }
    }

    char freq[10];
    memcpy(freq, reply + 7, 9);
    freq[9] = '\0';

    state.mainFrequencyHz =
        (uint32_t)strtoul(freq, nullptr, 10);

    if (!validFrequency(state.mainFrequencyHz)) {
        _status = CAT_ERR;
        state.valid = false;
        return false;
    }

    if (reply[16] != '+' && reply[16] != '-') {
        _status = CAT_ERR;
        state.valid = false;
        return false;
    }

    for (uint8_t i = 17; i <= 20; ++i) {
        if (reply[i] < '0' || reply[i] > '9') {
            _status = CAT_ERR;
            state.valid = false;
            return false;
        }
    }

    char rit[5];
    memcpy(rit, reply + 17, 4);
    rit[4] = '\0';

    int ritMagnitude = atoi(rit);
    state.ritOffsetHz =
        (int16_t)(reply[16] == '-' ? -ritMagnitude : ritMagnitude);

    if (reply[21] == '0') {
        state.ritOn = false;
    } else if (reply[21] == '1') {
        state.ritOn = true;
    } else {
        _status = CAT_ERR;
        state.valid = false;
        return false;
    }

    if (reply[22] == '0') {
        state.xitOn = false;
    } else if (reply[22] == '1') {
        state.xitOn = true;
    } else {
        _status = CAT_ERR;
        state.valid = false;
        return false;
    }

    if (!decodeModeChar(reply[23], state.mainMode)) {
        _status = CAT_ERR;
        state.valid = false;
        return false;
    }

    state.valid = true;
    return true;
}

bool FTX1CAT::readState(State &state) {
    State result;

    // IF already contains MAIN frequency, MAIN mode and clarifier state.
    // Therefore do not poll FA/MD MAIN/CF separately.
    if (!readIF(result)) {
        state.valid = false;
        return false;
    }

    if (!readSubFrequencyHz(result.subFrequencyHz)) {
        state.valid = false;
        return false;
    }

    if (!readTxSide(result.txSide)) {
        state.valid = false;
        return false;
    }

    if (!readSplit(result.split)) {
        state.valid = false;
        return false;
    }

    // IF describes MAIN; only SUB mode needs a separate MD query.
    if (!readMode(SUB, result.subMode)) {
        state.valid = false;
        return false;
    }

    if (!readTX(result.txState)) {
        state.valid = false;
        return false;
    }

    state = result;
    state.valid = true;
    _status = CAT_OK;
    return true;
}
