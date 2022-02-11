#include "link_delay_queue.h"

//extern unsigned long long  gpu_sim_cycle;
//extern unsigned long long  gpu_tot_sim_cycle;

//extern gpgpu_sim *g_the_gpu;

// link_delay_queue
link_delay_queue::link_delay_queue(const char* nm,
    unsigned int size, unsigned int latency, 
    gpgpu_context *ctx)
  : m_name(nm), m_size(size), m_latency(latency), m_ctx(ctx)
{
  assert(latency);

  m_arr_size = size + latency;

  m_data_array = new mem_fetch*[m_arr_size];
  m_is_head_array = new bool[m_arr_size];
  m_is_tail_array = new bool[m_arr_size];

  m_wr_ptr = latency;
  m_rd_ptr = 0;

  for (unsigned i=0; i<latency; i++) {
    m_data_array[i] = NULL;
    m_is_head_array[i] = false;
    m_is_tail_array[i] = false;
  }
}

link_delay_queue::~link_delay_queue()
{
  delete [] m_data_array;
  delete [] m_is_head_array;
  delete [] m_is_tail_array;
}

void link_delay_queue::push(bool is_head, bool is_tail, mem_fetch* mf)
{
  m_data_array[m_wr_ptr] = mf;
  m_is_head_array[m_wr_ptr] = is_head;
  m_is_tail_array[m_wr_ptr] = is_tail;
  m_wr_ptr = (m_wr_ptr+1) % m_arr_size;
}

mem_fetch* link_delay_queue::pop()
{
  mem_fetch *result = NULL;
  if (m_is_tail_array[m_rd_ptr]) {
    result = m_data_array[m_rd_ptr];
    //printf("MDQ::pop  %p %d\n", result, m_rd_ptr);
  }
  m_rd_ptr = (m_rd_ptr+1) % m_arr_size;
  return result;
}

void link_delay_queue::print() const
{
  printf("@%8lld %s : %d, %d\n", m_ctx->the_gpgpusim->g_the_gpu->gpu_sim_cycle, m_name, m_rd_ptr, m_wr_ptr);
}

const char* link_delay_queue::get_name()
{
  return m_name;
}

// compressed_link_delay_queue
compressed_link_delay_queue::compressed_link_delay_queue(const char* nm,
    unsigned int size, unsigned int latency,
    gpgpu_context *ctx)
  : m_name(nm), m_size(size), m_latency(latency), m_ctx(ctx)
{
  assert(latency);

  m_arr_size = size + latency;

  m_data_array = new mem_fetch*[m_arr_size];
  m_size_array = new unsigned[m_arr_size];
  m_time_array = new unsigned long long[m_arr_size];

  m_wr_ptr = 0;
  m_rd_ptr = 0;

  for (unsigned i=0; i<m_arr_size; i++) {
    m_data_array[i] = NULL;
    m_size_array[i] = 0;
    m_time_array[i] = 0ull;
  }
}

compressed_link_delay_queue::~compressed_link_delay_queue()
{
  delete [] m_data_array;
  delete [] m_size_array;
  delete [] m_time_array;
}

void compressed_link_delay_queue::push(mem_fetch* mf, unsigned size)
{
  //if (mf!=NULL) {
  //    printf("MDQ::push %p %d %d %d\n", mf, is_head, is_tail, m_wr_ptr);
  //}
  m_data_array[m_wr_ptr] = mf;
  m_size_array[m_wr_ptr] = size;
  m_time_array[m_wr_ptr] = m_ctx->the_gpgpusim->g_the_gpu->gpu_sim_cycle
    + m_ctx->the_gpgpusim->g_the_gpu->gpu_tot_sim_cycle;
  m_wr_ptr = (m_wr_ptr+1) % m_arr_size;
}

std::pair<mem_fetch *, unsigned> compressed_link_delay_queue::top()
{
  mem_fetch* mf = m_data_array[m_rd_ptr];
  unsigned size = 0;
  if (mf!=NULL) {
    unsigned long long time = m_time_array[m_rd_ptr];
    if ((m_ctx->the_gpgpusim->g_the_gpu->gpu_sim_cycle
         +  m_ctx->the_gpgpusim->g_the_gpu->gpu_tot_sim_cycle) > (time + m_latency)) {
      size = m_size_array[m_rd_ptr];
    } else {
      mf = NULL;
    }
  }
  return std::make_pair(mf, size);
}

void compressed_link_delay_queue::pop()
{
  m_data_array[m_rd_ptr] = NULL;
  m_rd_ptr = (m_rd_ptr+1) % m_arr_size;
}

void compressed_link_delay_queue::print() const
{
  printf("@%8lld %s : %d, %d\n", m_ctx->the_gpgpusim->g_the_gpu->gpu_sim_cycle, m_name, m_rd_ptr, m_wr_ptr);
}

const char* compressed_link_delay_queue::get_name()
{
  return m_name;
}
