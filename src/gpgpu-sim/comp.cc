#include <stdio.h>
#include <limits.h>

#include "../json/json.h"
#include "comp.h"
#include "MPCmodules/AllWordSameModule.h"
#include "MPCmodules/AllZeroModule.h"
#include "MPCmodules/BitplaneModule.h"
#include "MPCmodules/CompressionModule.h"
#include "MPCmodules/FPCModule.h"
#include "MPCmodules/PatternModule.h"
#include "MPCmodules/PredCompModule.h"
#include "MPCmodules/PredictorModule.h"
#include "MPCmodules/ResidueModule.h"
#include "MPCmodules/ScanModule.h"
#include "MPCmodules/XORModule.h"
#include "MPCmodules/CompStruct.h"

#define MIN_GRAN 32

compressor *g_comp;

// MPC ---------------------------------------------------------------------
unsigned MPCompressor::compress(uint8_t* data, int req_size)
{
  assert (req_size % MIN_GRAN == 0);

  std::vector<uint8_t> dataLine(data, data + req_size);
  unsigned compressed_size = 0;
  for (int i = 0; i < req_size / MIN_GRAN; i++) {
    std::vector<uint8_t> dataBlock(dataLine.begin()+i*MIN_GRAN, dataLine.begin()+(i+1)*MIN_GRAN);
    compressed_size += (this->*compressLine)(dataBlock);
  }

  // stat
  m_uncomp_size += req_size * BYTE;
  m_comp_size += compressed_size;

  return compressed_size;
}

unsigned MPCompressor::compressLineAllWordSame(std::vector<uint8_t> &dataLine)
{
  // Check ALLZERO
  {
    bool isAllZeros;
    unsigned compressedSize = checkAllZeros(0, isAllZeros, dataLine);

    if (isAllZeros)
      return compressedSize;
  }

  // Check ALLWORDSAME
  {
    bool isAllWordSame;
    unsigned compressedSize = checkAllWordSame(1, isAllWordSame, dataLine);

    if (isAllWordSame)
      return compressedSize;
  }

  // Check other patterns
  {
    unsigned compressedSize = checkOtherPatterns(2, dataLine);
    return compressedSize;
  }
}

unsigned MPCompressor::compressLineOnlyAllZero(std::vector<uint8_t> &dataLine)
{
  // Check ALLZERO
  {
    bool isAllZeros;
    unsigned compressedSize = checkAllZeros(0, isAllZeros, dataLine);

    if (isAllZeros)
      return compressedSize;
  }

  // Check other patterns
  {
    unsigned compressedSize = checkOtherPatterns(1, dataLine);
    return compressedSize;
  }
}

