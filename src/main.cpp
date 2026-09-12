// Advanced scientific calculator for the M5Stack Cardputer.
// Type expressions on the QWERTY keyboard (e.g. "2*sin(pi/4)+sqrt(16)")
// and press Enter to evaluate.
//
// Keys:
//   Enter        evaluate the expression
//   Backspace    delete last character
//   fn + Backspace   clear everything
//   opt + D      toggle DEG / RAD angle mode
//   fn + ;       recall an older history entry for editing (repeat to go
//                further back — the ; key has an up-arrow icon printed on it)
//   fn + .       step back toward the newest entry / a blank line (the .
//                key has a down-arrow icon printed on it)
//   fn + ,       move the edit cursor left  (, key has a left-arrow icon)
//   fn + /       move the edit cursor right (/ key has a right-arrow icon)
//   Tab          complete the function name before the cursor (adds the
//                opening "("); press again to cycle other matches
//   After a result is shown:
//     - typing an operator (+-*/^%) continues the calculation from the
//       previous result (chained like a normal calculator)
//     - typing anything else starts a brand new expression
//     - Backspace re-opens the previous expression for editing (without
//       deleting anything yet) so it can be tweaked and recalculated;
//       a second Backspace then deletes its last character as usual
//
// Supported: + - * / ^ % ( )  unary minus  x!
//   1-arg:  sin cos tan asin acos atan tanh sinh cosh asinh acosh atanh
//           sqrt cbrt log ln log2 exp abs floor ceil round int
//   2-arg:  pow(x,y)  mod(x,y) (sign follows the divisor, unlike %)
//           atan2(y,x)  min(a,b)  max(a,b)  gcd(a,b)  lcm(a,b)
//           ncr(n,r)  npr(n,r)  (combinations / permutations)
//   3-arg:  clamp(x,min,max)
//   0/2-arg: rand()  -> [0,1)   |   rand(min,max) -> [min,max)
//   2-arg:  randint(min,max) -> integer, inclusive of both ends
//   pi  e  ans (most recent result)  ans(n) (n-th most recent result)
//
// Type "help" and press Enter for an on-screen function reference
// (fn+;/. flips pages, Enter or Backspace exits back to the calculator).
//
// History (and the DEG/RAD setting) is auto-saved to the ESP32's internal
// flash (NVS) after every calculation, and restored on boot — this works
// even without an SD card inserted, and survives power loss.
//
// Type "save" and press Enter to additionally append the current history
// to /calc_log.txt on a microSD card, as plain text you can read on a PC.
//
// There's no RTC chip on this hardware and no Wi-Fi/NTP in this sketch, so
// there's no real clock by default. "settime(H,M,S)" sets a reference time
// (from millis() elapsed since); "time" shows the current computed time.
// This resets on every power-cycle — re-run settime() after each boot.
//
// Type "usbdrive" and press Enter to expose the microSD card to a computer
// over the same USB-C cable, as an ordinary USB drive — no card removal
// needed. This takes over the SD card and the USB port for that purpose;
// the calculator only works normally again after a reset/power-cycle. The
// raw SD-over-SPI block I/O routines (mscSdRawInit/mscReadSector/
// mscWriteSector below) are adapted from MOY-lightening-firmware's
// "M5-cardputer-mass-storage" (MIT License, Copyright (c) 2026 OZAN),
// https://github.com/MOY-lightening-firmware/M5-cardputer-mass-storage

#include <M5Cardputer.h>
#include <M5GFX.h>
#include <esp_random.h>
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include <USB.h>
#include <USBMSC.h>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <utility>

static M5Canvas canvas(&M5Cardputer.Display);

// microSD wiring on both Cardputer and Cardputer ADV (per M5Stack's
// official examples) — SPI bus is not shared with anything else.
static const int SD_SPI_SCK_PIN = 40;
static const int SD_SPI_MISO_PIN = 39;
static const int SD_SPI_MOSI_PIN = 14;
static const int SD_SPI_CS_PIN = 12;
static const char* SD_LOG_PATH = "/calc_log.txt";
static bool sdReady = false;
static uint32_t sdSectorCount = 0; // populated at boot while SD.h has it mounted

static Preferences prefs;
static const char* PREFS_NS = "calc";

// ---------------------------------------------------------------------
// USB Mass Storage: exposes the microSD card directly to a host computer
// over USB, without going through this sketch's normal FAT-mounted SD.h
// access. Needs raw sector-level SD-over-SPI I/O (a different protocol
// layer from SD.h/SdFat), adapted from the MIT-licensed reference noted
// above. Once entered (via the "usbdrive" command), the calculator no
// longer functions until reset — this mirrors the reference design and
// avoids the two very different SD access layers running at once.
// ---------------------------------------------------------------------
static const uint32_t MSC_SECTOR_SIZE = 512;
static USBMSC MSC;
static SPIClass mscSPI(HSPI);
static bool mscCardIsHC = false;
static bool mscModeActive = false;

static uint8_t mscSdTransfer(uint8_t b) { return mscSPI.transfer(b); }

static void mscSdSelect() {
    digitalWrite(SD_SPI_CS_PIN, LOW);
    delayMicroseconds(1);
}

static void mscSdDeselect() {
    digitalWrite(SD_SPI_CS_PIN, HIGH);
    mscSPI.transfer(0xFF);
}

static uint8_t mscSdCmd(uint8_t cmd, uint32_t arg) {
    mscSdDeselect();
    mscSdTransfer(0xFF);
    mscSdSelect();

    mscSdTransfer(0x40 | cmd);
    mscSdTransfer((arg >> 24) & 0xFF);
    mscSdTransfer((arg >> 16) & 0xFF);
    mscSdTransfer((arg >> 8) & 0xFF);
    mscSdTransfer(arg & 0xFF);

    uint8_t crc = 0xFF;
    if (cmd == 0) crc = 0x95;
    if (cmd == 8) crc = 0x87;
    mscSdTransfer(crc);

    uint8_t r = 0xFF;
    for (int i = 0; i < 8; i++) {
        r = mscSdTransfer(0xFF);
        if (!(r & 0x80)) break;
    }
    return r;
}

