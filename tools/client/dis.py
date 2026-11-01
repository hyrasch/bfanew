# dis.py <mangled symbol prefix>... [--max N]: raw arm64 disassembly with symbolized branch targets and string refs
import re, sys, bisect
from elftools.elf.elffile import ELFFile
from capstone import *
from capstone.arm64 import *
import os
path=os.environ.get('LIBGAME', 'apk/lib/arm64-v8a/libgame.so')
elf=ELFFile(open(path,'rb')); data=open(path,'rb').read()
syms={}; alls=[]
for sn in ('.symtab','.dynsym'):
    s=elf.get_section_by_name(sn)
    if s:
        for sym in s.iter_symbols():
            if sym['st_value']:
                syms.setdefault(sym.name,(sym['st_value'],sym['st_size'])); alls.append((sym['st_value'],sym.name))
alls.sort(); addrs=[a for a,_ in alls]
def symof(va):
    i=bisect.bisect_right(addrs,va)-1
    return '%s+%#x'%(alls[i][1][:80],va-alls[i][0]) if i>=0 else '?'
plt={}
rel=elf.get_section_by_name('.rela.plt'); dyn=elf.get_section_by_name('.dynsym')
if rel and dyn:
    base=elf.get_section_by_name('.plt')['sh_addr']+32
    for i,r in enumerate(rel.iter_relocations()): plt[base+i*16]=dyn.get_symbol(r['r_info_sym']).name
def cstr(a):
    e=data.find(b'\0',a,a+200); return data[a:e].decode('latin1') if e>0 else ''
md=Cs(CS_ARCH_ARM64,CS_MODE_ARM); md.detail=True
want=[a for a in sys.argv[1:] if not a.startswith('--')]
mx=int(sys.argv[sys.argv.index('--max')+1]) if '--max' in sys.argv else 400
for n,(a,sz) in sorted(syms.items(), key=lambda kv: kv[1][0]):
    if not any(n.startswith(w) for w in want) or sz==0: continue
    print(f'=== {n} {a:#x} {sz}')
    pages={}; cnt=0
    for i in md.disasm(data[a:a+min(sz,mx*4)],a):
        extra=''
        if i.id==ARM64_INS_ADRP: pages[i.operands[0].reg]=i.operands[1].imm
        elif i.id==ARM64_INS_ADD and len(i.operands)==3 and i.operands[1].type==ARM64_OP_REG and i.operands[1].reg in pages and i.operands[2].type==ARM64_OP_IMM:
            va=pages[i.operands[1].reg]+i.operands[2].imm; s=cstr(va)
            extra=f'  ; "{s}"' if s and all(32<=ord(c)<127 for c in s) else f'  ; {symof(va)}'
        elif i.id==ARM64_INS_LDR and len(i.operands)==2 and i.operands[1].type==ARM64_OP_MEM and i.operands[1].mem.base in pages:
            va=pages[i.operands[1].mem.base]+i.operands[1].mem.disp; extra=f'  ; {symof(va)}'
        elif i.id in (ARM64_INS_BL,ARM64_INS_B) and i.operands and i.operands[0].type==ARM64_OP_IMM:
            t=i.operands[0].imm; extra='  ; '+(plt.get(t) or symof(t))
        print(f'{i.address:x}  {i.mnemonic:7s} {i.op_str}{extra}')
