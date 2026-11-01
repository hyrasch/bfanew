# getters.py <class prefix e.g. 16UserUnitInfoBase>: every getter/setter of the class and the object offsets it touches
import re, sys
from elftools.elf.elffile import ELFFile
from capstone import *
from capstone.arm64 import *
import os
path=os.environ.get('LIBGAME', 'apk/lib/arm64-v8a/libgame.so')
elf=ELFFile(open(path,'rb')); data=open(path,'rb').read()
syms={}
for sn in ('.symtab','.dynsym'):
    s=elf.get_section_by_name(sn)
    if s:
        for sym in s.iter_symbols():
            if sym['st_value'] and sym['st_size']: syms.setdefault(sym.name,(sym['st_value'],sym['st_size']))
md=Cs(CS_ARCH_ARM64,CS_MODE_ARM); md.detail=True
cls=sys.argv[1]
for n,(a,sz) in sorted(syms.items(), key=lambda kv: kv[1][0]):
    if not n.startswith('_ZN'+cls) or not re.search(r'\d(get|set|is)[A-Z]',n) or sz>200: continue
    offs=[]
    for i in md.disasm(data[a:a+sz],a):
        for op in i.operands:
            if op.type==ARM64_OP_MEM and op.mem.base in (ARM64_REG_X0,ARM64_REG_X19,ARM64_REG_X20) and op.mem.disp>0: offs.append(op.mem.disp)
        if i.id==ARM64_INS_ADD and len(i.operands)==3 and i.operands[1].type==ARM64_OP_REG and i.operands[1].reg==ARM64_REG_X0 and i.operands[2].type==ARM64_OP_IMM: offs.append(i.operands[2].imm)
    short=re.sub(r'^_ZN\d+'+cls+r'\d+','',n).split('E')[0]
    print(f'{short:32s} {sorted(set(hex(o) for o in offs))[:4]}')
