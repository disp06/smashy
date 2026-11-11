import UnityPy, numpy as np, struct, os, io, wave, json, sys
from PIL import Image
import os as _os
APK = _os.environ.get('SMASHY_APK_DIR', 'work/apk') + '/'
WORK = _os.environ.get('SMASHY_WORK', 'work') + '/'

OUT=WORK + 'data'
env=UnityPy.load(APK + 'assets/bin/Data')
snd={}
for o in env.objects:
    if o.type.name=='AudioClip':
        c=o.read()
        try:
            s=c.samples
            for nm,data in s.items():
                w=wave.open(io.BytesIO(data))
                ch,sw,rate,n=w.getnchannels(),w.getsampwidth(),w.getframerate(),w.getnframes()
                raw=w.readframes(n)
                snd[c.m_Name]=(ch,sw,rate,n,raw)
                break
        except Exception as e:
            print('aerr',c.m_Name,e)
print({k:(v[0],v[1],v[2],v[3]) for k,v in snd.items()})
import pickle; pickle.dump(snd,open(WORK + 'snd.pkl','wb'))
# sprites & UI textures
os.makedirs(WORK + 'ui',exist_ok=True)
for o in env.objects:
    if o.type.name in('Sprite',):
        s=o.read()
        try: s.image.save(WORK + 'ui/%s.png'%s.m_Name.replace('/','_'))
        except Exception as e: print('serr',s.m_Name,e)
    if o.type.name=='Texture2D':
        t=o.read()
        if t.m_Width and t.m_Width<=1024 and (t.m_Name.lower().startswith(('btn','cash','pause','stop','zwaai','health','star','munt','car','win','earn','blue','orange','logo','title','smashy','box','handle','white','background','ui'))):
            try: t.image.save(WORK + 'ui/T_%s.png'%t.m_Name.replace('/','_'))
            except Exception as e: pass
    if o.type.name=='Font':
        f=o.read()
        fd=getattr(f,'m_FontData',None)
        if fd: open(WORK + 'ui/%s.ttf'%f.m_Name,'wb').write(bytes(fd)); print('font',f.m_Name,len(fd))
