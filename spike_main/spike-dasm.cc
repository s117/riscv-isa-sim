// See LICENSE for license details.

// This little program finds occurrences of strings like
//  DASM(ffabc013)
// in its input, then replaces them with the disassembly
// enclosed hexadecimal number, interpreted as a RISC-V
// instruction.

#include "disasm.h"
#include "extension.h"
#include <iostream>
#include <string>
#include <cstdint>
#include <fesvr/option_parser.h>
using namespace std;

template< typename T >
std::string int_to_hex( T i )
{
  std::stringstream stream;
  stream << "0x" << std::hex << i;
  return stream.str();
}

int main(int argc, char** argv)
{
  string s;
  disassembler_t d;

  std::function<extension_t*()> extension;
  option_parser_t parser;
  parser.option(0, "extension", 1, [&](const char* s){extension = find_extension(s);});

  while (getline(cin, s))
  {
    for (size_t start = 0; (start = s.find("DASM(", start)) != string::npos; )
    {
      size_t end = s.find(')', start);
      if (end == string::npos)
        break;

      char* endp;
      size_t numstart = start + strlen("DASM(");
      int64_t bits = strtoull(&s[numstart], &endp, 16);
      size_t nbits = 4 * (endp - &s[numstart]);
      if (nbits < 64)
        bits = bits << (64 - nbits) >> (64 - nbits);

      string dis = d.disassemble(bits);

      /* The following additional code prints useful 
       * decode information along with the ABI compatible
       * disassembly */
      insn_t insn = (insn_t)bits;
      string rs1 ("rs1=");
      string rs2 (" rs2=");
      string rd  (" rd=");
      string csr (" csr=");
      rs1 += int_to_hex(insn.rs1());
      rs2 += int_to_hex(insn.rs2());
      rd  += int_to_hex(insn.rd());
      csr += int_to_hex(insn.csr());
      dis = dis + " <" + rs1 + rs2 + rd + csr +">";

      s = s.substr(0, start) + dis + s.substr(end+1);
      start += dis.length();
    }

    cout << s << '\n';
  }

  return 0;
}
