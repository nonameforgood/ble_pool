#include "gj/base.h"

struct RecordTempsInfo
{
  bool m_record;
  uint32_t m_index;
  uint32_t m_time;
};

void GetLatestReadings(uint32_t &unixtime, uint8_t &r0, uint8_t &r1, uint8_t &r2);
void RecordTemps(RecordTempsInfo *info);
void InitTemps();