void MPCompressor::parseConfig(std::string &configPath)
{
  // open config file
  std::ifstream configFile;
  configFile.open(configPath);
  if (!configFile.is_open())
  {
    printf("Invalid File! \"%s\" is not valid path.\n", configPath.c_str());
    exit(1);
  }

  // declare json root
  Json::Value root;

  // instantiate json parser
  Json::CharReaderBuilder builder;
  builder["collecComments"] = false;
  JSONCPP_STRING errs;
  if (!parseFromStream(builder, configFile, &root, &errs))
  {
    std::cout << errs << std::endl;
    printf("Parsing ERROR! \"%s\" is not valid json file.\n", configPath.c_str());
    exit(1);
  }

  // parse overview field
  {
    m_NumModules = root["overview"]["num_modules"].asInt();
    m_NumClusters = m_NumModules + 1;
    m_LineSize = root["overview"]["lineSize"].asInt();
    if (root["overview"]["encoding_bits"].isNull())
    {
      int encodingBits = (int)ceil(log2f((float)m_NumClusters));
      m_EncodingBits.insert(std::make_pair(-1, encodingBits));
      for (int i = 0; i < m_NumModules; i++)
        m_EncodingBits.insert(std::make_pair(i, encodingBits));
    }
    else
    {
      for (int i = 0; i < m_NumClusters; i++)
      {
        int encodingBits = root["overview"]["encoding_bits"][i].asInt();
        m_EncodingBits.insert(std::make_pair(i - 1, encodingBits));
      }
    }
  }

  // parse module field
  {
    m_CompModules.resize(m_NumModules);
    for (int i = 0; i < m_NumModules; i++)
    {
      // reference of a module spec for convenient
      Json::Value &moduleSpec = root["modules"][std::to_string(i)];

      CompressionModule *compModule;
      std::string moduleName = moduleSpec["name"].asString();

      // if module is 'PredComp', parse submodules
      if (moduleName == "PredComp")
      {
        Json::Value &subModuleSpec = moduleSpec["submodules"];

        // residue module
        PredictorModule *predModule;

        Json::Value &predModuleSpec = subModuleSpec["ResidueModule"]["PredictorModule"];
        std::string predModuleName = predModuleSpec["name"].asString();
        int lineSize = predModuleSpec["LineSize"].asInt();
        int rootIndex = predModuleSpec["RootIndex"].asInt();
        if (predModuleName == "WeightBasePredictor")
        {
          std::vector<int> baseIndexTable;
          std::vector<float> weightTable;

          baseIndexTable.resize(lineSize);
          weightTable.resize(lineSize);
          for (int j = 0; j < lineSize; j++)
          {
            baseIndexTable[j] = predModuleSpec["BaseIndexTable"][j].asInt();
            weightTable[j] = predModuleSpec["WeightTable"][j].asFloat();
          }
          
          predModule = new WeightBasePredictor(rootIndex, lineSize, baseIndexTable, weightTable);
        }
        else if (predModuleName == "DiffBasePredictor")
        {
          std::vector<int> baseIndexTable;
          std::vector<int> diffTable;

          baseIndexTable.resize(lineSize);
          diffTable.resize(lineSize);
          for (int j = 0; j < lineSize; j++)
          {
            baseIndexTable[j] = predModuleSpec["BaseIndexTable"][j].asInt();
            diffTable[j] = predModuleSpec["DiffTable"][j].asInt();
          }

          predModule = new DiffBasePredictor(rootIndex, lineSize, baseIndexTable, diffTable);
        }
        else if (predModuleName == "OneBasePredictor")
        {
          predModule = new OneBasePredictor(rootIndex, lineSize);
        }
        else if (predModuleName == "ConsecutiveBasePredictor")
        {
          predModule = new ConsecutiveBasePredictor(rootIndex, lineSize);
        }
        else
        {
          printf("""%s"" is not a valid predictor module. Check the config file.\n", predModuleName.c_str());
          exit(1);
        }
        ResidueModule *residueModule = new ResidueModule(predModule);

        // bitplane module
        BitplaneModule *bitplaneModule = new BitplaneModule();

        // xor module
        bool consecutiveXOR = subModuleSpec["XORModule"]["consecutiveXOR"].asBool();
        XORModule *xorModule = new XORModule(consecutiveXOR);

        // scan module
        int tableSize = subModuleSpec["ScanModule"]["TableSize"].asInt();
        std::vector<int> rows;
        std::vector<int> cols;

        rows.resize(tableSize);
        cols.resize(tableSize);
        for (int j = 0; j < tableSize; j++)
        {
          rows[j] = subModuleSpec["ScanModule"]["Rows"][j].asInt();
          cols[j] = subModuleSpec["ScanModule"]["Cols"][j].asInt();
        }
        ScanModule *scanModule = new ScanModule(tableSize, rows, cols);

        // fpc module
        std::vector<PatternModule*> patternModules;

        int numPatternModule = subModuleSpec["FPCModule"]["num_modules"].asInt();
        patternModules.resize(numPatternModule);
        for (int j = 0; j < numPatternModule; j++)
        {
          Json::Value &patternModuleSpec = subModuleSpec["FPCModule"][std::to_string(j)];

          PatternModule *patternModule;
          std::string patternModuleName = patternModuleSpec["name"].asString();

          // parse pattern modules
          if (patternModuleName == "ZerosPattern")
          {
            int encodingBitsZRLE = patternModuleSpec["encodingBitsZRLE"].asInt();
            int encodingBitsZero = patternModuleSpec["encodingBitsZero"].asInt();

            patternModule = new ZerosPatternModule(encodingBitsZRLE, encodingBitsZero);
          }
          else if (patternModuleName == "SingleOnePattern")
          {
            int encodingBits = patternModuleSpec["encodingBits"].asInt();

            patternModule = new SingleOnePatternModule(encodingBits);
          }
          else if (patternModuleName == "TwoConsecutiveOnesPattern")
          {
            int encodingBits = patternModuleSpec["encodingBits"].asInt();

            patternModule = new TwoConsecutiveOnesPatternModule(encodingBits);
          }
          else if (patternModuleName == "MaskingPattern")
          {
            int encodingBits = patternModuleSpec["encodingBits"].asInt();
            std::vector<int> maskingVector;
            maskingVector.resize(patternModuleSpec["maskingVector"].size());

            for (int k = 0; k < maskingVector.size(); k++)
              maskingVector[k] = patternModuleSpec["maskingVector"][k].asInt();

            int isZerosFrontHalf = 1;
            int isZerosBackHalf = 1;
            // zeros front half check
            for (int k = 0; k < maskingVector.size(); k++)
            {
              if ((k < maskingVector.size() / 2) && (maskingVector[k] != 0))
              {
                isZerosFrontHalf = 0;
                break;
              }
              else if ((k >= maskingVector.size() / 2) && (maskingVector[k] != 2))
              {
                isZerosFrontHalf = 0;
                break;
              }
            }

            // zeros back half check
            for (int k = 0; k < maskingVector.size(); k++)
            {
              if ((k < maskingVector.size() / 2) && (maskingVector[k] != 2))
              {
                isZerosBackHalf = 0;
                break;
              }
              else if ((k >= maskingVector.size() / 2) && (maskingVector[k] != 0))
              {
                isZerosBackHalf = 0;
                break;
              }
            }

            // instantiate modules
            if (isZerosFrontHalf == 1)
              patternModule = new ZerosFrontHalfPatternModule(encodingBits);
            else if (isZerosBackHalf == 1)
              patternModule = new ZerosBackHalfPatternModule(encodingBits);
            else
              patternModule = new MaskingPatternModule(encodingBits, maskingVector);
          }
          else if (patternModuleName == "UncompressedPattern")
          {
            int encodingBits = patternModuleSpec["encodingBits"].asInt();

            patternModule = new UncompressedPatternModule(encodingBits);
          }
          else
          {
            printf("""%s"" is not a valid pattern module. Check the config file.\n", patternModuleName.c_str());
            exit(1);
          }
          patternModules[j] = patternModule;
        }

        FPCModule *fpcModule = new FPCModule(patternModules);

        // PredCompModule
        compModule = new PredCompModule(m_LineSize, residueModule, bitplaneModule, xorModule, scanModule);
      }
      else if (moduleName == "AllZero")
      {
        // AllZeroModule
        compModule = new AllZeroModule(m_LineSize);
        (this->compressLine) = &MPCompressor::compressLineOnlyAllZero;
      }
      else if (moduleName == "ByteplaneAllSame" || moduleName == "AllWordSame")
      {
        // AllWordSameModule
        compModule = new AllWordSameModule(m_LineSize);
        (this->compressLine) = &MPCompressor::compressLineAllWordSame;
      }
      else
      {
        printf("\"%s\" is not a valid compression module. Check the config file.\n", moduleName.c_str());
        exit(1);
      }

      m_CompModules[i] = compModule;
    }
  }
}

