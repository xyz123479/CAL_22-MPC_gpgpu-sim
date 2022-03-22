#ifndef ONEWAY_LINK_H
#define ONEWAY_LINK_H

#include <iostream>
#include <queue>
#include "../gpgpusim_entrypoint.h"
#include "gpu-sim.h"
#include "link_delay_queue.h"

// -------------------------------------------------------------------------
// Base oneway link interface
// -------------------------------------------------------------------------
class oneway_link {
public:
  oneway_link(const char* nm,
      unsigned link_latency,
      unsigned src_cnt, unsigned dst_cnt,
      gpgpu_context *ctx);
  ~oneway_link();

  virtual void push(unsigned src_id, mem_fetch *mf);
  void pop(unsigned dst_id);
  mem_fetch *top(unsigned dst_id);

  bool full(unsigned src_id);
  bool empty(unsigned dst_id);

  void step(unsigned n_flit);
  virtual void step_link_pop(unsigned n_flit);
  virtual void step_link_push(unsigned n_flit);

  void print() const;
  void print_stat() const;

  unsigned get_dst_id(mem_fetch *mf);
protected:
//  static const unsigned FLIT_SIZE = 128;      // max packet length in terms of FLIT
//  static const unsigned HT_OVERHEAD = 128;    // head+tail overheads
  //static const unsigned MAX_FLIT_CNT = 17;
  //static const unsigned WIDTH = 32;

  char m_name[256];
  link_delay_queue *queue;
  unsigned m_src_cnt, m_dst_cnt;
  unsigned m_cur_src_id;
  unsigned m_cur_flit_cnt;
  unsigned m_cur_flit_cnt_nodata;
  unsigned m_packet_bit_size;
  unsigned long long m_total_flit_cnt;
  unsigned long long m_transfer_flit_cnt;
  unsigned long long m_transfer_single_flit_cnt;
  unsigned long long m_transfer_multi_flit_cnt;
  std::queue<mem_fetch *> *m_ready_list;
  std::queue<mem_fetch *> *m_complete_list;

  gpgpu_context *m_ctx;
};

// -------------------------------------------------------------------------
// Compressed oneway link interface
// -------------------------------------------------------------------------
class compressed_oneway_link : public oneway_link {
public:
  compressed_oneway_link(const char* nm,
      unsigned link_latency,
      unsigned src_cnt, unsigned dst_cnt,
      gpgpu_context *ctx);

  void push(unsigned mem_id, mem_fetch *mf);
  bool push(mem_fetch *mf, unsigned packet_bit_size, unsigned& n_sent_flit_cnt, unsigned n_flit, bool update = true);

public:
  std::queue<mem_fetch *> *m_ready_long_list;
  std::queue<mem_fetch *> *m_ready_short_list;
  compressed_link_delay_queue *m_ready_compressed;
//  compressed_link_delay_queue *m_ready_decompressed;
  unsigned m_cur_comp_id;
  bool is_current_long;
  unsigned m_leftover;
  unsigned m_leftover_nodata;
};

class compressed_dn_link : public compressed_oneway_link {
public:
  compressed_dn_link(const char* nm,
      unsigned comp_link_latency,
      unsigned src_cnt, unsigned dst_cnt,
      gpgpu_context *ctx);

  void step_link_push(unsigned n_flit);
  void step_link_pop(unsigned n_flit);
};

class compressed_up_link : public compressed_oneway_link {
public:
  compressed_up_link(const char* nm,
      unsigned comp_link_latency,
      unsigned src_cnt, unsigned dst_cnt, 
      gpgpu_context *ctx);

  void step_link_push(unsigned n_flit);
};


#endif
