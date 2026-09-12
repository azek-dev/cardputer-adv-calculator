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
