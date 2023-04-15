#include "app_error.h"
#include "softdevice_handler.h"
//#include "app_timer.h"
#include "nrf_log_ctrl.h"
#include "nrf_gpio.h"
#include "nrf_drv_clock.h"

#include "gj/base.h"
#include "gj/gjbleserver.h"
#include "gj/eventmanager.h"
#include "gj/commands.h"
#include "gj/config.h"
#include "gj/gjota.h"
#include "gj/esputils.h"
#include "gj/nrf51utils.h"
#include "gj/datetime.h"
#include "gj/file.h"
#include "gj/appendonlyfile.h"
#include "gj/platform.h"
#include "gj/sensor.h"

#include "temps.h"
#include "ds18b20.h"
#include "readwriteints.h"

#define APP_TIMER_PRESCALER             0                                        /**< Value of the RTC1 PRESCALER register. */
#define APP_TIMER_OP_QUEUE_SIZE         4                                           /**< Size of timer operation queues. */

static void power_manage(void)
{
    if (softdevice_handler_is_enabled())
    {
      uint32_t err_code = sd_app_evt_wait();

      APP_ERROR_CHECK(err_code);
    }
    else
    {
      __WFE();
    }
}

GJBLEServer bleServer;
GJOTA ota;
BuiltInTemperatureSensor tempSensor;

bool GJIsIdle()
{
  return bleServer.IsIdle();
}

void PrintVersion()
{
  const char *s_buildDate =  __DATE__ "," __TIME__;
  extern int __vectors_load_start__;
  SER("NRF51 Pool temps Partition 0x%x Build:%s\r\n", &__vectors_load_start__, s_buildDate);
  SER("DeviceID:0x%x%x\n\r", NRF_FICR->DEVICEID[0], NRF_FICR->DEVICEID[1]);
}

void Command_Version()
{
  PrintVersion();
}

void Command_TempDie()
{
  uint32_t temp = tempSensor.GetValue();
  SER("Die Temp:%d\r\n", temp);
}

DEFINE_COMMAND_NO_ARGS(version, Command_Version);
DEFINE_COMMAND_NO_ARGS(tempdie, Command_TempDie);

//map file names to static flash locations
DEFINE_FILE_SECTORS(boot,       "/boot",      0x1bc00, 1);
DEFINE_FILE_SECTORS(tempreads,  "/tempreads", 0x3d000, 8);
DEFINE_FILE_SECTORS(lastdate,   "/lastdate",  0x3f800, 1);
DEFINE_FILE_SECTORS(config,     "/config",    0x3fc00, 1);

BEGIN_BOOT_PARTITIONS()
DEFINE_BOOT_PARTITION(0, 0x1c000, 0x10000)
DEFINE_BOOT_PARTITION(1, 0x2d000, 0x10000)
END_BOOT_PARTITIONS()

void Command_TestDelayEvent();

struct ManufData
{
  uint32_t unixtime;
  uint8_t temps[3];
};

uint32_t lastAdvTime = 0;

int main(void)
{

  for (uint32_t i = 0 ; i < 32 ; ++i)
    nrf_gpio_cfg_default(i);

  REFERENCE_COMMAND(tempdie);
  REFERENCE_COMMAND(version);

  Delay(100);
  InitMultiboot();

  APP_TIMER_INIT(APP_TIMER_PRESCALER, APP_TIMER_OP_QUEUE_SIZE, nullptr);

  InitializeDateTime();
  InitCommands(0);
  InitSerial();
  InitESPUtils();
  InitFileSystem("");

  InitConfig();
  PrintConfig();

  PrintVersion();

  GJOTA *otaInit = nullptr;
  ota.Init();
  otaInit = &ota;

  uint32_t maxEvents = 4;
  GJEventManager = new EventManager(maxEvents);

  const char *hostName = "poolX";

  if (NRF_FICR->DEVICEID[0] == 0xa721f4d9 && NRF_FICR->DEVICEID[1] == 0xef467143)
    hostName = "pool";
  else if (NRF_FICR->DEVICEID[0] == 0x3af1fa73 && NRF_FICR->DEVICEID[1] == 0x8376a0de)
    hostName = "poolB";
  else if (NRF_FICR->DEVICEID[0] == 0x777ea834 && NRF_FICR->DEVICEID[1] == 0x844d895a)
    hostName = "poolC";

  SER("DeviceID:0x%x %x\n\r", NRF_FICR->DEVICEID[0], NRF_FICR->DEVICEID[1]);
  SER("hostname:%s\n\r", hostName);

  //note:must initialize after InitFStorage()
  bleServer.Init(hostName, otaInit);

  InitTemps();
  InitReadWriteInts();

  for (;;)
  {
      bleServer.Update();
      GJEventManager->WaitForEvents(0);

      ManufData manufData = {};
      uint8_t other;
      GetLatestReadings(manufData.unixtime, manufData.temps[0], manufData.temps[1], manufData.temps[2]);

      if (manufData.unixtime != 0 && manufData.unixtime != lastAdvTime)
      {
        lastAdvTime = manufData.unixtime;
        bleServer.SetAdvManufData(&manufData, 7);
      }

      bool bleIdle = bleServer.IsIdle();
      bool evIdle = GJEventManager->IsIdle();
      bool const isIdle = bleIdle && evIdle;
      if (isIdle)
      {
          //GJ_DBG_PRINT("power_manage enter\n\r");
          power_manage();
          //GJ_DBG_PRINT("power_manage exit\n\r");
      }
  }
}


