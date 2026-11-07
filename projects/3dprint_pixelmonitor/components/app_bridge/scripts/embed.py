from pathlib import Path
import gzip
import json
from generate_locales import generate
from generate_pixel_layout import generate as generate_layout
generate_layout()
generate()
root=Path(__file__).resolve().parents[1]
windows=json.loads((root/'resources/timezones/windows-zones.json').read_text('utf-8'))
assert len(windows['zones'])>=140
zone_script='// Generated from Microsoft Windows timezone data.\nwindow.MatrixTimezones='+json.dumps(windows['rules'],separators=(',',':'))+';\n'
zone_script+='window.MatrixWindowsZones='+json.dumps(windows,separators=(',',':'),ensure_ascii=False)+';\n'
zone_target=root/'web/timezones.js'
if not zone_target.exists() or zone_target.read_text('utf-8')!=zone_script:zone_target.write_text(zone_script,encoding='utf-8')
dest=root/'main/assets';dest.mkdir(exist_ok=True)
for name in ['index.html','app.js','style.css','pixel-art.js','locales.js','i18n.js','forms.js','timezones.js','timezone-picker.js','printer-manager.js','online-update.js']:
 p=dest/(name.replace('.','_')+'.gz');data=gzip.compress((root/'web'/name).read_bytes().replace(b'\r\n',b'\n'),mtime=0)
 if not p.exists() or p.read_bytes()!=data:p.write_bytes(data)