unsigned MPCompressor::checkAllZeros(const int chosenCompModule, bool &isAllZeros, std::vector<uint8_t> &dataLine)
{
  isAllZeros = false;
  const unsigned uncompressedSize = dataLine.size() * BYTE;
  AllZeroModule *allZeroModule = static_cast<AllZeroModule*>(m_CompModules[chosenCompModule]);
  unsigned compressedSize = allZeroModule->CompressLine(dataLine);

  if (compressedSize == 0) 
  {
    isAllZeros = true;
    compressedSize += m_EncodingBits[chosenCompModule];
  }

  return compressedSize;
}

unsigned MPCompressor::checkAllWordSame(const int chosenCompModule, bool &isAllWordSame, std::vector<uint8_t> &dataLine)
{
  isAllWordSame = false;
  const unsigned uncompressedSize = dataLine.size() * BYTE;
  AllWordSameModule *allWordSameModule = static_cast<AllWordSameModule*>(m_CompModules[1]);
  unsigned compressedSize = allWordSameModule->CompressLine(dataLine);

  if (compressedSize == 4*BYTE)
  {
    isAllWordSame = true;
    compressedSize += m_EncodingBits[chosenCompModule];
  }

  return compressedSize;
}

unsigned MPCompressor::checkOtherPatterns(const int numStartingModule, std::vector<uint8_t> &dataLine)
{
  int chosenCompModule = -1;
  const unsigned uncompressedLineSize = dataLine.size() * BYTE;
  unsigned compressedLineSize = uncompressedLineSize;

  int numMaxScannedZRL = 0;
  Binary maxScanned;
  for (int i = numStartingModule; i < m_NumModules; i++)
  {
    PredCompModule *predCompModule = static_cast<PredCompModule*>(m_CompModules[i]);
    Binary scanned = predCompModule->CompressLine(dataLine, 0);

    // count zrl
    int numScannedZRL = 0;
    for (int row = 0; row <scanned.GetRowSize(); row++)
    {
      if (scanned.IsRowZeros(row))
        numScannedZRL++;
      else
        break;
    }

    if (numMaxScannedZRL <= numScannedZRL)
    {
      chosenCompModule = i;
      numMaxScannedZRL = numScannedZRL;
      maxScanned = scanned;
    }
  }

  int compressedSize = m_CommonEncoder.ProcessLine(maxScanned);
  if (compressedSize < uncompressedLineSize)
  {
    compressedLineSize = compressedSize;
  }
  else
  {
    chosenCompModule = -1;
    compressedLineSize = uncompressedLineSize;
  }
  compressedLineSize += m_EncodingBits[chosenCompModule];

  return compressedLineSize;
}

// CPACK ---------------------------------------------------------------------
unsigned CachePacker::compress(uint8_t *data, int req_size)
{
  std::vector<uint8_t> dataLine(data, data + req_size);
  unsigned uncompSize = req_size;
  unsigned currCSize = 0;


  for (int i = 0; i < dataLine.size() / CPACK_WORDSIZE; i++)
  {
    uint8_t word[CPACK_WORDSIZE] = {
      dataLine[CPACK_WORDSIZE * i], dataLine[CPACK_WORDSIZE * i + 1], dataLine[CPACK_WORDSIZE * i + 2], dataLine[CPACK_WORDSIZE * i + 3]
    };

    // check zero patterns
    if (word[0] == 0 && word[1] == 0 && word[2] == 0)
    {
      // pattern zzzz (00)
      if (word[3] == 0)
      {
        currCSize += m_PatternLength[0];
      }
      // pattern zzzx (1100)B
      else
      {
        currCSize += m_PatternLength[4];
      }
      continue;
    }

    // check dictionary patterns
    bool found = false;
    for (int j = 0; j < CPACK_NUM_ENTRY; j++)
    {
      uint8_t *currEntry = m_Dictionary[j];
      uint8_t dictWord[CPACK_WORDSIZE] = {
        currEntry[0], currEntry[1], currEntry[2], currEntry[3]
      };

      if (word[0] == dictWord[0] && word[1] == dictWord[1])
      {
        if (word[2] == dictWord[2])
        {
          // pattern mmmm (10)bbbb
          if (word[3] == dictWord[3])
          {
            currCSize += m_PatternLength[2];
          }
          // pattern mmmx (1110)bbbbB
          else
          {
            currCSize += m_PatternLength[5];
          }
        }
        // pattern mmxx (1100)bbbbBB
        else
        {
          currCSize += m_PatternLength[3];
        }
        found = true;
        break;
      }
    }

    if (found)
    {
      continue;
    }
   
    // pattern xxxx (01)BBBB
    else
    {
      currCSize += m_PatternLength[1];
      
      // add new pattern to dictionary
      uint8_t *newEntry = new uint8_t[CPACK_WORDSIZE];
      for (int k = 0; k < CPACK_WORDSIZE; k++)
        newEntry[k] = word[k];
      m_Dictionary.push_back(newEntry);
      
      uint8_t *popEntry = m_Dictionary.front();
      m_Dictionary.pop_front();
      delete[] popEntry;

    }
  }

  m_uncomp_size += req_size * BYTE;
  m_comp_size += currCSize;
  return currCSize;
}

