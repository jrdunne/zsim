#include "trace_reader.h"
#include <cstring>
#include <memory>
#include <iostream>
#include "wrapped_pin.h"
#include "log.h"

#define XC(cat) (XED_CATEGORY_##cat)
using std::string;

static bool xed_init_done = false;
static std::mutex xed_init_lock;

TraceReader::TraceReader() {}
TraceReader::TraceReader(const std::string & trace_file_path_) : trace_file_path(trace_file_path_) {}
TraceReader::~TraceReader() {}
bool TraceReader::operator!() { return false; }
const InstInfo *TraceReader::nextInstruction() { return nullptr; }

void TraceReader::make_nop(xed_decoded_inst_t * ins, uint8_t len, xed_state_t * xed_state_in) {
    // A 10-to-15-byte NOP instruction (direct XED support is only up to 9)
    static const char *nop15 =
      "\x66\x66\x66\x66\x66\x66\x2e\x0f\x1f\x84\x00\x00\x00\x00\x00";

    xed_decoded_inst_zero_set_mode(ins, xed_state_in);
    xed_error_enum_t res;

    // The reported instruction length must be 1-15 bytes
    len &= 0xf;
    assert(len > 0);
    if (len > 9) {
        int offset = 15 - len;
        const uint8_t *pos = reinterpret_cast<const uint8_t *>(nop15 + offset);
        res = xed_decode(ins, pos, 15 - offset);
    } else {
        uint8_t buf[10];
        res = xed_encode_nop(&buf[0], len);
        if (res != XED_ERROR_NONE) {
        warn("XED NOP encode error: %s", xed_error_enum_t2str(res));
        }
        res = xed_decode(ins, buf, sizeof(buf));
    }
    if (res != XED_ERROR_NONE) {
        warn("XED NOP decode error: %s", xed_error_enum_t2str(res));
    }
}

void TraceReader::init_xed(xed_state_t * xed_state_in) {
    // only init the tables once
    std::unique_lock<std::mutex> xed_lock(xed_init_lock);
    if (!xed_init_done) {
        xed_tables_init();
        xed_init_done = true;
    }
    // Set the XED machine mode to 64-bit
    xed_state_init2(xed_state_in, XED_MACHINE_MODE_LONG_64, XED_ADDRESS_WIDTH_64b);
}

////////////////////////////////////////////////
// IntelPTReader 
////////////////////////////////////////////////

IntelPTReader::IntelPTReader(const std::string & trace_file_path_) : TraceReader(trace_file_path_) {
    test_trace_file();
    init_xed(&xed_state);
    init_buffers();
}

IntelPTReader::~IntelPTReader() {}


bool IntelPTReader::operator!() {
    return trace_file_ptr == NULL;
}

InstInfo * IntelPTReader::nextInstruction() {
    // parse next line into InstInfo as next_instr_index
    // update instr_buffer[current_] with info from next instruction
    // 

    //if (count % 100000 == 0) {
    //    make_custom_op();
    //} else {
        parse_instr(get_next_line(), &instr_buffer[next_instr_index].first, &instr_buffer[next_instr_index].second);
    //}



    // update branch taken info
    if (instr_buffer[next_instr_index].first.pc == instr_buffer[current_instr_index].first.pc + 1) {
        instr_buffer[current_instr_index].first.taken = false;
    }else {
        instr_buffer[current_instr_index].first.taken = true;
    }


    size_t index_to_return = current_instr_index;
    current_instr_index = next_instr_index;
    ++next_instr_index;
    if (next_instr_index == buffer_size) next_instr_index = 0;

    //if (count % 1000000 == 0) printf("executed %d skipped %d", (int)count, (int)skipped);

    return &instr_buffer[index_to_return].first;
}

