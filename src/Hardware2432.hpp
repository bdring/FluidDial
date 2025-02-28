#ifdef DEBUG_TO_USB

// This variant lets you have debugging output on the
// USB serial port, at the expense of not being able
// to control the backlight brightness due to the use
// of GPIO 21 as backlight control on some boards.

constexpr static const int PND_RX_FNC_TX_PIN = GPIO_NUM_35;
constexpr static const int PND_TX_FNC_RX_PIN = GPIO_NUM_21;
constexpr static const int FNC_UART_NUM      = 1;
#else

// This variant lets you control the backlight via GPIO 21,
// and opens up GPIO pins that could be used for other
// purposes, but you do not have debug output.

constexpr static const int PND_RX_FNC_TX_PIN = GPIO_NUM_1;
constexpr static const int PND_TX_FNC_RX_PIN = GPIO_NUM_3;
constexpr static const int FNC_UART_NUM      = 0;
#endif

// GPIO assignments for the encoder are set by
// init_hardware() in Hardware2432.cpp, based
// on which board variant is detected by the
// LovyanGFX display autodetection code.
