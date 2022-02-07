#ifndef __ALL_ZERO_MODULE_H__
#define __ALL_ZERO_MODULE_H__

#include "CompressionModule.h"

class AllZeroModule : public CompressionModule
{
public:
  // constructor
  AllZeroModule(int lineSize)
    : CompressionModule(lineSize) {}

  unsigned CompressLine(std::vector<uint8_t> &dataLine);
  Binary CompressLine(std::vector<uint8_t> &dataLine, int nothing) { std::cout << "Not implemented." << std::endl; exit(1); }

};

#endif