void IntelPTReader::make_custom_op() {
    // make custom prefetch op into instr_buffer[next_instr_index]
    // ++next_instr_index and ++current_instr_index
    //--instr_buffer[next_instr_index].first;

    //  set length of prefetch opcode to 6
    make_nop(&instr_buffer[next_instr_index].second, 6, &xed_state);

    // dummy values for now
    //instr_buffer[next_instr_index].mem_addr[0] = instr_buffer[current_instr_index].pc + 10; // Addr to prefetch to
    // instr_buffer[next_instr_index].mem_addr[1] = 1; // size of prefetch in cache lines?

    instr_buffer[next_instr_index].first.taken = false;
    instr_buffer[next_instr_index].first.pc = instr_buffer[current_instr_index].first.pc;
    instr_buffer[next_instr_index].first.mem_addr[0] = instr_buffer[current_instr_index].first.pc;// address to load
    instr_buffer[next_instr_index].first.mem_addr[1] = 1;// number of lines to load
    instr_buffer[next_instr_index].first.target = 0;
    instr_buffer[next_instr_index].first.unknown_type = false;
    instr_buffer[next_instr_index].first.valid = true;
    instr_buffer[next_instr_index].first.custom_op = CustomOp::PREFETCH_BLOCK;
    ++count;
}

void IntelPTReader::fill_line_buffer() {
    int mcount = 0;
    while(fgets(line_buffer[mcount], line_size, trace_file_ptr) != NULL) {
        ++mcount;
        if (mcount == buffer_size) break;
    }
    // got to the end of the file and could not fill the buffer
    // mark end line as the end so we can mark the InstrInfo as invalid and end 
    // the simulation
    if (unlikely(mcount != buffer_size)) {
        line_buffer[mcount][0] = '!';
        pclose(trace_file_ptr);
        trace_file_ptr = NULL;
    }
}

char * IntelPTReader::get_next_line() {
    if (next_line_index == buffer_size) {
        fill_line_buffer();
        next_line_index = 0;
    }
    size_t index = next_line_index;
    ++next_line_index;
    return line_buffer[index];
}

void IntelPTReader::parse_instr(char * line, InstInfo * out, xed_decoded_inst_t * xed_ptr) {
    // last line that is read is marked as !. can be an error
    if (unlikely(end || line[0] == '!')) {
        end = true;
        //printf("Number of branches taken %llu number of total instrs %llu", branch_count, count);
        memset(out, 0, sizeof(InstInfo));
        return;
    }

    // parts we cant fill at this point
    out->tid = 0;
    out->pid = 0;
    out->taken = false;

    // <whitespace><hex pc> <(binary name)> <'ilen:'> <instr size> <'insn:'> <space separated hex><newline>
    //    ffffffff8a2137c1 ([kernel.kallsyms]) ilen: 1 insn: 5c
    
    char * begin = nullptr;
    char * end = line + line_size;
    char * iter = line;
    uint64_t ilen = 0;
    xed_uint8_t xed_buffer[15];
    memset(xed_buffer, 0, 15 * sizeof(xed_uint8_t));

    // skip leading whitespace
    while (iter != end && *iter == ' ') ++iter;
    assert(iter < end);
    iter += 5; // skip pid = [000]

    // get PC
    begin = iter;
    // sets iter to end of pc
    out->pc = strtoul(begin, &iter, 16);
    assert(out->pc != 0);

    // find binary name wrapped in ()
    begin = ++iter;
    assert(iter != end && *iter == '(');
    while (iter != end && *iter != ')') ++iter;
    //binary name == strcpy(begin, iter)
    assert(iter < end);

    // find ilen: 
    ++iter;
    assert(iter != end && *iter == ' ');
    begin = (iter += 6); // skip ilen: to next space
    assert(iter < end && *iter == ' ');
    ilen = strtoul(begin, &iter, 10);
    assert(ilen != 0);

    begin = (iter += 6);
    assert(iter < end && *iter == ' ');

    for (size_t i = 0; i < ilen; ++i) {
        begin = iter;
        xed_buffer[i] = strtoul(begin, &iter, 16);
    }
    out->ins = xed_ptr;

    xed_error_enum_t xed_error;
    xed_decoded_inst_zero_set_mode(xed_ptr, &xed_state);
    //xed_decoded_inst_set_mode(xed_ptr,
    //                            XED_MACHINE_MODE_LONG_64,
    //                            XED_ADDRESS_WIDTH_64b);
    xed_error = xed_decode(xed_ptr, 
                XED_STATIC_CAST(const xed_uint8_t*, xed_buffer),
                ilen);

    // unsupported instruction  trace_decoder.cpp:189
    ++count;
    if (xed_ptr->_inst == 0x0 || xed_decoded_inst_get_iclass(xed_ptr) == XED_ICLASS_IRETQ) {
        ++skipped;
        if (skipped > 10000) panic("Skipped over 10000 instructions");
        make_nop(xed_ptr, ilen, &xed_state);
    }

    out->unknown_type = (xed_error != XED_ERROR_NONE);
    out->valid = true;
    out->target = xed_decoded_inst_get_branch_displacement(xed_ptr) + out->pc;
    //xed_decoded_inst_number_of_memory_operands
    //xed_decoded_inst_operand_elements ( , index)
    out->mem_addr[0] = 0;
    out->mem_addr[1] = 0;
    out->mem_used[0] = false;
    out->mem_used[1] = false;
    out->custom_op = CustomOp::NONE;


    // static const xed_operand_enum_t mems[2] = {XED_OPERAND_MEM0, XED_OPERAND_MEM1};

    // for (size_t op = 0; op < xed_decoded_inst_number_of_memory_operands(xed_ptr); ++op) {
    //     if (xed_decoded_inst_mem_written(xed_ptr, op) || xed_decoded_inst_mem_read(xed_ptr, op)) {
    //         out->mem_addr[op] = xed_decoded_inst_get_reg(xed_ptr, mems[op]); //xed_decoded_inst_operand_elements(xed_ptr, op);
    //         out->mem_used[op] = true;
    //     }
    // }

    out->cat = (xed_category_enum_t) INS_Category(xed_ptr);
    if (out->cat == XED_CATEGORY_COND_BR || out->cat == XED_CATEGORY_UNCOND_BR) {
        ++branch_count;
    }


    out->taken = false; // dont know yet
}

