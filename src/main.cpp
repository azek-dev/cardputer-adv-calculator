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
// There's no RTC chip on this hardware, so there's no real clock (or
// calendar) unless you set one. "timeset(H,M,S)" / "dateset(Y,M,D)" each
// set a reference by hand (from millis() elapsed since, independently of
// each other); "time" / "date" show the current computed value. Both
// reset on every power-cycle — re-run after each boot if you want them.
//
// Alternatively, "wifi(ssid,pass)" saves Wi-Fi credentials to flash and
// immediately syncs both the time and date via NTP (hardcoded to JST);
// "wifi()" retries with the saved credentials (e.g. after a reboot); bare
// "wifi" just shows what's saved. Nothing here ever prompts for Wi-Fi
// automatically — it's entirely opt-in, and boot/typing/calculating is
// unaffected if unused.
//
// The calculator deep-sleeps after 10 minutes with no key press, or
// immediately if the physical G0/BtnA button (on top of the device) is
// pressed at any time — this board has no PMIC for a true power-off, so
// deep sleep is the closest equivalent (a few tens of µA instead of a
// full shutdown). The same G0/BtnA button also wakes it back up (not a
// keyboard key — the whole keyboard is powered down during sleep); if
// Wi-Fi credentials are saved, waking this way also silently retries an
// NTP time sync (a plain power-on never does this on its own). "sleeptime(n)"
// changes the idle timeout to n minutes (0 disables it); bare "sleeptime"
// shows the current setting. The setting is saved to flash and persists
// across power cycles.
//
// Type "usbdrive" and press Enter to expose the microSD card to a computer
// over the same USB-C cable, as an ordinary USB drive — no card removal
// needed. This takes over the SD card and the USB port for that purpose;
// the calculator only works normally again after a reset/power-cycle. The
// raw SD-over-SPI block I/O routines (mscSdRawInit/mscReadSector/
// mscWriteSector below) are adapted from MOY-lightening-firmware's
// "M5-cardputer-mass-storage" (MIT License, Copyright (c) 2026 OZAN),
// https://github.com/MOY-lightening-firmware/M5-cardputer-mass-storage
// If "usbdrive" fails on a particular card, "usbdebug" (after a reset)
// shows the last low-level SD error recorded during that attempt.

#include <M5Cardputer.h>
#include <M5GFX.h>
#include <esp_random.h>
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <utility>
#include <CardputerClock.h>
#include <CardputerUsbDrive.h>
#include <CardputerSleep.h>

static M5Canvas canvas(&M5Cardputer.Display);
static const int LINE_H = 12;   // px per text line at text size 1
static const int TOP_Y = 16;    // first history line's y (below the title)

// microSD wiring on both Cardputer and Cardputer ADV (per M5Stack's
// official examples) — SPI bus is not shared with anything else.
static const int SD_SPI_SCK_PIN = 40;
static const int SD_SPI_MISO_PIN = 39;
static const int SD_SPI_MOSI_PIN = 14;
static const int SD_SPI_CS_PIN = 12;
static const char* SD_LOG_PATH = "/calc_log.txt";
static const uint32_t SD_SECTOR_SIZE = 512; // must match CardputerUsbDrive's own sector size
static bool sdReady = false;
static uint32_t sdSectorCount = 0; // populated at boot while SD.h has it mounted

static Preferences prefs;
static const char* PREFS_NS = "calc";

