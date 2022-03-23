#ifndef __COMP_H__
#define __COMP_H__

#include <iostream>
#include <string>
#include <map>
#include <unordered_map>
#include <list>
#include <vector>
#include <algorithm>
#include <deque>
#include <assert.h>

#include "./MPCmodules/CompressionModule.h"
#include "./MPCmodules/FPCModule.h"
#include "../abstract_hardware_model.h"

//--------------------------------------------------------------------
//#define LSIZE (256)     // in bits
//#define LSIZE (512)     // in bits
//#define LSIZE (1024)    // in bits

#define BYTE (8)
//------------------------------------------------------------------------------
typedef uint8_t             UINT8;
typedef uint16_t            UINT16;
typedef uint32_t            UINT32;
typedef uint64_t            UINT64;
typedef int8_t              INT8;
typedef int16_t             INT16;
typedef int32_t             INT32;
typedef int64_t             INT64;
typedef float               FLT32;
typedef double              FLT64;

typedef unsigned long long  CNT;
typedef unsigned            LENGTH;
typedef INT64               KEY;

//static const UINT32 _MAX_BYTES_PER_LINE     = LSIZE/8;
//static const UINT32 _MAX_WORDS_PER_LINE     = LSIZE/16;
//static const UINT32 _MAX_DWORDS_PER_LINE    = LSIZE/32;
//static const UINT32 _MAX_QWORDS_PER_LINE    = LSIZE/64;
//static const UINT32 _MAX_FLOATS_PER_LINE    = LSIZE/32;
//static const UINT32 _MAX_DOUBLES_PER_LINE   = LSIZE/64;

typedef struct { UINT64 m : 52; UINT64 e : 11; UINT64 s : 1; } FLT64_P;
typedef struct { UINT32 m : 23; UINT32 e : 8;  UINT32 s : 1; } FLT32_P;
//------------------------------------------------------------------------------
//typedef union CACHELINE_DATA {
//    UINT8   byte[_MAX_BYTES_PER_LINE];
//    UINT16  word[_MAX_WORDS_PER_LINE];
//    UINT32  dword[_MAX_DWORDS_PER_LINE];
//    UINT64  qword[_MAX_QWORDS_PER_LINE];
//
//    INT8    s_byte[_MAX_BYTES_PER_LINE];
//    INT16   s_word[_MAX_WORDS_PER_LINE];
//    INT32   s_dword[_MAX_DWORDS_PER_LINE];
//    INT64   s_qword[_MAX_QWORDS_PER_LINE];
//
//    FLT32   flt[_MAX_FLOATS_PER_LINE];
//    FLT64   dbl[_MAX_DOUBLES_PER_LINE];
//    FLT32_P flt_p[_MAX_FLOATS_PER_LINE];
//    FLT64_P dbl_p[_MAX_DOUBLES_PER_LINE];
//} CACHELINE_DATA;
//------------------------------------------------------------------------------
typedef unsigned long long virtual_stream_id;
//------------------------------------------------------------------------------
//using namespace std;

//------------------------------------------------------------------------------
class compressor {
public:
  compressor() {}
  void print() {
    printf("Total data size = %llu\n", m_uncomp_size);
    printf("Total data compressed size = %llu\n", m_comp_size);
    double comp_ratio = (double)m_uncomp_size / (double)m_comp_size;
    printf("Compression ratio = %lf\n", comp_ratio);
  }

  virtual unsigned compress(uint8_t* data, int req_size) = 0;

public:
  uint64_t m_uncomp_size = 0;
  uint64_t m_comp_size = 0;
};
//------------------------------------------------------------------------------
extern char *configPath;
class MPCompressor : public compressor
{
public:
  /*** constructors ***/
  MPCompressor()
  {
    std::string configPathStr(configPath);
    parseConfig(configPathStr);
  }

  /*** getters ***/
  int GetCachelineSize()  { return m_LineSize; }
  int GetNumModules()     { return m_NumModules; }
  int GetNumClusters()    { return m_NumClusters; }

  /*** methods ***/
  virtual unsigned compress(uint8_t *data, int req_size);

private :
  void parseConfig(std::string &configPath);
  unsigned compressLineOnlyAllZero(std::vector<uint8_t> &dataLine);
  unsigned compressLineAllWordSame(std::vector<uint8_t> &dataLine);

  unsigned checkAllZeros(const int chosenCompModule, bool &isAllZeros, std::vector<uint8_t> &dataLine);
  unsigned checkAllWordSame(const int chosenCompModule, bool &isAllWordSame, std::vector<uint8_t> &dataLine);
  unsigned checkOtherPatterns(const int numStartingModule, std::vector<uint8_t> &dataLine);

private:
  /*** members ***/
  unsigned (MPCompressor::*compressLine)(std::vector<uint8_t> &dataLine);

  int m_LineSize;
  std::map<int, int> m_EncodingBits;

  std::vector<CompressionModule*> m_CompModules;
  FPCModule m_CommonEncoder;
  int m_NumModules;
  int m_NumClusters;
};

//------------------------------------------------------------------------------
#define CPACK_WORDSIZE 4
#define CPACK_DICTSIZE 64
#define CPACK_NUM_ENTRY (CPACK_DICTSIZE/CPACK_WORDSIZE)
#define CPACK_NUM_PATTERNS 6

class CachePacker : public compressor
{
public:
  CachePacker()
  {
    // init dictionary
    for (int i = 0; i < CPACK_NUM_ENTRY; i++)
    {
      uint8_t *init = new uint8_t[CPACK_WORDSIZE];
      for (int j = 0; j < CPACK_WORDSIZE; j++)
        init[j] = 0;
      m_Dictionary.push_back(init);
    }
  }

  virtual unsigned compress(uint8_t *data, int req_size); 

private:
  std::deque<uint8_t*> m_Dictionary;

  // 0. zzzz (00)         : 2
  // 1. xxxx (01)BBBB     : 34
  // 2. mmmm (10)bbbb     : 6
  // 3. mmxx (1100)bbbbBB : 24
  // 4. zzzx (1100)B      : 12
  // 5. mmmx (1110)bbbbB  : 16
  const unsigned m_PatternLength[CPACK_NUM_PATTERNS] = { 2, 34, 6, 24, 12, 16 };
};

extern compressor *g_comp;

#endif /* __COMP_H__*/
