import UnityPy, struct, json
import os as _os
APK = _os.environ.get('SMASHY_APK_DIR', 'work/apk') + '/'
WORK = _os.environ.get('SMASHY_WORK', 'work') + '/'

env=UnityPy.load(APK + 'assets/bin/Data')
def getfile(n):
    for k,f in env.files.items():
        if k.split('/')[-1]==n: return f
f=getfile('level2')
res=[]
for pid,o in f.objects.items():
    if o.type.name!='MonoBehaviour': continue
    raw=o.get_raw_data()
    if len(raw)<40: continue
    gofid,gopid=struct.unpack_from('<iq',raw,0)
    off=12; off+=1; off=(off+3)&~3
    sfid,spid=struct.unpack_from('<iq',raw,off); off+=12
    nl=struct.unpack_from('<i',raw,off)[0]; off+=4+nl; off=(off+3)&~3
    try:
        shape=struct.unpack_from('<i',raw,off)[0]; o2=off+4
        n=struct.unpack_from('<i',raw,o2)[0]; o2+=4
        if not(0<=n<=64): continue
        grid=[]
        for i in range(n):
            m=struct.unpack_from('<i',raw,o2)[0]; o2+=4
            if not(0<=m<=64): raise Exception
            grid.append(list(raw[o2:o2+m])); o2+=m; o2=(o2+3)&~3
        n=struct.unpack_from('<i',raw,o2)[0]; o2+=4; ct=list(raw[o2:o2+n]); o2+=n; o2=(o2+3)&~3
        n=struct.unpack_from('<i',raw,o2)[0]; o2+=4; ct2=list(struct.unpack_from('<%di'%n,raw,o2)); o2+=4*n
        flags=list(raw[o2:o2+3])
        if o2+3>len(raw)+0: raise Exception
    except Exception: continue
    if gopid not in f.objects: continue
    go=f.objects[gopid].read()
    if not hasattr(go,'m_Component'): continue
    tr=None
    for c in go.m_Component:
        p=c.component
        if p.deref().type.name=='Transform': tr=p.read()
    if tr is None: continue
    lp=tr.m_LocalPosition
    res.append({'pid':gopid,'name':go.m_Name,'shape':shape,'grid':grid,'conn':ct,'conn2':ct2,'flags':flags,'pos':[lp.x,lp.y,lp.z],'rot':[tr.m_LocalRotation.y,tr.m_LocalRotation.w]})
json.dump(res,open(WORK + 'dump/tiles.json','w'))
print(len(res))
import collections
print(collections.Counter(r['name'] for r in res))
for r in res[:6]: print(r)