// BDI -----------------------------------------------------------------------
unsigned BDICompressor::compress(uint8_t *data, int req_size)
{
  std::vector<uint8_t> dataLine(data, data + req_size);

  const unsigned lineSize = dataLine.size();
  const unsigned uncompressedSize = BYTE * lineSize;

  BDIState select = BDIState::Uncompressed;

  unsigned bestCSize, currCSize;
  currCSize = uncompressedSize;
  bestCSize = currCSize;

  if(isZeros(dataLine))
  {
    bestCSize = BYTE;
    select = BDIState::Zeros;
  }
  else if(isRepeated(dataLine, 8))
  {
    bestCSize = BYTE * 8;
    select = BDIState::Repeat;
  }
  else
  {
    // base8-delta1
    // bestcase[32B / 64B] : (8+3)Bytes+4bits / (8+7)Bytes+8bits
    currCSize = checkBDI(dataLine, 8, 1);
    select = bestCSize > currCSize ? BDIState::Base8Delta1 : select;
    bestCSize = bestCSize > currCSize ? currCSize : bestCSize;

    // base8-delta2
    // bestcase[32B / 64B] : (8+6)Bytes+4bits / (8+14)Bytes+8bits
    currCSize = checkBDI(dataLine, 8, 2);
    select = bestCSize > currCSize ? BDIState::Base8Delta2 : select;
    bestCSize = bestCSize > currCSize ? currCSize : bestCSize;

    // base8-delta4
    // bestcase[32B / 64B] : (8+12)Bytes+4bits / (8+28)Bytes+8bits
    currCSize = checkBDI(dataLine, 8, 4);
    select = bestCSize > currCSize ? BDIState::Base8Delta4 : select;
    bestCSize = bestCSize > currCSize ? currCSize : bestCSize;

    // base4-delta1
    // bestcase[32B / 64B] : (4+7)Bytes+8bits / (4+15)Bytes+16bits
    currCSize = checkBDI(dataLine, 4, 1);
    select = bestCSize > currCSize ? BDIState::Base4Delta1 : select;
    bestCSize = bestCSize > currCSize ? currCSize : bestCSize;

    // base4-delta2
    // bestcase[32B / 64B] : (4+14)Bytes+8bits / (4+30)Bytes+16bits
    currCSize = checkBDI(dataLine, 4, 2);
    select = bestCSize > currCSize ? BDIState::Base4Delta2 : select;
    bestCSize = bestCSize > currCSize ? currCSize : bestCSize;

    // base2-delta1
    // bestcase[32B / 64B] : (2+15)Bytes+16bits / (2+31)Bytes+32bits
    currCSize = checkBDI(dataLine, 2, 1);
    select = bestCSize > currCSize ? BDIState::Base2Delta1 : select;
    bestCSize = bestCSize > currCSize ? currCSize : bestCSize;

    // incompressible
    select = (bestCSize == uncompressedSize) ? BDIState::Uncompressed : select;

  }

  // compressedSize + encodingBits
  int compressedSize = (bestCSize + 4);

  // stat
  m_uncomp_size += req_size * BYTE;
  m_comp_size += compressedSize;

  return compressedSize;
}

bool BDICompressor::isZeros(std::vector<uint8_t> &dataLine)
{
  for(int i = 0; i < dataLine.size(); i++)
    if(dataLine[i] != 0)
      return false;
  return true;
}

bool BDICompressor::isRepeated(std::vector<uint8_t> &dataLine, const unsigned granularity)
{
  // data concatenation
  std::vector<uint64_t> dataConcat;
  uint64_t temp;

  unsigned dataConcatSize = dataLine.size() / granularity;
  for(int i = 0; i < dataConcatSize; i++)
  {
    temp = 0;
    for(int j = 0; j < granularity; j++)
      temp = (temp << BYTE) | dataLine[i*granularity + j];

    dataConcat.push_back(temp);
  }

  // repeated block check
  uint64_t firstVal = dataConcat[0];
  for(int i = 1; i < dataConcatSize; i++)
    if(dataConcat[i] != firstVal)
      return false;
  return true;
}

