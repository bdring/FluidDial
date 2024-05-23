
#include "Encoder.h"
#include "sdkconfig.h"
#include "driver/pcnt.h"
#include "driver/gpio.h"

/* clang-format: off */
void init_encoder(int a_pin, int b_pin) {
    pcnt_config_t enc_config = {
        .pulse_gpio_num = a_pin,  //Rotary Encoder Chan A
        .ctrl_gpio_num  = b_pin,  //Rotary Encoder Chan B

        .lctrl_mode = PCNT_MODE_KEEP,     // Rising A on HIGH B = CW Step
        .hctrl_mode = PCNT_MODE_REVERSE,  // Rising A on LOW B = CCW Step
        .pos_mode   = PCNT_COUNT_INC,     // Count Only On Rising-Edges
        .neg_mode   = PCNT_COUNT_DEC,     // Discard Falling-Edge

        .counter_h_lim = INT16_MAX,
        .counter_l_lim = INT16_MIN,

        .unit    = PCNT_UNIT_0,
        .channel = PCNT_CHANNEL_0,
    };
    pcnt_unit_config(&enc_config);

    enc_config.pulse_gpio_num = b_pin;
    enc_config.ctrl_gpio_num  = a_pin;
    enc_config.channel        = PCNT_CHANNEL_1;
    enc_config.pos_mode       = PCNT_COUNT_DEC;  //Count Only On Falling-Edges
    enc_config.neg_mode       = PCNT_COUNT_INC;  // Discard Rising-Edge
    pcnt_unit_config(&enc_config);

    pcnt_set_filter_value(PCNT_UNIT_0, 250);  // Filter Runt Pulses

    pcnt_filter_enable(PCNT_UNIT_0);

    gpio_pullup_en((gpio_num_t)a_pin);
    gpio_pullup_en((gpio_num_t)b_pin);

    pcnt_counter_pause(PCNT_UNIT_0);  // Initial PCNT init
    pcnt_counter_clear(PCNT_UNIT_0);
    pcnt_counter_resume(PCNT_UNIT_0);
}

int16_t get_encoder() {
    int16_t count;
    pcnt_get_counter_value(PCNT_UNIT_0, &count);
    return count;
}
