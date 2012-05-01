typedef enum {
  STATE_UNKNOWN,
  STATE_EEPROM_READY,
  STATE_READY,
} STATE;

typedef struct {
  STATE state;
  RESOLUTION resolution;
} ds18b20_t;

CONFIG static rom_t rom [] = {
#include "ds18b20_list.c"
};

static ds18b20_t ds18b20_tab [DS18B20_NR]; // zeros are fine - no need to initialize


static const uint8_t eeprom_val     = 0xbd;
static const uint8_t scratchpad_val = 0xdb;


//
// Error & Handling / Debug stuff
//
jmp_buf * ds18b20_err_handler = 0;

enum {
  ERR_NO_PRESENCE,
  ERR_SCRATCHPAD_0,
  ERR_SCRATCHPAD_CRC,
  ERR_EEPROM_WRITE,
  ERR_SCRATCHPAD_WRITE,
  ERR_RESOLUTION,
  ERR_TEMP,
  ERR_NR,
};

void ds18b20_error(DS18B20 i, uint8_t errno)
{
#ifndef NDEBUG
  i = MIN(DS18B20_NR, i);
  DBG static uint8_t ds18b20_err_cnt[DS18B20_NR + 1][ERR_NR]; 
  if (ds18b20_err_cnt[i][errno] < (typeof(ds18b20_err_cnt[i][errno]))-1)
    ds18b20_err_cnt[i][errno]++;
#else
  (void)i;
#endif
  assert(ds18b20_err_handler);
  longjmp(*ds18b20_err_handler, errno);
}

void ds18b20_set_resolution(DS18B20 i, RESOLUTION r)
{
  if (ds18b20_tab[i].resolution != r) {
    ds18b20_tab[i].resolution = r;
    if (ds18b20_tab[i].state > STATE_EEPROM_READY)
        ds18b20_tab[i].state = STATE_EEPROM_READY;
  }
  ds18b20_init(i);
}

void ds18b20_reset(timer_t rst_time)
{
  onewire_0();
  timer_sleep_ticks(rst_time);
  for (/*DS18B20*/ uint8_t i = 0; i < DS18B20_NR; i++)
    if (ds18b20_tab[i].state > STATE_EEPROM_READY)
        ds18b20_tab[i].state = STATE_EEPROM_READY;
}

/* match rom wrapper */
static void ds18b20_match_rom(DS18B20 i)
{
  if (onewire_match_rom(i < DS18B20_NR ? CONFIG_GET(rom[i]) : (rom_t){{0}})) ds18b20_error(i, ERR_NO_PRESENCE);
}



//
// FUNCTION commands
//

void ds18b20_convert_t(DS18B20 i)
{
  onewire_write8(0x44);
#if 0 // gain ~20% of time, but external power is required!
  while (!onewire_read());
#else
  timer_t pu_time = TIMER_MS(750.0) >> (3 - ds18b20_tab[i].resolution);
  onewire_pullup();
  timer_sleep_ticks(pu_time);
#endif
}

void ds18b20_read_scratchpad(DS18B20 i, uint8_t * scratchpad)
{
  const uint8_t scratchpad_size = 8;
 
  ds18b20_match_rom(i);
  onewire_write8(0xbe);
  onewire_read_l(scratchpad, scratchpad_size);
  
  uint8_t j = 0;
  while (scratchpad[j] == 0) if (++j > scratchpad_size)
    ds18b20_error(i, ERR_SCRATCHPAD_0);

  if (onewire_read8() != crc8(scratchpad, scratchpad_size))
    ds18b20_error(i, ERR_SCRATCHPAD_CRC);
}

bool ds18b20_check_scratchpad(DS18B20 i, const uint8_t * scratchpad)
{
  uint8_t readback [8];
  ds18b20_read_scratchpad(i, readback);
  
  for (uint8_t i = 0; i < 3; i++)
    if (readback[2+i] != scratchpad[i]) return 0;

  return 1;
}

void ds18b20_write_scratchpad(DS18B20 i, const uint8_t * scratchpad)
{
  ds18b20_match_rom(i);
  onewire_write8(0x4e);
  for (uint8_t i = 0; i < 3; i++)
    onewire_write8(scratchpad[i]);

  if (!ds18b20_check_scratchpad(i, scratchpad))
    ds18b20_error(i, ERR_SCRATCHPAD_WRITE);
}

void ds18b20_copy_scratchpad(DS18B20 i)
{
  ds18b20_match_rom(i);
  onewire_write8(0x48);
#if 0
  timer_t pu_time = TIMER_MS(10.0);
  onewire_pullup();
  timer_sleep_ticks(pu_time);
#else
  onewire_pullup_dly(10000.0);
#endif
}

void ds18b20_recall_eeprom(DS18B20 i)
{
  ds18b20_match_rom(i);
  onewire_write8(0xb8);
  //while (!onewire_read()); - I think this is not really needed - if it is, timeout should be implemented also
}

bool ds18b20_read_power_supply(DS18B20 i)
{
  ds18b20_match_rom(i);
  onewire_write8(0xb4);
  return onewire_read();
}

