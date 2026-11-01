# keymap.py <readParam mangled symbol> <class prefix like 10FriendInfo>: key -> setter / field offset, following the strcmp chain
import re, sys
from elftools.elf.elffile import ELFFile
from capstone import *
from capstone.arm64 import *
import os
path=os.environ.get('LIBGAME', 'apk/lib/arm64-v8a/libgame.so')
elf=ELFFile(open(path,'rb')); data=open(path,'rb').read()
syms={}; byaddr={}
for sn in ('.symtab','.dynsym'):
    s=elf.get_section_by_name(sn)
    if s:
        for sym in s.iter_symbols():
            if sym['st_value']:
                syms.setdefault(sym.name,(sym['st_value'],sym['st_size'])); byaddr.setdefault(sym['st_value'],sym.name)
plt={}
rel=elf.get_section_by_name('.rela.plt'); dyn=elf.get_section_by_name('.dynsym')
if rel and dyn:
    base=elf.get_section_by_name('.plt')['sh_addr']+32
    for i,r in enumerate(rel.iter_relocations()): plt[base+i*16]=dyn.get_symbol(r['r_info_sym']).name
def cstr(a):
    e=data.find(b'\0',a); return data[a:e].decode('latin1')
md=Cs(CS_ARCH_ARM64,CS_MODE_ARM); md.detail=True
fn=sys.argv[1]; cls=sys.argv[2]
a,sz=syms[fn]
ins_list=list(md.disasm(data[a:a+sz],a))
byaddr_ins={i.address:i for i in ins_list}
# getter offsets
getters={}
for n,(ga,gs) in syms.items():
    if n.startswith('_ZN'+cls) and re.search(r'\d(get|is)[A-Z]',n) and 0<gs<=64:
        for i in md.disasm(data[ga:ga+gs],ga):
            if i.id in (ARM64_INS_LDR,ARM64_INS_LDRB,ARM64_INS_LDRH,ARM64_INS_LDRSW,ARM64_INS_LDUR) and i.operands[-1].type==ARM64_OP_MEM and i.operands[-1].mem.base==ARM64_REG_X0:
                getters.setdefault(i.operands[-1].mem.disp,set()).add(re.sub(r'^_ZN\d+'+cls+r'\d+','',n).split('E')[0]); break
            if i.id==ARM64_INS_ADD and i.operands[1].type==ARM64_OP_REG and i.operands[1].reg==ARM64_REG_X0 and i.operands[2].type==ARM64_OP_IMM:
                getters.setdefault(i.operands[2].imm,set()).add(re.sub(r'^_ZN\d+'+cls+r'\d+','',n).split('E')[0]); break
# strcmp chain
pages={}; laststr=None; chain=[]
for k,i in enumerate(ins_list):
    if i.id==ARM64_INS_ADRP: pages[i.operands[0].reg]=i.operands[1].imm
    elif i.id==ARM64_INS_ADD and len(i.operands)==3 and i.operands[1].type==ARM64_OP_REG and i.operands[1].reg in pages and i.operands[2].type==ARM64_OP_IMM:
        va=pages[i.operands[1].reg]+i.operands[2].imm
        if 0x2000000<va<0x2500000:
            s=cstr(va)
            if re.fullmatch(r'[0-9A-Za-z_]{4,24}',s): laststr=s
    elif i.id==ARM64_INS_BL and (plt.get(i.operands[0].imm)=='strcmp' or byaddr.get(i.operands[0].imm)=='strcmp'):
        # next branch
        for j in range(k+1,k+4):
            n=ins_list[j]
            if n.id in (ARM64_INS_CBZ,): chain.append((laststr,n.operands[1].imm)); break
            if n.id in (ARM64_INS_CBNZ,): chain.append((laststr,n.address+4)); break
            if n.id==ARM64_INS_B and n.cc==ARM64_CC_EQ: chain.append((laststr,n.operands[0].imm)); break
            if n.id==ARM64_INS_B and n.cc==ARM64_CC_NE: chain.append((laststr,n.address+4)); break
def block(start):
    out=[]; addr=start; steps=0; hops=0
    while addr in byaddr_ins and steps<120:
        i=byaddr_ins[addr]; steps+=1
        if i.id==ARM64_INS_BL:
            t=i.operands[0].imm; n=plt.get(t) or byaddr.get(t,'')
            if n.startswith('_ZN'+cls) and 'set' in n: out.append(re.sub(r'^_ZN\d+'+cls+r'\d+','',n).split('E')[0])
        elif i.id in (ARM64_INS_STR,ARM64_INS_STRB,ARM64_INS_STRH,ARM64_INS_STUR) and i.operands[-1].type==ARM64_OP_MEM:
            m=i.operands[-1].mem
            if m.base not in (ARM64_REG_SP,ARM64_REG_X29) and m.disp in getters: out.append(f'[{m.disp:#x}]='+'/'.join(sorted(getters[m.disp])))
        elif i.id==ARM64_INS_ADD and len(i.operands)==3 and i.operands[1].type==ARM64_OP_REG and i.operands[2].type==ARM64_OP_IMM and i.operands[2].imm in getters and i.operands[1].reg not in (ARM64_REG_SP,ARM64_REG_X29):
            out.append(f'&[{i.operands[2].imm:#x}]='+'/'.join(sorted(getters[i.operands[2].imm])))
        if i.id==ARM64_INS_B and i.cc==ARM64_CC_INVALID:
            t=i.operands[0].imm
            if hops<3 and t>chain_end and not out: hops+=1; addr=t; continue
            break
        if i.id==ARM64_INS_RET: break
        addr+=4
    return out
chain_end=max(t for _,t in chain) if False else min(t for _,t in chain)
for key,tgt in chain:
    print(f'{key:10s} -> {tgt:#x}  {block(tgt)[:6]}')
print('getters:',{hex(k):'/'.join(sorted(v)) for k,v in sorted(getters.items())})