// Re-initializes the SD card at the raw SPI protocol level (independent
// of SD.h's FAT mount, which must already be released via SD.end()).
static bool mscSdRawInit() {
    mscSPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
    mscSdDeselect();
    for (int i = 0; i < 10; i++) mscSdTransfer(0xFF);

    if (mscSdCmd(0, 0) != 0x01) {
        mscSPI.endTransaction();
        return false;
    }

    bool v2 = false;
    if (mscSdCmd(8, 0x000001AA) == 0x01) {
        uint8_t r7[4];
        for (int i = 0; i < 4; i++) r7[i] = mscSdTransfer(0xFF);
        if (r7[2] == 0x01 && r7[3] == 0xAA) v2 = true;
    }

    uint32_t deadline = millis() + 2000;
    uint8_t r;
    do {
        mscSdCmd(55, 0);
        r = mscSdCmd(41, v2 ? 0x40000000 : 0);
        if (millis() > deadline) {
            mscSPI.endTransaction();
            return false;
        }
    } while (r != 0x00);

    if (v2 && mscSdCmd(58, 0) == 0x00) {
        uint8_t ocr[4];
        for (int i = 0; i < 4; i++) ocr[i] = mscSdTransfer(0xFF);
        mscCardIsHC = (ocr[0] & 0x40) != 0;
    }

    if (!mscCardIsHC && mscSdCmd(16, MSC_SECTOR_SIZE) != 0x00) {
        mscSPI.endTransaction();
        return false;
    }

    mscSdDeselect();
    mscSPI.endTransaction();
    return true;
}

static bool mscReadSectors(uint8_t* buf, uint32_t lba, uint32_t count) {
    mscSPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));
    for (uint32_t i = 0; i < count; i++) {
        uint32_t addr = mscCardIsHC ? (lba + i) : ((lba + i) * MSC_SECTOR_SIZE);

        mscSdDeselect();
        mscSdTransfer(0xFF);
        mscSdSelect();
        mscSdTransfer(0x40 | 17);
        mscSdTransfer((addr >> 24) & 0xFF);
        mscSdTransfer((addr >> 16) & 0xFF);
        mscSdTransfer((addr >> 8) & 0xFF);
        mscSdTransfer(addr & 0xFF);
        mscSdTransfer(0xFF);

        uint8_t r1 = 0xFF;
        for (int t = 0; t < 10; t++) {
            r1 = mscSdTransfer(0xFF);
            if (!(r1 & 0x80)) break;
        }
        if (r1 != 0x00) {
            mscSdDeselect();
            mscSPI.endTransaction();
            return false;
        }

        uint8_t token = 0xFF;
        uint32_t dl = millis() + 500;
        while (millis() < dl) {
            token = mscSdTransfer(0xFF);
            if (token != 0xFF) break;
        }
        if (token != 0xFE) {
            mscSdDeselect();
            mscSPI.endTransaction();
            return false;
        }

        for (uint32_t b = 0; b < MSC_SECTOR_SIZE; b++) buf[i * MSC_SECTOR_SIZE + b] = mscSdTransfer(0xFF);
        mscSdTransfer(0xFF);
        mscSdTransfer(0xFF);
        mscSdDeselect();
    }
    mscSPI.endTransaction();
    return true;
}

static bool mscWriteSectors(const uint8_t* buf, uint32_t lba, uint32_t count) {
    mscSPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));
    for (uint32_t i = 0; i < count; i++) {
        uint32_t addr = mscCardIsHC ? (lba + i) : ((lba + i) * MSC_SECTOR_SIZE);

        mscSdDeselect();
        mscSdTransfer(0xFF);
        mscSdSelect();
        mscSdTransfer(0x40 | 24);
        mscSdTransfer((addr >> 24) & 0xFF);
        mscSdTransfer((addr >> 16) & 0xFF);
        mscSdTransfer((addr >> 8) & 0xFF);
        mscSdTransfer(addr & 0xFF);
        mscSdTransfer(0xFF);

        uint8_t r1 = 0xFF;
        for (int t = 0; t < 10; t++) {
            r1 = mscSdTransfer(0xFF);
            if (!(r1 & 0x80)) break;
        }
        if (r1 != 0x00) {
            mscSdDeselect();
            mscSPI.endTransaction();
            return false;
        }

        mscSdTransfer(0xFF);
        mscSdTransfer(0xFE);
        for (uint32_t b = 0; b < MSC_SECTOR_SIZE; b++) mscSdTransfer(buf[i * MSC_SECTOR_SIZE + b]);
        mscSdTransfer(0xFF);
        mscSdTransfer(0xFF);

        uint8_t dresp = mscSdTransfer(0xFF);
        if ((dresp & 0x1F) != 0x05) {
            mscSdDeselect();
            mscSPI.endTransaction();
            return false;
        }

        uint32_t dl = millis() + 2000;
        while (millis() < dl) {
            if (mscSdTransfer(0xFF) != 0x00) break;
        }
        mscSdDeselect();
    }
    mscSPI.endTransaction();
    return true;
}

static int32_t mscOnRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    (void)offset;
    uint32_t count = bufsize / MSC_SECTOR_SIZE;
    if (count == 0) return -1;
    return mscReadSectors((uint8_t*)buffer, lba, count) ? (int32_t)bufsize : -1;
}

static int32_t mscOnWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    (void)offset;
    uint32_t count = bufsize / MSC_SECTOR_SIZE;
    if (count == 0) return -1;
    return mscWriteSectors(buffer, lba, count) ? (int32_t)bufsize : -1;
}

static bool mscOnStartStop(uint8_t power_condition, bool start, bool load_eject) {
    (void)power_condition;
    (void)start;
    (void)load_eject;
    return true;
}

