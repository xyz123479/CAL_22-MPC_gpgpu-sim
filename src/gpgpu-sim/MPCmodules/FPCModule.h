#ifndef __FPC_MODULE_H__
#define __FPC_MODULE_H__

#include <iostream>
#include <vector>

#include "PredCompModule.h"
#include "CompStruct.h"

class PatternModule;

enum ePattern
{
  ZRLE = 0,
  Zero = 1,
  SingleOne = 2,
  TwoConsecOnes = 3,
  FrontHalfZeros = 4,
  BackHalfZeros = 5,
  Uncompressible = 6,
};

class FPCModule
{
friend class PredCompModule;

public:
  // constructor
  FPCModule() {}
  FPCModule(std::vector<PatternModule*> patternModules)
    : m_PatternModules(patternModules) {}

  void AddModule(PatternModule *patternModule);
  void RemoveModule(int number);

  int ProcessLine(Binary &scanned);

private:
  bool isRowZeros(std::vector<uint8_t> &row);
  bool isRowSingleOne(std::vector<uint8_t> &row);
  bool isRowTwoConsecOnes(std::vector<uint8_t> &row);
  bool isRowFrontHalfZeros(std::vector<uint8_t> &row);
  bool isRowBackHalfZeros(std::vector<uint8_t> &row);

private:
  std::vector<PatternModule*> m_PatternModules;
  // ZRLE
  // Zero
  // SigleOne
  // ConsecOnes
  // FrontHalfZeros
  // BackHalfZeros
  // Uncompressible
  const compSizeList m_EncodingBitsSize = { 7, 4, 7, 8, 12, 12, 17 };
};

#endif

