import argparse,copy,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--template',type=Path,required=True);p.add_argument('--flac',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);a=p.parse_args()
template=json.loads(a.template.read_text('utf-8'))['head'][0]
aliases={
 'vr_melee_baton_hit':['h1_dlc_wpn/melee_batn/h1_wpn_melee_batn_stab','h1_dlc_wpn/melee_batn/h1_wpn_melee_batn_stab_sec'],
 'vr_melee_baton_extend':['h1_dlc_wpn/melee_batn/h1_wpn_melee_batn_pout_1st_02']}
for name,files in aliases.items():
 head=[]
 for index,recording in enumerate(files):
  source=a.source/'loaded_sound'/(recording+'.flac');output_name=name+'/'+str(index)
  destination=a.assets/'loaded_sound'/(output_name+'.flac');destination.parent.mkdir(parents=True,exist_ok=True)
  subprocess.run([str(a.flac),'--force','--verify','--blocksize=1024','-o',str(destination),str(source)],check=True)
  entry=copy.deepcopy(template);entry['aliasName']=name;entry['soundfile']={'type':1,'exists':True,'name':output_name};entry['sequence']=index+1
  entry['flags'] &= ~0x21;head.append(entry)
 target=a.assets/'sounds'/(name+'.json');target.parent.mkdir(parents=True,exist_ok=True);target.write_text(json.dumps({'aliasName':name,'count':len(head),'head':head},indent=2),encoding='utf-8')
manifest=json.loads((a.assets/'assets.json').read_text('utf-8'));manifest['sounds']=list(aliases);(a.assets/'assets.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
print(json.dumps({'aliases':list(aliases),'recordings':sum(map(len,aliases.values()))}))
