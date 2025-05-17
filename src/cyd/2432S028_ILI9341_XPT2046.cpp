// Board configuration for a CYD with an ILI9341 display and XPT2046 resistive touch controller
// This is untested; the information was derived from the LovyanGFX autodetect code

#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_SPI         _bus;
    lgfx::Panel_ILI9341_2 _panel_instance;
    lgfx::Light_PWM       _light;
    lgfx::Touch_XPT2046   _touch;

public:
    LGFX(void) {
        {
            auto cfg       = _bus.config();
            cfg.freq_write = 55000000;
            cfg.freq_read  = 20000000;
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
            auto cfg             = _panel_instance.config();
            cfg.pin_cs           = GPIO_NUM_15;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 6;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable         = true;
            cfg.invert           = true;
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = false;
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
            //            _panel_instance.setTouch(&_touch);
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