unsigned BDICompressor::checkBDI(std::vector<uint8_t> &dataLine,
    const unsigned baseSize, const unsigned deltaSize)
{
  const unsigned lineSize = dataLine.size();
  uint64_t deltaLimit = 0;

  // maximum size delta can have
  switch(deltaSize)
  {
  case 1:
    deltaLimit = BYTEMAX;
    break;
  case 2:
    deltaLimit = BYTE2MAX;
    break;
  case 4:
    deltaLimit = BYTE4MAX;
    break;
  }

  // little endian
  std::vector<uint8_t> littleEndian;
  for(int i = 0; i < lineSize; i += baseSize)
    for(int j = baseSize-1; j >= 0; j--)
      littleEndian.push_back(dataLine[i + j]);

  // define appropriate size for the immediate-mask
  unsigned maskSize = lineSize / baseSize;
  bool* mask = new bool[maskSize];
  for(int i = 0; i < maskSize; i++)
    mask[i] = false;

  // concatenate little endian'ed data in a new vector
  std::vector<uint64_t> dataConcat;
  uint64_t temp;
  for(int i = 0; i < maskSize; i++)
  {
    temp = 0;
    for(int j = 0; j < baseSize; j++)
      temp = (temp << BYTE) | littleEndian[i*baseSize + j];

    // sign extension
    if((temp & (1 << (BYTE*baseSize-1))) != 0)
      temp &= BYTE8MAX;

    dataConcat.push_back(temp);
  }

  // find immediate block
  unsigned immediateCount = 0;
  for(int i = 0; i < maskSize; i++)
  {
    if(reduceSign(dataConcat[i]) <= deltaLimit)
    {
      mask[i] = true;
      immediateCount++;
    }
  }

  // find non-zero base
  uint64_t base = 0;
  int baseIdx = 0;
  for(int i = 0; i < maskSize; i++)
  {
    if(!mask[i])
    {
      base = dataConcat[i];
      baseIdx = i;
      break;
    }
  }

  // base-delta computation
  bool notAllDelta = false;
  for(int i = baseIdx + 1; i < maskSize; i++)
  {
    if(!mask[i])
    {
      if(reduceSign(base - dataConcat[i]) > deltaLimit)
      {
        notAllDelta = true;
        break;
      }
    }
  }

  delete[] mask;

  // immediateMask + immediateDeltas + base + deltas
  if(notAllDelta)
    return maskSize + BYTE*((immediateCount*deltaSize) + ((maskSize-immediateCount)*baseSize));
  else
    return maskSize + BYTE*((immediateCount*deltaSize) + (baseSize + (maskSize - immediateCount - 1)*deltaSize));
}

uint64_t BDICompressor::reduceSign(uint64_t x)
{
  uint64_t t = x >> 63;
  if(t)
  {
    for(int i = 62; i >= 0; i--)
    {
      t = (x >> i) & 0x01;
      if(t == 0)
      {
        return x & (BYTE8MAX >> (63 - (i + 1)));
      }
    }
  }
  return x;
}

// FPC -----------------------------------------------------------------------
unsigned FPCompressor::compress(uint8_t *data, int req_size)
{
  std::vector<uint8_t> dataLine(data, data + req_size);

  std::vector<uint32_t> dataConcat = concatenate(dataLine);
  const unsigned concatSize = dataConcat.size();

  unsigned currCSize = 0;
  unsigned i = 0;
  while(i < concatSize)
  {
    uint32_t val = dataConcat[i];

    // prefix 000 : zero value runs
    if(val == 0x00000000)
    {
      currCSize += 3 + PREFIX_SIZE;
      i++;
      while(dataConcat[i] == 0x00000000)
      {
        i++;
      }
      continue;
    }
    // prefix 001 : 4-bit sign extended
    else if(((val & 0xFFFFFFF8) == 0x00000000)
        ||  ((val & 0xFFFFFFF8) == 0xFFFFFFF8))
    {
      currCSize += 4 + PREFIX_SIZE;
    }
    // prefix 010 : 8-bit sign extended
    else if(((val & 0xFFFFFF80) == 0x00000000)
        ||  ((val & 0xFFFFFF80) == 0xFFFFFF80))
    {
      currCSize += 8 + PREFIX_SIZE;
    }
    // prefix 011 : 16-bit sign extended
    else if(((val & 0xFFFF8000) == 0x00000000)
        ||  ((val & 0xFFFF8000) == 0xFFFF8000))
    {
      currCSize += 16 + PREFIX_SIZE;
    }
    // prefix 100 : 16-bit padded with a zero
    else if((val & 0x0000FFFF) == 0x00000000)
    {
      currCSize += 16 + PREFIX_SIZE;
    }
    // prefix 101 : two halfwords, each a byte sign-extended
    else if(((val & 0xFF80FF80) == 0x00000000)
        ||  ((val & 0xFF80FF80) == 0xFF800000)
        ||  ((val & 0xFF80FF80) == 0x0000FF80)
        ||  ((val & 0xFF80FF80) == 0xFF80FF80))
    {
      currCSize += 16 + PREFIX_SIZE;
    }
    // prefix 110 : word consisting fo repeated bytes
    else if(((val & 0xFF) == ((val >> BYTE) & 0xFF))
        &&  ((val & 0xFF) == ((val >> 2*BYTE) & 0xFF))
        &&  ((val & 0xFF) == ((val >> 3*BYTE) & 0xFF)))
    {
      currCSize += 8 + PREFIX_SIZE;
    }
    else
    {
      currCSize += 4*BYTE + PREFIX_SIZE;
    }
    i++;
  }

  unsigned compressedSize = currCSize;

  // stat
  m_uncomp_size += req_size * BYTE;
  m_comp_size += compressedSize;
  return compressedSize;
}

