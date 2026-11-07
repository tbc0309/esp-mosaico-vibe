"""Package native ESP-IDF output. Run from an activated ESP-IDF Python environment."""
import argparse,hashlib,json,pathlib,shutil,struct,subprocess,sys

parser=argparse.ArgumentParser()
parser.add_argument('--build',type=pathlib.Path,default=pathlib.Path('build'))
parser.add_argument('--output',type=pathlib.Path,default=pathlib.Path('dist'))
args=parser.parse_args();build=args.build.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
description=json.loads((build/'project_description.json').read_text())
board=json.loads((build/'matrix-board.json').read_text())
assert description['project_name']==board['project'],'Unexpected project'
flashing=json.loads((build/'flasher_args.json').read_text());assert flashing['extra_esptool_args']['chip']==board['target']
version=description['project_version'];stem=board['id']
app=out/(stem+'.bin');shutil.copy2(build/flashing['app']['file'],app)
full=out/(stem+'-full.bin');command=[sys.executable,'-m','esptool','--chip',board['target'],'merge-bin','--output',str(full),*flashing['write_flash_args']]
parts=[]
for offset,name in flashing['flash_files'].items():
 source=build/name;command.extend([offset,str(source)]);parts.append({'offset':offset,'file':name,'size':source.stat().st_size,'sha256':hashlib.sha256(source.read_bytes()).hexdigest()})
subprocess.run(command,check=True)
files=[{'file':p.name,'size':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in [app,full]]
table=(build/flashing['partition-table']['file']).read_bytes();slot=0
for offset in range(0,len(table)-31,32):
 magic,kind,subtype,address,size,label,flags=struct.unpack_from('<HBBII16sI',table,offset)
 if magic!=0x50aa:break
 if kind==0 and subtype==16:
  assert address==int(flashing['app']['offset'],16),'Application offset does not match board partitions'
  slot=size
assert slot,'Missing application partition'
manifest={'project':'3dprint_pixelmonitor','version':version,'board':board['id'],'boardName':board['name'],'projectName':board['project'],'target':board['target'],'flash':str(board['flashBytes']//1048576)+'MB','psram':str(board['psramBytes']//1048576)+'MB','flashBytes':board['flashBytes'],'psramBytes':board['psramBytes'],'appSlotBytes':slot,'files':files,'flashParts':parts}
assert app.stat().st_size<=manifest['appSlotBytes'],'Application exceeds OTA partition'
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(out/('manifest-'+board['id']+'.json')).write_text(json.dumps(manifest,indent=2)+'\n')
ota={key:manifest[key] for key in ['project','version','board','boardName','projectName','target','flashBytes','appSlotBytes']}
ota.update(url='https://github.com/tbc0309/esp-mosaico-vibe/releases/download/'+version+'/'+app.name,size=app.stat().st_size,sha256=hashlib.sha256(app.read_bytes()).hexdigest())
ota_text=json.dumps(ota,indent=2)+'\n'
(pathlib.Path(__file__).resolve().parents[1]/'boards'/board['id']/'ota.json').write_text(ota_text)
(out/('ota-'+board['id']+'.json')).write_text(ota_text)
(out/'SHA256SUMS').write_text(''.join(f"{p['sha256']}  {p['file']}\n" for p in files))
print('Packaged application OTA and initial full image; manifest and SHA256SUMS written.')