// Hands the SD card over to the host computer as a USB drive. Returns
// false (leaving normal calculator operation untouched) if there's no
// card or the raw protocol handshake fails.
static bool enterUsbDriveMode() {
    if (!sdReady || sdSectorCount == 0) return false;

    SD.end(); // release the FAT mount before raw sector access begins
    delay(20);
    pinMode(SD_SPI_CS_PIN, OUTPUT);
    mscSPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);

    if (!mscSdRawInit()) return false;

    MSC.vendorID("CalcCard");
    MSC.productID("Cardputer");
    MSC.productRevision("1.0");
    MSC.onRead(mscOnRead);
    MSC.onWrite(mscOnWrite);
    MSC.onStartStop(mscOnStartStop);
    MSC.mediaPresent(true);
    MSC.begin(sdSectorCount, MSC_SECTOR_SIZE);

    USB.manufacturerName("CardputerCalc");
    USB.productName("SD Card");
    USB.begin();

    mscModeActive = true;
    return true;
}

struct HistEntry {
    std::string expr;
    std::string result; // formatted display string, e.g. "5.414213562"
    double value;        // same result as a double, for ans()/ans(n)
};

static std::string expr;          // current expression being typed
static std::string resultLine;    // "= 12.34" or "ERR: ..." shown below
static std::vector<HistEntry> history; // last evaluated expr/result pairs
static bool haveResult = false;   // true right after Enter, before new typing
static bool degMode = false;      // false = radians, true = degrees
static int browseIndex = -1;      // -1 = not browsing history, else index into `history`
static size_t cursorPos = 0;      // insert/delete position within `expr`

static bool helpMode = false;
static int helpPage = 0;
static const std::vector<std::vector<std::string>> helpPages = {
    {"Trig (opt+D = deg/rad):", "sin cos tan", "asin acos atan", "atan2(y,x)", "ex: sin(pi/2) = 1"},
    {"Hyperbolic:", "sinh cosh tanh", "asinh acosh atanh", "ex: tanh(1) = 0.7615..."},
    {"Power & log:", "sqrt cbrt pow(x,y)", "exp log ln log2(x)", "ex: pow(2,10) = 1024"},
    {"Compare & integer:", "min(a,b) max(a,b)", "clamp(x,lo,hi)", "gcd(a,b) lcm(a,b) mod(a,b)", "ex: gcd(12,18) = 6"},
    {"Combinatorics/random:", "ncr(n,r) npr(n,r)", "rand() rand(lo,hi)", "randint(lo,hi) (inclusive)", "ex: randint(1,6) = dice"},
    {"Rounding & misc:", "abs floor ceil round int", "pi  e  x!  ^  %", "ex: int(rand(1,11)) = 1..10"},
    {"Previous results:", "ans = most recent result", "ans(n) = n-th most recent", "ex: ans(1)+ans(2)+ans(3)"},
    {"Saving:", "History auto-saves to flash", "(survives power off, no SD", "card needed).", "Type save + Enter to also", "append it to calc_log.txt", "on a microSD card."},
    {"Clock (no RTC on this", "board, resets each boot):", "settime(H,M,S) sets it", "time shows current H:M:S", "ex: settime(9,30,0)"},
    {"USB drive mode:", "usbdrive exposes the SD", "card to a computer over", "USB. Needs reset/power-", "cycle to return to the", "calculator afterward."},
    {"Keys:", "fn+BkSp = clear all", "fn+;/.  = history up/down", "fn+,//  = cursor left/right", "opt+D   = deg/rad toggle", "Tab     = complete func name"},
};

static const size_t MAX_EXPR_LEN = 200;
// How many past calculations are kept in memory (browsable via fn+;,
// and available to ans(n)) — plenty of RAM headroom for this.
static const size_t HISTORY_STORE_CAP = 20;
// How many of the most recent entries fit on screen at once; computed at
// startup in setup() from the actual display size (see historyDisplayCap).
static size_t historyDisplayCap = 4;

// ---------------------------------------------------------------------
// Recursive-descent expression parser / evaluator
// ---------------------------------------------------------------------
class ParseError : public std::exception {
public:
    explicit ParseError(const char* m) : msg(m) {}
    const char* what() const noexcept override { return msg; }
private:
    const char* msg;
};

class Parser {
public:
    Parser(const std::string& s, bool deg) : src(s), pos(0), useDegrees(deg) {}

    double run() {
        double v = parseExpr();
        skipSpaces();
        if (pos != src.size()) throw ParseError("syntax");
        return v;
    }

private:
    const std::string& src;
    size_t pos;
    bool useDegrees; // named to avoid clashing with Arduino's degrees() macro

    void skipSpaces() { while (pos < src.size() && src[pos] == ' ') pos++; }

    char peek() { skipSpaces(); return pos < src.size() ? src[pos] : '\0'; }

    bool consume(char c) {
        if (peek() == c) { pos++; return true; }
        return false;
    }

    void expect(char c) {
        if (!consume(c)) throw ParseError("expected char");
    }

    // expr := term (('+'|'-') term)*
    double parseExpr() {
        double v = parseTerm();
        for (;;) {
            char c = peek();
            if (c == '+') { pos++; v += parseTerm(); }
            else if (c == '-') { pos++; v -= parseTerm(); }
            else break;
        }
        return v;
    }

    // term := power (('*'|'/'|'%') power)*
    double parseTerm() {
        double v = parsePower();
        for (;;) {
            char c = peek();
            if (c == '*') { pos++; v *= parsePower(); }
            else if (c == '/') {
                pos++;
                double d = parsePower();
                if (d == 0.0) throw ParseError("div by zero");
                v /= d;
            } else if (c == '%') {
                pos++;
                double d = parsePower();
                if (d == 0.0) throw ParseError("div by zero");
                v = std::fmod(v, d);
            } else break;
        }
        return v;
    }

