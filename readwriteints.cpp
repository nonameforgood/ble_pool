#include "gj/base.h"
#include "gj/commands.h"
#include "gj/eventmanager.h"

bool GetIntsArgs(const char *command, uint32_t &adr, uint32_t &cnt)
{
  CommandInfo info;
  GetCommandInfo(command, info);

  if (info.m_argCount < 2)
  {
    SER("incorrect args\n\r");
    return false;
  }

  adr = strtol(info.m_argsBegin[0], nullptr, 0);
  cnt = strtol(info.m_argsBegin[1], nullptr, 0);
  return true;
}

void ReadInts(uint32_t adr, uint32_t cnt)
{
  if (!AreTerminalsReady())
  {
    EventManager::Function f = std::bind(ReadInts, adr, cnt);
    GJEventManager->DelayAdd(f, 10 * 1000);
    return;
  }

  uint32_t val = *(uint32_t*)adr;

  SER("0x%08x:0x%08x (%d)\n\r", adr, val, val);
  adr += 4;
  cnt--;

  if (cnt)
  {
    EventManager::Function f = std::bind(ReadInts, adr, cnt);
    GJEventManager->Add(f);
  }
}


void Command_readints(const char *command)
{
  uint32_t adr;
  uint32_t cnt;

  if (!GetIntsArgs(command, adr, cnt))
    return;

  EventManager::Function f = std::bind(ReadInts, adr, cnt);
  GJEventManager->Add(f);
}

void WriteInt(uint32_t adr, uint32_t newVal)
{
  if (!AreTerminalsReady())
  {
    EventManager::Function f = std::bind(ReadInts, adr, newVal);
    GJEventManager->DelayAdd(f, 10 * 1000);
    return;
  }

  uint32_t val = *(uint32_t*)adr;

  SER("0x%08x:0x%08x -> 0x%08x\n\r", adr, val, newVal);
}

void Command_writeint(const char *command)
{
  uint32_t adr;
  uint32_t val;

  if (!GetIntsArgs(command, adr, val))
    return;

  EventManager::Function f = std::bind(WriteInt, adr, val);
  GJEventManager->Add(f);
}


DEFINE_COMMAND_ARGS(readints, Command_readints);
DEFINE_COMMAND_ARGS(writeint, Command_writeint);


void InitReadWriteInts()
{

  REFERENCE_COMMAND(readints);
  REFERENCE_COMMAND(writeint);

}