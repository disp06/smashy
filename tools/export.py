# Smashy Road: Wanted -> Vita data exporter
# Reads the Unity data from the APK and writes compact binary assets.
import UnityPy, numpy as np, struct, json, os, sys, re, collections, math
from UnityPy.helpers.MeshHelper import MeshHandler
from PIL import Image
import os as _os
APK = _os.environ.get('SMASHY_APK_DIR', 'work/apk') + '/'
WORK = _os.environ.get('SMASHY_WORK', 'work') + '/'


SRC = APK + 'assets/bin/Data'
OUT = sys.argv[1] if len(sys.argv) > 1 else WORK + 'data'
os.makedirs(OUT, exist_ok=True)
env = UnityPy.load(SRC)
FILES = {}
for k, f in env.files.items():
    if hasattr(f, 'objects'):
        FILES[k.split('/')[-1]] = f

def fobj(fname, pid):
    return FILES[fname].objects[pid]

def resolve(fname, fid, pid):
    f = FILES[fname]
    if fid == 0:
        return fname, pid
    return f.externals[fid - 1].path.split('/')[-1], pid

# ---------------------------------------------------------------- textures
TEX = {}          # key -> id
TEXLIST = []      # (w,h,bytes)
def pow2(n):
    p = 8
    while p < n: p *= 2
    return p

def add_texture_img(key, img):
    if key in TEX: return TEX[key]
    img = img.convert('RGBA')
    w, h = img.size
    W, H = pow2(w), pow2(h)
    if (W, H) != (w, h):
        img = img.resize((W, H), Image.NEAREST)
    tid = len(TEXLIST)
    TEXLIST.append((W, H, img.tobytes()))
    TEX[key] = tid
    return tid

def material_texture(mat):
    tex = None; col = (1, 1, 1, 1)
    for k, v in mat.m_SavedProperties.m_TexEnvs:
        if k == '_MainTex' and v.m_Texture.m_PathID:
            try: tex = v.m_Texture.read()
            except Exception: tex = None
    for k, v in mat.m_SavedProperties.m_Colors:
        if k == '_Color': col = (v.r, v.g, v.b, v.a)
    ckey = tuple(round(c, 3) for c in col)
    if tex is not None:
        key = ('t', tex.assets_file.name, tex.object_reader.path_id, ckey)
        if key in TEX: return TEX[key]
        img = tex.image.convert('RGBA').transpose(Image.FLIP_TOP_BOTTOM)
        if ckey != (1, 1, 1, 1):
            a = np.array(img).astype(np.float32)
            a[..., :3] *= np.array(col[:3])
            img = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))
        return add_texture_img(key, img)
    key = ('c', ckey)
    c = tuple(int(max(0, min(1, x)) * 255) for x in col)
    return add_texture_img(key, Image.new('RGBA', (8, 8), c))

# ---------------------------------------------------------------- transforms
def quat_mat(q):
    x, y, z, w = q.x, q.y, q.z, q.w
    return np.array([
        [1 - 2*(y*y + z*z), 2*(x*y - z*w), 2*(x*z + y*w)],
        [2*(x*y + z*w), 1 - 2*(x*x + z*z), 2*(y*z - x*w)],
        [2*(x*z - y*w), 2*(y*z + x*w), 1 - 2*(x*x + y*y)]])

def local_mat(tr):
    m = np.eye(4)
    p, q, s = tr.m_LocalPosition, tr.m_LocalRotation, tr.m_LocalScale
    m[:3, :3] = quat_mat(q) @ np.diag([s.x, s.y, s.z])
    m[:3, 3] = [p.x, p.y, p.z]
    return m

def comps(go):
    r = {}
    for c in go.m_Component:
        p = c.component
        try:
            o = p.deref()
        except Exception:
            continue
        r.setdefault(o.type.name, []).append(p)
    return r

MESHCACHE = {}
def mesh_data(mesh):
    key = (mesh.assets_file.name, mesh.object_reader.path_id)
    if key in MESHCACHE: return MESHCACHE[key]
    h = MeshHandler(mesh); h.process()
    v = np.array(h.m_Vertices, dtype=np.float64).reshape(-1, 3)
    uv = np.array(h.m_UV0, dtype=np.float64).reshape(-1, 2) if h.m_UV0 is not None and len(h.m_UV0) else np.zeros((len(v), 2))
    subs = [np.array(t, dtype=np.int64).reshape(-1, 3) for t in h.get_triangles()]
    MESHCACHE[key] = (v, uv, subs)
    return MESHCACHE[key]

