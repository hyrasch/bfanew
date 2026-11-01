import sys, bisect, struct
from elftools.elf.elffile import ELFFile
import os
path=os.environ.get('LIBGAME', 'apk/lib/arm64-v8a/libgame.so')
elf=ELFFile(open(path,'rb')); data=open(path,'rb').read()
alls=[]; byname={}
for sn in ('.symtab','.dynsym'):
    s=elf.get_section_by_name(sn)
    if s:
        for sym in s.iter_symbols():
            if sym['st_value']: alls.append((sym['st_value'],sym.name)); byname.setdefault(sym.name,(sym['st_value'],sym['st_size']))
alls.sort(); addrs=[a for a,_ in alls]
def symof(va):
    i=bisect.bisect_right(addrs,va)-1
    return '%s+%#x'%(alls[i][1][:90],va-alls[i][0]) if i>=0 else '?'
segs=[s for s in elf.iter_segments() if s['p_type']=='PT_LOAD']
def off(va):
    for s in segs:
        if s['p_vaddr']<=va<s['p_vaddr']+s['p_filesz']: return s['p_offset']+va-s['p_vaddr']
# relocations for vtable slots (RELATIVE)
rela={}
for sec in elf.iter_sections():
    if sec['sh_type']=='SHT_RELA':
        for r in sec.iter_relocations():
            if r['r_info_type']==1027: rela[r['r_offset']]=r['r_addend']
vt=byname[sys.argv[1]][0]
for slot in sys.argv[2:]:
    so=int(slot,16); va=vt+16+so
    tgt=rela.get(va) or struct.unpack('<Q',data[off(va):off(va)+8])[0]
    print(slot, symof(tgt))
