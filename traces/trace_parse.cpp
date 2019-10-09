#include <iostream>
#include <stdint.h>
#include <cstring>
#include <zlib.h>
#include <cassert>

using std::string;
using std::cout;
using std::endl;
using std::cerr;
//using std::ofstream;
//using std::stringstream;

struct zsim_instr {
    uint64_t pc;
    // from here we have options to pre process the source binary and map it to a uint8_t
    uint8_t source_binary;
    uint8_t size;
    // can make this 14 and use the extra byte to store the binary info
    // will need to map lenght 15 instructions to something else
    // Although i think there is only one len = 15 instruction
    uint8_t insn[15];
};


void help() {
    cerr << "Usage. ./executable <filename>" << endl;
    exit(1);
}

int skipped = 0;
void parse_failure(char * buffer, const size_t size){
    cerr << "Parsing failed for: " << endl;
    for (int i = 0; i < size; ++i){
        if (buffer[i] == '\n') break;
        cerr << buffer[i];
    }
    cout << endl;
    ++skipped;
    if (skipped >= 1000) exit(1);
    //exit(1);
}


void parse_instr(char * buffer, const size_t size, gzFile out_ptr) {
    struct zsim_instr instr;
    memset(&instr, 0, sizeof(instr));

    char * begin = nullptr;
    char * end = buffer + size;
    char * iter = buffer;

    // skip leading whitespace + pid = [XXX]
    while (iter != end && *iter == ' ') ++iter;
    //assert(iter < end);
    if (iter >= end) {
        parse_failure(buffer, size);
        return;
    }
    iter += 5; // skip pid = [000]

    // get PC
    begin = iter;
    // sets iter to end of pc
    instr.pc = strtoul(begin, &iter, 16);
    //assert(instr.pc != 0);
    if (instr.pc == 0) {
        parse_failure(buffer, size);
        return;
    }

    // skip binary name wrapped in (...)
    begin = ++iter;
    //assert(iter != end && *iter == '(');
    if (iter >= end || *iter != '(') {
        parse_failure(buffer, size);
        return;
    }
    while (iter != end && *iter != ')') ++iter;
    //binary name == strcpy(begin, iter)
    //assert(iter < end);
    if (iter >= end) {
        parse_failure(buffer, size);
        return;
    }

    // find ilen: 
    ++iter;
    //assert(iter != end && *iter == ' ');
    if (iter >= end || *iter != ' ') {
        parse_failure(buffer, size);
        return;
    }
    begin = (iter += 6); // skip ilen: to next space
    //assert(iter < end && *iter == ' ');
    if (iter >= end || *iter != ' ') {
        parse_failure(buffer, size);
        return;
    }
    instr.size = strtoul(begin, &iter, 10);
    //assert(instr.size != 0 && instr.size < 16);
    if (instr.size == 0 || instr.size >= 16) {
        parse_failure(buffer, size);
        return;
    }

    //skip insn:
    begin = (iter += 6);
    //assert(iter < end && *iter == ' ');
    if (iter >= end || *iter != ' ') {
        parse_failure(buffer, size);
        return;
    }
    for (size_t i = 0; i < instr.size; ++i) {
        begin = iter;
        instr.insn[i] = strtoul(begin, &iter, 16);
    }

    if (gzwrite(out_ptr, &instr, sizeof(zsim_instr)) == 0){
        cerr << "writing failed" << endl;
        exit(1);
    }
}

int64_t parse_file(const string & input, const string & output) {
    const size_t line_size = 256;
    char buffer[line_size];
    memset(buffer, 0, line_size);

    gzFile file_ptr = gzopen(input.c_str(), "rb"); //open_file(input);
    gzFile out_ptr = gzopen(output.c_str(), "wb");

    if (!out_ptr || !file_ptr) {
        cerr << "failed to open files" << endl;
        exit(1);
    }

    if (gzbuffer(file_ptr, 131072) == -1) {
        cerr << "input buffer failed to set" << endl;
        exit(1);
    }

    if (gzbuffer(out_ptr, 131072) == -1) {
        cerr << "output buffer failed to set" << endl;
        exit(1);
    }

    int64_t count = 0;
    while(gzgets(file_ptr, buffer, line_size) != Z_NULL) {
        ++count;
        parse_instr(buffer, line_size, out_ptr);
    }
    gzclose(file_ptr);
    gzclose(out_ptr);
    return count;
}


int main(int argc, char ** argv) {
    //cout << sizeof(zsim_instr) << endl;
    //exit(1);
    if (argc != 2 && argc != 3) help();
    string filename = argv[1];
    string outname;
    if (argc == 2) {
        outname = filename.substr(0, filename.find_last_of(".") + 1) + "trc";
    }else {
        outname = argv[2];
    }

    cout << "Parsing from " << filename << " to " << outname << endl;

    int64_t count = parse_file(filename, outname);

    cout << "Parsed " << count << " instructions" << endl;
    return 0;
}