    // power := unary ('^' power)?   (right associative)
    double parsePower() {
        double v = parseUnary();
        if (peek() == '^') {
            pos++;
            double e = parsePower();
            v = std::pow(v, e);
        }
        return v;
    }

    // unary := ('-'|'+') unary | postfix
    double parseUnary() {
        if (consume('-')) return -parseUnary();
        if (consume('+')) return parseUnary();
        return parsePostfix();
    }

    // postfix := primary ('!')?
    double parsePostfix() {
        double v = parsePrimary();
        while (consume('!')) {
            if (v < 0 || v != std::floor(v) || v > 170)
                throw ParseError("bad factorial");
            double r = 1.0;
            for (int i = 2; i <= (int)v; i++) r *= i;
            v = r;
        }
        return v;
    }

    // primary := number | ident ['(' expr ')'] | '(' expr ')'
    double parsePrimary() {
        char c = peek();
        if (c == '(') {
            pos++;
            double v = parseExpr();
            expect(')');
            return v;
        }
        if (std::isdigit((unsigned char)c) || c == '.') {
            return parseNumber();
        }
        if (std::isalpha((unsigned char)c)) {
            std::string id = parseIdent();
            return applyIdentifier(id);
        }
        throw ParseError("unexpected char");
    }

    double parseNumber() {
        skipSpaces();
        size_t start = pos;
        while (pos < src.size() && (std::isdigit((unsigned char)src[pos]) || src[pos] == '.'))
            pos++;
        if (pos == start) throw ParseError("bad number");
        return std::stod(src.substr(start, pos - start));
    }

    std::string parseIdent() {
        skipSpaces();
        size_t start = pos;
        // First character must be a letter (parsePrimary guarantees this);
        // digits are allowed after that, so names like atan2/log2 work.
        if (pos < src.size() && std::isalpha((unsigned char)src[pos])) pos++;
        while (pos < src.size() && std::isalnum((unsigned char)src[pos])) pos++;
        return src.substr(start, pos - start);
    }

    double toRad(double v) { return useDegrees ? v * M_PI / 180.0 : v; }
    double fromRad(double v) { return useDegrees ? v * 180.0 / M_PI : v; }

    static double gcd2(double a, double b) {
        long long x = std::llround(std::fabs(a));
        long long y = std::llround(std::fabs(b));
        while (y != 0) {
            long long t = y;
            y = x % y;
            x = t;
        }
        return (double)x;
    }

    static double lcm2(double a, double b) {
        long long x = std::llround(std::fabs(a));
        long long y = std::llround(std::fabs(b));
        if (x == 0 || y == 0) return 0;
        long long g = (long long)gcd2((double)x, (double)y);
        return (double)(x / g * y);
    }

    static double nPr(double nd, double rd) {
        long long n = std::llround(nd), r = std::llround(rd);
        if (n < 0 || r < 0 || r > n) throw ParseError("bad nPr");
        double result = 1;
        for (long long i = 0; i < r; i++) result *= (double)(n - i);
        return result;
    }

    static double nCr(double nd, double rd) {
        long long n = std::llround(nd), r = std::llround(rd);
        if (n < 0 || r < 0 || r > n) throw ParseError("bad nCr");
        if (r > n - r) r = n - r;
        double result = 1;
        for (long long i = 0; i < r; i++) {
            result *= (double)(n - i);
            result /= (double)(i + 1);
        }
        return result;
    }

    // Uniform double in [0, 1) from the ESP32's hardware RNG.
    static double randomUnit() { return (double)esp_random() / 4294967296.0; }

    // Parses "(arg1, arg2, ...)" into a list of evaluated arguments.
    std::vector<double> parseArgs() {
        std::vector<double> args;
        if (!consume('(')) throw ParseError("expected (");
        if (peek() != ')') {
            args.push_back(parseExpr());
            while (consume(',')) args.push_back(parseExpr());
        }
        expect(')');
        return args;
    }