# ---------------------------------------------------------------- prefab walk
class Model:
    def __init__(self):
        self.tris = collections.defaultdict(list)  # (tex,dir) -> list of 3x5 arrays
        self.colliders = []
        self.flags = set()
        self.lights = []
    def add(self, tex, P, UV):
        # P: (n,3,3) world tris ; UV: (n,3,2)
        e1 = P[:, 1] - P[:, 0]; e2 = P[:, 2] - P[:, 0]
        n = np.cross(e1, e2)
        ln = np.linalg.norm(n, axis=1)
        ok = ln > 1e-9
        P, UV, n = P[ok], UV[ok], n[ok] / ln[ok, None]
        # 0:+X 1:-X 2:+Y 3:-Y 4:+Z 5:-Z
        ax = np.argmax(np.abs(n), axis=1)
        sg = np.take_along_axis(n, ax[:, None], 1)[:, 0] < 0
        d = ax * 2 + sg
        for k in range(6):
            sel = d == k
            if sel.any():
                self.tris[(tex, k)].append(np.concatenate([P[sel], UV[sel]], axis=2))

SKIPNAMES = re.compile(r'(?i)(particle|shadow|smoke|vonk|boom2?$|fire|explos|spark|bliksem|forcefield|oil)')

def walk(go, M, model, depth=0, root=True):
    if not go.m_IsActive and not root:
        return
    if not root and SKIPNAMES.search(go.m_Name):
        return
    c = comps(go)
    tr = (c.get('Transform') or c.get('RectTransform'))[0].read()
    L = M if root else M @ local_mat(tr)
    if 'MeshFilter' in c and 'MeshRenderer' in c:
        mr = c['MeshRenderer'][0].read()
        if mr.m_Enabled:
            try:
                mesh = c['MeshFilter'][0].read().m_Mesh.read()
                v, uv, subs = mesh_data(mesh)
                W = (L[:3, :3] @ v.T).T + L[:3, 3]
                mats = mr.m_Materials
                for si, t in enumerate(subs):
                    if len(t) == 0: continue
                    mp = mats[min(si, len(mats) - 1)]
                    if not mp.m_PathID: continue
                    mat = mp.read()
                    tid = material_texture(mat)
                    model.add(tid, W[t], uv[t])
            except Exception as e:
                print('  mesh err', go.m_Name, e)
    for bp in c.get('BoxCollider', []):
        b = bp.read()
        if not b.m_Enabled: continue
        cen = np.array([b.m_Center.x, b.m_Center.y, b.m_Center.z]); sz = np.array([b.m_Size.x, b.m_Size.y, b.m_Size.z])
        corners = np.array([[sx, sy, sz_] for sx in (-.5, .5) for sy in (-.5, .5) for sz_ in (-.5, .5)]) * sz + cen
        Wc = (L[:3, :3] @ corners.T).T + L[:3, 3]
        model.colliders.append((Wc.min(0), Wc.max(0), int(b.m_IsTrigger), go.m_Name))
    for mb in c.get('MonoBehaviour', []):
        try:
            raw = mb.deref().get_raw_data()
            sfid, spid = struct.unpack_from('<iq', raw, 16)
            sn = FILES[go.assets_file.name.split('/')[-1]].externals[sfid - 1].path.split('/')[-1] if sfid else go.assets_file.name.split('/')[-1]
            s = FILES[sn].objects[spid].read()
            model.flags.add(s.m_ClassName)
        except Exception:
            pass
    if 'Light' in c:
        model.lights.append((L[:3, 3].copy(), go.m_Name))
    for ch in tr.m_Children:
        cg = ch.read().m_GameObject.read()
        walk(cg, L, model, depth + 1, False)

def build_model(go, keep_root_rot=False):
    m = Model()
    M = np.eye(4)
    if keep_root_rot:
        c = comps(go); tr = c['Transform'][0].read()
        M[:3, :3] = quat_mat(tr.m_LocalRotation) @ np.diag([tr.m_LocalScale.x, tr.m_LocalScale.y, tr.m_LocalScale.z])
    walk(go, M, m)
    return m

MODELS = {}  # name -> index
MODELLIST = []
def write_model(name, m):
    # convert Unity left-handed -> right-handed by mirroring Z (and flipping winding)
    subs = []
    lo = np.full(3, 1e9); hi = np.full(3, -1e9)
    for (tex, d), parts in sorted(m.tris.items()):
        a = np.concatenate(parts, 0)  # (n,3,5)
        a = a.copy()
        a[:, :, 2] *= -1
        a = a[:, ::-1, :]  # flip winding
        nd = {4: 5, 5: 4}.get(d, d)
        P = a[:, :, :3].reshape(-1, 3)
        lo = np.minimum(lo, P.min(0)); hi = np.maximum(hi, P.max(0))
        subs.append((tex, nd, a.astype(np.float32)))
    cols = []
    for mn, mx, trig, nm in m.colliders:
        mn = mn.copy(); mx = mx.copy()
        mn[2], mx[2] = -mx[2], -mn[2]
        cols.append((mn, mx, trig))
    if not subs:
        lo = np.zeros(3); hi = np.zeros(3)
    b = bytearray(b'MDL1')
    b += struct.pack('<6f', *lo, *hi)
    b += struct.pack('<ii', len(subs), len(cols))
    for mn, mx, trig in cols:
        b += struct.pack('<6fi', *mn, *mx, trig)
    for tex, d, a in subs:
        b += struct.pack('<iii', tex, d, a.shape[0] * 3)
        b += a.tobytes()
    idx = len(MODELLIST)
    MODELLIST.append(bytes(b))
    MODELS[name] = idx
    return idx, lo, hi

