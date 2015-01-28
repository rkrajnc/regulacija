pumping_state_t pumping_state __attribute__ ((section (".noinit")));

void pumping_loop()
{
  temp_t tab [] = {
    DS18B20_HOUSE_S_B,
    DS18B20_HOUSE_S_T,
    DS18B20_FURNACE_T,
    DS18B20_STABLE_S_B,
    DS18B20_STABLE_S_T,
    DS18B20_COLLECTOR,
  };

  ds18b20_get_temp_tab(sizeof(tab)/sizeof(temp_t), RESOLUTION_9, 7, tab);
  
  temp_t t_house_s_b  = tab[0];
  temp_t t_house_s_t  = tab[1];
  temp_t t_furnace_t  = tab[2];
  temp_t t_stable_s_b = tab[3];
  temp_t t_stable_s_t = tab[4];
  temp_t t_collector  = tab[5];

  CONFIG static temp_t pumping_diff_on  = TEMP(5);
  CONFIG static temp_t pumping_diff_off = TEMP(2);

  temp_t diff_on       () { return CONFIG_GET(pumping_diff_on ); }
  temp_t diff_off      () { return CONFIG_GET(pumping_diff_off); }
  temp_t diff          () { return t_house_s_t - t_stable_s_t; }
  bool diff_h2s        () { return  diff() >= diff_on() || (pumping_state == PUMPING_H2S &&  diff() >= diff_off()); }
  bool diff_s2h        () { return -diff() >= diff_on() || (pumping_state == PUMPING_S2H && -diff() >= diff_off()); }
  CONFIG static temp_t pumping_furnace_t_th = TEMP(75);
  CONFIG static temp_t pumping_house_s_b_th = TEMP(55);
  bool furnace         () { return t_furnace_t >= CONFIG_GET(pumping_furnace_t_th) && t_house_s_b >= CONFIG_GET(pumping_house_s_b_th); }
  bool collector       () { return t_collector > MIN(t_house_s_b, t_stable_s_b) && t_house_s_b < t_stable_s_b; }
  date_t mms = (date_t){ 0, 0,  4, -1, -1, -1, -1 };
  date_t mme = (date_t){ 0, 0,  8, -1, -1, -1, -1 };
  date_t mes = (date_t){ 0, 0, 16, -1, -1, -1, -1 };
  date_t mee = (date_t){ 0, 0, 20, -1, -1, -1, -1 };
  bool milking_time    () { return (timecmp_lt(mms, date) && timecmp_ge(mme, date)) || (timecmp_lt(mes, date) && timecmp_ge(mee, date)); }
  //bool milking_time    () { return 1; }
  bool h2s             () { return  milking_time() && diff_h2s(); }
  bool s2h             () { return !milking_time() && diff_s2h(); }
  
  if      (furnace()   || h2s()) pumping_state = PUMPING_H2S;
  else if (collector() || s2h()) pumping_state = PUMPING_S2H;
  else                                           pumping_state = PUMPING_IDLE;
  
  switch (pumping_state) {
    case PUMPING_H2S:
      if (valve_closed(VALVE_SH0) && valve_opened(VALVE_SH1)) relay_on(RELAY_PUMP_SH);
      valve_close_for(VALVE_SH0, VALVE_STATE_MAX);
      valve_open_for (VALVE_SH1, VALVE_STATE_MAX);
      break;
    case PUMPING_S2H:
      if (valve_opened(VALVE_SH0) && valve_closed(VALVE_SH1)) relay_on(RELAY_PUMP_SH);
      valve_open_for (VALVE_SH0, VALVE_STATE_MAX);
      valve_close_for(VALVE_SH1, VALVE_STATE_MAX);
      break;
    default:
      relay_off(RELAY_PUMP_SH);
      valve_close_for(VALVE_SH0, VALVE_STATE_MAX);
      valve_close_for(VALVE_SH1, VALVE_STATE_MAX);
      break;
  }
}