    double applyIdentifier(const std::string& id) {
        if (id == "pi") return M_PI;
        if (id == "e") return M_E;

        if (id == "ans") {
            // Bare `ans` = most recent result; `ans(n)` = n-th most
            // recent (1 = last, 2 = the one before that, ...). Parens
            // are optional, unlike every other function below.
            int n = 1;
            if (peek() == '(') {
                std::vector<double> a = parseArgs();
                if (a.size() != 1) throw ParseError("ans expects 0 or 1 arg");
                n = (int)std::llround(a[0]);
            }
            if (n < 1 || n > (int)history.size()) throw ParseError("no such ans");
            return history[history.size() - n].value;
        }

        std::vector<double> args = parseArgs();
        auto arg1 = [&]() -> double {
            if (args.size() != 1) throw ParseError("expects 1 arg");
            return args[0];
        };
        auto arg2 = [&]() -> std::pair<double, double> {
            if (args.size() != 2) throw ParseError("expects 2 args");
            return {args[0], args[1]};
        };

        if (id == "sin") return std::sin(toRad(arg1()));
        if (id == "cos") return std::cos(toRad(arg1()));
        if (id == "tan") return std::tan(toRad(arg1()));
        if (id == "asin") return fromRad(std::asin(arg1()));
        if (id == "acos") return fromRad(std::acos(arg1()));
        if (id == "atan") return fromRad(std::atan(arg1()));
        if (id == "tanh") return std::tanh(arg1());
        if (id == "sqrt") {
            double a = arg1();
            if (a < 0) throw ParseError("neg sqrt");
            return std::sqrt(a);
        }
        if (id == "cbrt") return std::cbrt(arg1());
        if (id == "log") {
            double a = arg1();
            if (a <= 0) throw ParseError("bad log");
            return std::log10(a);
        }
        if (id == "ln") {
            double a = arg1();
            if (a <= 0) throw ParseError("bad log");
            return std::log(a);
        }
        if (id == "exp") return std::exp(arg1());
        if (id == "abs") return std::fabs(arg1());
        if (id == "floor") return std::floor(arg1());
        if (id == "ceil") return std::ceil(arg1());
        if (id == "round") return std::round(arg1());
        if (id == "int") return std::trunc(arg1()); // truncate toward zero
        if (id == "pow") {
            std::pair<double, double> a2 = arg2();
            return std::pow(a2.first, a2.second);
        }
        if (id == "mod") {
            // Mathematical modulo: result takes the sign of the divisor
            // (unlike the % operator, which takes the sign of the dividend).
            std::pair<double, double> a2 = arg2();
            if (a2.second == 0.0) throw ParseError("div by zero");
            double r = std::fmod(a2.first, a2.second);
            if (r != 0.0 && ((r < 0) != (a2.second < 0))) r += a2.second;
            return r;
        }
        if (id == "sinh") return std::sinh(arg1());
        if (id == "cosh") return std::cosh(arg1());
        if (id == "asinh") return std::asinh(arg1());
        if (id == "acosh") {
            double a = arg1();
            if (a < 1) throw ParseError("bad acosh");
            return std::acosh(a);
        }
        if (id == "atanh") {
            double a = arg1();
            if (a <= -1 || a >= 1) throw ParseError("bad atanh");
            return std::atanh(a);
        }
        if (id == "atan2") {
            std::pair<double, double> a2 = arg2();
            return fromRad(std::atan2(a2.first, a2.second));
        }
        if (id == "log2") {
            double a = arg1();
            if (a <= 0) throw ParseError("bad log2");
            return std::log2(a);
        }
        if (id == "min") {
            std::pair<double, double> a2 = arg2();
            return std::min(a2.first, a2.second);
        }
        if (id == "max") {
            std::pair<double, double> a2 = arg2();
            return std::max(a2.first, a2.second);
        }
        if (id == "gcd") {
            std::pair<double, double> a2 = arg2();
            return gcd2(a2.first, a2.second);
        }
        if (id == "lcm") {
            std::pair<double, double> a2 = arg2();
            return lcm2(a2.first, a2.second);
        }
        if (id == "ncr") {
            std::pair<double, double> a2 = arg2();
            return nCr(a2.first, a2.second);
        }
        if (id == "npr") {
            std::pair<double, double> a2 = arg2();
            return nPr(a2.first, a2.second);
        }
        if (id == "rand") {
            if (args.size() == 0) return randomUnit();
            if (args.size() == 2) return args[0] + randomUnit() * (args[1] - args[0]);
            throw ParseError("rand expects 0 or 2 args");
        }
        if (id == "randint") {
            // Integer random number, inclusive of both endpoints.
            std::pair<double, double> a2 = arg2();
            long long lo = std::llround(a2.first);
            long long hi = std::llround(a2.second);
            if (lo > hi) std::swap(lo, hi);
            long long range = hi - lo + 1;
            long long r = lo + (long long)(randomUnit() * (double)range);
            if (r > hi) r = hi; // guard against floating-point edge case
            return (double)r;
        }
        if (id == "clamp") {
            if (args.size() != 3) throw ParseError("expects 3 args");
            double x = args[0], lo = args[1], hi = args[2];
            if (lo > hi) std::swap(lo, hi);
            if (x < lo) return lo;
            if (x > hi) return hi;
            return x;
        }
        throw ParseError("unknown func");
    }
};

// ---------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------
static std::string formatNumber(double v) {
    if (std::isnan(v)) return "ERR";
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    char buf[64];
    // Use enough precision but trim trailing zeros
    snprintf(buf, sizeof(buf), "%.10g", v);
    return std::string(buf);
}

static bool equalsIgnoreCase(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i]; i++) {
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    }
    return i == a.size() && b[i] == '\0';
}

static bool startsWithIgnoreCase(const std::string& a, const char* prefix) {
    size_t i = 0;
    for (; prefix[i]; i++) {
        if (i >= a.size() || std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)prefix[i]))
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------
// Software clock: this hardware has no RTC chip and this sketch has no
// Wi-Fi/NTP, so there's no time source unless the user sets one. settime()
// anchors a wall-clock time to the current millis(); currentTimeSeconds()
// projects it forward. Resets to "unset" on every power-cycle.
// ---------------------------------------------------------------------
static bool timeSet = false;
static uint32_t timeBaseMillis = 0;
static int32_t timeBaseSeconds = 0;

static int32_t currentTimeSeconds() {
    uint32_t elapsedMs = millis() - timeBaseMillis; // unsigned wraparound-safe
    int32_t total = (timeBaseSeconds + (int32_t)(elapsedMs / 1000)) % 86400;
    if (total < 0) total += 86400;
    return total;
}

static std::string formatHMS(int32_t totalSeconds) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", (int)(totalSeconds / 3600),
             (int)((totalSeconds % 3600) / 60), (int)(totalSeconds % 60));
    return std::string(buf);
}

// Parses "settime(H,M,S)" (the part in parens) into three integers.
static bool parseSettimeArgs(const std::string& s, int& h, int& m, int& sec) {
    size_t open = s.find('(');
    size_t close = s.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open) return false;
    std::string inner = s.substr(open + 1, close - open - 1);

    int vals[3];
    int count = 0;
    size_t pos = 0;
    while (count < 3) {
        size_t comma = inner.find(',', pos);
        std::string tok = (comma == std::string::npos) ? inner.substr(pos) : inner.substr(pos, comma - pos);
        size_t a = tok.find_first_not_of(' ');
        size_t b = tok.find_last_not_of(' ');
        if (a == std::string::npos) return false;
        tok = tok.substr(a, b - a + 1);
        if (tok.empty()) return false;
        for (char c : tok)
            if (!std::isdigit((unsigned char)c)) return false;
        vals[count++] = atoi(tok.c_str());
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    if (count != 3) return false;
    h = vals[0];
    m = vals[1];
    sec = vals[2];
    return true;
}