std::vector<uint32_t> FPCompressor::concatenate(std::vector<uint8_t> &dataLine)
{
  const unsigned granularity = 4;
  std::vector<uint32_t> dataConcat;
  uint32_t temp;

  unsigned dataConcatSize = dataLine.size() / granularity;
  for(unsigned i = 0; i < dataConcatSize; i++)
  {
    temp = 0;
    // little endian
    for(int j = granularity-1; j >= 0; j--)
      temp = (temp << BYTE) | dataLine[i*granularity + j];

    dataConcat.push_back(temp);
  }

  return dataConcat;
}

// BPC -----------------------------------------------------------------------

static const unsigned ZRL_CODE_SIZE[34] = {0, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
static const unsigned singleOneSize = 10;
static const unsigned consecutiveDoubleOneSize = 10;
static const unsigned allOneSize = 5;
static const unsigned zeroDBPSize = 5;
// 1        -> uncompressed
// 01       -> Z-RLE: 2~33
// 001      -> Z-RLE: 1
// 00000    -> single 1
// 00001    -> consecutive two 1s
// 00010    -> zero DBP
// 00011    -> All 1s

unsigned BPCompressor::compress(uint8_t *data, int req_size)
{
  /*
    The original dataline is placed in row-wise order.
    And their DBPs are placed in col-wise order.
     ------------              -------------------
    |  data[0]   |            |   |   |   |   |   |
     ------------             | D | D | D | . | D |
    |  data[1]   |            | B | B | B | . | B |
     ------------      =>     | P | P | P | . | P |
    |    ...     |            | 0 | 1 | 2 | . | 3 |
     ------------             |   |   |   |   | 1 |
    |  data[31]  |            |   |   |   |   |   |
     ------------              -------------------
    */
  std::vector<uint8_t> _dataLine(data, data + req_size);
  const unsigned lineSize = _dataLine.size();
  const unsigned uncompressedSize = BYTE * lineSize;

  // converts uint8_t to uint64_t
  std::vector<int64_t> dataLine(lineSize / 4, 0);
  for (int i = 0; i < lineSize; i += 4)
  {
    int64_t data;
    std::memcpy(&data, &_dataLine[i], 4);
    dataLine[i/4] = data;
  }
  
  // delta
  std::vector<int64_t> deltas;
  for (int row = 1; row < dataLine.size(); row++)
    deltas.push_back(dataLine[row] - dataLine[row - 1]);

  int32_t prevDBP;
  int32_t DBP[33];
  int32_t DBX[33];
  for (int col = 32; col >= 0; col--)
  {
    // a buffer of bit-plane
    int32_t buf = 0;
    for (int row = deltas.size() - 1; row >= 0; row--)
    {
      buf <<= 1;
      buf |= ((deltas[row] >> col) & 1);
    }

    // base-xor
    if (col == 32)
    {
      DBP[32] = buf;
      DBX[32] = buf;
      prevDBP = buf;
    }
    else
    {
      DBP[col] = buf;
      DBX[col] = buf ^ prevDBP;
      prevDBP = buf;
    }
  }

  // first 32-bit word in original form (dataLine)
  unsigned compressedSize = encodeFirst(dataLine[0]);
  // the rest of the data
  compressedSize += encodeDeltas(DBP, DBX);

  // stat
  m_uncomp_size += req_size * BYTE;
  m_comp_size += compressedSize;

  return compressedSize;
}

unsigned BPCompressor::encodeFirst(int64_t base)
{
  if (base = 0)
    return 3;
  else if (isSignExtended(base, 4))
    return 3 + 4;
  else if (isSignExtended(base, 8))
    return 3 + 8;
  else if (isSignExtended(base, 16))
    return 3 + 16;
  else
    return 1 + 32;
}

unsigned BPCompressor::encodeDeltas(int32_t* DBP, int32_t* DBX)
{
  unsigned length = 0;
  unsigned runLength = 0;
  bool firstNZDBX = false;
  bool secondNZDBX = false;
  for (int i = 32; i >= 0; i--)
  {
    if (DBX[i] == 0)
    {
      runLength++;
    }
    else
    {
      // Z-RLE
      if (runLength > 0) 
      {
        length += ZRL_CODE_SIZE[runLength];
      }
      runLength = 0;

      // zero DBP
      if (DBP[i] == 0)
      {
        length += zeroDBPSize;
      }
      // All 1s
      else if (DBX[i] == 0x7fffffff)
      {
        length += allOneSize;
      }
      else
      {
        // find where the 1s are
        int oneCnt = 0;
        for (int j = 0; j < 32; j++)
          if ((DBX[i] >> j) & 1)
            oneCnt++;
        unsigned twoDistance = 0;
        int firstPos = -1;
        if (oneCnt <= 2)
          for (int j = 0; j < 32; j++)
            if ((DBX[i] >> j) & 1)
              if (firstPos == -1)
                firstPos = j;
              else
                twoDistance = j - firstPos;

        // single 1
        if (oneCnt == 1)
        {
          length += singleOneSize;
        }
        // consec double 1s
        else if ((oneCnt == 2) && (twoDistance == 1))
        {
          length += consecutiveDoubleOneSize;
        }
        // uncompressible
        else
        {
          length += 32;
        }
      }
    }
  }
  // final Z-RLE
  if (runLength > 0)
  {
    length += ZRL_CODE_SIZE[runLength];
  }

  return length;
}

bool BPCompressor::isSignExtended(uint64_t value, uint8_t bitSize)
{
  uint64_t max = (1ULL << (bitSize - 1)) - 1;     // bitSize: 4 -> ...0000111
  uint64_t min = ~max;                            // bitSize: 4 -> ...1111000
  return (value <= max) | (value >= min);
}

bool BPCompressor::isZeroExtended(uint64_t value, uint8_t bitSize)
{
  uint64_t max = (1ULL << (bitSize)) - 1;         // bitSize: 4 -> ...0001111
  return (value <= max);
}

// SC2 -----------------------------------------------------------------------

#define HEAP_CAPACITY SC2_ENTRIES
#define WORD_GRAN 4

namespace huffman
{

Node *createNode(int64_t symbol, uint64_t freq, Node *left, Node *right)
{
  Node *node = new Node;
  node->symbol = symbol;
  node->freq = freq;
  node->left = left;
  node->right = right;
  return node;
}

MinHeap::MinHeap(std::map<int64_t, uint64_t> symbolMap)
{
  this->heapSize = symbolMap.size();
  assert (heapSize <= HEAP_CAPACITY);

  this->heapArr = new Node * [HEAP_CAPACITY];

  for (auto it = symbolMap.begin(); it != symbolMap.end(); it++)
  {
    int index = std::distance(symbolMap.begin(), it);
    this->heapArr[index] = createNode(it->first, it->second);
  }

  this->buildHeap();
}

int MinHeap::GetLeftChild(int i)
{
  return 2 * i + 1;
}

int MinHeap::GetRightChild(int i)
{
  return 2 * i + 2;
}

int MinHeap::GetParent(int i)
{
  return ceil((float) i / 2) - 1;
}

int MinHeap::buildHeap()
{
  int lastParentIndex = this->heapSize / 2 - 1;

  for (int i = lastParentIndex; i >= 0; i--)
  {
    this->minHeapify(i);
  }

  return 0;
}

int MinHeap::minHeapify(int index)
{
  int minIndex = index;
  int leftChild = this->GetLeftChild(index);
  int rightChild = this->GetRightChild(index);

  if (leftChild <= this->heapSize - 1 && this->heapArr[leftChild]->freq < this->heapArr[minIndex]->freq)
  {
    minIndex = leftChild;
  }

  if (rightChild <= this->heapSize - 1 && this->heapArr[rightChild]->freq < this->heapArr[minIndex]->freq)
  {
    minIndex = rightChild;
  }

  if (minIndex != index)
  {
    this->swapHeapNodes(index, minIndex);
    this->minHeapify(minIndex);
  }

  return 0;
}

Node* MinHeap::ExtractMin()
{
  Node *minNode = this->heapArr[0];
  this->swapHeapNodes(0, this->heapSize - 1);
  this->heapSize--;
  this->minHeapify(0);

  return minNode;
}

int MinHeap::AddNode(int64_t symbol, uint64_t freq, Node *left, Node *right)
{
  assert (this->heapSize <= HEAP_CAPACITY);

  this->heapArr[this->heapSize++] = createNode(symbol, freq, left, right);

  for (int i = this->heapSize - 1; i > 0 && this->heapArr[this->GetParent(i)]->freq > this->heapArr[i]->freq;)
  {
    int j = this->GetParent(i);
    this->swapHeapNodes(i, j);
    i = j;
  }

  return 0;
}

int MinHeap::swapHeapNodes(int i, int j)
{
  Node *temp = this->heapArr[i];
  this->heapArr[i] = this->heapArr[j];
  this->heapArr[j] = temp;

  return 0;
}

int MinHeap::PrintHeap()
{
  for (int i = 0; i < this->heapSize; i++)
  {
    std::cout << this->heapArr[i]->symbol << " - " << this->heapArr[i]->freq << std::endl;
  }

  return 0;

}

MinHeap BuildHuffmanTree(MinHeap minHeap)
{
  while (minHeap.size() > 1)
  {
    Node *leftNode = minHeap.ExtractMin();
    Node *rightNode = minHeap.ExtractMin();
    minHeap.AddNode(-1, leftNode->freq + rightNode->freq, leftNode, rightNode);
  }

  return minHeap;
}

int GetHuffmanCode(Node *huffmanNode, std::map<int64_t, std::string> &encodedSymbols, std::string code)
{
  if (!huffmanNode->left && !huffmanNode->right)
  {
    encodedSymbols[huffmanNode->symbol] = code;
    return 0;
  }

  GetHuffmanCode(huffmanNode->left, encodedSymbols, code + "0");
  GetHuffmanCode(huffmanNode->right, encodedSymbols, code + "1");

  return 0;
}

bool comparator(std::pair<int64_t, std::string> left, std::pair<int64_t, std::string> right)
{
  return left.second.size() == right.second.size() ? left.first < right.first : left.second.size() < right.second.size();
}

int GetBitSize(int n)
{
  int bits = 1;
  while (pow(2, bits) <= n)
  {
    bits++;
  }

  return bits;
}

std::string GetBinaryString(int n, int bitSize)
{
  std::stringstream stream;
  std::string reverseBinary, binaryStr;

  do {
    stream << (char) n % 2;
    n /= 2;
  } while(n);

  if (bitSize != -1 && stream.str().size() < bitSize)
  {
    int paddingSize = bitSize - stream.str().size();
    while (paddingSize--)
    {
      stream << '0';
    }
  }

  reverseBinary = stream.str();
  binaryStr.assign(reverseBinary.rbegin(), reverseBinary.rend());

  return binaryStr;
}

std::map<int64_t, std::string> GetCanonicalCode(std::map<int64_t, std::string> huffmanCode)
{
  std::set<std::pair<int64_t, std::string>, bool(*)(std::pair<int64_t, std::string>, std::pair<int64_t, std::string>)> orderedHuffman(huffmanCode.begin(), huffmanCode.end(), &comparator);
  int currentVal, prevBitLength;
  currentVal = 0;
  prevBitLength = (int)orderedHuffman.begin()->second.size();

  std::map<int64_t, std::string> canonicalCode;

  for (std::pair<int64_t, std::string> current: orderedHuffman)
  {
    int shiftBits = current.second.size() - prevBitLength;
    currentVal = currentVal << shiftBits;
    canonicalCode[current.first] = GetBinaryString(currentVal, current.second.size());
    ++currentVal;
    prevBitLength = current.second.size();
  }

  return canonicalCode;
}

std::vector<int> GetBitLengthCodes(std::map<int64_t, std::string> canonicalCode)
{
  std::vector<int> bitCodes;

  for (auto it = canonicalCode.begin(); it != canonicalCode.end(); it++)
  {
    bitCodes.push_back(it->second.size());
  }

  return bitCodes;
}

std::map<int64_t, uint64_t> *GetFreqMap(std::vector<int64_t> &dataBlock, std::map<int64_t, uint64_t> *freqMap)
{
  if (freqMap == nullptr)
    freqMap = new std::map<int64_t, uint64_t>;

  for (int i = 0; i < dataBlock.size(); i++)
  {
    if (freqMap->find(dataBlock[i]) == freqMap->end())
    {
      freqMap->insert(std::make_pair(dataBlock[i], 1));
      continue;
    }
    freqMap->at(dataBlock[i])++;
  }

  return freqMap;
}


bool cmp(const std::pair<int64_t, uint64_t> &lhs, const std::pair<int64_t, uint64_t> &rhs)
{
  if (lhs.second == rhs.second) return lhs.first < rhs.first;
  return lhs.second < rhs.second;
}

}

void SC2Compressor::SetSamplingCnt(unsigned cnt)
{
  m_maxSamplingCnt = cnt;
}

unsigned SC2Compressor::compress(uint8_t *data, int req_size)
{
  std::vector<uint8_t> dataLine(data, data + req_size);
  const unsigned lineSize = dataLine.size();
  const unsigned uncompressedSize = BYTE * lineSize;

  // convert uint8_t vector into int64_t vector
  unsigned *words = (unsigned*)dataLine.data();
  std::vector<int64_t> dataBlock;
  dataBlock.resize(lineSize/WORD_GRAN);
  for (int i = 0; i < lineSize / WORD_GRAN; i++)
  {
    unsigned &elem = words[i];
    dataBlock[i] = elem;
  }

  if (m_samplingCnt < m_maxSamplingCnt) // count frequency
  {
    mp_symFreqMap = huffman::GetFreqMap(dataBlock, mp_symFreqMap);
    m_samplingCnt++;
  }
  else if (m_samplingCnt == m_maxSamplingCnt) // build huffmanTree
  {
    // erase least freq symbols from the symFreqMap
    // The vector below is ordered by freq
    if (mp_symFreqMap->size() > HEAP_CAPACITY)
    {
      std::vector<std::pair<int64_t, uint64_t>> symFreqVec(mp_symFreqMap->begin(), mp_symFreqMap->end());
      std::sort(symFreqVec.begin(), symFreqVec.end(), huffman::cmp);
      for (auto it = symFreqVec.begin(); it != symFreqVec.end(); it++)
      {
        int64_t &symbol = it->first;
        uint64_t &freq = it->second;

        mp_symFreqMap->erase(symbol);
        if (!(mp_symFreqMap->size() > HEAP_CAPACITY))
          break;
      }
    }

    huffman::MinHeap minHeap(*mp_symFreqMap);
    minHeap = huffman::BuildHuffmanTree(minHeap);
    huffman::GetHuffmanCode(minHeap.GetRoot()[0], m_huffmanCodes);
    m_samplingCnt++;
  }

  unsigned compressedSize = 0;
  for (int i = 0; i < dataBlock.size(); i++)
  {
    int64_t &symbol = dataBlock[i];
    auto it = m_huffmanCodes.find(symbol);
    if (it == m_huffmanCodes.end()) // Not found from the huffman tree
    {
      // +1b for uncompressed tag
      compressedSize += BYTE*WORD_GRAN + 1;
    }
    else
    {
      unsigned encodedSymbolSize = it->second.size();
      compressedSize += encodedSymbolSize;
    }
  }

  // stat
  m_uncomp_size += req_size * BYTE;
  m_comp_size += compressedSize;

  return compressedSize;
}







