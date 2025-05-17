// Board configuration for a CYD with an ST7789 display and XPT2046 resistive touch controller
// Many 2.8" CYDs with resistive touch use this configuration

#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_SPI       _bus;
    lgfx::Panel_ST7789  _panel_instance;
    lgfx::Light_PWM     _light;
    lgfx::Touch_XPT2046 _touch;

public:
    LGFX(void) {
        {
            auto cfg       = _bus.config();
            cfg.freq_write = 8000000;
            cfg.freq_read  = 16000000;
            cfg.use_lock   = true;

            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.spi_host    = HSPI_HOST;
            cfg.pin_mosi    = GPIO_NUM_13;
            cfg.pin_miso    = GPIO_NUM_12;
            cfg.pin_sclk    = GPIO_NUM_14;
            cfg.pin_dc      = GPIO_NUM_2;
            cfg.spi_mode    = 0;
            cfg.spi_3wire   = false;

            _bus.config(cfg);
            _panel_instance.bus(&_bus);
        }
        {
            auto cfg            = _panel_instance.config();
            cfg.pin_cs          = GPIO_NUM_15;
            cfg.offset_rotation = 2;
            cfg.pin_rst         = -1;
            cfg.pin_busy        = -1;
            cfg.bus_shared      = false;
            _panel_instance.config(cfg);
        }
        {
            auto cfg        = _light.config();
            cfg.pin_bl      = GPIO_NUM_21;
            cfg.freq        = 12000;
            cfg.pwm_channel = 7;
            cfg.offset      = 0;
            cfg.invert      = false;
            _light.config(cfg);
            _panel_instance.light(&_light);
        }
        {
            auto cfg            = _touch.config();
            cfg.x_min           = 300;
            cfg.x_max           = 3900;
            cfg.y_min           = 3700;
            cfg.y_max           = 200;
            cfg.pin_int         = -1;
            cfg.bus_shared      = false;
            cfg.spi_host        = -1;  // -1:use software SPI for XPT2046
            cfg.pin_sclk        = GPIO_NUM_25;
            cfg.pin_mosi        = GPIO_NUM_32;
            cfg.pin_miso        = GPIO_NUM_39;
            cfg.pin_cs          = GPIO_NUM_33;
            cfg.offset_rotation = 2;
            _touch.config(cfg);
            _panel_instance.setTouch(&_touch);
        }
        setPanel(&_panel_instance);
    }
};

LGFX xdisplay;

extern int enc_a, enc_b;

void init_board() {
    enc_a = GPIO_NUM_22;
    enc_b = GPIO_NUM_27;
}
