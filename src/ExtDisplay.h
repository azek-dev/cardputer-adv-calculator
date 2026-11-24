// External 320x240 SPI screen on the Cardputer ADV EXT 2.54-14P header
// (the clip-on "Cardputer ADV Expansion Screen"). It is an ILI9341 wired as:
//   SCK G40 / MOSI G14 / MISO G39  -- shared with the microSD card (SD CS is G12)
//   CS G5 / DC G6 / RST G3
//   power: header 5VOUT -> a small 3.3V regulator board (YP-8) -> panel VCC.
//   Backlight (LED): rewired from the regulator output to EXT pin 3 = G4, so
//   firmware can switch it (HIGH = on). Before that rewire G4 is unconnected
//   and driving it does nothing.
// The calculator mirrors its 240x135 canvas onto it, scaled up 4/3.
#pragma once

#include <M5GFX.h>
#include <lgfx/v1/panel/Panel_LCD.hpp>
#include <lgfx/v1/platforms/esp32/Bus_SPI.hpp>

namespace ext_display {

static constexpr int PIN_BACKLIGHT = 4; // EXT pin 3

struct Panel_ILI9341_Ext : public lgfx::Panel_LCD {
    Panel_ILI9341_Ext() {
        _cfg.memory_width = _cfg.panel_width = 240;
        _cfg.memory_height = _cfg.panel_height = 320;
    }

protected:
    const uint8_t* getInitCommands(uint8_t listno) const override {
        // Standard ILI9341 power-on sequence (as in LovyanGFX's Panel_ILI9341).
        static constexpr uint8_t list0[] = {
            0xEF, 3, 0x03, 0x80, 0x02,
            0xCF, 3, 0x00, 0xC1, 0x30,
            0xED, 4, 0x64, 0x03, 0x12, 0x81,
            0xE8, 3, 0x85, 0x00, 0x78,
            0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
            0xF7, 1, 0x20,
            0xEA, 2, 0x00, 0x00,
            0xC0, 1, 0x23,             // power control 1
            0xC1, 1, 0x10,             // power control 2
            0xC5, 2, 0x3E, 0x28,       // VCOM control 1
            0xC7, 1, 0x86,             // VCOM control 2
            0xB1, 2, 0x00, 0x13,       // frame rate
            0xB6, 3, 0x08, 0xC2, 0x27, // display function control
            0xF2, 1, 0x00,             // 3-gamma off
            0x26, 1, 0x01,             // gamma curve
            0xE0, 15, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
                      0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
            0xE1, 15, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
                      0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
            0x11, 0 + CMD_INIT_DELAY, 120, // sleep out
            0x29, 0,                       // display on
            0xFF, 0xFF,
        };
        return listno == 0 ? list0 : nullptr;
    }
};

class ExtLCD : public lgfx::LGFX_Device {
    Panel_ILI9341_Ext panel_;
    lgfx::Bus_SPI bus_;

public:
    ExtLCD() {
        auto b = bus_.config();
        b.spi_host = SPI2_HOST; // same host as the Arduino SPI object the SD card uses
        b.spi_mode = 0;
        b.freq_write = 27000000;
        b.freq_read = 8000000;
        b.pin_sclk = 40;
        b.pin_mosi = 14;
        b.pin_miso = 39;
        b.pin_dc = 6;
        b.spi_3wire = false;
        b.use_lock = true;
        b.dma_channel = SPI_DMA_CH_AUTO;
        bus_.config(b);
        panel_.setBus(&bus_);

        auto p = panel_.config();
        p.pin_cs = 5;
        p.pin_rst = 3;
        p.pin_busy = -1;
        p.readable = false; // MISO is not wired to the panel
        p.invert = false;
        p.rgb_order = false;
        p.bus_shared = true; // the microSD card sits on the same bus
        panel_.config(p);
        setPanel(&panel_);
    }
};

} // namespace ext_display
