// Copyright (c) 2009-2011, Tor M. Aamodt
// The University of British Columbia
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
// Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution. Neither the name of
// The University of British Columbia nor the names of its contributors may be
// used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "mem_fetch.h"
#include "gpu-sim.h"
#include "mem_latency_stat.h"
#include "shader.h"
#include "visualizer.h"
#include "gpu-sim.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <queue>
#include <set>
#include "../abstract_hardware_model.h"
#include "../cuda-sim/memory.h"

// JIN
#include "../../libcuda/gpgpu_context.h"
#include "../gpgpusim_entrypoint.h"
extern FILE *data_trace_output_FP;

extern gpgpu_context *ctx;

unsigned mem_fetch::sm_next_mf_request_uid = 1;

mem_fetch::mem_fetch(const mem_access_t &access, const warp_inst_t *inst,
                     unsigned ctrl_size, unsigned wid, unsigned sid,
                     unsigned tpc, const memory_config *config,
                     unsigned long long cycle, mem_fetch *m_original_mf,
                     mem_fetch *m_original_wr_mf)
    : m_access(access)

{
  m_request_uid = sm_next_mf_request_uid++;
  m_access = access;
  if (inst) {
    m_inst = *inst;
    assert(wid == m_inst.warp_id());
  }
  m_data_size = access.get_size();
  m_ctrl_size = ctrl_size;
  m_sid = sid;
  m_tpc = tpc;
  m_wid = wid;

  config->m_address_mapping.addrdec_tlx(access.get_addr(), &m_raw_addr);
  m_partition_addr =
      config->m_address_mapping.partition_address(access.get_addr());
  m_type = m_access.is_write() ? WRITE_REQUEST : READ_REQUEST;
  m_timestamp = cycle;
  m_timestamp2 = 0;
  m_status = MEM_FETCH_INITIALIZED;
  m_status_change = cycle;
  m_mem_config = config;
  icnt_flit_size = config->icnt_flit_size;
  original_mf = m_original_mf;
  original_wr_mf = m_original_wr_mf;
  m_inst_count[0] = 0; //song
  if (m_original_mf) {
    m_raw_addr.chip = m_original_mf->get_tlx_addr().chip;
    m_raw_addr.sub_partition = m_original_mf->get_tlx_addr().sub_partition;
  }
}

mem_fetch::~mem_fetch() { m_status = MEM_FETCH_DELETED; }

#define MF_TUP_BEGIN(X) static const char *Status_str[] = {
#define MF_TUP(X) #X
#define MF_TUP_END(X) \
  }                   \
  ;
#include "mem_fetch_status.tup"
#undef MF_TUP_BEGIN
#undef MF_TUP
#undef MF_TUP_END

void mem_fetch::print(FILE *fp, bool print_inst) const {
  if (this == NULL) {
    fprintf(fp, " <NULL mem_fetch pointer>\n");
    return;
  }
  fprintf(fp, "  mf: uid=%6u, sid%02u:w%02u, part=%u, ", m_request_uid, m_sid,
          m_wid, m_raw_addr.chip);
  m_access.print(fp);
  if ((unsigned)m_status < NUM_MEM_REQ_STAT)
    fprintf(fp, " status = %s (%llu), ", Status_str[m_status], m_status_change);
  else
    fprintf(fp, " status = %u??? (%llu), ", m_status, m_status_change);
  if (!m_inst.empty() && print_inst)
    m_inst.print(fp);
  else
    fprintf(fp, "\n");
}

void mem_fetch::write_data(unsigned char *input_data) {
  memcpy(data, input_data , get_data_size());
}

