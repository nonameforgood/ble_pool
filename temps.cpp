#include "temps.h"
#include "ds18b20.h"
#include "gj/commands.h"
#include "gj/eventmanager.h"
#include "gj/appendonlyfile.h"
#include "gj/config.h"
#include "gj/datetime.h"

DEFINE_CONFIG_INT32(period, period, 15);

DEFINE_CONFIG_INT32(datapin0, datapin0, 24);
DEFINE_CONFIG_INT32(datapin1, datapin1, 25);
DEFINE_CONFIG_INT32(datapin2, datapin2, 22);

DEFINE_CONFIG_INT32(pwrpin0, pwrpin0, 3);
DEFINE_CONFIG_INT32(pwrpin1, pwrpin1, 4);
DEFINE_CONFIG_INT32(pwrpin2, pwrpin2, 5);

#define DELAY_ADD_EVENT(f, d) GJEventManager->DelayAdd(f, d)

constexpr uint16_t minReading = 150; //15°
constexpr uint16_t maxReading = 150 + 255; //40.5°

struct TempSession
{
  static constexpr uint32_t MaxTemps = 16;
  uint32_t m_time = 0;
  uint8_t m_period = 0;  //minutes
  uint8_t m_id = 0;
  uint8_t m_count = 0;
  uint8_t m_pad = 0;
  uint8_t m_readings[MaxTemps] = {};
};
TempSession tempSessions[3];

struct LatestReadings
{
  uint32_t unixtime;
  uint8_t readings[3];
} s_latestReadings = {};

void GetLatestReadings(uint32_t &unixtime, uint8_t &r0, uint8_t &r1, uint8_t &r2)
{
  unixtime = s_latestReadings.unixtime; 
  r0 = s_latestReadings.readings[0];
  r1 = s_latestReadings.readings[1];
  r2 = s_latestReadings.readings[2];
}

void DisplayTempSession(const TempSession &session)
{
  const uint8_t *v = session.m_readings;

  char buffer[300];
  uint32_t len = sprintf(buffer, "id:%d t:%d p:%d",
      session.m_id,
      session.m_time,
      session.m_period);

  for (int i = 0 ; i < TempSession::MaxTemps ; ++i)
  {
    char tempBuffer[20];

    uint16_t encodedRead = (uint16_t)v[i] + minReading;
    uint16_t readingInt = encodedRead / 10;
    uint16_t readingDec = encodedRead % 10;
    sprintf(tempBuffer, " %d.%d", (uint16_t)readingInt, (uint16_t)readingDec);

    strcat(buffer, tempBuffer);
  }

  SER("%s\n", buffer); 
}

void DisplayActiveTemps(uint32_t index)
{
  if (!AreTerminalsReady())
  {
    EventManager::Function f;
    f = std::bind(DisplayActiveTemps, index);
    DELAY_ADD_EVENT(f, 100 * 1000);
    return;
  }

  if (index == 0)
  {
    SER("Active Temperature readings:\n\r");
  }

  DisplayTempSession(tempSessions[index]);

  if (index < 2)
  {
    index++;
    EventManager::Function f;
    f = std::bind(DisplayActiveTemps, index);
    GJEventManager->Add(f);
  }
}

bool DisplayTempsIndex(uint32_t index, uint32_t minTime)
{
  uint32_t blockIndex = 0;
  bool read = false;

  auto onBlock = [&](uint32_t size, const void *data)
  {
    const TempSession *session = (TempSession*)data;
    const TempSession *sessionEnd = (TempSession*)((char*)data + size);

    while(session < sessionEnd)
    { 
      //if (size != sizeof(TempSession))
      //  return;

      if (session->m_time < minTime)
      {
        session++;
        continue;
      }

      if (blockIndex == index)
      {
        read = true;

        DisplayTempSession(*session);
      }

      session++;
      blockIndex++;
    }
  };

  AppendOnlyFile file("/tempreads");
  file.ForEach(onBlock);

  bool done = !read;
  return done;
}

void DisplayTemps(uint32_t index, uint32_t minTime)
{
  static uint32_t tempCount = 0;

  if (!AreTerminalsReady())
  {
    EventManager::Function f;
    f = std::bind(DisplayTemps, index, minTime);
    DELAY_ADD_EVENT(f, 100 * 1000);
    return;
  }

  if (index == 0)
  {
    SER("Temperature readings:\n\r");
    tempCount = 0;
  }

  bool done = DisplayTempsIndex(index, minTime);    

  if (!done)
  {
    tempCount ++;
    index++;
    EventManager::Function f;
    f = std::bind(DisplayTemps, index, minTime);
    GJEventManager->Add(f);
  }
  else
  {
    SER("Total readings:%d\n\r", tempCount);
  }
}