// USB Mass Storage (exposes the microSD card to a host computer as an
// ordinary USB drive) and the underlying raw SD-over-SPI I/O now live in
// the shared CardputerUsbDrive library — see cardputer-common.
static CardputerUsbDrive usbDrive;

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
    {"Trig & hyperbolic:", "sin cos tan atan2 (opt+D", " toggles deg/rad)",
     "sinh cosh tanh", "asinh acosh atanh", "ex: sin(pi/2)=1  tanh(1)=.76"},
    {"Power/log & compare:", "sqrt cbrt pow(x,y)", "exp log ln log2(x)",
     "min max clamp(x,lo,hi)", "gcd lcm mod(a,b)", "ex: pow(2,10)=1024", "ex: gcd(12,18)=6"},
    {"Combinatorics & rounding:", "ncr(n,r) npr(n,r)", "rand() rand(lo,hi)",
     "randint(lo,hi) inclusive", "abs floor ceil round int", "pi e x! ^ %", "ex: randint(1,6)=dice"},
    {"Previous results:", "ans = most recent result", "ans(n) = n-th most recent", "ex: ans(1)+ans(2)+ans(3)"},
    {"Saving:", "History auto-saves to flash", "(survives power off, no SD", "card needed).", "Type save + Enter to also", "append it to calc_log.txt", "on a microSD card."},
    {"Clock (no RTC on this", "board, resets each boot):", "timeset(H,M,S) / time", "dateset(Y,M,D) / date", "ex: timeset(9,30,0)", "ex: dateset(2026,9,13)"},
    {"USB drive mode:", "usbdrive exposes the SD", "card to a computer over", "USB. Needs reset/power-", "cycle to return to the", "calculator afterward.", "usbdebug shows why it", "failed, after a reset."},
    {"Auto-sleep (no PMIC, so", "this is deep sleep, not a", "real power-off):", "sleeptime(n) sets n min", "sleeptime shows current", "G0/BtnA (top button) also", "sleeps/wakes on demand;", "wake retries saved wifi"},
    {"Wifi time sync (opt-in,", "never asked automatically):", "wifi(ssid,pass) saves +", "syncs via NTP (JST)", "wifi() retries saved creds", "wifi shows saved SSID"},
    {"Battery & uptime:", "battery = level %/volts", "uptime = time since last", "  boot/wake (resets on", "  sleep, like the clock)"},
    {"Keys:", "fn+BkSp = clear all", "fn+;/.  = history up/down", "fn+,//  = cursor left/right", "opt+D   = deg/rad toggle", "Tab     = complete func name"},
};

static const size_t MAX_EXPR_LEN = 200;
// How many past calculations are kept in memory (browsable via fn+;,
// and available to ans(n)) — plenty of RAM headroom for this.
static const size_t HISTORY_STORE_CAP = 20;
// How many of the most recent entries fit on screen at once; computed at
// startup in setup() from the actual display size (see historyDisplayCap).
static size_t historyDisplayCap = 4;

// Idle-timeout deep sleep (no PMIC on this board) now lives in the shared
// CardputerSleep library — see cardputer-common.
static const int WAKE_BUTTON_PIN = 0; // G0 / BtnA, the side button
static CardputerSleep sleepMgr;

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

