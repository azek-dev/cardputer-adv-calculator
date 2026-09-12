# Manual

*[日本語版はこちら](MANUAL.ja.md)*

## Getting started

Type a math expression on the keyboard and press **Enter** to evaluate it:

```
2*sin(pi/4)+sqrt(16)
```

The result appears below as `= 5.414213562`. History of past calculations is
shown above the input line, dimmed, and the last 20 are kept in memory even
after they scroll off screen.

## Basic editing

| Key | Effect |
|---|---|
| Any letter/digit/operator | Insert at the cursor |
| `Backspace` | Delete the character before the cursor |
| `fn` + `Backspace` | Clear the current expression and result entirely |
| `fn` + `,` | Move the cursor left |
| `fn` + `/` | Move the cursor right |
| `Tab` | Complete the function name before the cursor (see below) |
| `Enter` | Evaluate |

The `|` character shown while typing marks the cursor position — you're not
limited to editing from the end of the line.

## After you get a result

Once `= ...` is showing, the next key you press decides what happens:

- An **operator** (`+ - * / ^ %`) continues the calculation from that result,
  like a normal calculator's chain-entry: press `+`, then `3`, then `Enter`
  to add 3 to the last answer.
- **Any other key** (a digit, a letter, `(`) discards the old expression and
  starts a fresh one.
- **`Backspace`** re-opens the *previous* expression for editing instead of
  clearing it — handy for fixing a typo or tweaking one number and
  recalculating. A second `Backspace` then deletes its last character as
  normal.

## History

- `fn` + `;` — step to an older history entry and load it into the edit
  line (repeat to keep going back; the `;` key has an up-arrow printed on it)
- `fn` + `.` — step toward the newest entry, then to a blank line (the `.`
  key has a down-arrow printed on it)
- Recalled entries are fully editable — change anything and press `Enter`
  to recompute.
- `ans` / `ans(n)` (see the function table below) let you reference past
  *results* directly inside a new expression, without recalling the whole
  line.

## Tab-completion

Press **Tab** while typing a function name to complete it:

```
si<Tab>   ->  sin(
at<Tab>   ->  atan(      (press Tab again: atan2( , again: atanh( , again: back to atan()
```

Words that don't take arguments (`pi`, `e`, `ans`, `help`) complete without
adding `(`. Pressing any other key ends the completion cycle.

## DEG / RAD

`opt` + `D` toggles between degrees and radians for all trig functions
(`sin cos tan asin acos atan atan2`). The current mode is shown in the
top-left corner: `[DEG]` or `[RAD]`.

## On-device help

Type `help` and press `Enter` to open a scrollable function reference on
the screen itself:

- `fn` + `;` / `fn` + `.` — flip pages
- `Enter` or `Backspace` — return to the calculator

## Saving your history

**Automatic, to internal flash:** history (up to the last 20 calculations)
and the DEG/RAD setting are saved after every calculation to the ESP32's
internal flash (NVS), and restored automatically on boot. This needs no SD
card and survives a full power cycle — turning the calculator off and back
on won't lose your history.

**Manual, to a microSD card:** type `save` and press `Enter`. If a microSD
card is inserted, the current history is appended to `/calc_log.txt` as
plain text, e.g.:

```
---- save #1 (3 entries, RAD) ----
2+3 = 5
10*2 = 20
ans(1)+ans(2) = 25

```

Each `save` appends a new labeled block rather than overwriting the file,
so running it repeatedly builds up a running log — but note it re-writes
whatever is currently in the on-screen history each time, so running `save`
twice without any new calculation in between will duplicate that block.
Read the file on a computer with any microSD card reader. If no card is
present (or it fails to initialize), the calculator shows `SD ERR` instead
of a result and continues working normally otherwise.

## Setting the time

This hardware has no RTC chip, and this sketch has no Wi-Fi/NTP, so there's
no clock unless you set one:

- `settime(H,M,S)` — set the current time (24-hour, e.g. `settime(9,30,0)`
  for 9:30:00 AM). This anchors a wall-clock time to the calculator's
  internal uptime timer.
- `time` — show the current time, computed from that anchor plus elapsed
  time since it was set.

The clock resets to "unset" every time the calculator loses power — there's
no battery-backed clock to carry it forward, so run `settime` again after
each boot if you want timestamps. Once set, `save` entries are stamped with
the time they were written, e.g. `---- save #2 (4 entries, RAD) @ 09:47:12 ----`.

## Reading the SD card without removing it

Type `usbdrive` and press `Enter` to expose the microSD card directly to a
computer over the same USB-C cable — it shows up as an ordinary USB drive,
so you can drag `calc_log.txt` out (or copy anything onto the card) without
ever popping the card out.

This takes over the SD card and USB connection for that one purpose: **the
calculator stops responding to the keyboard once you do this**, and the
only way back is to reset or power-cycle the device. There's no in-between
state, so only use it when you're done calculating for now. If no card is
inserted (or it fails to initialize), the calculator shows `SD ERR` instead
and keeps working normally.

If `usbdrive` enumerates but the card fails to actually mount on your
computer, reset the calculator and type `usbdebug` to see the last
low-level SD error that was recorded during the attempt (useful if you run
into this with a particular card).

## Auto-sleep

The Cardputer/Cardputer ADV has no power management chip capable of a true
power-off, so the closest available thing is deep sleep — a few tens of
microamps instead of a full shutdown. By default, the calculator goes to
sleep after **10 minutes** with no key press.