//
// INIT functions
//
void ds18b20_init(DS18B20 i)
{
  switch (ds18b20_tab[i].state) {
    case STATE_UNKNOWN: {
      const uint8_t eeprom [] = { eeprom_val, eeprom_val, (RESOLUTION_9 << 5) | 0x1f };
      ds18b20_recall_eeprom(i);
      if (!ds18b20_check_scratchpad(i, eeprom)) {
        ds18b20_write_scratchpad(i, eeprom);
        ds18b20_copy_scratchpad(i);
        ds18b20_recall_eeprom(i);
        if (!ds18b20_check_scratchpad(i, eeprom)) ds18b20_error(i, ERR_EEPROM_WRITE);
      }
      ds18b20_tab[i].state = STATE_EEPROM_READY;
      // don't break - continue with scratchpad initialization
    }
    case STATE_EEPROM_READY: {
      const uint8_t scratchpad [] = { scratchpad_val, scratchpad_val, (ds18b20_tab[i].resolution << 5) | 0x1f };
      ds18b20_write_scratchpad(i, scratchpad);
      ds18b20_tab[i].state = STATE_READY;
    }
    case STATE_READY: {
    }
  }
}

//
// Temperature reading functions
//

temp_t ds18b20_read_temp(DS18B20 i)
{
  uint8_t scratchpad [8];
  ds18b20_read_scratchpad(i, scratchpad);
  
  if (((scratchpad[4] >> 5) & 0x3) != ds18b20_tab[i].resolution) {
    if (ds18b20_tab[i].state > STATE_EEPROM_READY)
        ds18b20_tab[i].state = STATE_EEPROM_READY;
    ds18b20_error(i, ERR_RESOLUTION);
  }

  if (scratchpad[0] == 0x50 &&
      scratchpad[1] == 0x05 &&
      scratchpad[2] != scratchpad_val) {
    ds18b20_error(i, ERR_TEMP);
  }

  return (scratchpad[1] << 12)
       | (scratchpad[0] <<  4);
}

temp_t ds18b20_get_temp_bare(DS18B20 i, RESOLUTION r)
{
  ds18b20_set_resolution(i, r);
  ds18b20_match_rom(i);
  ds18b20_convert_t(i);
  return ds18b20_read_temp(i);
}

temp_t ds18b20_get_temp(DS18B20 i, RESOLUTION r, uint8_t rty)
{
#if 0
  /* static */ volatile uint8_t try; try = 0;
  /* static */ volatile timer_t rst_time; rst_time = TIMER_MS(10);
  jmp_buf tmp_eh;

  assert(ds18b20_err_handler == 0);
  ds18b20_err_handler = &tmp_eh;
  uint8_t errno = setjmp(tmp_eh);
  
#ifndef NDEBUG
  DBG static uint8_t ds18b20_max_rty[DS18B20_NR][2];
  if (ds18b20_max_rty[i][0] < try) {
    ds18b20_max_rty[i][0] = try;
    ds18b20_max_rty[i][1] = errno;
  }
#else
  (void)i;
  (void)errno;
#endif

  temp_t val;
  if (try <= rty) {
    if (try >= 2) {
      ds18b20_reset(rst_time);
      rst_time <<= 1; // double time for next try
    }
    try++;
    val = ds18b20_get_temp_bare(i, r);
  } else {
    val = TEMP_ERR;
  }
  
  ds18b20_err_handler = 0;

  return val;
#else
  temp_t val = i;
  ds18b20_get_temp_tab(1, r, rty, &val);
  return val;
#endif
}

USED void ds18b20_get_temp_tab(DS18B20 p_nr, RESOLUTION p_r, uint8_t p_rty, temp_t * p_tab)
{
  const timer_t initial_rst_time = TIMER_MS(10);
  /* parameters and variables used has to be vloatile cause of setjmp/longjmp */
  volatile DS18B20    nr       = p_nr;
  volatile RESOLUTION v_r      = p_r;
  volatile uint8_t    v_rty    = p_rty;
  temp_t * volatile   tab      = p_tab;
  volatile uint8_t    try      = 0;
  volatile timer_t    rst_time = initial_rst_time;
  /* setjmp */
  jmp_buf tmp_eh;
  assert(ds18b20_err_handler == 0);
  ds18b20_err_handler = &tmp_eh;
  uint8_t errno = setjmp(tmp_eh);
  /* after setjum was called - readonly variables can be latched as non-volatile */
  RESOLUTION r   = v_r;
  uint8_t    rty = v_rty;
  /* this could be further optimized by by having volatile & non-volatile versions and use it appropriate */

#ifndef NDEBUG
  DBG static uint8_t ds18b20_max_rty[DS18B20_NR][2];
  if (ds18b20_max_rty[*tab][0] < try) { // *tab index is a lie here for most types of errors
    ds18b20_max_rty[*tab][0] = try;
    ds18b20_max_rty[*tab][1] = errno;
  }
#else
  (void)i;
  (void)errno;
#endif
  
  while (nr) {
    if (try <= rty) {
      if (try >= 2) {
        ds18b20_reset(rst_time);
        rst_time <<= 1; // double time for next try
      }
      try++;
    
      for (uint8_t i = 0; i < nr; i++) {
        DS18B20 s = tab[i];
        ds18b20_set_resolution(s, r);
      }

      if (nr >= 2) {
        for (/*DS18B20*/ uint8_t i = 0; i < DS18B20_NR; i++) {
          if (ds18b20_tab[i].resolution > r) {
            ds18b20_set_resolution(i, RESOLUTION_9);
          }
        }
        ds18b20_match_rom(DS18B20_NR);
      } else {
        ds18b20_match_rom(tab[0]);
      }
      ds18b20_convert_t(tab[0]);

      while (nr) {
        DS18B20 s = tab[0];
        tab[0] = ds18b20_read_temp(s);
        tab++;
        nr--;
      }
    } else {
      tab[0] = TEMP_ERR;
      tab++;
      nr--;
      try = 0;
      rst_time = initial_rst_time;
    }
  }
  
  ds18b20_err_handler = 0;
}

