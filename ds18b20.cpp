#include "gj/base.h"
#include "nrf_nvic.h"

#include <bspacm/utility/led.h>
#include <bspacm/newlib/ioctl.h>
#include <bspacm/utility/misc.h>
#include <bspacm/utility/hires.h>
#include <bspacm/utility/onewire.h>


/** Soft-device--aware stop of HFCLK */
__STATIC_INLINE void
vBSPACMnrf51_HFCLKSTOP ()
{
  uint32_t ec = sd_clock_hfclk_release();
}

static void iBSPACMhiresTerminate()
{
  vBSPACMnrf51_HFCLKSTOP();
}

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x80 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x01 ? '1' : '0') 


#ifndef ONEWIRE_DQ_PIN
#define ONEWIRE_DQ_PIN 29
#endif /* ONEWIRE_DQ_PIN */
#ifndef ONEWIRE_PWR_PIN
#define ONEWIRE_PWR_PIN 0
#endif /* ONEWIRE_PWR_PIN */

struct TimerRecord
{
  uint32_t request;
  uint32_t effective;
};
extern TimerRecord timerDiff[];
extern uint32_t timerDiffIndex;

volatile uint32_t ReadT2();
volatile uint32_t DiffT1(uint32_t b, uint32_t e);

void* BeginDS18b20(uint32_t dataPin)
{
  sBSPACMonewireBus *bus_config = new sBSPACMonewireBus;

  do
  {
    hBSPACMonewireBus bus = hBSPACMonewireConfigureBus(bus_config, dataPin, ONEWIRE_PWR_PIN);
    int rc;

    /* Configure high-resolution timer at 1 MHz */
    rc = iBSPACMhiresInitialize(1000U * 1000U);
    if (0 != rc) {
      delete bus_config;
      bus_config = nullptr;
      SER("ERR: Failed to initialize high-resolution clock\n");
      break;
    }
    //printf("Hires divsor is %u\n", 1U << BSPACM_HIRES_TIMER->PRESCALER);
    //printf("1 hrt = %u hfclk = %u ns\n", uiBSPACMhiresConvert_hrt_hfclk(1), uiBSPACMhiresConvert_hrt_us(1000));
    //printf("128 us = %u hrt\n", uiBSPACMhiresConvert_us_hrt(128));
    (void)iBSPACMhiresSetEnabled(true);

    if (! iBSPACMonewireReset(bus)) {
      SER("ERR: No DS18B20 present on P0.%u\n", dataPin);
      break;
    }

    static const char * const supply_type[] = { "parasitic", "external" };

    int external_power = 1;//iBSPACMonewireReadPowerSupply(bus);
    //printf("Power supply: %s\n", supply_type[external_power]);
    if (0 > external_power) {
      SER("ERROR: Device not present?\n");
      break;
    }

    //uint64_t serial_b = GetElapsedMicros();
    //sBSPACMonewireSerialNumber serial;
    //rc = iBSPACMonewireReadSerialNumber(bus, &serial);
    //uint64_t serial_e = GetElapsedMicros();
    //printf("Serial t:%d got %d: ", (uint32_t)(serial_e - serial_b), rc);
    //vBSPACMconsoleDisplayOctets(serial.id, sizeof(serial.id));
    //putchar('\n');
  }
  while(false);

  return bus_config;
}

int32_t ReadDS18b20(void *handle, uint16_t &temp)
{
  hBSPACMonewireBus bus = (hBSPACMonewireBus)handle;
  int32_t rc = -1;
  int external_power = 1;

  if (0 == iBSPACMonewireRequestTemperature(bus)) {
    if (external_power) {
      /* Wait for read to complete.  Conversion time can be as long as
        * 750 ms if 12-bit resolution is used (this resolution is the
        * default). Timing will be wrong unless interrupts are enabled
        * so uptime overflow events can be handled.  Sleep for 600ms,
        * then test at 10ms intervals until the result is ready. */
      vBSPACMhiresSleep_ms(600);
      while (! iBSPACMonewireReadBit(bus)) {
        vBSPACMhiresSleep_ms(10);
      }
    } else {
      /* Output high on the parasitic power boost line for 750ms, to
        * power the conversion.  Then switch that signal back to
        * input so the data can flow over the same circuit. */
      vBSPACMonewireParasitePower(bus, true);
      vBSPACMhiresSleep_ms(750);
      vBSPACMonewireParasitePower(bus, false);
    }
    int16_t t_xCel = -1;
    
    rc = iBSPACMonewireReadTemperature(bus, &t_xCel);
    if (rc < 0)
      return -1;

    temp = BSPACM_ONEWIRE_xCel_TO_dCel(t_xCel);

    return 0;
  }

  return -1;
}