// Software clock (no RTC on this hardware) and Wi-Fi/NTP sync now live in
// the shared CardputerClock library — see cardputer-common.
static CardputerClock clock_;

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

    std::string tsSuffix;
    if (clock_.isDateSet()) {
        tsSuffix += " @ " + clock_.dateString();
        if (clock_.isTimeSet()) tsSuffix += " " + clock_.timeString();
    } else if (clock_.isTimeSet()) {
        tsSuffix = " @ " + clock_.timeString();
    }
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
        resultLine = clock_.isTimeSet() ? clock_.timeString() : "Time not set (timeset(H,M,S))";
        expr.clear();
        haveResult = true;
        browseIndex = -1;
        cursorPos = 0;
        return;
    }
    if (startsWithIgnoreCase(expr, "timeset(") && !expr.empty() && expr.back() == ')') {
        int h, m, s;
        if (CardputerClock::parseThreeIntArgs(expr, h, m, s) && clock_.setTime(h, m, s)) {
            resultLine = "Time set to " + clock_.timeString();
        } else {
            resultLine = "ERR: timeset(H,M,S) 0-23,0-59,0-59";
        }
        expr.clear();
        haveResult = true;
        browseIndex = -1;
        cursorPos = 0;
        return;
    }
    if (equalsIgnoreCase(expr, "date")) {
        resultLine = clock_.isDateSet() ? clock_.dateString() : "Date not set (dateset(Y,M,D))";
        expr.clear();
        haveResult = true;
        browseIndex = -1;
        cursorPos = 0;
        return;
    }
    if (startsWithIgnoreCase(expr, "dateset(") && !expr.empty() && expr.back() == ')') {
        int y, m, d;
        if (CardputerClock::parseThreeIntArgs(expr, y, m, d) && clock_.setDate(y, m, d)) {
            resultLine = "Date set to " + clock_.dateString();
        } else {
            resultLine = "ERR: dateset(Y,M,D) e.g. dateset(2026,9,13)";
        }
        expr.clear();
        haveResult = true;
        browseIndex = -1;
        cursorPos = 0;
        return;
    }
    if (equalsIgnoreCase(expr, "wifi")) {
        // Status only, no side effects: never blocks or touches the radio.
        resultLine = clock_.hasSavedWifi() ? ("saved: " + clock_.savedSsid() + " (wifi() to sync)") : "no wifi saved (wifi(ssid,pass))";
        expr.clear();
        haveResult = true;
        browseIndex = -1;
        cursorPos = 0;
        return;
    }
    if (startsWithIgnoreCase(expr, "wifi(") && !expr.empty() && expr.back() == ')') {
        size_t open = expr.find('(');
        size_t close = expr.rfind(')');
        std::string inner = expr.substr(open + 1, close - open - 1);
        if (inner.empty()) {
            // wifi(): retry with whatever is already saved.
            if (!clock_.hasSavedWifi()) {
                resultLine = "ERR: no saved wifi (use wifi(ssid,pass))";
            } else {
                resultLine = clock_.wifiRetry() ? ("Time synced: " + clock_.timeString())
                                                 : "Wifi/NTP failed (time unchanged)";
            }
        } else {
            size_t comma = inner.find(',');
            if (comma == std::string::npos) {
                resultLine = "ERR: wifi(ssid,pass)";
            } else {
                std::string ssid = inner.substr(0, comma);
                std::string pass = inner.substr(comma + 1);
                resultLine = clock_.wifiSync(ssid, pass) ? ("Time synced: " + clock_.timeString())
                                                          : "Saved. Wifi/NTP failed (time unchanged)";
            }
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
        // don't matter once usbDrive.isActive() flips loop()/render() over.
        usbDrive.setSdInfo(sdReady, sdSectorCount);
        if (!usbDrive.enter()) {
            resultLine = "SD ERR (no card?)";
            expr.clear();
            haveResult = true;
        }
        browseIndex = -1;
        cursorPos = 0;
        return;
    }
    if (equalsIgnoreCase(expr, "usbdebug")) {
        // Shows the last low-level SD failure recorded during a usbdrive
        // session — useful if usbdrive fails on a particular SD card.
        resultLine = CardputerUsbDrive::lastDebugInfo();
        expr.clear();
        haveResult = true;
        browseIndex = -1;
        cursorPos = 0;
        return;
    }
    if (equalsIgnoreCase(expr, "sleeptime")) {
        char buf[48];
        uint32_t mins = sleepMgr.idleMinutes();
        if (mins == 0) snprintf(buf, sizeof(buf), "auto-sleep disabled");
        else snprintf(buf, sizeof(buf), "auto-sleep after %lu min", (unsigned long)mins);
        resultLine = buf;
        expr.clear();
        haveResult = true;
        browseIndex = -1;
        cursorPos = 0;
        return;
    }
    if (startsWithIgnoreCase(expr, "sleeptime(") && !expr.empty() && expr.back() == ')') {
        size_t open = expr.find('(');
        size_t close = expr.rfind(')');
        std::string inner = (open != std::string::npos && close != std::string::npos && close > open)
                                 ? expr.substr(open + 1, close - open - 1)
                                 : "";
        size_t a = inner.find_first_not_of(' ');
        bool valid = a != std::string::npos;
        if (valid) {
            size_t b = inner.find_last_not_of(' ');
            inner = inner.substr(a, b - a + 1);
        }
        for (char c : inner)
            if (!std::isdigit((unsigned char)c)) valid = false;
        long n = valid && !inner.empty() ? atol(inner.c_str()) : -1;
        if (valid && n >= 0 && n <= 1440 && sleepMgr.setIdleMinutes((uint32_t)n)) {
            char buf[48];
            uint32_t mins = sleepMgr.idleMinutes();
            if (mins == 0) snprintf(buf, sizeof(buf), "auto-sleep disabled");
            else snprintf(buf, sizeof(buf), "auto-sleep after %lu min", (unsigned long)mins);
            resultLine = buf;
        } else {
            resultLine = "ERR: sleeptime(0-1440)";
        }
        expr.clear();
        haveResult = true;
        browseIndex = -1;
        cursorPos = 0;
        return;
    }
    if (equalsIgnoreCase(expr, "battery")) {
        // No charge-status pin is wired on this board (it charges directly
        // through the Stamp S3 module), so M5Unified can't report
        // charging/not-charging here — level and voltage only.
        int32_t level = M5Cardputer.Power.getBatteryLevel();
        int16_t mv = M5Cardputer.Power.getBatteryVoltage();
        char buf[48];
        if (level < 0) snprintf(buf, sizeof(buf), "battery: unavailable");
        else snprintf(buf, sizeof(buf), "battery: %ld%% (%.2fV)", (long)level, mv / 1000.0f);
        resultLine = buf;
        expr.clear();
        haveResult = true;
        browseIndex = -1;
        cursorPos = 0;
        return;
    }
    if (equalsIgnoreCase(expr, "uptime")) {
        uint32_t totalSeconds = millis() / 1000;
        uint32_t days = totalSeconds / 86400;
        int32_t secOfDay = (int32_t)(totalSeconds % 86400);
        char buf[32];
        if (days > 0) snprintf(buf, sizeof(buf), "%lud %s", (unsigned long)days, CardputerClock::formatHMS(secOfDay).c_str());
        else snprintf(buf, sizeof(buf), "%s", CardputerClock::formatHMS(secOfDay).c_str());
        resultLine = buf;
        expr.clear();
        haveResult = true;
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
    if (usbDrive.isActive()) {
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
    // Only accept characters that are meaningful in an expression, plus
    // space/underscore for typing wifi(ssid,pass) — most home SSIDs use
    // only letters, digits, spaces, hyphens, and underscores.
    static const std::string allowed = "0123456789.+-*/^%()!, _";
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
    "pi", "e", "ans", "help", "save", "time", "timeset", "date", "dateset", "usbdrive", "usbdebug", "sleeptime", "wifi", "battery", "uptime",
    "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
    "tanh", "sinh", "cosh", "asinh", "acosh", "atanh",
    "sqrt", "cbrt", "pow", "exp", "log", "ln", "log2",
    "abs", "floor", "ceil", "round", "int",
    "mod", "min", "max", "clamp", "gcd", "lcm", "ncr", "npr",
    "rand", "randint",
};

// Words that stand alone (no argument list), so Tab shouldn't add "(".
static bool isBareWord(const std::string& w) {
    return w == "pi" || w == "e" || w == "ans" || w == "help" || w == "save" || w == "time" || w == "date" ||
           w == "usbdrive" || w == "usbdebug" || w == "sleeptime" || w == "wifi" || w == "battery" || w == "uptime";
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

        // Kept in FUNCTION_NAMES's own declared order (not alphabetical),
        // so more commonly-used names can be placed to complete first —
        // e.g. usbdrive before usbdebug.
        std::vector<std::string> matches;
        for (auto& name : FUNCTION_NAMES) {
            if (name.size() >= prefix.size() && name.compare(0, prefix.size(), prefix) == 0) {
                matches.push_back(name);
            }
        }
        if (matches.empty()) return;

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
    Serial.begin(115200);
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
    clock_.begin();       // restore saved Wi-Fi credentials (not the clock itself)
    sleepMgr.begin(WAKE_BUTTON_PIN);

    // SD card is optional: the calculator works fine without one, "save"
    // just reports an error until a card is present.
    SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
    sdReady = SD.begin(SD_SPI_CS_PIN, SPI, 25000000);
    if (sdReady) sdSectorCount = (uint32_t)(SD.totalBytes() / SD_SECTOR_SIZE);
    usbDrive.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN,
                   "CalcCard", "Cardputer", "1.0", "CardputerCalc", "SD Card");

    // Only right after waking from deep sleep via the G0 button — never on
    // a plain power-on — silently retry a saved Wi-Fi sync, since that's
    // exactly when the software clock has just been wiped.
    if (sleepMgr.wokenByButton() && clock_.hasSavedWifi()) {
        canvas.fillSprite(TFT_BLACK);
        canvas.setTextColor(TFT_YELLOW, TFT_BLACK);
        canvas.setCursor(2, 2);
        canvas.print("Syncing time via Wi-Fi...");
        canvas.pushSprite(0, 0);
        clock_.wifiRetry(); // best-effort; failure just leaves the clock unset
    }

    render();
}

static bool wordHas(const Keyboard_Class::KeysState& s, char c) {
    return std::find(s.word.begin(), s.word.end(), c) != s.word.end();
}

void loop() {
    if (usbDrive.isActive()) {
        // The SD card now belongs entirely to the raw MSC callbacks; do
        // nothing at all here, not even redraw the display — the display
        // is also SPI, and any activity on it while a raw SD transaction
        // from the host is in flight was corrupting reads (this was the
        // actual bug behind read failures during mount).
        delay(1000);
        return;
    }

    if (sleepMgr.idleTimeoutReached()) {
        sleepMgr.enterDeepSleep(saveStateToFlash); // never returns
    }

    // G0/BtnA also works as an immediate manual sleep button, not just as
    // the wake source: press it any time to skip the idle timeout.
    if (sleepMgr.buttonPressedEdge()) {
        sleepMgr.enterDeepSleep(saveStateToFlash); // never returns
    }

    M5Cardputer.update();

    if (M5Cardputer.Keyboard.isChange()) {
        if (M5Cardputer.Keyboard.isPressed()) {
            sleepMgr.noteActivity();
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
