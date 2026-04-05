#pragma once
#include <LovyanGFX.hpp>

// =============================================================================
// LovyanGFX Konfiguration: ILI9488 3,5" SPI-Display + XPT2046 Touch
// ESP32-S3-WROOM1 N16R8
// =============================================================================
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9488  _panel;
    lgfx::Bus_SPI        _bus;
    lgfx::Light_PWM      _light;
    lgfx::Touch_XPT2046  _touch;

public:
    LGFX() {
        // ── SPI-Bus ────────────────────────────────────────────────────────
        {
            auto cfg = _bus.config();
            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = 40000000;  // 40 MHz
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = 12;        // SCK
            cfg.pin_mosi    = 11;        // MOSI
            cfg.pin_miso    = -1;        // MISO nicht belegen — ILI9488 tristatet SDO nicht,
                                         // GPIO 13 bleibt frei für XPT2046
            cfg.pin_dc      = 9;         // DC
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }

        // ── Display-Panel ─────────────────────────────────────────────────
        {
            auto cfg = _panel.config();
            cfg.pin_cs           = 10;   // CS
            cfg.pin_rst          = 8;    // RST
            cfg.pin_busy         = -1;
            cfg.panel_width      = 320;  // Nativauflösung (Hochformat)
            cfg.panel_height     = 480;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable         = false;  // kein Read-Back vom Display (MISO nicht angeschlossen)
            cfg.invert           = false;
            cfg.rgb_order        = false; // BGR (ILI9488 Standard)
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = true;  // Bus geteilt mit Touch
            _panel.config(cfg);
        }

        // ── Hintergrundbeleuchtung (PWM) ──────────────────────────────────
        {
            auto cfg = _light.config();
            cfg.pin_bl     = 7;     // BL
            cfg.invert     = false; // HIGH = an
            cfg.freq       = 44100;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }

        // Touch wird extern über XPT2046_read() in display.cpp gelesen (kein LovyanGFX-Touch)

        setPanel(&_panel);
    }
};