void IntelPTReader::test_trace_file() {
    trace_file_ptr = popen(gunzip_command.c_str(), "r");
    if (trace_file_ptr == NULL) {
        panic("Trace file not found %s", trace_file_path.c_str());
    }
}

void IntelPTReader::init_buffers() {
    if (trace_file_ptr == NULL) {
        panic("Trace file invalid after successful initialization");
    }
    fill_line_buffer();

    for (size_t i = 0; i < buffer_size; ++i) {
        xed_decoded_inst_zero_set_mode(&instr_buffer[i].second, &xed_state);
        //xed_decoded_inst_set_mode(&instr_buffer[i].second,
        //                            XED_MACHINE_MODE_LONG_64,
        //                            XED_ADDRESS_WIDTH_64b);
    }

    parse_instr(get_next_line(), &instr_buffer[current_instr_index].first, &instr_buffer[current_instr_index].second);
    ++next_line_index;
}

////////////////////////////////////////////////
// ParsedIntelPTReader 
////////////////////////////////////////////////

ParsedIntelPTReader::ParsedIntelPTReader(const std::string & trace_file_path_) : TraceReader(trace_file_path_) {
    init_xed(&xed_state);
    open_gz_file();
    init_buffer();

    // init current and next to instructions
    read_next_instr();
    ++next_instr_index;
    read_next_instr();
    set_branch_taken(instr_buffer[current_instr_index].first, instr_buffer[next_instr_index].first);

}

ParsedIntelPTReader::~ParsedIntelPTReader() {
    gzclose(input_gz);
}


bool ParsedIntelPTReader::operator!() {
    return input_gz == NULL;
}

InstInfo *ParsedIntelPTReader::nextInstruction() {

    auto to_return = current_instr_index;
    current_instr_index = next_instr_index;

    // update next instruction index to be filled by read_next_instr()
    ++next_instr_index;
    if (next_instr_index == buffer_size) {
        next_instr_index = 0;
    }
    
    // parse new instr into next_instr_index
    read_next_instr();

    set_branch_taken(instr_buffer[to_return].first, instr_buffer[current_instr_index].first);

    return &instr_buffer[to_return].first; 
}


void ParsedIntelPTReader::open_gz_file() {
    input_gz = gzopen(trace_file_path.c_str(), "rb");
    if (input_gz == NULL) {
        panic("Trace file not found %s", trace_file_path.c_str());
    }
    if (gzbuffer(input_gz, gz_buffer_size) == -1) {
        panic("Failed to set buffer size to %zu", gz_buffer_size);
    }
}

