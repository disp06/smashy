import UnityPy, json, sys, collections, struct
from UnityPy.helpers.TypeTreeGenerator import TypeTreeGenerator
import os as _os
APK = _os.environ.get('SMASHY_APK_DIR', 'work/apk') + '/'
WORK = _os.environ.get('SMASHY_WORK', 'work') + '/'

D=APK
env=UnityPy.load(D+'assets/bin/Data')
g=TypeTreeGenerator("2020.3.21f1")
from UnityPy.helpers.TypeTreeNode import TypeTreeNode
def gnu(asm,cn,_c={}):
    k=(asm,cn)
    if k not in _c:
        b=g.get_nodes(asm.replace('.dll',''),cn)
        _c[k]=TypeTreeNode.from_list([TypeTreeNode(x.m_Level,x.m_Type,x.m_Name,0,0,m_MetaFlag=x.m_MetaFlag) for x in b])
    return _c[k]
g.load_il2cpp(open(D+'lib/armeabi-v7a/libil2cpp.so','rb').read(), open(D+'assets/bin/Data/Managed/Metadata/global-metadata.dat','rb').read())
# script table
scripts={}
for o in env.objects:
    if o.type.name=='MonoScript':
        s=o.read(); scripts[(o.assets_file.name.split('/')[-1],o.path_id)]=(s.m_AssemblyName,(s.m_Namespace+'.' if s.m_Namespace else '')+s.m_ClassName)
def ext_name(af,fid):
    if fid==0: return af.name.split('/')[-1]
    return af.externals[fid-1].path.split('/')[-1]
def clean(v):
    if isinstance(v,dict):
        if set(v.keys())>={'m_FileID','m_PathID'}: return ('PPtr',v['m_FileID'],v['m_PathID'])
        return {k:clean(x) for k,x in v.items()}
    if isinstance(v,list):
        r=[clean(x) for x in v[:200]]
        if len(v)>200: r.append('...%d'%len(v))
        return r
    if isinstance(v,(bytes,bytearray)): return '<%dbytes>'%len(v)
    return v
skip={'Text','LayoutElement','Image','Button','Outline','CanvasScaler','GraphicRaycaster','EventTrigger','ContentSizeFitter','VerticalLayoutGroup','ScrollRect','Mask','RawImage','EventSystem','StandaloneInputModule','TouchInputModule','Slider'}
out=collections.defaultdict(list); errs=collections.Counter()
for o in env.objects:
    if o.type.name!='MonoBehaviour': continue
    raw=o.get_raw_data()
    gofid,gopid=struct.unpack_from('<iq',raw,0)
    sfid,spid=struct.unpack_from('<iq',raw,16)
    sn=ext_name(o.assets_file,sfid)
    key=(sn,spid)
    if key not in scripts: errs['noscript']+=1; continue
    asm,cn=scripts[key]
    short=cn.split('.')[-1]
    if short in skip or short.startswith('CC_'): continue
    try:
        node=gnu(asm,cn)
        tt=o.read_typetree(nodes=node,check_read=False)
    except Exception as e:
        errs[cn]+=1; continue
    out[short].append({'file':o.assets_file.name.split('/')[-1],'pid':o.path_id,'go':(gofid,gopid),'v':clean(tt)})
json.dump(out,open(WORK + 'dump/mb.json','w'),indent=1,default=str)
print({k:len(v) for k,v in out.items()}); print(errs)
