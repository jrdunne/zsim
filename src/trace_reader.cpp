#include "trace_reader.h"
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <fstream>
#include <utility>
#include <memory>
#include "wrapped_pin.h"
// #ifdef ZSIM_USE_YT
// #include "experimental/users/granta/yt/chunkio/xz-chunk-reader.h"
// #include "experimental/users/granta/yt/element-reader.h"
// #endif  // ZSIM_USE_YT
//#include "elf.h"
#include "log.h"


#define XC(cat) (XED_CATEGORY_##cat)

//analyzer_t is linked via a static lib which creates problems with the zsim shared library
//I think it can be fixed by building a memtrace analzyer shared lib or by ?
//#include "/home/hlitz/dynamorio/clients/drcachesim/analyzer.cpp"

// Remove and include <memory> when using C++14 or later
// template<typename T, typename... Args>
// static std::unique_ptr<T> make_unique(Args&&... args) {
//   return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
// }

// using std::get;
// using std::ifstream;
// using std::ignore;
// using std::make_pair;
// using std::tie;
// using std::unique_ptr;
// using std::make_unique;
// using std::make_tuple;
using std::string;

static bool xed_init_done = false;
static std::mutex xed_init_lock;


// Indices to 'xed_map_' cached features
static constexpr int MAP_MEMOPS = 0;
static constexpr int MAP_UNKNOWN = 1;
static constexpr int MAP_COND = 2;
static constexpr int MAP_REP = 3;
static constexpr int MAP_XED = 4;

TraceReader::TraceReader() {}
TraceReader::TraceReader(const std::string & trace_file_path_) : trace_file_path(trace_file_path_) {}
TraceReader::~TraceReader() {}
bool TraceReader::operator!() { return false; }
const InstInfo *TraceReader::nextInstruction() { return nullptr; }
////////////////////////////////////////////////
// IntelPTReader 
////////////////////////////////////////////////

IntelPTReader::IntelPTReader(const std::string & trace_file_path_) : TraceReader(trace_file_path_) {
    test_trace_file();
    init_xed();
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

    if (count % 1000000 == 0) {
        // inster custom op
    } else {
        parse_instr(get_next_line(), &instr_buffer[next_instr_index].first, &instr_buffer[next_instr_index].second);
    }



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

    make_nop(&instr_buffer[next_instr_index].second, xed_decoded_inst_get_length(&instr_buffer[next_instr_index].second));


    instr_buffer[next_instr_index].first.taken = false;
    instr_buffer[next_instr_index].first.pc = 0; // can be reused for fetching size maybe?
    instr_buffer[next_instr_index].first.target = 0;
    instr_buffer[next_instr_index].first.unknown_type = true;
    instr_buffer[next_instr_index].first.valid = true;
    instr_buffer[next_instr_index].first.custom_op = CustomOp::PREFETCH_BLOCK;
}

void IntelPTReader::fill_line_buffer() {
    int count = 0;
    while(fgets(line_buffer[count], line_size, trace_file_ptr) != NULL) {
        ++count;
        if (count == buffer_size) break;
    }
    // got to the end of the file and could not fill the buffer
    // mark end line as the end so we can mark the InstrInfo as invalid and end 
    // the simulation
    if (unlikely(count != buffer_size)) {
        line_buffer[count][0] = '!';
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
        printf("Number of branches taken %llu number of total instrs %llu", branch_count, count);
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
        make_nop(xed_ptr, ilen);
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


    static const xed_operand_enum_t mems[2] = {XED_OPERAND_MEM0, XED_OPERAND_MEM1};

    for (size_t op = 0; op < xed_decoded_inst_number_of_memory_operands(xed_ptr); ++op) {
        if (xed_decoded_inst_mem_written(xed_ptr, op) || xed_decoded_inst_mem_read(xed_ptr, op)) {
            out->mem_addr[op] = xed_decoded_inst_get_reg(xed_ptr, mems[op]); //xed_decoded_inst_operand_elements(xed_ptr, op);
            out->mem_used[op] = true;
        }
    }

    xed_category_enum_t cat = (xed_category_enum_t) INS_Category(xed_ptr);
    if (cat == XC(COND_BR) || cat == XC(UNCOND_BR)) {
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

void IntelPTReader::init_xed() {
    // only init the tables once
    std::unique_lock<std::mutex> xed_lock(xed_init_lock);
    if (!xed_init_done) {
        xed_tables_init();
        xed_init_done = true;
    }
    // Set the XED machine mode to 64-bit
    xed_state_init2(&xed_state, XED_MACHINE_MODE_LONG_64, XED_ADDRESS_WIDTH_64b);
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

void IntelPTReader::make_nop(xed_decoded_inst_t * ins, uint8_t len) {
    // A 10-to-15-byte NOP instruction (direct XED support is only up to 9)
    static const char *nop15 =
      "\x66\x66\x66\x66\x66\x66\x2e\x0f\x1f\x84\x00\x00\x00\x00\x00";

    xed_decoded_inst_zero_set_mode(ins, &xed_state);
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