// ---------------------------------------------------------------------
// Persistence: history + settings auto-saved to internal flash (NVS),
// and an explicit text export to a microSD card.
// ---------------------------------------------------------------------
static void saveStateToFlash() {
    prefs.begin(PREFS_NS, false);
    prefs.putBool("deg", degMode);
    prefs.putUInt("hn", (uint32_t)history.size());
    for (size_t i = 0; i < history.size(); i++) {
        char key[12];
        snprintf(key, sizeof(key), "he%u", (unsigned)i);
        prefs.putString(key, history[i].expr.c_str());
        snprintf(key, sizeof(key), "hr%u", (unsigned)i);
        prefs.putString(key, history[i].result.c_str());
        snprintf(key, sizeof(key), "hv%u", (unsigned)i);
        prefs.putDouble(key, history[i].value);
    }
    prefs.end();
}

static void loadStateFromFlash() {
    prefs.begin(PREFS_NS, true);
    degMode = prefs.getBool("deg", false);
    uint32_t n = prefs.getUInt("hn", 0);
    history.clear();
    for (uint32_t i = 0; i < n && i < HISTORY_STORE_CAP; i++) {
        char key[12];
        HistEntry he;
        snprintf(key, sizeof(key), "he%u", (unsigned)i);
        he.expr = prefs.getString(key, "").c_str();
        snprintf(key, sizeof(key), "hr%u", (unsigned)i);
        he.result = prefs.getString(key, "").c_str();
        snprintf(key, sizeof(key), "hv%u", (unsigned)i);
        he.value = prefs.getDouble(key, 0.0);
        history.push_back(he);
    }
    prefs.end();
}

// Appends the current in-memory history to /calc_log.txt on the SD card
// as a labeled block, so re-running "save" doesn't overwrite older saves.
static bool saveHistoryToSD() {
    if (!sdReady) return false;
    File f = SD.open(SD_LOG_PATH, FILE_APPEND);
    if (!f) return false;

    prefs.begin(PREFS_NS, false);
    uint32_t saveNum = prefs.getUInt("savenum", 0) + 1;
    prefs.putUInt("savenum", saveNum);
    prefs.end();

    std::string tsSuffix = timeSet ? (" @ " + formatHMS(currentTimeSeconds())) : std::string("");
    f.printf("---- save #%u (%u entries, %s)%s ----\n", (unsigned)saveNum,
              (unsigned)history.size(), degMode ? "DEG" : "RAD", tsSuffix.c_str());
    for (auto& h : history) {
        f.printf("%s = %s\n", h.expr.c_str(), h.result.c_str());
    }
    f.println();
    f.close();
    return true;
}

static void evaluate() {
    if (expr.empty()) return;
    if (equalsIgnoreCase(expr, "help")) {
        helpMode = true;
        helpPage = 0;
        expr.clear();
        resultLine.clear();
        haveResult = false;
        browseIndex = -1;
        cursorPos = 0;
        return;
    }
    if (equalsIgnoreCase(expr, "save")) {
        bool ok = saveHistoryToSD();
        resultLine = ok ? ("Saved to " + std::string(SD_LOG_PATH)) : "SD ERR (no card?)";
        expr.clear();
        haveResult = true;
        browseIndex = -1;
        cursorPos = 0;
        return;
    }
    if (equalsIgnoreCase(expr, "time")) {
        resultLine = timeSet ? formatHMS(currentTimeSeconds()) : "Time not set (settime(H,M,S))";
        expr.clear();
        haveResult = true;
        browseIndex = -1;
        cursorPos = 0;
        return;
    }
    if (startsWithIgnoreCase(expr, "settime(") && !expr.empty() && expr.back() == ')') {
        int h, m, s;
        if (parseSettimeArgs(expr, h, m, s) && h >= 0 && h < 24 && m >= 0 && m < 60 && s >= 0 && s < 60) {
            timeBaseSeconds = h * 3600 + m * 60 + s;
            timeBaseMillis = millis();
            timeSet = true;
            resultLine = "Time set to " + formatHMS(timeBaseSeconds);
        } else {
            resultLine = "ERR: settime(H,M,S) 0-23,0-59,0-59";
        }
        expr.clear();
        haveResult = true;
        browseIndex = -1;
        cursorPos = 0;
        return;
    }
    if (equalsIgnoreCase(expr, "usbdrive")) {
        // Only reachable via the calculator UI, which renders its own
        // screen right up until this call, so haveResult/render() here
        // don't matter once mscModeActive flips loop()/render() over.
        if (!enterUsbDriveMode()) {
            resultLine = "SD ERR (no card?)";
            expr.clear();
            haveResult = true;
        }
        browseIndex = -1;
        cursorPos = 0;
        return;
    }
    try {
        Parser p(expr, degMode);
        double v = p.run();
        std::string res = formatNumber(v);
        resultLine = "= " + res;
        history.push_back({expr, res, v});
        if (history.size() > HISTORY_STORE_CAP) history.erase(history.begin());
        haveResult = true;
        saveStateToFlash();
    } catch (const std::exception& ex) {
        resultLine = std::string("ERR: ") + ex.what();
        haveResult = true;
    }
    browseIndex = -1;
}

// Load history[idx] into the edit buffer for editing/recalculation.
static void loadHistoryEntry(int idx) {
    if (idx < 0 || idx >= (int)history.size()) return;
    expr = history[idx].expr;
    resultLine.clear();
    haveResult = false;
    browseIndex = idx;
    cursorPos = expr.size();
}

// fn+; : step to an older history entry (first press = the newest one).
static void historyOlder() {
    if (history.empty()) return;
    int idx = (browseIndex == -1) ? (int)history.size() - 1 : browseIndex - 1;
    if (idx < 0) idx = 0;
    loadHistoryEntry(idx);
}

