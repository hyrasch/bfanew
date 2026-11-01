import os
# syms.py <regex> [--sizes]: list function symbols of libgame.so (arm64) matching the regex (mangled names)
import re, sys, os
from elftools.elf.elffile import ELFFile
f=ELFFile(open(os.environ.get('LIBGAME', 'apk/lib/arm64-v8a/libgame.so'),'rb'))
s=f.get_section_by_name('.symtab') or f.get_section_by_name('.dynsym')
rx=re.compile(sys.argv[1])
seen=set()
for sym in s.iter_symbols():
    n=sym.name
    if sym['st_value'] and rx.search(n) and n not in seen:
        seen.add(n); print(f"{sym['st_value']:#x} {sym['st_size']:5d} {n}")
