#ifdef DEBUG_TO_USB
constexpr static const int FNC_RX_PIN   = GPIO_NUM_35;
constexpr static const int FNC_TX_PIN   = GPIO_NUM_21;
constexpr static const int FNC_UART_NUM = 1;
#else
constexpr static const int FNC_RX_PIN   = GPIO_NUM_3;
constexpr static const int FNC_TX_PIN   = GPIO_NUM_1;
constexpr static const int FNC_UART_NUM = 0;
#endif