# ---------------------------------------------------------------- gather
L2 = 'level2'
mbd = json.load(open(WORK + 'dump/mb.json'))
om = mbd['omgevingManager'][0]['v']
pm = mbd['policeAndArmyManager'][0]['v']
tm = mbd['trafficManager'][0]['v']
meta = {'models': {}, 'tiles': {}, 'buildings': {}, 'cars': [], 'traffic': [], 'police': {}}

def go_of(fname, pid):
    o = fobj(fname, pid)
    x = o.read()
    if o.type.name != 'GameObject':
        x = x.m_GameObject.read()
    return x

def export_go(name, fname, pid, keep_rot=True):
    go = go_of(fname, pid)
    m = build_model(go, keep_rot)
    idx, lo, hi = write_model(name, m)
    meta['models'][name] = {'idx': idx, 'lo': lo.tolist(), 'hi': hi.tolist(), 'flags': sorted(m.flags),
                            'cols': [[c[0][0], c[0][1], -c[1][2], c[1][0], c[1][1], -c[0][2], c[2]] for c in m.colliders],
                            'lights': [[p[0], p[1], -p[2], n] for p, n in m.lights]}
    return idx

# road tiles
tiles = json.load(open(WORK + 'dump/tiles.json'))
seen = set()
for r in tiles:
    nm = r['name']
    if nm in seen or not r['grid'] or not (nm.startswith('road') or nm.startswith('brug')):
        continue
    seen.add(nm)
    export_go('tile_' + nm, L2, r['pid'], keep_rot=True)
    meta['tiles'][nm] = {'grid': r['grid'], 'conn': r['conn'], 'conn2': r['conn2'], 'shape': r['shape'], 'rot': r['rot']}
    print('tile', nm, meta['models']['tile_' + nm]['lo'], meta['models']['tile_' + nm]['hi'])

# ground (Omgeving) – one instance
for r in tiles:
    if r['name'] == 'Omgeving':
        export_go('ground', L2, r['pid']); break

# buildings
for key in ['gebouw1', 'gebouwhoek1', 'gebouwrecht1', 'gebouw4', 'gebouw1Woestijn', 'gebouw1WoestijnRock', 'gebouw4Woestijn',
            'gebouw1Land', 'gebouw1LandRock', 'gebouw4Land']:
    lst = []
    for p in om[key]:
        if p[2] == 0: continue
        fn, pid = resolve(L2, p[1], p[2])
        nm = 'b_%s_%d' % (fn, pid)
        if nm not in meta['models']:
            try:
                export_go(nm, fn, pid)
            except Exception as e:
                print('bld err', nm, e); continue
        lst.append(nm)
    meta['buildings'][key] = lst
    print(key, len(lst))

# coins
meta['coin'] = None
for p in om['munt'][:1]:
    fn, pid = resolve(L2, p[1], p[2]); export_go('coin', fn, pid); meta['coin'] = 'coin'

# traffic
for i, p in enumerate(tm['traffc']):
    fn, pid = resolve(L2, p[1], p[2])
    nm = 'traffic%d' % i
    try:
        export_go(nm, fn, pid); meta['traffic'].append(nm)
    except Exception as e:
        print('traffic err', e)

# police & army
for k in ['police', 'policeHeli', 'swat', 'armyJeep', 'armyHeli', 'tank', 'roadBlockPolice', 'roadBlockSwat', 'roadBlockJeep', 'roadBlockTank']:
    p = pm[k]
    fn, pid = resolve(L2, p[1], p[2])
    try:
        export_go('cop_' + k, fn, pid); meta['police'][k] = 'cop_' + k
    except Exception as e:
        print('cop err', k, e)

# player cars: root objects named carN in bundle files; collect distinct (mesh,material) variants
carroots = {}
for fn, f in FILES.items():
    if not re.fullmatch(r'[0-9a-f]{32}', fn): continue
    for pid, o in f.objects.items():
        if o.type.name != 'GameObject': continue
        g = o.read()
        m_ = re.fullmatch(r'car(\d+)', g.m_Name)
        if m_:
            c = comps(g); tr = c['Transform'][0].read()
            if not tr.m_Father.m_PathID:
                carroots.setdefault(int(m_.group(1)), []).append((fn, pid))
