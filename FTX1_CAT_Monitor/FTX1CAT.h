#ifndef FTX1CAT_H
#define FTX1CAT_H

#include <Arduino.h>

class FTX1CAT {
public:
    static constexpr uint32_t DEFAULT_BAUD = 38400UL;

    enum Side : uint8_t {
        MAIN = 0,
        SUB  = 1
    };

    enum Mode : uint8_t {
        MODE_UNKNOWN   = 0xFF,
        MODE_LSB       = '1',
        MODE_USB       = '2',
        MODE_CW_U      = '3',
        MODE_FM        = '4',
        MODE_AM        = '5',
        MODE_RTTY_L    = '6',
        MODE_CW_L      = '7',
        MODE_DATA_L    = '8',
        MODE_RTTY_U    = '9',
        MODE_DATA_FM   = 'A',
        MODE_FM_N      = 'B',
        MODE_DATA_U    = 'C',
        MODE_AM_N      = 'D',
        MODE_PSK       = 'E',
        MODE_DATA_FM_N = 'F'
    };

    enum CATStatus : uint8_t {
        CAT_ERR = 0,
        CAT_OK  = 1
    };

    struct State {
        uint32_t mainFrequencyHz = 0;
        uint32_t subFrequencyHz = 0;

        Mode mainMode = MODE_UNKNOWN;
        Mode subMode  = MODE_UNKNOWN;

        bool split = false;
        Side txSide = MAIN;

        bool ritOn = false;
        bool xitOn = false;
        int16_t ritOffsetHz = 0;

        uint8_t txState = 0;       // 0=RX, 1=CAT TX, 2=radio TX
        bool valid = false;
    };

    explicit FTX1CAT(Stream &catPort);

    // Serial is initialized by the main sketch.
    // This method only clears stale data and disables Auto Information.
    void begin();

    CATStatus status() const;
    bool isCATConnected() const;

    bool setMainFrequencyHz(uint32_t frequencyHz);
    bool readMainFrequencyHz(uint32_t &frequencyHz);

    bool setSubFrequencyHz(uint32_t frequencyHz);
    bool readSubFrequencyHz(uint32_t &frequencyHz);

    bool setSplit(bool enabled);
    bool readSplit(bool &enabled);

    bool setTxSide(Side side);
    bool readTxSide(Side &side);

    bool setMode(Side side, Mode mode);
    bool readMode(Side side, Mode &mode);

    // MAIN clarifier/RIT. This project does not use XIT.
    bool setRIT(bool enabled);
    bool readRIT(bool &enabled);

    bool setRITOffset(int16_t offsetHz);
    bool readRITOffset(int16_t &offsetHz);

    bool readTX(uint8_t &txState);

    // Documented IF response.
    bool readIF(State &state);

    // State needed by the controller.
    bool readState(State &state);

private:
   Stream &_port;
    CATStatus _status;

    static constexpr uint16_t DEFAULT_TIMEOUT_MS = 120;
    static constexpr uint8_t MAX_REPLY_LENGTH = 64;

    void clearRx();
    bool sendCommand(const char *command);
    bool readReply(char *reply, uint8_t maxLen, uint16_t timeoutMs);
    bool transact(const char *command, char *reply, uint8_t maxLen,
                  uint16_t timeoutMs = DEFAULT_TIMEOUT_MS);

    bool validateReply(const char *reply, const char *prefix,
                       uint8_t expectedLength) const;

    bool parseFrequency(const char *reply, const char *prefix,
                        uint32_t &frequencyHz) const;

    static bool validFrequency(uint32_t frequencyHz);
    static bool decodeModeChar(char c, Mode &mode);
};

#endif