// Added by song
void mem_fetch::print_data(int type) const {
  if(data_trace_output_FP == NULL) {
    // unsigned char buffer[128];
    if (get_type() == 0) {
       printf("REQ,");
  
      if (type == 0) printf("DRAM,");
      else if (type == 1) printf("RAM,");
  
      printf("READ,");
      printf("%llu,", m_status_change);
      printf("%02d,", get_tpc());
      printf("%02d,", get_sid());
      printf("%02d,", get_wid());
      printf("0x%08x,", get_pc());
      printf("%d,", m_inst_count[0]);
      printf("0x%08llx,", get_addr());
      printf("%d,", get_data_size());
      printf("%d,", get_access_type());
      printf("RAW,0x%03x,%03d,%03d,0x%03x,",
              m_raw_addr.row, m_raw_addr.chip, m_raw_addr.bk, m_raw_addr.col);
      for (int i = 0; i < get_data_size(); i++) printf("%02x,", data[i]);
      printf("\n");
    }
    else {
      for (int j = 0; j < 4; j++) {
  
        if (mm_tpc[j] == -1 || mm_sid[j] == -1 || mm_wid[j] == -1) continue;
  
        printf("REQ,");
        if (type == 0) printf("DRAM,");
        else if (type == 1) printf("RAM,");
        
        printf("WRITE,");
        printf("%llu,", m_status_change);
        printf("%02d,", mm_tpc[j]);
        printf("%02d,", mm_sid[j]);
        printf("%02d,", mm_wid[j]);
        printf("0x%08x,", get_pc());
        printf("%d,", m_inst_count[j]);
        printf("0x%08llx,", get_addr() + j * 32);
        printf("%d,", 32);
        printf("%d,", get_access_type());
        printf("RAW,0x%03x,%03d,%03d,0x%03x,", m_raw_addr.row,
                m_raw_addr.chip, m_raw_addr.bk, m_raw_addr.col);
        for (int i = 32*j; i < 32*j+32 ; i++) printf("%02x,", data[i]);
        printf("\n");
      }
    }
    return ;
  }
  else {
    // JIN
    if (get_type() == 0) {
      char *req_pos = (char *)malloc(sizeof(char) * 5);
      if (type == 0)      strcpy(req_pos, "DRAM");
      else if (type == 1) strcpy(req_pos, " RAM");

	  char rw = 'r';
	  unsigned long long cycle = m_status_change;
	  unsigned int cid = get_tpc();
	  unsigned int sid = get_sid();
	  unsigned int wid = get_wid();
	  unsigned int pc = get_pc();
	  unsigned int inst_cnt = m_inst_count[0];
	  unsigned long long mem_addr = get_addr();
	  unsigned int req_type = get_access_type();
	  unsigned int row  = m_raw_addr.row;
	  unsigned int chip = m_raw_addr.chip;
	  unsigned int bank = m_raw_addr.bk;
	  unsigned int col  = m_raw_addr.col;
	  unsigned int req_size = get_data_size();
	  unsigned char *req_data = (unsigned char *)malloc(sizeof(unsigned char) * req_size);
	  for(int i = 0; i < req_size; i++) req_data[i] = data[i];

	  fwrite(req_pos,   sizeof(char),       4, data_trace_output_FP);
	  fwrite(&rw,       sizeof(char),       1, data_trace_output_FP);
	  fwrite(&cycle,    sizeof(long long),  1, data_trace_output_FP);
	  fwrite(&cid,      sizeof(int),        1, data_trace_output_FP);
	  fwrite(&sid,      sizeof(int),        1, data_trace_output_FP);
	  fwrite(&wid,      sizeof(int),        1, data_trace_output_FP);
	  fwrite(&pc,       sizeof(int),        1, data_trace_output_FP);
	  fwrite(&inst_cnt, sizeof(int),        1, data_trace_output_FP);
	  fwrite(&mem_addr, sizeof(long long),  1, data_trace_output_FP);
	  fwrite(&req_type, sizeof(int),        1, data_trace_output_FP);
	  fwrite(&row,      sizeof(int),        1, data_trace_output_FP);
	  fwrite(&chip,     sizeof(int),        1, data_trace_output_FP);
	  fwrite(&bank,     sizeof(int),        1, data_trace_output_FP);
	  fwrite(&col,      sizeof(int),        1, data_trace_output_FP);
	  fwrite(&req_size, sizeof(int),        1, data_trace_output_FP);
	  fwrite(req_data,  sizeof(char), req_size, data_trace_output_FP);

	  free(req_pos);
	  free(req_data);
	}
	else {
      for(int j = 0; j < 4; j++) {
        if (mm_tpc[j] == -1 || mm_sid[j] == -1 || mm_wid[j] == -1) continue;
		char *req_pos = (char *)malloc(sizeof(char) * 5);
        if (type == 0)      strcpy(req_pos, "DRAM");
        else if (type == 1) strcpy(req_pos, " RAM");
    
   	    char rw = 'w';
   	    unsigned long long cycle = m_status_change;
   	    unsigned int cid = get_tpc();
   	    unsigned int sid = get_sid();
   	    unsigned int wid = get_wid();
        unsigned int pc = get_pc();
        unsigned int inst_cnt = m_inst_count[0];
        unsigned long long mem_addr = get_addr() + j * 32;
        unsigned int req_type = get_access_type();
        unsigned int row  = m_raw_addr.row;
        unsigned int chip = m_raw_addr.chip;
        unsigned int bank = m_raw_addr.bk;
        unsigned int col  = m_raw_addr.col;
        unsigned int req_size = 32;
        unsigned char *req_data = (unsigned char *)malloc(sizeof(unsigned char) * req_size);
		for (int i = 0; i < req_size; i++) req_data[i] = data[i + req_size * j];
        
        fwrite(req_pos,   sizeof(char),       4, data_trace_output_FP);
        fwrite(&rw,       sizeof(char),       1, data_trace_output_FP);
        fwrite(&cycle,    sizeof(long long),  1, data_trace_output_FP);
        fwrite(&cid,      sizeof(int),        1, data_trace_output_FP);
        fwrite(&sid,      sizeof(int),        1, data_trace_output_FP);
        fwrite(&wid,      sizeof(int),        1, data_trace_output_FP);
        fwrite(&pc,       sizeof(int),        1, data_trace_output_FP);
        fwrite(&inst_cnt, sizeof(int),        1, data_trace_output_FP);
        fwrite(&mem_addr, sizeof(long long),  1, data_trace_output_FP);
        fwrite(&req_type, sizeof(int),        1, data_trace_output_FP);
        fwrite(&row,      sizeof(int),        1, data_trace_output_FP);
        fwrite(&chip,     sizeof(int),        1, data_trace_output_FP);
        fwrite(&bank,     sizeof(int),        1, data_trace_output_FP);
        fwrite(&col,      sizeof(int),        1, data_trace_output_FP);
        fwrite(&req_size, sizeof(int),        1, data_trace_output_FP);
        fwrite(req_data,  sizeof(char), req_size, data_trace_output_FP);

		free(req_pos);
		free(req_data);
	  }
	}
    return ;
  }
}

