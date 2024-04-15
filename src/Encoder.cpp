#include "Encoder.h"

#ifdef ARDUINO

#    include "sdkconfig.h"
#    include "driver/pcnt.h"
#    include "driver/gpio.h"

/* clang-format: off */
void init_encoder() {
    pcnt_config_t enc_config = {
        .pulse_gpio_num = GPIO_NUM_40,  //Rotary Encoder Chan A
        .ctrl_gpio_num  = GPIO_NUM_41,  //Rotary Encoder Chan B

        .lctrl_mode = PCNT_MODE_KEEP,     // Rising A on HIGH B = CW Step
        .hctrl_mode = PCNT_MODE_REVERSE,  // Rising A on LOW B = CCW Step
        .pos_mode   = PCNT_COUNT_INC,     //Count Only On Rising-Edges
        .neg_mode   = PCNT_COUNT_DEC,     // Discard Falling-Edge

        .counter_h_lim = INT16_MAX,
        .counter_l_lim = INT16_MIN,

        .unit    = PCNT_UNIT_0,
        .channel = PCNT_CHANNEL_0,
    };
    pcnt_unit_config(&enc_config);

    enc_config.pulse_gpio_num = GPIO_NUM_41;
    enc_config.ctrl_gpio_num  = GPIO_NUM_40;
    enc_config.channel        = PCNT_CHANNEL_1;
    enc_config.pos_mode       = PCNT_COUNT_DEC;  //Count Only On Falling-Edges
    enc_config.neg_mode       = PCNT_COUNT_INC;  // Discard Rising-Edge
    pcnt_unit_config(&enc_config);

    pcnt_set_filter_value(PCNT_UNIT_0, 250);  // Filter Runt Pulses

    pcnt_filter_enable(PCNT_UNIT_0);

    gpio_pullup_en(GPIO_NUM_40);
    gpio_pullup_en(GPIO_NUM_41);

    pcnt_counter_pause(PCNT_UNIT_0);  // Initial PCNT init
    pcnt_counter_clear(PCNT_UNIT_0);
    pcnt_counter_resume(PCNT_UNIT_0);
}

int16_t get_encoder() {
    int16_t count;
    pcnt_get_counter_value(PCNT_UNIT_0, &count);
    return count;
}
#else
void    init_encoder() {}
int16_t get_encoder() {
    return 0;
}
#endif
