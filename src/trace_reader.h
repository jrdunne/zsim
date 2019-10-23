/** $lic$
 * Copyright (C) 2012-2015 by Massachusetts Institute of Technology
 * Copyright (C) 2010-2013 by The Board of Trustees of Stanford University
 * Copyright (C) 2017 by Google (implemented by Grant Ayers)
 *
 * This file is part of zsim.
 *
 * zsim is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2.
 *
 * If you use this software in your research, we request that you reference
 * the zsim paper ("ZSim: Fast and Accurate Microarchitectural Simulation of
 * Thousand-Core Systems", Sanchez and Kozyrakis, ISCA-40, June 2013) as the
 * source of the simulator in any publications that use this software, and that
 * you send us a citation of your work.
 *
 * zsim is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef ZSIM_TRACE_READER_H
#define ZSIM_TRACE_READER_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <string>
#include <stdio.h>
#include <zlib.h>

extern "C" {
#include "public/xed/xed-interface.h"
}

enum class CustomOp : uint8_t {
  NONE,
  PREFETCH_CODE,
  PREFETCH_BLOCK
};

struct InstInfo {
  uint64_t pc;                    // instruction address
  const xed_decoded_inst_t *ins;  // XED info
  uint64_t pid;                   // process ID
  uint64_t tid;                   // thread ID
  uint64_t target;                // branch target
  uint64_t mem_addr[2];           // memory addresses
  bool mem_used[2];               // mem address usage flags
  CustomOp custom_op;             // Special or non-x86 ISA instruction
  bool taken;                     // branch taken
  bool unknown_type;              // No available decode info (presents a nop)
  bool valid;                     // True until the end of the sequence
  xed_category_enum_t cat;
};

class TraceReader {
 public:
    // Attemps to open trace file. ! operator returns false if failed
    TraceReader(const std::string & trace_file_path_);
    TraceReader();

    ~TraceReader();
    // A constructor that fails will cause operator! to return true
    virtual bool operator!();

    // Returns next instruction from the trace file
    virtual const InstInfo *nextInstruction();

    const std::string trace_file_path;
    unsigned long long count = 0;
    unsigned long long branch_count = 0;
    unsigned long long skipped = 0;

    static void make_nop(xed_decoded_inst_t * ins, uint8_t len, xed_state_t * xed_state_in);
    static void init_xed(xed_state_t * xed_state_in);

  private:
};

class ParsedIntelPTReader : public TraceReader {
  public:
    ParsedIntelPTReader(const std::string & trace_file_path_);
    ~ParsedIntelPTReader();

    InstInfo * nextInstruction() override;

    bool operator!() override;

    private:

    void open_gz_file();
    void init_buffer();
    void read_next_instr();
    void set_branch_taken(InstInfo & current, const InstInfo & next);
        
    typedef struct zsim_instr {
      uint64_t pc;
      // from here we have options to pre process the source binary and map it to a uint8_t
      uint8_t source_binary;
      uint8_t size;
      // can make this 14 and use the extra byte to store the binary info
      // will need to map lenght 15 instructions to something else
      // Although i think there is only one len = 15 instruction
      uint8_t insn[15];
    } zsim_instr;

    static constexpr size_t buffer_size = 32;

    gzFile input_gz = NULL;

    static constexpr size_t instr_size = sizeof(zsim_instr);

    zsim_instr instr;
    // 1MiB
    static constexpr size_t gz_buffer_size = 1048576;

    xed_state_t xed_state;

    // This will contain the 
    std::pair<InstInfo, xed_decoded_inst_t> instr_buffer[buffer_size];
    
    // current_instr_index + 1 this will be parsed on the next call the nextInstruction
    size_t next_instr_index = 0;
    // instruction to return from nextInstruction public function
    size_t current_instr_index = 0;

    bool end = false;
};

class IntelPTReader : public TraceReader {
    public:
    IntelPTReader(const std::string & trace_file_path_);
    ~IntelPTReader();

    // on end of file returns invalid InstInfo
    // parses line_buffer[next_line_index] into instr_buffer[next_instr_index]
    // updates instr_buffer[current_instr_index] based on instr_buffer[next_instr_index]
    // returns instr_buffer[current_instr_index]
    // increments current_instr_index and next_instr_index
    InstInfo * nextInstruction() override;

    bool operator!() override;

    private:

    void parse_instr(char * line, InstInfo * out, xed_decoded_inst_t * xed_ptr);
    char * get_next_line();
    void fill_line_buffer();
    void test_trace_file();
    void init_buffers();
    void make_custom_op();


    static constexpr size_t buffer_size = 256;
    static constexpr size_t line_size = 256;
    static constexpr size_t gz_buffer_size = 1048576;

    //const std::string trace_file_path;
    const std::string gunzip_command = "gunzip -c " + trace_file_path;

    gzFile trace_file_ptr = NULL;
    xed_state_t xed_state;

    char line_buffer[buffer_size][line_size];

    // next un parsed index in the line buffer 
    size_t next_line_index = 0;
    // TODO: Add caching for effeciency 

    // This will contain the 
    std::pair<InstInfo, xed_decoded_inst_t> instr_buffer[buffer_size];
    
    // current_instr_index + 1 this will be parsed on the next call the nextInstruction
    size_t next_instr_index = 0;
    // instruction to return from nextInstruction public function
    size_t current_instr_index = 0;

    bool end = false;

};

#endif
