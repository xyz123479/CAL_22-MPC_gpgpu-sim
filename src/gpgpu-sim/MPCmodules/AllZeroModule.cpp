#include "AllZeroModule.h"
#include "../comp.h"

unsigned AllZeroModule::CompressLine(std::vector<uint8_t> &dataLine)
{
  for (int i = 0; i < m_LineSize; i++)
  {
    if (dataLine[i] != 0)
      return m_LineSize * BYTE;
  }
  return 0;
}
