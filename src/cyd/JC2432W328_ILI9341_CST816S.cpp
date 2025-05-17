// Board configuration for a CYD with an ILI9341 display and CST816S capacitive touch controller
// An example of such a board is the Guition JC2432W328

#include <LovyanGFX.hpp>
#include <driver/i2c.h>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_SPI         _bus;
    lgfx::Panel_ILI9341_2 _panel_instance;
    lgfx::Light_PWM       _light;
    lgfx::Touch_CST816S   _touch;

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
            cfg.pin_bl      = GPIO_NUM_27;
            cfg.freq        = 12000;
            cfg.pwm_channel = 7;
            cfg.offset      = 0;
            cfg.invert      = false;
            _light.config(cfg);
            _panel_instance.light(&_light);
        }
        {
            auto cfg            = _touch.config();
            cfg.i2c_port        = I2C_NUM_0;
            cfg.pin_sda         = GPIO_NUM_33;
            cfg.pin_scl         = GPIO_NUM_32;
            cfg.pin_rst         = GPIO_NUM_25;
            cfg.pin_int         = -1;
            cfg.offset_rotation = 6;
            cfg.freq            = 400000;
            cfg.x_max           = 240;
            cfg.y_max           = 320;
            _touch.config(cfg);
            _panel_instance.setTouch(&_touch);
        }
        setPanel(&_panel_instance);
    }
};

LGFX xdisplay;

extern int enc_a, enc_b;
extern int red_button_pin, dial_button_pin, green_button_pin;

void init_board() {
#ifdef LOCKOUT_PIN
    pinMode(LOCKOUT_PIN, INPUT);
#endif
#ifdef CYD_BUTTONS
    enc_a = GPIO_NUM_22;
    enc_b = GPIO_NUM_21;
    // rotary_button_pin = GPIO_NUM_35;
    // pinMode(rotary_button_pin, INPUT);  // Pullup does not work on GPIO35

    red_button_pin   = GPIO_NUM_4;   // RGB LED Red
    dial_button_pin  = GPIO_NUM_17;  // RGB LED Blue
    green_button_pin = GPIO_NUM_16;  // RGB LED Green
    pinMode(red_button_pin, INPUT_PULLUP);
    pinMode(dial_button_pin, INPUT_PULLUP);
    pinMode(green_button_pin, INPUT_PULLUP);
#else
    enc_a = GPIO_NUM_22;
    enc_b = GPIO_NUM_17;  // RGB LED Blue
#endif
}
