#ifndef __ALL_WORD_SAME_MODULE_H__
#define __ALL_WORD_SAME_MODULE_H__

#include "CompressionModule.h"

class AllWordSameModule : public CompressionModule
{
public:
  // constructor
  AllWordSameModule(int lineSize)
    : CompressionModule(lineSize) {}

  unsigned CompressLine(std::vector<uint8_t> &dataLine);
  Binary CompressLine(std::vector<uint8_t> &dataLine, int nothing) { std::cout << "Not implemented." << std::endl; exit(1); }
};

#endif
