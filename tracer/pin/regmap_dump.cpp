/*
 * Dump Pin's register numbering, once.
 *
 * The ChampSim tracer records raw Pin register ids, which distinguish
 * partial-width views of the same architectural register -- rax, eax and al
 * are different ids. A machine with an eight-entry register file has to treat
 * those as one register or its scoreboard tracks dependencies that do not
 * exist. Pin knows the answer (REG_FullRegName), so it emits the table rather
 * than anything downstream guessing it.
 */
#include "pin.H"
#include <fstream>

KNOB<std::string> KnobOut(KNOB_MODE_WRITEONCE, "pintool", "o", "regmap.txt", "output file");

int main(int argc, char* argv[])
{
  if (PIN_Init(argc, argv)) {
    return 1;
  }
  std::ofstream out(KnobOut.Value().c_str());
  for (UINT32 r = 0; r < REG_LAST; ++r) {
    const REG reg = static_cast<REG>(r);
    if (!REG_valid(reg)) {
      continue;
    }
    const REG full = REG_FullRegName(reg);
    out << r << " " << static_cast<UINT32>(full) << " " << REG_StringShort(reg) << " " << REG_StringShort(full) << "\n";
  }
  out.close();
  return 0;
}
