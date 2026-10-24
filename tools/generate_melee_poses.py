"""Use the existing guarded sampler and anatomical retargeter for H2 poses."""
import argparse,json,re,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--adapter-root',type=Path,required=True);p.add_argument('--h1-exports',type=Path,required=True);p.add_argument('--h2-exports',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
sys.path.insert(0,str(a.adapter_root))
from tools.weapon_adaptation.reader import load_model,load_animation
from tools.weapon_adaptation.transforms import *
from tools.weapon_adaptation.donor_gestures import canonical_left,bind_bases,fingers_in_wrist
identity=IDENTITY
def model(root,name):return load_model(root/'xmodels'/name/(name+'_LOD0.semodel'))
target=model(a.h2_exports,'viewhands_us_army')
source_hand=model(a.h1_exports,'viewmodel_base_viewhands')
def sample(hand,gun,clip,frame):
 h=sample_model_pose(hand,clip,frame)
 for bone in hand.bones:
  name=bone.name
  if re.match(r'j_(?:index|mid|pinky|ring|thumb)_',name):h['local'][name]=(bone.localPosition,h['local'][name][1])
  parent=h['world'][hand.bones[bone.boneParent].name] if bone.boneParent>=0 else identity
  h['world'][name]=compose(parent,h['local'][name])
 g=sample_model_pose(gun,clip,frame,root_pose=h['world']['tag_weapon']);root=g['world'][gun.bones[0].name]
 return h,{n:relative(root,v) for n,v in h['world'].items()},{n:relative(root,v) for n,v in g['world'].items()}
def vec(v):return '{'+','.join(f'{x:.9f}f' for x in v)+'}'
def pose(v):return '{'+vec(tuple(x/2.54 for x in v[0]))+','+vec(v[1])+'}'
lines=['#pragma once','#include "hands/pose_schema.hpp"','namespace vr::gameplay::equipment::melee_poses','{',
 '// Source curves sampled offline; H2 finger lengths and anatomical bind bases retained.',
 '// No stock camera/arm trajectory or source gameplay event is applied by these poses.']
report={}
for name,root,hand,receiver,animation,axis in [
 ('baton',a.h1_exports,source_hand,'wpn_h1_melee_baton_vm','h1_wpn_melee_baton_idle',(0,0,1)),
 ('karambit',a.h1_exports,source_hand,'wpn_h1_melee_karambit_vm','h1_wpn_melee_karambit_idle',(0,0,-1)),
 ('ending',a.h2_exports,target,'viewmodel_commando_knife','h2_wpn_melee_knife_dizzy_idle',(1,0,0))]:
 gun=model(root,receiver);clip=load_animation(root/'xanims'/(animation+'.seanim'))
 h,hp,gp=sample(hand,gun,clip,0)
 fingers={n:pose_[1] for n,pose_ in h['local'].items() if re.match(r'j_(?:index|mid|pinky|ring|thumb)_ri_',n)}
 wrist,source_fingers=canonical_left(hand,hp['j_wrist_ri'],(0,0,0),fingers,True)
 bases=bind_bases(hand,target)[2]
 wrist=(wrist[0],normalize(multiply(wrist[1],bases['j_wrist_le'])))
 # Retarget only the 15 actual finger joints. Webbing is an optional skin
 # deformation leaf in H2, not part of its neutral 30-joint finger schema.
 sb,tb,bases=bind_bases(hand,target);fingers={}
 for joint,q in source_fingers.items():
  parent=target.bones[tb[joint].boneParent].name
  if hand.bones[sb[joint].boneParent].name!=parent:raise ValueError('changed finger parent: '+joint)
  fingers[joint]=normalize(multiply(multiply(conjugate(bases[parent]),q),bases[joint]))
 attachment=relative(wrist,identity)
 target_pose=fingers_in_wrist(target,fingers,wrist)
 middle=target_pose['j_mid_le_0'][0];ring=target_pose['j_ring_le_0'][0]
 midpoint=tuple((x+y)/2 for x,y in zip(middle,ring));projection=sum(x*y for x,y in zip(midpoint,axis))
 centre=tuple(projection*x for x in axis)
 report[name]={'source_model':receiver,'source_animation':animation,'frame':0,'source_hand':'right',
  'canonical_left_attachment_cm':attachment,'grip_center_cm':centre,'finger_count':len(fingers)}
 lines += [f'// {animation}, frame 0; source right hand anatomically mirrored/retargeted to H2.',
           f'inline constexpr hands::anchor {name}_attachment{pose(attachment)};',
           f'inline constexpr hands::vec {name}_grip_center{vec(tuple(x/2.54 for x in centre))};',
           f'inline constexpr std::array<hands::joint_pose,{len(fingers)}> {name}_fingers'+'{{']
 lines += ['{"'+n+'",'+vec(q)+'},' for n,q in sorted(fingers.items())];lines+=['}};']
 if name=='baton':
  deploy=load_animation(root/'xanims/h1_wpn_melee_baton_pullout_first.seanim')
  frames=[]
  for frame in range(22,46):
   _,_,bones=sample(hand,gun,deploy,frame);frames.append([bones[b.name] for b in gun.bones])
  lines += ['// Original pullout_first frames 22..45, 30 FPS. Rods and blended strap retain native deformation.',
            'inline constexpr float baton_fps=30.f;',
            'inline constexpr std::array<std::array<hands::anchor,11>,24> baton_deploy{{']
  lines += ['{{'+','.join(pose(b) for b in frame)+'}},' for frame in frames];lines+=['}};']
  # Contact ends at the end of the last telescopic rod, in that rod's bind frame.
  vertices=[v.position for mesh in gun.meshes for v in mesh.vertices if any(i==8 and w>.99 for i,w in v.weights)]
  tip=max(vertices,key=lambda v:v[2]);bind=gun.bones[8]
  tip_local=relative((bind.globalPosition,bind.globalRotation),(tip,identity[1]))[0]
  lines += [f'inline constexpr hands::vec baton_tip_in_rod{vec(tuple(x/2.54 for x in tip_local))};']
lines += ['}'];a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text('\n'.join(lines)+'\n',encoding='utf-8')
a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
