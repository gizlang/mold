#include "mold.h"

using namespace llvm;
using namespace llvm::ELF;

std::atomic_int num_relocs;

InputSection::InputSection(ObjectFile *file, const ELF64LE::Shdr &shdr, StringRef name)
  : file(file), shdr(shdr) {
  this->name = name;
  this->output_section = OutputSection::get_instance(name, shdr.sh_flags, shdr.sh_type);

  uint64_t align = (shdr.sh_addralign == 0) ? 1 : shdr.sh_addralign;
  if (align > UINT32_MAX)
    error(toString(file) + ": section sh_addralign is too large");
  if (__builtin_popcount(align) != 1)
    error(toString(file) + ": section sh_addralign is not a power of two");
}

void InputSection::copy_to(uint8_t *buf) {
  if (shdr.sh_type == SHT_NOBITS || shdr.sh_size == 0)
    return;

  ArrayRef<uint8_t> data = check(file->obj.getSectionContents(shdr));
  buf = buf + output_section->shdr.sh_offset + offset;
  memcpy_nontemporal(buf, &data[0], data.size());
}

void InputSection::scan_relocations() {
  for (const ELF64LE::Rela &rel : rels) {
    Symbol *sym = file->get_symbol(rel.getSymbol(false));
    if (!sym)
      continue;

    switch (rel.getType(false)) {
    case R_X86_64_GOT32:
    case R_X86_64_GOT64:
    case R_X86_64_GOTOFF64:
    case R_X86_64_GOTPC32:
    case R_X86_64_GOTPC32_TLSDESC:
    case R_X86_64_GOTPC64:
    case R_X86_64_GOTPCREL:
    case R_X86_64_GOTPCRELX:
    case R_X86_64_GOTTPOFF:
    case R_X86_64_REX_GOTPCRELX:
      sym->needs_got = true;
      break;
    case R_X86_64_PLT32:
      sym->needs_got = true;
      sym->needs_plt = true;
      break;
    }
  }
}

void InputSection::relocate(uint8_t *buf) {
  int i = 0;
  for (const ELF64LE::Rela &rel : rels) {
    uint8_t *loc = buf + output_section->shdr.sh_offset + offset + rel.r_offset;

    uint64_t cur = output_section->shdr.sh_addr + offset + rel.r_offset;
    uint64_t dst = file->get_symbol_value(rel.getSymbol(false));

    switch (rel.getType(false)) {
    case R_X86_64_8:
      *loc = dst;
      break;
    case R_X86_64_PC8:
      *loc = dst - cur;
      break;
    case R_X86_64_16:
      *(uint16_t *)loc = dst;
      break;
    case R_X86_64_PC16:
      *(uint16_t *)loc = dst - cur - 4;
      break;
    case R_X86_64_32:
    case R_X86_64_32S:
      *(uint32_t *)loc = dst;
      break;
    case R_X86_64_PC32:
      *(uint32_t *)loc = dst - cur - 4;
      break;
    case R_X86_64_64:
      *(uint64_t *)loc = dst;
      break;
    case R_X86_64_PC64:
      *(uint64_t *)loc = dst - cur - 4;
      break;
    case R_X86_64_DTPOFF32:
    case R_X86_64_GOTPC32:
    case R_X86_64_GOTPCREL:
    case R_X86_64_GOTPCRELX:
    case R_X86_64_GOTTPOFF:
    case R_X86_64_PLT32:
    case R_X86_64_REX_GOTPCRELX:
    case R_X86_64_TLSGD:
    case R_X86_64_TLSLD:
    case R_X86_64_TPOFF32:
      break;
    default:
      error(toString(this) + ": unknown relocation: " + std::to_string(rel.getType(false)));
    }
    // num_relocs++;
  }
}

std::string toString(InputSection *isec) {
  return (toString(isec->file) + ":(" + isec->name + ")").str();
}
