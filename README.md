# Cardputer ADV Calculator

An advanced scientific calculator for the [M5Stack Cardputer / Cardputer ADV](https://docs.m5stack.com/en/core/Cardputer),
built with [PlatformIO](https://platformio.org/). Type math expressions directly
on the built-in QWERTY keyboard and evaluate them with Enter — like
`2*sin(pi/4)+sqrt(16)` — with history, editing, and a built-in help screen.

## Features

- Full expression parser: `+ - * / ^ % ( )`, unary minus, factorial (`x!`)
- Functions: trig (`sin cos tan asin acos atan atan2`), hyperbolic
  (`sinh cosh tanh asinh acosh atanh`), power/log
  (`sqrt cbrt pow log ln log2 exp`), rounding (`abs floor ceil round int`),
  comparison/integer (`min max clamp gcd lcm mod`), combinatorics
  (`ncr npr`), random (`rand randint`)
- Constants: `pi`, `e`
- `ans` / `ans(n)` — reuse the most recent result(s) in a new expression
  (e.g. `ans(1)+ans(2)+ans(3)`)
- DEG/RAD toggle (`opt+D`)
- History: keeps the last 20 calculations, browsable with `fn+;` / `fn+.`
  and re-editable/re-computable
- Cursor-based editing (`fn+,` / `fn+/`) — not just backspace-from-the-end
- Tab-completion for function names (press again to cycle multiple matches)
- On-device help screen — type `help` and press Enter
- History and DEG/RAD setting auto-save to internal flash and survive
  power-off, no SD card required
- Type `save` and press Enter to also export the current history as plain
  text to `calc_log.txt` on a microSD card
- `settime(H,M,S)` / `time` — a simple software clock (no RTC chip on this
  hardware), timestamps `save` entries; resets on power-cycle
- `usbdrive` — expose the microSD card to a computer over USB as an
  ordinary drive, no card removal needed (requires a reset to return to
  the calculator afterward); `usbdebug` shows the last low-level SD
  error if it fails on a particular card

## Keys

| Key | Action |
|---|---|
| `Enter` | Evaluate the expression |
| `Backspace` | Delete character before cursor |
| `fn` + `Backspace` | Clear everything |
| `opt` + `D` | Toggle DEG / RAD |
| `fn` + `;` / `fn` + `.` | Browse older / newer history entry |
| `fn` + `,` / `fn` + `/` | Move edit cursor left / right |
| `Tab` | Complete the function name before the cursor |

After a result is shown, typing an operator continues the calculation from
that result (chained, like a normal calculator); typing anything else starts
a fresh expression. Pressing `Backspace` right after a result re-opens that
expression for editing instead of clearing it.

See **[MANUAL.md](MANUAL.md)** (**[日本語版](MANUAL.ja.md)**) for a full
walkthrough and the complete function reference with examples.

## Building

Requires [PlatformIO Core](https://platformio.org/install/cli).

```bash
pio run              # build
pio run -t upload    # build and flash over USB-C
```

The board target (`m5stack-stamps3`) auto-detects both the original
Cardputer and Cardputer ADV at runtime.

## Credits

The raw SD-over-SPI block I/O routines behind the `usbdrive` command are
adapted from [MOY-lightening-firmware/M5-cardputer-mass-storage](https://github.com/MOY-lightening-firmware/M5-cardputer-mass-storage)
(MIT License, Copyright (c) 2026 OZAN).

## License

MIT