// fn+. : step toward the newest entry, then back to a blank line.
static void historyNewer() {
    if (browseIndex == -1) return;
    int idx = browseIndex + 1;
    if (idx >= (int)history.size()) {
        browseIndex = -1;
        expr.clear();
        resultLine.clear();
        haveResult = false;
        cursorPos = 0;
    } else {
        loadHistoryEntry(idx);
    }
}

// fn+, / fn+/ : move the edit cursor within the expression.
static void cursorLeft() {
    if (cursorPos > 0) cursorPos--;
}
static void cursorRight() {
    if (cursorPos < expr.size()) cursorPos++;
}

// ---------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------
static const int LINE_H = 12;   // px per text line at text size 1
static const int TOP_Y = 16;    // first history line's y (below the title)

static void renderUsbDriveScreen() {
    canvas.fillSprite(TFT_BLACK);
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_GREEN, TFT_BLACK);
    canvas.setCursor(2, 2);
    canvas.print("USB Mass Storage");
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.setCursor(2, TOP_Y);
    canvas.print("SD card exposed over USB.");
    canvas.setCursor(2, TOP_Y + LINE_H);
    canvas.print("Find it as a drive on your");
    canvas.setCursor(2, TOP_Y + 2 * LINE_H);
    canvas.print("computer.");
    canvas.setTextColor(TFT_YELLOW, TFT_BLACK);
    canvas.setCursor(2, TOP_Y + 4 * LINE_H);
    canvas.print("Reset/power-cycle to return");
    canvas.setCursor(2, TOP_Y + 5 * LINE_H);
    canvas.print("to the calculator.");
    canvas.pushSprite(0, 0);
}

static void render() {
    if (mscModeActive) {
        renderUsbDriveScreen();
        return;
    }

    int h = canvas.height();
    canvas.fillSprite(TFT_BLACK);
    canvas.setTextSize(1);

    if (helpMode) {
        canvas.setTextColor(TFT_GREEN, TFT_BLACK);
        canvas.setCursor(2, 2);
        canvas.printf("Help  page %d/%d", helpPage + 1, (int)helpPages.size());

        canvas.setTextColor(TFT_WHITE, TFT_BLACK);
        int hy = TOP_Y;
        for (auto& line : helpPages[helpPage]) {
            canvas.setCursor(2, hy);
            canvas.print(line.c_str());
            hy += LINE_H;
        }

        canvas.setTextColor(TFT_DARKGREY, TFT_BLACK);
        canvas.setCursor(2, h - LINE_H - 2);
        canvas.print("fn+;/. page   Enter/BkSp exit");

        canvas.pushSprite(0, 0);
        return;
    }

    canvas.setTextColor(TFT_GREEN, TFT_BLACK);
    canvas.setCursor(2, 2);
    canvas.printf("Adv Calc  [%s]", degMode ? "DEG" : "RAD");

    // expression + result are pinned to the bottom two lines
    int resultY = h - LINE_H - 2;
    int exprY = resultY - LINE_H;

    // history (older calculations, dimmed), filling the space above.
    // Only the most recent `historyDisplayCap` entries fit on screen;
    // older ones are still stored and reachable via fn+; (and ans(n)).
    size_t startIdx = history.size() > historyDisplayCap ? history.size() - historyDisplayCap : 0;
    int y = TOP_Y;
    canvas.setTextColor(TFT_DARKGREY, TFT_BLACK);
    for (size_t i = startIdx; i < history.size(); i++) {
        canvas.setCursor(2, y);
        canvas.print((history[i].expr + " = " + history[i].result).c_str());
        y += LINE_H;
    }

    // current expression, with a "|" marking the edit cursor position
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.setCursor(2, exprY);
    if (haveResult) {
        canvas.print(expr.c_str());
    } else {
        std::string shown = expr.substr(0, cursorPos) + "|" + expr.substr(cursorPos);
        canvas.print(shown.c_str());
    }

    // result / error line
    canvas.setTextColor(haveResult && resultLine.rfind("ERR", 0) == 0 ? TFT_RED : TFT_YELLOW, TFT_BLACK);
    canvas.setCursor(2, resultY);
    canvas.print(resultLine.c_str());

    canvas.pushSprite(0, 0);
}

// ---------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------
static void clearAll() {
    expr.clear();
    resultLine.clear();
    haveResult = false;
    browseIndex = -1;
    cursorPos = 0;
}

static void handleChar(char c) {
    // Only accept characters that are meaningful in an expression.
    static const std::string allowed = "0123456789.+-*/^%()!,";
    bool isFuncLetter = std::isalpha((unsigned char)c);
    if (allowed.find(c) == std::string::npos && !isFuncLetter) return;

    browseIndex = -1; // typing diverges from any recalled history entry

    if (haveResult) {
        // Start a fresh expression, unless the first new char is an
        // operator, in which case continue from the previous result.
        bool isOperator = (c == '+' || c == '-' || c == '*' || c == '/' ||
                            c == '^' || c == '%');
        std::string prevResult;
        if (isOperator && resultLine.rfind("= ", 0) == 0) {
            prevResult = resultLine.substr(2);
        }
        expr.clear();
        resultLine.clear();
        haveResult = false;
        if (!prevResult.empty()) expr += prevResult;
        cursorPos = expr.size();
    }

    if (expr.size() < MAX_EXPR_LEN) {
        expr.insert(expr.begin() + cursorPos, c);
        cursorPos++;
    }
}

static void handleBackspace() {
    if (haveResult) {
        // Re-open the previous expression for editing instead of
        // clearing it (use fn+Backspace to actually clear).
        resultLine.clear();
        haveResult = false;
        cursorPos = expr.size();
        return;
    }
    if (cursorPos > 0) {
        expr.erase(expr.begin() + (cursorPos - 1));
        cursorPos--;
        browseIndex = -1; // editing diverges from any recalled history entry
    }
}