void ParsedIntelPTReader::set_branch_taken(InstInfo & current, const InstInfo & next){
    if (next.pc != current.pc + 1){
        current.taken = true;
    }
}

void ParsedIntelPTReader::init_buffer() {
    for (size_t i = 0; i < buffer_size; ++i){
        xed_decoded_inst_zero_set_mode(&instr_buffer[i].second, &xed_state);
        instr_buffer[i].first.pc = 0;
        instr_buffer[i].first.ins = &instr_buffer[i].second;
        instr_buffer[i].first.tid = 0;
        instr_buffer[i].first.pid = 0;
        instr_buffer[i].first.unknown_type = false;
        instr_buffer[i].first.target = 0;
        instr_buffer[i].first.taken = false;
        instr_buffer[i].first.valid = true;
        instr_buffer[i].first.mem_addr[0] = 0;
        instr_buffer[i].first.mem_addr[1] = 0;
        instr_buffer[i].first.mem_used[0] = false;
        instr_buffer[i].first.mem_used[1] = false;
        instr_buffer[i].first.custom_op = CustomOp::NONE;
    }
}

void ParsedIntelPTReader::read_next_instr() {
    if (end) {
        //printf("Number of branches taken %llu number of total instrs %llu", branch_count, count);
        memset(&instr_buffer[next_instr_index].first, 0, sizeof(InstInfo));
        return;
    }

    int res = gzread(input_gz, &instr, instr_size);
    if (res != static_cast<int>(instr_size)) {
        end = true;
        memset(&instr_buffer[next_instr_index].first, 0, sizeof(InstInfo));
        return;
    }

    // std::cout << std::hex << instr.pc << " " << instr.size << " ";
    // for (size_t i = 0; i < instr.size; ++i){
    //     std::cout << std::hex << static_cast<uint16_t>(instr.insn[i]) << " ";
    // }
    // std::cout << std::endl;

    auto & out = instr_buffer[next_instr_index].first;
    auto * xed_ptr = &instr_buffer[next_instr_index].second;

    out.pc = instr.pc;
    out.taken = false;
    out.mem_used[0] = false;
    out.mem_used[1] = false;
    out.cat = (xed_category_enum_t)0;

    // TODO: UNUSED at this point/constant 
    //out.ins = xed_ptr;
    //out.tid = 0;
    //out.pid = 0;
    //out.valid = true;
    //out.mem_addr[0] = 0;
    //out.mem_addr[1] = 0;
    //out.mem_used[0] = false;
    //out.mem_used[1] = false;
    //out.custom_op = CustomOp::NONE;

    xed_error_enum_t xed_error;
    xed_decoded_inst_zero_set_mode(xed_ptr, &xed_state);

    xed_error = xed_decode(xed_ptr, 
                XED_STATIC_CAST(const xed_uint8_t*, instr.insn),
                instr.size);

    // unsupported instruction  trace_decoder.cpp:189
    ++count;
    // if (xed_ptr->_inst == 0x0 || xed_decoded_inst_get_iclass(xed_ptr) == XED_ICLASS_IRETQ) {
    //     ++skipped;
    //     if (skipped > 10000) panic("Skipped over 10000 instructions");
    //     make_nop(xed_ptr, instr.size, &xed_state);
    // }

    out.target = xed_decoded_inst_get_branch_displacement(xed_ptr) + out.pc;

    if (xed_ptr->_inst == 0x0) {
        ++skipped;
        make_nop(xed_ptr, instr.size, &xed_state);
    }
    out.unknown_type = (xed_error != XED_ERROR_NONE);

    out.cat = (xed_category_enum_t) INS_Category(xed_ptr);
    if (out.cat == XC(COND_BR) || out.cat == XC(UNCOND_BR)) {
        ++branch_count;
    }

    for (size_t op = 0; op < xed_decoded_inst_number_of_memory_operands(xed_ptr); ++op) {
        if (xed_decoded_inst_mem_written(xed_ptr, op) || xed_decoded_inst_mem_read(xed_ptr, op)) {
            out.mem_used[op] = true;
        }
    }
}