void EndDS18b20(void *handle)
{
  hBSPACMonewireBus bus = (hBSPACMonewireBus)handle;

  vBSPACMonewireShutdown(bus);
  iBSPACMhiresSetEnabled(false);
  iBSPACMhiresTerminate();

  delete handle;
}

uint16_t ReadDS18b20(uint32_t dataPin)
{
  uint16_t val = 0;
    
  void* handle = BeginDS18b20(dataPin);
  if (handle)
  {
    ReadDS18b20(handle, val);
    EndDS18b20(handle);
  }

  return val;
}

#if 0
uint16_t ReadDS18b20(uint32_t dataPin)
{

  timerDiffIndex = 0;

  uint16_t ret = 0;
  do
  {
    sBSPACMonewireBus bus_config;
    hBSPACMonewireBus bus = hBSPACMonewireConfigureBus(&bus_config, dataPin, ONEWIRE_PWR_PIN);
    int rc;

    /* Configure high-resolution timer at 1 MHz */
    rc = iBSPACMhiresInitialize(1000U * 1000U);
    if (0 != rc) {
      SER("ERR: Failed to initialize high-resolution clock\n");
      break;
    }
    //printf("Hires divsor is %u\n", 1U << BSPACM_HIRES_TIMER->PRESCALER);
    //printf("1 hrt = %u hfclk = %u ns\n", uiBSPACMhiresConvert_hrt_hfclk(1), uiBSPACMhiresConvert_hrt_us(1000));
    //printf("128 us = %u hrt\n", uiBSPACMhiresConvert_us_hrt(128));
    (void)iBSPACMhiresSetEnabled(true);

    if (! iBSPACMonewireReset(bus)) {
      SER("ERR: No DS18B20 present on P0.%u\n", dataPin);
      break;
    }

    static const char * const supply_type[] = { "parasitic", "external" };

    int external_power = 1;//iBSPACMonewireReadPowerSupply(bus);
    //printf("Power supply: %s\n", supply_type[external_power]);
    if (0 > external_power) {
      SER("ERROR: Device not present?\n");
      break;
    }

    //uint64_t serial_b = GetElapsedMicros();
    //sBSPACMonewireSerialNumber serial;
    //rc = iBSPACMonewireReadSerialNumber(bus, &serial);
    //uint64_t serial_e = GetElapsedMicros();
    //printf("Serial t:%d got %d: ", (uint32_t)(serial_e - serial_b), rc);
    //vBSPACMconsoleDisplayOctets(serial.id, sizeof(serial.id));
    //putchar('\n');


    if (0 == iBSPACMonewireRequestTemperature(bus)) {
      if (external_power) {
        /* Wait for read to complete.  Conversion time can be as long as
         * 750 ms if 12-bit resolution is used (this resolution is the
         * default). Timing will be wrong unless interrupts are enabled
         * so uptime overflow events can be handled.  Sleep for 600ms,
         * then test at 10ms intervals until the result is ready. */
        vBSPACMhiresSleep_ms(600);
        while (! iBSPACMonewireReadBit(bus)) {
          vBSPACMhiresSleep_ms(10);
        }
      } else {
        /* Output high on the parasitic power boost line for 750ms, to
         * power the conversion.  Then switch that signal back to
         * input so the data can flow over the same circuit. */
        vBSPACMonewireParasitePower(bus, true);
        vBSPACMhiresSleep_ms(750);
        vBSPACMonewireParasitePower(bus, false);
      }
      int16_t t_xCel = -1;
      
      rc = iBSPACMonewireReadTemperature(bus, &t_xCel);
      vBSPACMonewireShutdown(bus);

      ret = BSPACM_ONEWIRE_xCel_TO_dCel(t_xCel);
      SER("DS18b20 0x%x xCel, %d dCel, %d d[degF], %d dK\n",
             t_xCel,
             BSPACM_ONEWIRE_xCel_TO_dCel(t_xCel),
             BSPACM_ONEWIRE_xCel_TO_ddegF(t_xCel),
             BSPACM_ONEWIRE_xCel_TO_dK(t_xCel));
      //vBSPACMhiresSleep_ms(1000);
    }
  }
  while(false);

  iBSPACMhiresSetEnabled(false);
  iBSPACMhiresTerminate();


  for (uint32_t i = 0 ; i < timerDiffIndex ; ++i)
  {
    TimerRecord &t = timerDiff[i];
    printf("Timer %d %d %d\n", i, t.request, t.effective);
  }

  return ret;
}
#endif