// Added by song
void mem_fetch::print_line(unsigned dqbytes) const {
  printf("IN,");
  printf("DRAM,");

  if (get_type() == 0) {
    printf("READ,");
    printf("%llu,", m_status_change);
    printf("%02d,", get_tpc());
    printf("%02d,", get_sid());
    printf("%02d,", get_wid());
    printf("0x%08x,", get_pc());
    printf("%d,", m_inst_count[0]);
    printf("0x%08llx,", get_addr());
    printf("%d,", get_data_size());
    printf("%d,", get_access_type());
    for (int i = 0; i < get_data_size(); i++) printf("%02x,", data[i]);
    printf("\n");
  }
  else {

    if (mm_tpc[dqbytes/32] == -1 || mm_sid[dqbytes/32] == -1 || mm_wid[dqbytes/32] == -1) return;

    printf("WRITE,");
    printf("%llu,", m_status_change);
    printf("%02d,", mm_tpc[dqbytes/32]);
    printf("%02d,", mm_sid[dqbytes/32]);
    printf("%02d,", mm_wid[dqbytes/32]);
    printf("0x%08x,", get_pc());
    printf("%d,", m_inst_count[dqbytes/32]);
    printf("0x%08llx,", get_addr() + dqbytes);
    printf("%d,", 32);
    printf("%d,", get_access_type());
    for (int i = dqbytes; i < dqbytes + 32; i++) printf("%02x,", data[i]);
    printf("\n");
  }
}
void mem_fetch::set_status(enum mem_fetch_status status,
                           unsigned long long cycle) {
  m_status = status;
  m_status_change = cycle;
}

bool mem_fetch::isatomic() const {
  if (m_inst.empty()) return false;
  return m_inst.isatomic();
}

void mem_fetch::do_atomic() { m_inst.do_atomic(m_access.get_warp_mask()); }

bool mem_fetch::istexture() const {
  if (m_inst.empty()) return false;
  return m_inst.space.get_type() == tex_space;
}

bool mem_fetch::isconst() const {
  if (m_inst.empty()) return false;
  return (m_inst.space.get_type() == const_space) ||
         (m_inst.space.get_type() == param_space_kernel);
}

/// Returns number of flits traversing interconnect. simt_to_mem specifies the
/// direction
unsigned mem_fetch::get_num_flits(bool simt_to_mem) {
  unsigned sz = 0;
  // If atomic, write going to memory, or read coming back from memory, size =
  // ctrl + data. Else, only ctrl
  if (isatomic() || (simt_to_mem && get_is_write()) ||
      !(simt_to_mem || get_is_write()))
    sz = size();
  else
    sz = get_ctrl_size();

  return (sz / icnt_flit_size) + ((sz % icnt_flit_size) ? 1 : 0);
}
