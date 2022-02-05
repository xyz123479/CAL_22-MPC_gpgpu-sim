#ifndef __XOR_MODULE_H__
#define __XOR_MODULE_H__

#include "PredCompModule.h"


class XORModule
{
friend class PredCompModule;

public:
  // constructor
  XORModule(bool consecutiveXOR)
    : mb_ConsecutiveXOR(consecutiveXOR) {}

  Binary ProcessLine(Binary &bitplane);

private:
  bool mb_ConsecutiveXOR;
};


#endif