- `sleeptime(n)` — set the idle timeout to `n` minutes (0 disables
  auto-sleep entirely). Saved to flash, so it persists across power cycles.
- `sleeptime` (no parens) — show the current timeout.

**Waking up:** press the physical **G0/BtnA button on the side** of the
device — not a keyboard key. The entire keyboard matrix is unpowered
during sleep, so no ordinary key press can wake it. History and settings
are saved to flash right before sleeping, same as normal.

## Function reference

Trig functions respect the DEG/RAD toggle above.

### Trigonometric

| Function | Description | Example | Result |
|---|---|---|---|
| `sin(x)` | Sine | `sin(pi/2)` | `1` |
| `cos(x)` | Cosine | `cos(0)` | `1` |
| `tan(x)` | Tangent | `tan(pi/4)` | `1` |
| `asin(x)` | Arcsine | `asin(1)` | `1.570796327` |
| `acos(x)` | Arccosine | `acos(0)` | `1.570796327` |
| `atan(x)` | Arctangent | `atan(1)` | `0.7853981634` |
| `atan2(y,x)` | Angle of point (x,y) | `atan2(1,1)` | `0.7853981634` |

### Hyperbolic

| Function | Description | Example | Result |
|---|---|---|---|
| `sinh(x)` | Hyperbolic sine | `sinh(1)` | `1.175201194` |
| `cosh(x)` | Hyperbolic cosine | `cosh(0)` | `1` |
| `tanh(x)` | Hyperbolic tangent | `tanh(1)` | `0.7615941560` |
| `asinh(x)` | Inverse hyperbolic sine | `asinh(1)` | `0.8813735870` |
| `acosh(x)` | Inverse hyperbolic cosine (x≥1) | `acosh(1)` | `0` |
| `atanh(x)` | Inverse hyperbolic tangent (-1<x<1) | `atanh(0.5)` | `0.5493061443` |

### Power, root, and logarithm

| Function | Description | Example | Result |
|---|---|---|---|
| `sqrt(x)` | Square root | `sqrt(16)` | `4` |
| `cbrt(x)` | Cube root | `cbrt(27)` | `3` |
| `pow(x,y)` | x to the power of y | `pow(2,10)` | `1024` |
| `exp(x)` | e to the power of x | `exp(1)` | `2.718281828` |
| `log(x)` | Base-10 logarithm | `log(1000)` | `3` |
| `ln(x)` | Natural logarithm | `ln(2.718281828)` | `1` |
| `log2(x)` | Base-2 logarithm | `log2(1024)` | `10` |

### Comparison, ranges, and integer math

| Function | Description | Example | Result |
|---|---|---|---|
| `min(a,b)` | Smaller of the two | `min(3,7)` | `3` |
| `max(a,b)` | Larger of the two | `max(3,7)` | `7` |
| `clamp(x,min,max)` | Restrict x to [min,max] | `clamp(15,0,10)` | `10` |
| `gcd(a,b)` | Greatest common divisor | `gcd(12,18)` | `6` |
| `lcm(a,b)` | Least common multiple | `lcm(4,6)` | `12` |
| `mod(a,b)` | Modulo, sign follows the divisor (unlike `%`) | `mod(-7,3)` | `2` |

### Combinatorics and randomness

| Function | Description | Example | Result |
|---|---|---|---|
| `ncr(n,r)` | Combinations (nCr) | `ncr(5,2)` | `10` |
| `npr(n,r)` | Permutations (nPr) | `npr(5,2)` | `20` |
| `rand()` | Random number in [0,1) | `rand()` | e.g. `0.4213` |
| `rand(lo,hi)` | Random number in [lo,hi) | `rand(1,10)` | e.g. `7.62` |
| `randint(lo,hi)` | Random **integer**, inclusive of both ends | `randint(1,6)` | dice roll, `1`–`6` |

### Rounding and misc

| Function | Description | Example | Result |
|---|---|---|---|
| `abs(x)` | Absolute value | `abs(-5)` | `5` |
| `floor(x)` | Round down | `floor(3.7)` | `3` |
| `ceil(x)` | Round up | `ceil(3.2)` | `4` |
| `round(x)` | Round to nearest | `round(3.5)` | `4` |
| `int(x)` | Truncate toward zero | `int(3.9)` | `3` |

### Previous results

| Function | Description | Example |
|---|---|---|
| `ans` | Most recent result | `ans*2` |
| `ans(n)` | n-th most recent result (1 = last) | `ans(1)+ans(2)+ans(3)` (sum of the last 3 results) |

### Constants and operators

| Symbol | Description | Example | Result |
|---|---|---|---|
| `pi` | π | `2*pi` | `6.283185307` |
| `e` | Euler's number | `e^2` | `7.389056099` |
| `+ - * /` | Arithmetic | `(3+4)*2/7` | `2` |
| `^` | Exponent | `2^8` | `256` |
| `%` | Modulo, sign follows the dividend | `-7%3` | `-1` |
| `!` | Factorial | `5!` | `120` |
| `( )` | Grouping | `(1+2)*(3+4)` | `21` |

## Example: getting a whole-number random result

`rand(min,max)` is exclusive of `max`, so `int(rand(1,10))` only ever
produces `1`–`9`, never `10`. Use `randint(min,max)` instead when you want
an inclusive integer range:

```
randint(1,10)   ->  1 through 10, inclusive
randint(1,6)    ->  a six-sided die
```
