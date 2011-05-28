typedef enum {
  ADR_SIZE,
  ADR     ,
  DAT_SIZE,
  DAT     ,
  CRC     ,
} pac_state_t;

static pac_state_t rx_state = ADR_SIZE;
static bool        rx_write;
static uint8_t *   rx_adr;
static uint8_t     rx_adr_len;
static uint8_t     rx_dat_len;
static uint8_t     rx_crc = 0;
static uint8_t     rx_buf [16];
static bool        rx_timer = 0;
static uint8_t     rx_ovf_cnt;

static pac_state_t tx_state = ADR_SIZE;
static bool        tx_write;
static uint8_t *   tx_adr;
static uint8_t     tx_adr_len;
static uint8_t     tx_dat_len;
static uint8_t     tx_crc = 0;

void rx_reset()
{
  rx_state = ADR_SIZE;
  rx_crc = 0;
  UCSRB &= ~(1 << RXEN);
  UCSRB |=  (1 << RXEN);
}

void rx_timeout()
{
  rx_timer = 0;
  if (!(UCSRA & (1 << RXC))) {
    printf("ERR: UART RX timeout\n");
    rx_reset();
  }
}

ISR(USART_RXC_vect)
{
#if PLAIN_CONSOLE
  assert(0);
#else
  if (rx_timer) {
    rx_timer = 0;
    timer_cancel(rx_timeout, 0);
  }

  bool err = 0;

  if (UCSRA & ((1<<FE)|(1<<DOR)|(1<<PE))) { //0x1c;
    rx_ovf_cnt++;
    err = 1;
  } else {
    uint8_t byte = UDR;
    rx_crc = _crc_ibutton_update(rx_crc, byte);
    static uint8_t i;
 
    switch (rx_state) {
      case ADR_SIZE: {
        rx_write   = byte & 0x80;
        rx_adr_len = byte & 0x03;
        rx_adr = 0;
        i = 1;
        rx_state = ADR;
        break;
      }
      case ADR: {
        uint8_t * adr_p = (uint8_t *)(&rx_adr);
        if (i) {
          adr_p[1] = byte;
          i = 0;
        } else {
          adr_p[0] = byte;
          rx_state = DAT_SIZE;
        }
        break;
      }
      case DAT_SIZE: {
        rx_dat_len = byte;
        if (rx_write) {
          if (rx_dat_len > sizeof(rx_buf)) {
            err = 1;
          } else {
            i = 0;
            rx_state = DAT;
          }
        } else {
          rx_state = CRC;
        }
        break;
      }
      case DAT: {
        rx_buf[i++] = byte;
        if (i >= rx_dat_len) rx_state = CRC;
        break;
      }
      default: {
        if (rx_crc) {
          err = 1;
        } else {
          if (rx_write) {
            for (uint8_t i = 0; i < rx_dat_len; i++) {
              *(rx_adr+i) = rx_buf[i];
            }
            if (rx_adr == (uint8_t *)&exexec_func) {
              sch_add(exexec);
            }
          }
 
          if (UCSRB & (1 << UDRIE)) {
            printf("ERR: transmitter busy - ignoring packet\n");
          } else {
            tx_write    = rx_write ? 0 : 1;
            tx_adr      = rx_adr;
            tx_adr_len  = rx_adr_len;
            tx_dat_len  = rx_dat_len;
            UCSRB |= 1 << UDRIE;
          }
          rx_state = ADR_SIZE;
        }
        break;
      }
    }
  }
 
  if (err) {
    rx_reset();
  } else if (rx_state != ADR_SIZE && !(UCSRA & (1 << RXC))) {
    rx_timer = 1;
    timer_add(TIMER_MS(100), rx_timeout, 0, -1);
  }
#endif
}

ISR(USART_UDRE_vect)
{
#if PLAIN_CONSOLE
  if (print_buf_empty()) {
    if (print_buf_ovf) {
      uint8_t ovf = print_buf_ovf;
      print_buf_ovf = 0;
      printf("\n\n# LOST >= %d #\n\n", ovf);
    } else {
      UCSRB &= ~(1 << UDRIE);
    }
  } else
    UDR = print_buf_read();
#else
  uint8_t byte;

  switch (tx_state) {
    case ADR_SIZE: {
      byte = tx_adr_len;
      if (tx_write) byte |= 0x80;
      tx_state = tx_adr_len ? ADR : DAT_SIZE;
      break;
    }
    case ADR: {
      tx_adr_len--;
      uint8_t * adr_ptr = (uint8_t *)(&tx_adr);
      byte = adr_ptr[tx_adr_len];
      if (tx_adr_len == 0) tx_state = DAT_SIZE;
      break;
    }
    case DAT_SIZE: {
      byte = tx_dat_len;
      tx_state = tx_write && tx_dat_len ? DAT : CRC;
      break;
    }
    case DAT: {
      tx_dat_len--;
      byte = *tx_adr;
      tx_adr++;
      if (tx_dat_len == 0) tx_state = CRC;
      break;
    }
    default: { // CRC
      byte = tx_crc;
      tx_state = ADR_SIZE;
      UCSRB &= ~(1 << UDRIE);
      break;
    }
  }

  tx_crc = _crc_ibutton_update(tx_crc, byte);
  UDR = byte;
#endif
}

void uart_init()
{
  #include <util/setbaud.h>
  UBRRH = UBRRH_VALUE;
  UBRRL = UBRRL_VALUE;
#if USE_2X
  UCSRA |=  (1 << U2X);
#else
  UCSRA &= ~(1 << U2X);
#endif
  
  UCSRC = (1 << URSEL) | (3 << UCSZ0);
#if PLAIN_CONSOLE
  UCSRB = (1 << TXEN);
#else
  UCSRB = (1 << RXCIE) | (1 << RXEN) | (1 << TXEN);
#endif
}
