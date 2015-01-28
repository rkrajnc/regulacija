#include <util/setbaud.h>
static uint16_t ubrr_value = UBRR_VALUE;
static bool     use_2x     = USE_2X;

void uart_set_baud()
{
#if __AVR_ATmega32__
  UBRR0H = ubrr_value >> 8;
  UBRR0L = ubrr_value >> 0;
#else
  UBRR0 = ubrr_value;
#endif
  UCSR0A = (UCSR0A & ~(1 << U2X0)) | (use_2x ? 1 << U2X0 : 0);
}

//
// Rx
//
pac_state_t rx_state = ADR_SIZE;
pac_t       rx;
uint8_t     rx_crc = 0;
uint8_t     rx_buf [16];
bool        rx_timer = 0;

bool receive(uint8_t byte)
{
  bool err = 0;

  rx_crc = _crc_ibutton_update(rx_crc, byte);
  static uint8_t i;

  switch (rx_state) {
    case ADR_SIZE: {
      rx.write = byte & 0x80;
      byte &= 0x7f;
      if (byte) {
        if (byte <= 2) {
          rx.adr_len = byte & 0x03;
          rx.adr = 0;
          i = rx.adr_len - 1;
          rx_state = ADR;
        } else {
          err = 1;
        }
      } else {
        if (rx.write) {
          err = 1; // when not PtP, slave select could be done this way?
        } else {
          send((pac_t){ 1, sizeof(&build), (uint8_t *)&build, sizeof(build) });
        }
      }
      break;
    }
    case ADR: {
      uint8_t * adr_p = (uint8_t *)(&rx.adr);
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
      rx.dat_len = byte;
      if (rx.write) {
        if (rx.dat_len > sizeof(rx_buf)) {
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
      if (i >= rx.dat_len) rx_state = CRC;
      break;
    }
    default: {
      if (rx_crc) {
        err = 1;
      } else {
        if (rx.write) { // TODO limit writeable space?
          for (uint8_t i = 0; i < rx.dat_len; i++) {
            *(rx.adr+i) = rx_buf[i];
          }
        }
        
        pac_t response = rx;
        response.write = response.write ? 0 : 1;
        send(response);
        rx_state = ADR_SIZE;
      }
      break;
    }
  }

  return err;
}
  
static bool error = 0;

void rx_timeout()
{
  rx_timer = 0;
  error = 0;
}

const sch_t rx_timeout_sch = { rx_timeout, 0, (uint8_t)-1 };

void put_safe(uint8_t b)
{
  while (!(UCSR0A & (1 << UDRE0)));
  UDR0 = b;
}

DBG_ISR(USART_RX_vect, ISR_BLOCK)
{
#if PLAIN_CONSOLE
  put_safe(UDR0);
#else
  uint8_t ucsr = UCSR0A;
  uint8_t byte = UDR0;
  
  if (rx_timer) {
    rx_timer = 0;
    assert(timer_cancel(rx_timeout_sch, 1) == 1);
  }
 
  if (ucsr & ((1<<FE0)|(1<<DOR0)|(1<<UPE0))) {
    if (ucsr & (1<<FE0) && byte == 0) {
      ubrr_value = UBRR_VALUE;
      use_2x     = USE_2X;
      uart_set_baud();
    }
    error = 1;
  } else if (!error) {
    error = receive(byte);
  }
 
  if (rx_state != ADR_SIZE || error) {
    rx_timer = 1;
    timer_add(TIMER_MS(100), rx_timeout_sch);
    
    if (error) {
      //put_safe(~byte);
      rx_state = ADR_SIZE;
      rx_crc = 0;
    }
  }
#endif
}

//
// Tx
//
pac_state_t tx_state = ADR_SIZE;
pac_t       tx_queue [1];
uint8_t     tx_crc = 0;

static uint8_t wp;
static uint8_t rp;

static uint8_t pinc(uint8_t p)
{
  p++;
  if (p >= sizeof(tx_queue)/sizeof(tx_queue[0])) p = 0;
  return p;
}

void send(pac_t p)
{
#ifndef NDEBUG
  if (wp == rp && (UCSR0B & (1 << UDRIE0))) DBG_CNT_CLIP(tx_busy);
#endif
  while (wp == rp && (UCSR0B & (1 << UDRIE0)));
  tx_queue[wp] = p;
  
  DBG_ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    wp = pinc(wp);
    UCSR0B |= 1 << UDRIE0;
  }
}

DBG_ISR(USART_UDRE_vect, ISR_BLOCK)
{
#if PLAIN_CONSOLE && PRINT_BUF_SIZE
  if (print_buf_empty()) {
    if (print_buf_ovf) {
      uint8_t ovf = print_buf_ovf;
      print_buf_ovf = 0;
      printf("\n\n# LOST >= %d #\n\n", ovf);
    } else {
      UCSR0B &= ~(1 << UDRIE0);
    }
  } else {
    UDR0 = print_buf_read();
  }
#else
  uint8_t byte;

  switch (tx_state) {
    case ADR_SIZE: {
      byte = tx_queue[rp].adr_len;
      if (tx_queue[rp].write) byte |= 0x80;
      tx_state = tx_queue[rp].adr_len ? ADR : DAT_SIZE;
      break;
    }
    case ADR: {
      tx_queue[rp].adr_len--;
      uint8_t * adr_ptr = (uint8_t *)(&tx_queue[rp].adr);
      byte = adr_ptr[tx_queue[rp].adr_len];
      if (tx_queue[rp].adr_len == 0) tx_state = DAT_SIZE;
      break;
    }
    case DAT_SIZE: {
      byte = tx_queue[rp].dat_len;
      tx_state = tx_queue[rp].write && tx_queue[rp].dat_len ? DAT : CRC;
      break;
    }
    case DAT: {
      tx_queue[rp].dat_len--;
      byte = *tx_queue[rp].adr;
      tx_queue[rp].adr++;
      if (tx_queue[rp].dat_len == 0) tx_state = CRC;
      break;
    }
    default: { // CRC
      byte = tx_crc;
      tx_state = ADR_SIZE;

      rp = pinc(rp);
      if (wp == rp) UCSR0B &= ~(1 << UDRIE0);
      break;
    }
  }

  UDR0 = byte;
  tx_crc = _crc_ibutton_update(tx_crc, byte);
#endif
}

void uart_init()
{
#if __AVR_ATmega32__
  UCSRC = (1 << URSEL) | (3 << UCSZ0);
#else
  UCSR0C = (3 << UCSZ00);
#endif
  UCSR0B = (1 << RXCIE0) | (1 << RXEN0) | (1 << TXEN0);

  uart_set_baud();

  //send((pac_t){ 1, sizeof(&build), (uint8_t *)&build, sizeof(build) });
}

//BAUD = (F_CPU << U2X) / ((UBRR + 1) << 4)
