#define QUEUE_LEN 12

static volatile func_t queue[QUEUE_LEN] = { 0 };
static uint8_t wp = 0;
static uint8_t rp = 0;

static uint8_t pinc(uint8_t p)
{
  p++;
  if (p >= QUEUE_LEN) p = 0;
  return p;
}

void sch_add(func_t func /*, uint8_t level*/)
{
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
  {
    assert(queue[wp] == 0);
    queue[wp] = func;
    wp = pinc(wp);
  }
}

void sch()
{
  while (queue[rp]) {
    func_t func = queue[rp];
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
      queue[rp] = 0;
    }
    rp = pinc(rp);
    last_sch_func = func;
    func();
    last_sch_func = 0;
  }
}