print('car roots', sorted(carroots))
for n in sorted(carroots):
    fn, pid = carroots[n][0]
    root = go_of(fn, pid)
    # children: variants (CarN, Car2_N, Car3_N)
    tr = comps(root)['Transform'][0].read()
    variants = []
    for ch in tr.m_Children:
        cg = ch.read().m_GameObject.read()
        nm = 'car%d_v%d' % (n, len(variants))
        ctr = comps(cg)['Transform'][0].read()
        M0 = np.eye(4); M0[:3, :3] = quat_mat(ctr.m_LocalRotation) @ np.diag([ctr.m_LocalScale.x, ctr.m_LocalScale.y, ctr.m_LocalScale.z])
        m = Model(); walk(cg, M0, m)
        # recentre on root (variant transform ignored) -> use child local transform without its position
        idx, lo, hi = write_model(nm, m)
        meta['models'][nm] = {'idx': idx, 'lo': lo.tolist(), 'hi': hi.tolist(), 'flags': sorted(m.flags),
                              'cols': [[c[0][0], c[0][1], -c[1][2], c[1][0], c[1][1], -c[0][2], c[2]] for c in m.colliders],
                              'lights': []}
        variants.append(nm)
    meta['cars'].append({'n': n, 'variants': variants})
    print('car', n, variants, [meta['models'][v]['lo'] + meta['models'][v]['hi'] for v in variants][:1])

# tank bullet / rocket
try:
    tk = mbd['tankShoot'][0]['v']
    fn, pid = resolve('sharedassets2.assets', tk['bullet'][1], tk['bullet'][2])
    export_go('bullet', fn, pid)
except Exception as e:
    print('bullet err', e)

# ground textures
meta['groundtex'] = {}
for key in ['texNormal', 'texDessert', 'texGrass']:
    ids = []
    for p in om[key]:
        fn, pid = resolve(L2, p[1], p[2])
        t = fobj(fn, pid).read()
        ids.append(add_texture_img(('g', fn, pid), t.image.convert('RGBA').transpose(Image.FLIP_TOP_BOTTOM)))
    meta['groundtex'][key] = ids
# ---------------------------------------------------------------- write packs
def write_pack(path, blobs):
    with open(path, 'wb') as f:
        f.write(struct.pack('<4sI', b'PAK1', len(blobs)))
        off = 8 + 8 * len(blobs)
        for b in blobs:
            f.write(struct.pack('<II', off, len(b))); off += len(b)
        for b in blobs:
            f.write(b)

def encode_tex(w, h, d):
    a = np.frombuffer(d, np.uint8).reshape(h, w, 4)
    u = np.unique(a.reshape(-1, 4).view(np.uint32))
    if len(u) > 256:
        img = Image.fromarray(a, 'RGBA')
        q = img.quantize(256, method=Image.Quantize.FASTOCTREE)
        pal = np.array(q.getpalette('RGBA')[:1024] if hasattr(q, 'getpalette') else q.getpalette()[:768], np.uint8)
        idx = np.array(q, np.uint8)
        pal = np.array(q.palette.colors and [None] or [], dtype=object)
        # fall back: rebuild palette via actual mapping
        idxf = idx.reshape(-1); src = a.reshape(-1, 4).astype(np.int64)
        P = np.zeros((256, 4), np.int64); C = np.zeros(256, np.int64)
        np.add.at(P, idxf, src); np.add.at(C, idxf, 1)
        P = (P // np.maximum(C, 1)[:, None]).astype(np.uint8)
        return struct.pack('<4sIII', b'TEX1', w, h, 1) + P.tobytes() + idx.tobytes()
    u32 = a.reshape(-1, 4).copy().view(np.uint32).reshape(-1)
    lut = {int(c): i for i, c in enumerate(u)}
    idx = np.searchsorted(u, u32).astype(np.uint8)
    P = np.zeros((256, 4), np.uint8); P[:len(u)] = u.view(np.uint8).reshape(-1, 4)
    return struct.pack('<4sIII', b'TEX1', w, h, 1) + P.tobytes() + idx.tobytes()
texblobs = [encode_tex(w, h, d) for (w, h, d) in TEXLIST]
write_pack(os.path.join(OUT, 'textures.pak'), texblobs)
write_pack(os.path.join(OUT, 'models.pak'), MODELLIST)
json.dump(meta, open(WORK + 'meta.json', 'w'), indent=1)
print('textures', len(TEXLIST), sum(len(b) for b in texblobs) / 1e6, 'MB; models', len(MODELLIST), sum(len(b) for b in MODELLIST) / 1e6, 'MB')