void WriteTemps()
{
  AppendOnlyFile file("/tempreads");

  const uint32_t size = sizeof(tempSessions);

  if (!file.BeginWrite(size))
  {
    file.Erase();
    bool ret = file.BeginWrite(size);
    APP_ERROR_CHECK_BOOL(ret);
  }
  file.Write(tempSessions, size);
  file.EndWrite();

  SER("Session file written\n\r");
}

void RecordTemp(uint32_t time, uint16_t temp, uint16_t reading)
{
  reading = Max<uint16_t>(reading, minReading);
  reading = Min<uint16_t>(reading, maxReading);

  uint8_t readingRel = reading - minReading;
  readingRel = (readingRel == 0) ? 1 : readingRel;

  TempSession &session = tempSessions[temp];

  if (session.m_time == 0)
  {
    session.m_time = time;
    session.m_period = GJ_CONF_INT32_VALUE(period);
    session.m_id = temp;
  }
  
  for (int i = 0 ; i < TempSession::MaxTemps ; ++i)
  {
    if (session.m_readings[i] == 0)
    {
      session.m_readings[i] = readingRel;
      break;
    }
  }
}

void RecordTemps(RecordTempsInfo *info)
{
  if (!AreTerminalsReady())
  {
    EventManager::Function f = std::bind(RecordTemps, info);
    DELAY_ADD_EVENT(f, 100 * 1000);
    return;
  }

  if (info->m_index == 0)
  {
    info->m_time = GetUnixtime();
    SER("%s begin %d\n\r", info->m_record ? "RecordTemps" : "Read Temps",info->m_time);

    const uint8_t lastIndex = TempSession::MaxTemps - 1;
    const bool sessionFull = tempSessions[0].m_readings[lastIndex] != 0;
    if (sessionFull)
    {
      memset(tempSessions, 0, sizeof(tempSessions));
    }  
  }

  int32_t pwrPins[3] = {GJ_CONF_INT32_VALUE(pwrpin0), GJ_CONF_INT32_VALUE(pwrpin1), GJ_CONF_INT32_VALUE(pwrpin2)};
  int32_t dataPins[3] = {GJ_CONF_INT32_VALUE(datapin0), GJ_CONF_INT32_VALUE(datapin1), GJ_CONF_INT32_VALUE(datapin2)};

  uint32_t sensorIndex = info->m_index / 5;
  uint32_t retryIndex = info->m_index % 5;


  {
    bool input = false;
    bool pullUp = false;
    SetupPin(pwrPins[sensorIndex], input, pullUp);
    WritePin(pwrPins[sensorIndex], 1);

    Delay(100);

    void *handle = BeginDS18b20(dataPins[sensorIndex]);
    if (!handle)
    {
      EventManager::Function f = std::bind(RecordTemps, info);
      DELAY_ADD_EVENT(f, 100 * 1000);
      return;
    }

    uint16_t vals[3] = {};
    uint16_t minVal = 0xffff;
    uint16_t maxVal = 0;

    uint32_t valIndex = 0;
    for (int i = 0 ; i < 10 ; ++i)
    {
      if (ReadDS18b20(handle, vals[valIndex]) >= 0)
      {
        //printf("%d \n\r", vals[valIndex]);
        minVal = Min<uint16_t>(minVal, vals[valIndex]);
        maxVal = Max<uint16_t>(maxVal, vals[valIndex]);
        ++valIndex;
        if (valIndex == 3)
          break;
      }
    }
    
    EndDS18b20(handle);

    WritePin(pwrPins[sensorIndex], 0);

    uint16_t val = 0;

    if ((maxVal - minVal) < 5 && valIndex == 3)
    {
      val = (maxVal + minVal) >> 1;
    }

    if (val || retryIndex == 4)
    {
      SER("Temperature on dataPin %d pwrPin %d:%d\n\r", dataPins[sensorIndex], pwrPins[sensorIndex], val);

      //read success
      if (info->m_record)
        RecordTemp(info->m_time, sensorIndex, val);
      
      sensorIndex++;
      retryIndex = 0;
    }
    else
    {
      if (retryIndex < 4)
      { 
        retryIndex++;
        SER("Failed read, retry...\n\r");
      }
    }
  }

  if (sensorIndex < 3)
  {
    info->m_index = sensorIndex * 5 + retryIndex;
    EventManager::Function f = std::bind(RecordTemps, info);
    uint32_t delay = (retryIndex == 0) ? 100 : 500;
    DELAY_ADD_EVENT(f, delay * 1000);
  }
  else
  {
    SER("%s end %d\n\r", info->m_record ? "RecordTemps" : "Read Temps", GetUnixtime());

    if (info->m_record)
    {
      const uint8_t lastIndex = TempSession::MaxTemps - 1;
      const bool sessionFull = tempSessions[0].m_readings[lastIndex] != 0;
      if (sessionFull)
      {
        WriteTemps();

        const uint32_t t = GetUnixtime();
        SetUnixtime(t); //update datetime file
      }

      for (uint32_t i = TempSession::MaxTemps ; i > 0  ; --i)
      {
        const uint32_t index = i - 1;
        if (tempSessions[0].m_readings[index] != 0)
        {
          s_latestReadings.unixtime = tempSessions[0].m_time + index * GJ_CONF_INT32_VALUE(period) * 60;
          s_latestReadings.readings[0] = tempSessions[0].m_readings[index];
          s_latestReadings.readings[1] = tempSessions[1].m_readings[index];
          s_latestReadings.readings[2] = tempSessions[2].m_readings[index];
          break;
        }
      }

      info->m_index = 0;
      EventManager::Function f = std::bind(RecordTemps, info);

      int64_t nextDelay = GJ_CONF_INT32_VALUE(period) * 60;
      
      //remove time spent reading sensors
      nextDelay -= GetUnixtime() - info->m_time;

      nextDelay = Max<int64_t>(nextDelay, 5);

      DELAY_ADD_EVENT(f, nextDelay * 1000 * 1000);

      SER("Next record scheduled to %d (current:%d)\n\r", (uint32_t)(GetUnixtime() + nextDelay), GetUnixtime());
    }
    else
    {
      delete info;
    }
  }

  //bleServer.RestartAdvertising();
}

