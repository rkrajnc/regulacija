#define MAX_TIMERS 16 /* also used as invalid value */

typedef uint8_t ptr_t;

typedef struct {
  ptr_t next;
  timer_t cmp;
  func_t func;
  void * arg;
  uint8_t level;
} slot_t;

slot_t slot [MAX_TIMERS];
ptr_t first = MAX_TIMERS;
timer_t timer_tracked_time;

void timer_init()
{
  for (ptr_t i = 0; i < MAX_TIMERS; i++) slot[i].next = MAX_TIMERS;
  timer_start();
}

void timer_tracked_set(timer_t t)
{
  timer_tracked_time = t;
}

timer_t timer_tracked_get()
{
  /*if (first == MAX_TIMERS)*/ timer_tracked_time = timer_now();
  return timer_tracked_time;
}

void timer_int()
{
  slot_t c = slot[first];
  slot[first].next = MAX_TIMERS;

  if (c.next == first) {
    first = MAX_TIMERS;
    timer_unset();
  } else {
    timer_tracked_set(c.cmp);
    first = c.next;
    timer_set(c.cmp, slot[first].cmp);
  }
  
  if (c.level == (uint8_t)(-1)) {
    log_adr();
    last_timer_func = c.func;
    c.func(c.arg);
    last_timer_func = 0;
  } else {
    sch_add(c.func);
  }
}

void slot_insert(ptr_t p, ptr_t c, timer_t now)
{
  bool isOnly  = first == MAX_TIMERS;
  bool isFirst = isOnly || p == MAX_TIMERS;
  bool isLast  = isOnly || p == slot[p].next;

  if (isLast) {
    slot[c].next = c;
  } else if (isFirst) {
    slot[c].next = first;
  } else {
    slot[c].next = slot[p].next;
  }

  if (isFirst) {
    first = c;
    timer_set(now, slot[c].cmp);
  } else {
    slot[p].next = c;
  }
}

void slot_remove(ptr_t p, ptr_t c)
{
  bool isFirst = c == first;
  bool isLast  = c == slot[c].next;
  bool isOnly  = isFirst && isLast;
  
  if (isOnly) {
    first = MAX_TIMERS;
  } else if (isFirst) {
    first = slot[c].next;
  } else if (isLast) {
    slot[p].next = p;
  } else {
    slot[p].next = slot[c].next;
  }

  if (isOnly) {
    timer_unset();
  } else if (isFirst) {
    //timer_set(slot[c].cmp, slot[first].cmp);
    timer_t now  = timer_now();
    timer_t orig = slot[c].cmp;
    timer_t new  = slot[first].cmp;
    timer_set(in_range(new, orig, now) ? orig : now,  new);
  }

  slot[c].next = MAX_TIMERS;
}

void timer_add_cmp(timer_t now, timer_t cmp, void (*func)(), void * arg, uint8_t level)
{
  DBG_ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    /* find empty slot */
    ptr_t c = 0;
    while (slot[c].next != MAX_TIMERS) {
      c++;
      assert(c < MAX_TIMERS);
    }
 
    slot[c].cmp   = cmp;
    slot[c].func  = func;
    slot[c].arg   = arg;
    slot[c].level = level;

    /* find position */
    ptr_t p = MAX_TIMERS;
    ptr_t n = first;
      
    while (p != n && !in_range(now, cmp, slot[n].cmp)) {
      p = n;
      n = slot[n].next;
    }

    /* link it */
    slot_insert(p, c, now);
  }
}

void timer_add(timer_t cnt, void (*func)(), void * arg, uint8_t level)
{
  timer_t now = timer_now();
  timer_t cmp = now + cnt;
  if (in_range(now - 0x20000, slot[first].cmp, now)) {
    DBG static timer_t timer_late_max;
    timer_late_max = MAX(timer_late_max, now - slot[first].cmp);
    DBG static uint8_t timer_late;
    if (timer_late < (uint8_t)-1) timer_late++;
    now = slot[first].cmp;
  }
  timer_add_cmp(now, cmp, func, arg, level);
}

void timer_cancel(void (*func)(), void * arg)
{
  ptr_t p = MAX_TIMERS;
  DBG_ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    ptr_t c = first;
 
    while (p != c && c != MAX_TIMERS) {
      if (slot[c].func == func && slot[c].arg == arg) {
        slot_remove(p, c);
      } else {
        p = c;
      }
      c = slot[c].next;
    }
  }
}

void timer_sleep_ticks(timer_t t)
{
  timer_t start = timer_now();
  timer_t end   = start + t;
  while (in_range(start, timer_now(), end));
}

uint8_t timer_count(func_t func)
{
  uint8_t cnt = 0;
  for (uint8_t i = 0; i < MAX_TIMERS; i++)
    if (slot[i].next < MAX_TIMERS && slot[i].func == func) cnt++;
  return cnt;
}

