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

#include <M5Cardputer.h>
#include <M5GFX.h>
#include <esp_random.h>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <utility>

static M5Canvas canvas(&M5Cardputer.Display);

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
    try {
        Parser p(expr, degMode);
        double v = p.run();
        std::string res = formatNumber(v);
        resultLine = "= " + res;
        history.push_back({expr, res, v});
        if (history.size() > HISTORY_STORE_CAP) history.erase(history.begin());
        haveResult = true;
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

static void render() {
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
    "pi", "e", "ans", "help",
    "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
    "tanh", "sinh", "cosh", "asinh", "acosh", "atanh",
    "sqrt", "cbrt", "pow", "exp", "log", "ln", "log2",
    "abs", "floor", "ceil", "round", "int",
    "mod", "min", "max", "clamp", "gcd", "lcm", "ncr", "npr",
    "rand", "randint",
};

// Words that stand alone (no argument list), so Tab shouldn't add "(".
static bool isBareWord(const std::string& w) {
    return w == "pi" || w == "e" || w == "ans" || w == "help";
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

    render();
}

static bool wordHas(const Keyboard_Class::KeysState& s, char c) {
    return std::find(s.word.begin(), s.word.end(), c) != s.word.end();
}

void loop() {
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