void Command_tempdisp(const char *command)
{
  uint32_t minTime = 0;

  CommandInfo info;
  GetCommandInfo(command, info);

  if (info.m_argCount)
  {
    minTime = strtol(info.m_argsBegin[0], nullptr, 0);
  }

  DisplayTemps(0, minTime);
}


void Command_tempdispactive()
{
  DisplayActiveTemps(0);
}

void Command_tempclear()
{
  AppendOnlyFile file("/tempreads");
  file.Erase();
  SER("cleared\n\r");
}

void Command_readtemps(const char *command)
{
  CommandInfo2 info;
  GetCommandInfo(command, info);

  if (info.m_argCount)
  {
    uint32_t dataPin = 4;
    uint32_t pwrPin = 0;

    if (info.m_argCount)
    {
      dataPin = atoi(info.m_args[0].c_str());
    }

    if (info.m_argCount >= 2)
    {
      pwrPin = atoi(info.m_args[1].c_str());
    }

    if (pwrPin)
    {
      bool input = false;
      bool pullUp = false;
      SetupPin(pwrPin, input, pullUp);
      WritePin(pwrPin, 1);
    }

    uint16_t val = ReadDS18b20(dataPin);

    SER("Temperature:%d\n\r", val);

    if (pwrPin)
    {
      WritePin(pwrPin, 0);
    }

    return;
  }
  
  {
    RecordTempsInfo *info = new RecordTempsInfo;
    info->m_record = false;
    info->m_index = 0;
    info->m_time = 0;
    EventManager::Function f = std::bind(RecordTemps, info);
    DELAY_ADD_EVENT(f, 100 * 1000);
  }
}


DEFINE_COMMAND_ARGS(tempread, Command_readtemps);
DEFINE_COMMAND_ARGS(tempdisp, Command_tempdisp);
DEFINE_COMMAND_NO_ARGS(tempdispactive, Command_tempdispactive);
DEFINE_COMMAND_NO_ARGS(tempclear, Command_tempclear);

void InitTemps()
{
  REFERENCE_COMMAND(tempread);
  REFERENCE_COMMAND(tempdisp);
  REFERENCE_COMMAND(tempdispactive);
  REFERENCE_COMMAND(tempclear);

  
  {
    RecordTempsInfo *info = new RecordTempsInfo;
    info->m_record = true;
    info->m_index = 0;
    info->m_time = 0;

    EventManager::Function f = std::bind(RecordTemps, info);
    DELAY_ADD_EVENT(f, 1 * 1000 * 1000);
  }  
}