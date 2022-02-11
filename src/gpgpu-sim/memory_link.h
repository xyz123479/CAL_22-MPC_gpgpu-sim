#ifndef MEMORY_LINK_H
#define MEMORY_LINK_H

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <queue>
#include <set>
#include "../abstract_hardware_model.h"
#include "../cuda-sim/memory.h"
#include "gpu-sim.h"
#include "comp.h"
#include "oneway_link.h"

class memory_link {
public:
  memory_link(const char* nm,
      unsigned link_latency,
      const struct memory_config *config,
      gpgpu_context *ctx);
  ~memory_link();

  // methods related to the down link
  void dnlink_step(double n_flit);
  bool dnlink_full(unsigned mem_id);
  void dnlink_push(unsigned mem_id, mem_fetch *mf);
  bool dnlink_empty(unsigned mem_id);
  mem_fetch *dnlink_top(unsigned mem_id);
  void dnlink_pop(unsigned mem_id);

  // methods related to the up link
  void uplink_step(double n_flit);
  bool uplink_full(unsigned mem_id);
  void uplink_push(unsigned mem_id, mem_fetch *mf);
  bool uplink_empty(unsigned mem_id);
  mem_fetch *uplink_top(unsigned mem_id);
  void uplink_pop(unsigned mem_id);

  // methods related to the printing
  void print() const;
  void print_stat() const;

protected:
  double dnlink_remainder;
  double uplink_remainder;

  const struct memory_config *m_config;
  char m_nm[256];
  class oneway_link *m_dn;
  class oneway_link *m_up;

  gpgpu_context *m_ctx;
};

class compressed_memory_link : public memory_link {
public:
//  compressed_memory_link(const char* nm, unsigned int latency,
//                         const struct memory_config *config,
//                         gpgpu_context *ctx);
  compressed_memory_link(const char* nm,
     unsigned link_latency, unsigned comp_latency, unsigned decomp_latency,
     const struct memory_config *config,
     gpgpu_context *ctx);
};

#endif
