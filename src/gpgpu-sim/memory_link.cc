#include "memory_link.h"

memory_link::memory_link(const char* nm,
    unsigned link_latency,
    const struct memory_config *config,
    gpgpu_context *ctx)
  : m_config(config), m_ctx(ctx)
{
  strcpy(m_nm, nm);

  char link_nm[256];
  sprintf(link_nm, "%s.dn", nm);
  m_dn = new oneway_link(link_nm,
      link_latency,
      config->m_n_mem * config->m_n_sub_partition_per_memory_channel,
      config->m_n_mem * config->m_n_sub_partition_per_memory_channel,
      ctx);
  sprintf(link_nm, "%s.up", nm);
  m_up = new oneway_link(link_nm,
      link_latency,
      config->m_n_mem * config->m_n_sub_partition_per_memory_channel,
      config->m_n_mem * config->m_n_sub_partition_per_memory_channel,
      ctx);

  dnlink_remainder = 0.;
  uplink_remainder = 0.;
}

memory_link::~memory_link()
{
  delete m_dn;
  delete m_up;
}

void memory_link::dnlink_step(double n_flit)
{
  unsigned flit_rounded = (unsigned) (n_flit + dnlink_remainder);
  m_dn->step(flit_rounded);
  dnlink_remainder = (n_flit + dnlink_remainder) - flit_rounded;
}

bool memory_link::dnlink_full(unsigned mem_id)
{ 
  return m_dn->full(mem_id);
}

void memory_link::dnlink_push(unsigned mem_id, mem_fetch *mf)
{ 
  m_dn->push(mem_id, mf);
}

bool memory_link::dnlink_empty(unsigned mem_id)
{ 
  return m_dn->empty(mem_id);
}
mem_fetch* memory_link::dnlink_top(unsigned mem_id)
{ 
  return m_dn->top(mem_id);
}
void memory_link::dnlink_pop(unsigned mem_id)
{ 
  m_dn->pop(mem_id);
}

void memory_link::uplink_step(double n_flit)
{
  unsigned flit_rounded = (unsigned) (n_flit + uplink_remainder);
  m_up->step(flit_rounded);
  uplink_remainder = (n_flit + uplink_remainder) - flit_rounded;
}
bool memory_link::uplink_full(unsigned mem_id)
{ 
  return m_up->full(mem_id); 
}
void memory_link::uplink_push(unsigned mem_id, mem_fetch *mf)
{ 
  m_up->push(mem_id, mf);
}
bool memory_link::uplink_empty(unsigned mem_id)
{
  return m_up->empty(mem_id);
}
mem_fetch* memory_link::uplink_top(unsigned mem_id)
{
  return m_up->top(mem_id);
}
void memory_link::uplink_pop(unsigned mem_id)
{
  m_up->pop(mem_id);
}

void memory_link::print() const
{
  m_dn->print();
  m_up->print();
}
void memory_link::print_stat() const
{
  m_dn->print_stat();
  m_up->print_stat();
}

//compressed_memory_link::compressed_memory_link(const char* nm, unsigned int latency, const struct memory_config *config,
//                                               gpgpu_context *ctx)
//  : memory_link(nm, 1, config, ctx)
//{
//  strcpy(m_nm, nm);
//
//  char link_nm[256];
//  sprintf(link_nm, "%s.dn", nm);
//  m_dn = new compressed_dn_link(link_nm, latency,
//      config->m_n_mem * config->m_n_sub_partition_per_memory_channel,
//      config->m_n_mem * config->m_n_sub_partition_per_memory_channel,
//      ctx);
//  sprintf(link_nm, "%s.up", nm);
//  m_up = new compressed_up_link(link_nm, latency,
//      config->m_n_mem * config->m_n_sub_partition_per_memory_channel,
//      config->m_n_mem * config->m_n_sub_partition_per_memory_channel,
//      ctx);
//}

compressed_memory_link::compressed_memory_link(const char* nm,
    unsigned link_latency, unsigned comp_latency, unsigned decomp_latency,
    const struct memory_config *config,
    gpgpu_context *ctx)
  : memory_link(nm, 1, config, ctx)
{
  strcpy(m_nm, nm);
  
  char link_nm[256];
  sprintf(link_nm, "%s.dn", nm);
  m_dn = new compressed_dn_link(link_nm,
      link_latency, comp_latency,
      config->m_n_mem * config->m_n_sub_partition_per_memory_channel,
      config->m_n_mem * config->m_n_sub_partition_per_memory_channel,
      ctx);
  sprintf(link_nm, "%s.up", nm);
  m_up = new compressed_up_link(link_nm,
      link_latency, decomp_latency,
      config->m_n_mem * config->m_n_sub_partition_per_memory_channel,
      config->m_n_mem * config->m_n_sub_partition_per_memory_channel,
      ctx);
}
