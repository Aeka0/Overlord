"""Offline audit/generator for the exact supported H2 surface stream ABI.

Requires a clean, owned native text/pdata capture. Never scan-and-patch at game
runtime: the generated, reviewed instruction contracts are compiled into the
module and checked as one transaction. Census assertions deliberately reject
different native versions or an incomplete writer/reader audit.
"""
import argparse
from pathlib import Path
import struct,bisect,json
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
from capstone.x86 import X86_OP_IMM,X86_OP_MEM,X86_REG_RIP
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--text',type=Path,required=True)
parser.add_argument('--pdata',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--audit',type=Path)
args=parser.parse_args()
base=0x140001000
data=args.text.read_bytes();raw=args.pdata.read_bytes()
ranges=sorted((0x140000000+a,0x140000000+b) for a,b,_ in struct.iter_unpack('<III',raw[:len(raw)//12*12]))
starts=[a for a,b in ranges];cs=Cs(CS_ARCH_X86,CS_MODE_64);cs.detail=True;cs.skipdata=True
decoded={};ordered=[]
for lo,hi in ranges:
 if lo<base or hi>base+len(data):continue
 for inst in cs.disasm(data[lo-base:hi-base],lo):
  if inst.id:decoded[inst.address]=inst
ordered=sorted(decoded.values(),key=lambda x:x.address)
patches={}
def add(inst,field,size,value,reason,dynamic=0):
 old=bytes(inst.bytes);new=bytearray(old)
 new[field:field+size]=value.to_bytes(size,'little',signed=False)
 assert len(new)==len(old) and 0<=field<len(old)
 assert inst.address not in patches,hex(inst.address)
 patches[inst.address]={'address':inst.address,'old':old.hex(),'new':new.hex(),
   'dynamic':dynamic,'field':field,'reason':reason,'asm':inst.mnemonic+' '+inst.op_str}
def imm(at,expected,new,reason):
 inst=decoded[at]
 assert any(o.type==X86_OP_IMM and o.imm&0xffffffff==expected for o in inst.operands),(hex(at),inst.op_str)
 add(inst,inst.imm_offset,inst.imm_size,new,reason)
def disp(at,expected,new,reason):
 inst=decoded[at];assert inst.disp==expected,(hex(at),inst.op_str)
 add(inst,inst.disp_offset,inst.disp_size,new,reason)
def scale(at):
 inst=decoded[at];assert inst.mnemonic=='lea' and inst.operands[1].mem.scale==4
 offset=inst.modrm_offset+1
 assert inst.bytes[offset]>>6==2
 add(inst,offset,1,inst.bytes[offset]|0xc0,'surface ID address scale 4 -> 8')

base_sites=[];mask_sites=[];field_counts={}
for i,inst in enumerate(ordered):
 values=[o.imm&0xffffffff for o in inst.operands if o.type==X86_OP_IMM]
 # A literal-base search missed 24 base+field loads and caused two crashes.
 # Audit every non-RIP frontend-arena operand in the renderer, preserving its
 # field addend. Other code contains similarly valued RVAs/string immediates;
 # those are not frontend fields and must not be rewritten.
 operands=[(o.imm,inst.imm_offset,inst.imm_size) if o.type==X86_OP_IMM else
           (o.mem.disp,inst.disp_offset,inst.disp_size)
           for o in inst.operands if o.type==X86_OP_IMM or
           (o.type==X86_OP_MEM and o.mem.base!=X86_REG_RIP)]
 arena=[v for v in operands if 0x62b700<=v[0]<0x66b700]
 if not 0x140710000<=inst.address<0x1407c1000:
  assert not any(0x62b700<=v[0]<0x62b800 for v in operands),('surface member reference outside audited renderer',hex(inst.address))
 if arena and 0x140710000<=inst.address<0x1407c1000:
  assert len(arena)==1
  value,field,size=arena[0]
  assert size==4
  add(inst,field,size,value,f'relocated surface arena + field {value-0x62b700:#x}',1);base_sites.append(inst.address)
  field_counts[value-0x62b700]=field_counts.get(value-0x62b700,0)+1
 if inst.mnemonic=='and' and 0x3fffc in values:
  imm(inst.address,0x3fffc,0x7fff8,'packed 16-bit surface ID mask, 8-byte units');mask_sites.append(inst.address)
  # In each supported specialization the shift immediately preceding this
  # mask extracts the same packed surfId field. Stop at any competing shift.
  shifts=[x for x in ordered[max(0,i-5):i] if x.mnemonic=='shr' and x.address>=inst.address-30]
  assert shifts,(hex(inst.address),'missing packed-ID shift')
  shift=shifts[-1]
  imm(shift.address,0x12,0x11,'packed 16-bit surface ID shift, 8-byte units')
assert field_counts=={0:65,4:7,6:2,8:2,16:1,24:2,40:6,48:2,92:2},field_counts
assert len(base_sites)==89,len(base_sites)
assert len(mask_sites)==59,len(mask_sites)

# One compiler-folded (base/4 + surfId)*4 expression is not in the raw-base census.
inst=decoded[0x140777432]
add(inst,inst.imm_offset,inst.imm_size,0x18adc0,'folded relocated arena displacement / 8',2)
for address in [0x140773e08,0x140774192,0x140777201,0x140777445]:scale(address)
for address in [0x140723fa8,0x140775460,0x14077735e,0x140777ad8,0x14075ca63]:
 imm(address,2,3,'encode or advance surface ID in 8-byte units')
inst=decoded[0x140774db1]
add(inst,inst.imm_offset,inst.imm_size,0x14900,'fused shadow encoder: (-arena displacement) modulo capacity',3)
imm(0x140774dbf,0x12,0x11,'fused shadow byte offset -> packed 16-bit ID')
for address in [0x1407740e8,0x1407745b4]:imm(address,6,3,'24-byte brush surface index advance')
for address in [0x14071f991,0x140723f3a,0x14075ca49]:
 imm(address,0x40000,0x80000,'512 KiB reservation bound')

# Hidden entries were the sole 4-byte records. All valid rigid records are
# (boneCount+2)*32; skinned records are 56 bytes. Walkers must match writers.
for address in [0x14071f47c,0x14071f67b,0x14075c70c,0x14075c86f,
                0x14071e106,0x14071e331,0x14071e57b,0x14071e5b8,
                0x14075f1fd,0x140760270]:imm(address,4,8,'hidden surface byte stride')
disp(0x140774c91,-0x11,0xf3,'hidden surface bytes: 21 - 13 = 8')
disp(0x14077534f,7,11,'hidden surface bytes: -3 + 11 = 8')
disp(0x14077724e,7,11,'hidden bytes = 8; logical surface ID increment stays 1')
disp(0x1407775aa,3,7,'hidden bytes = 8; logical surface ID increment stays 1')

# Brush transform remains 28 bytes of content, but its prefix occupies 32.
# Its following surface records are 24-byte pointer triples, already aligned.
disp(0x140723f23,0x1c,0x20,'align brush prefix reservation')
disp(0x140723f85,0x1c,0x20,'align first brush surface after its transform')

rows=sorted(patches.values(),key=lambda x:x['address'])
# The native reset selects one of exactly two records using this stride/base.
# These unchanged instructions guard the bank identity assumed by relocation.
for address in [0x14076e8ef,0x14076e8f6]:
 inst=decoded[address]
 patches[address]={'address':address,'old':inst.bytes.hex(),'new':inst.bytes.hex(),
   'dynamic':0,'field':0,'reason':'native frontend bank identity guard','asm':inst.mnemonic+' '+inst.op_str}
rows=sorted(patches.values(),key=lambda x:x['address'])
if args.audit:args.audit.write_text(json.dumps(rows,indent=2))
print('validated patches',len(rows),'base',len(base_sites),'packed decoders',len(mask_sites))
def array(value):return '{'+','.join('0x'+value[i:i+2] for i in range(0,len(value),2))+'}'
header=['#pragma once','#include <array>','#include <cstdint>',
 'namespace scene_surface_storage::contract {',
 'struct patch { std::uintptr_t address; std::array<std::uint8_t,15> before,after; unsigned size,field,dynamic; };',
 '// Exact H2 ABI: arena relocation, 8-byte stream records and 16-bit index units',
 '// are ONE contract. Never remove a subset as a performance optimization.',
 f'inline constexpr std::array<patch,{len(rows)}> patches{{{{']
for row in rows:
 header.append(f'    // {row["reason"]}; {row["asm"]}')
 header.append(f'    {{0x{row["address"]:x}, {array(row["old"])}, {array(row["new"])}, {len(row["old"])//2}, {row["field"]}, {row["dynamic"]}}},')
header.extend(['}};','}'])
args.output.write_text('\n'.join(header)+'\n',encoding='utf-8')