// Names Tab-completion will offer, i.e. everything applyIdentifier()
// recognizes plus the "help" command.
static const std::vector<std::string> FUNCTION_NAMES = {
    "pi", "e", "ans", "help", "save", "time", "settime", "usbdrive",
    "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
    "tanh", "sinh", "cosh", "asinh", "acosh", "atanh",
    "sqrt", "cbrt", "pow", "exp", "log", "ln", "log2",
    "abs", "floor", "ceil", "round", "int",
    "mod", "min", "max", "clamp", "gcd", "lcm", "ncr", "npr",
    "rand", "randint",
};

// Words that stand alone (no argument list), so Tab shouldn't add "(".
static bool isBareWord(const std::string& w) {
    return w == "pi" || w == "e" || w == "ans" || w == "help" || w == "save" || w == "time" || w == "usbdrive";
}

// Tab-completion state: which span of `expr` is being cycled, and which
// candidate (from the last scan) is currently shown there.
static bool tabActive = false;
static size_t tabWordStart = 0;
static std::vector<std::string> tabCandidates;
static size_t tabCandidateIdx = 0;

static void handleTab() {
    if (haveResult) return; // nothing to complete once a result is shown

    if (!tabActive) {
        // Find the identifier prefix immediately before the cursor.
        size_t start = cursorPos;
        while (start > 0 && std::isalpha((unsigned char)expr[start - 1])) start--;
        std::string prefix = expr.substr(start, cursorPos - start);
        if (prefix.empty()) return;

        std::vector<std::string> matches;
        for (auto& name : FUNCTION_NAMES) {
            if (name.size() >= prefix.size() && name.compare(0, prefix.size(), prefix) == 0) {
                matches.push_back(name);
            }
        }
        if (matches.empty()) return;
        std::sort(matches.begin(), matches.end());

        tabWordStart = start;
        tabCandidates = matches;
        tabCandidateIdx = 0;
        tabActive = true;
    } else {
        // Repeated Tab: cycle to the next candidate for the same prefix.
        tabCandidateIdx = (tabCandidateIdx + 1) % tabCandidates.size();
    }

    const std::string& chosen = tabCandidates[tabCandidateIdx];
    expr.erase(tabWordStart, cursorPos - tabWordStart);
    expr.insert(tabWordStart, chosen);
    cursorPos = tabWordStart + chosen.size();

    // Auto-open the argument list for real functions, unless one's
    // already there.
    if (!isBareWord(chosen) && (cursorPos >= expr.size() || expr[cursorPos] != '(')) {
        expr.insert(expr.begin() + cursorPos, '(');
        cursorPos++;
    }

    browseIndex = -1;
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(80);
    canvas.setColorDepth(8);
    canvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
    canvas.setTextFont(1);

    // Fit as many history lines as the screen allows, above the two
    // lines reserved at the bottom for the expression and result.
    int reservedBottom = 2 * LINE_H + 2;
    int available = canvas.height() - TOP_Y - reservedBottom;
    size_t fits = available > 0 ? (size_t)(available / LINE_H) : 0;
    historyDisplayCap = std::min(fits, HISTORY_STORE_CAP);
    if (historyDisplayCap < 1) historyDisplayCap = 1;

    loadStateFromFlash(); // restore history + DEG/RAD from before power-off

    // SD card is optional: the calculator works fine without one, "save"
    // just reports an error until a card is present.
    SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
    sdReady = SD.begin(SD_SPI_CS_PIN, SPI, 25000000);
    if (sdReady) sdSectorCount = (uint32_t)(SD.totalBytes() / MSC_SECTOR_SIZE);

    render();
}

static bool wordHas(const Keyboard_Class::KeysState& s, char c) {
    return std::find(s.word.begin(), s.word.end(), c) != s.word.end();
}

void loop() {
    if (mscModeActive) {
        // The SD card now belongs entirely to the raw MSC callbacks;
        // don't touch the keyboard/SD-via-SD.h from here until reset.
        static uint32_t lastDraw = 0;
        if (millis() - lastDraw > 1000) {
            lastDraw = millis();
            render();
        }
        return;
    }

    M5Cardputer.update();

    if (M5Cardputer.Keyboard.isChange()) {
        if (M5Cardputer.Keyboard.isPressed()) {
            Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

            // This library's `fn`/`opt` are plain modifier flags: holding
            // either still lets the pressed key populate `word` normally,
            // so combos are detected by checking the flag alongside `word`.
            bool optD = status.opt && (wordHas(status, 'd') || wordHas(status, 'D'));
            bool fnUp = status.fn && wordHas(status, ';');
            bool fnDown = status.fn && wordHas(status, '.');
            bool fnLeft = status.fn && wordHas(status, ',');
            bool fnRight = status.fn && wordHas(status, '/');

            if (helpMode) {
                // While the help screen is up, fn+;/. flips pages and
                // Enter/Backspace returns to the calculator; everything
                // else (typing, deg toggle, etc.) is ignored.
                if (fnUp) {
                    helpPage = (helpPage + (int)helpPages.size() - 1) % (int)helpPages.size();
                } else if (fnDown) {
                    helpPage = (helpPage + 1) % (int)helpPages.size();
                } else if (status.enter || status.del) {
                    helpMode = false;
                }
                render();
                return;
            }

            if (status.tab) {
                handleTab();
                render();
                return;
            }
            tabActive = false; // any other key ends a completion cycle

            if (status.fn && status.del) {
                clearAll();
            } else if (fnUp) {
                historyOlder();
            } else if (fnDown) {
                historyNewer();
            } else if (fnLeft) {
                cursorLeft();
            } else if (fnRight) {
                cursorRight();
            } else if (optD) {
                degMode = !degMode;
                saveStateToFlash();
            } else if (status.del) {
                handleBackspace();
            } else if (status.enter) {
                evaluate();
            } else {
                for (char c : status.word) {
                    handleChar(c);
                }
            }
            render();
        }
    }
}
