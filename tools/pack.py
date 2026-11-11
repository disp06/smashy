# Packs UI images, font atlas and sounds for the Vita build
import struct, os, pickle, json, numpy as np, subprocess
from PIL import Image, ImageFont, ImageDraw
import os as _os
APK = _os.environ.get('SMASHY_APK_DIR', 'work/apk') + '/'
WORK = _os.environ.get('SMASHY_WORK', 'work') + '/'

B = WORK
OUT = B + 'data/'
UI = B + 'ui/'

def write_pack(path, entries):
    # entries: list of (name, bytes)
    with open(path, 'wb') as f:
        f.write(struct.pack('<4sI', b'NPK1', len(entries)))
        hdr = 8 + len(entries) * (32 + 8)
        off = hdr
        for nm, b in entries:
            f.write(nm.encode()[:31].ljust(32, b'\0')); f.write(struct.pack('<II', off, len(b))); off += len(b)
        for nm, b in entries:
            f.write(b)

def p2(n):
    p = 8
    while p < n: p *= 2
    return p

def img_entry(img):
    img = img.convert('RGBA')
    w, h = img.size
    W, H = p2(w), p2(h)
    canvas = Image.new('RGBA', (W, H), (0, 0, 0, 0)); canvas.paste(img, (0, 0))
    return struct.pack('<4sIIIII', b'IMG1', W, H, w, h, 0) + canvas.tobytes()

ui = []
names = ['btnPlay', 'btnPlayDruk', 'btnCar', 'btnCarDruk', 'btnPrize', 'btnPrizeDruk', 'btnBack', 'btnBackDruk',
         'btnSet', 'btnSetBtn', 'cashIcon', 'cash', 'zwaailichtenBlue', 'zwaailichtenRed', 'zwaailichten', 'pauseBtn',
         'boxBase', 'boxBaseDown', 'boxBaseDownGreen', 'boxBaseDownGrey', 'btnBase 1', 'btnBaseDruk 1', 'btnEmpty',
         'btnEmptySelect', 'btnGreen', 'white', 'Health WheelSmall', 'handleBarPiece', 'btnBaseError', 'btnBaseDrukGreen']
for n in names:
    p = UI + 'T_%s.png' % n
    if not os.path.exists(p): p = UI + '%s.png' % n
    if not os.path.exists(p): print('missing', n); continue
    ui.append((n, img_entry(Image.open(p))))
for i in range(1, 31):
    for suf in ['', 'BW']:
        p = UI + 'T_Car%d%s.png' % (i, suf)
        if os.path.exists(p):
            ui.append(('Car%d%s' % (i, suf), img_entry(Image.open(p))))

# font atlas from the game's 8-bit TTF (white glyphs, alpha)
font = ImageFont.truetype(UI + '8Bit Font.ttf', 32)
from fontTools.ttLib import TTFont
cmap = TTFont(UI + '8Bit Font.ttf').getBestCmap()
import glob
fb_path = (glob.glob('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf') + glob.glob('/usr/share/fonts/**/*Bold*.ttf', recursive=True))[0]
fallback = ImageFont.truetype(fb_path, 30)
chars = [chr(c) for c in range(32, 127)]
cell = 48
atlas = Image.new('RGBA', (512, 512), (255, 255, 255, 0))
d = ImageDraw.Draw(atlas)
metrics = []
asc, desc = font.getmetrics()
for i, ch in enumerate(chars):
    cx, cy = (i % 10) * cell + 4, (i // 10) * cell + 4
    if ord(ch) in cmap or ch == ' ':
        d.text((cx, cy), ch, font=font, fill=(255, 255, 255, 255))
        adv = font.getlength(ch)
    else:
        small = ImageFont.truetype(fb_path, 11)
        g = Image.new('L', (14, 14), 0)
        ImageDraw.Draw(g).text((1, 0), ch, font=small, fill=255)
        g = g.point(lambda v: 255 if v > 110 else 0).resize((42, 42), Image.NEAREST)
        atlas.paste(Image.new('RGBA', (42, 42), (255, 255, 255, 255)), (cx, cy), g)
        bb = g.getbbox()
        adv = (bb[2] + 6) if bb else 18
    metrics.append((cx, cy, int(round(adv))))
fnt = struct.pack('<4sIII', b'FNT1', cell - 4, asc + desc, len(chars)) + b''.join(struct.pack('<HHH', *m) for m in metrics)
ui.append(('font', img_entry(atlas)))
ui.append(('fontmetrics', fnt))
write_pack(OUT + 'ui.pak', ui)
print('ui entries', len(ui), os.path.getsize(OUT + 'ui.pak') / 1e6)

# sounds
snd = pickle.load(open(B + 'snd.pkl', 'rb'))
alias = {
    'BUTTON Short (mono)': 'button', 'gravel loop 1': 'gravel', 'gearwhine': 'engine', 'muntSound': 'coin',
    '32544__freqman__t-clap': 'clap', '135434__cyberkineticfilms__industrial-warehouse-lights-turn-on-epic': 'reveal',
    'scrape loop 8': 'scrape8', 'SPLASH Big Rumble Noise (stereo)': 'splash', '12896__harri__circus-short': 'circus',
    '79140__razzvio__blowing-bubbles': 'bubbles', 'rocketSound': 'rocket', 'carImpact': 'impact',
    '8BIT RETRO Jump Three Note Up Rapid (mono)': 'jump', 'scrape loop 4': 'scrape', 'police': 'siren', 'turbo': 'turbo',
    'helicopterTest': 'heli', 'Turbo Dump': 'turbodump', 'skid loop 4': 'skid', 'wind_ext': 'wind',
    'UI Toggle Voice Tripe Note Enable (stereo)': 'toggle', 'boem': 'boom', 'Enemies': 'music',
    '265299__gblanke__waterblub-03': 'blub', '81173__mkoenig__horn-honk': 'horn', 'POP Mouth (mono)': 'pop',
    '8000-clickfix': 'click', '8BIT RETRO Beep Smooth Sine (mono)': 'beep', 'Btn2Sund': 'btn2', 'Btn1Sund': 'btn1',
    'ErrorSund': 'error', 'BUTTON Plastic Light Switch On (mono)': 'switchon', 'BUTTON Plastic Light Switch Off (mono)': 'switchoff'}

# IMA ADPCM encoder (C helper for speed)
enc_c = r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
static const int idxT[16]={-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
static const int stepT[89]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
int main(int c,char**v){int ch=atoi(v[1]);FILE*f=fopen(v[2],"rb");fseek(f,0,2);long n=ftell(f)/2;fseek(f,0,0);int16_t*s=malloc(n*2);fread(s,2,n,f);fclose(f);
 long frames=n/ch;int pred[2]={0,0},ix[2]={0,0};FILE*o=fopen(v[3],"wb");
 unsigned char nib[2];long cnt=0;unsigned char acc=0;
 for(long i=0;i<frames;i++)for(int k=0;k<ch;k++){int x=s[i*ch+k];int st=stepT[ix[k]];int d=x-pred[k];int code=0;if(d<0){code=8;d=-d;}
  int diff=st>>3;if(d>=st){code|=4;d-=st;diff+=st;}st>>=1;if(d>=st){code|=2;d-=st;diff+=st;}st>>=1;if(d>=st){code|=1;diff+=st;}
  if(code&8)pred[k]-=diff;else pred[k]+=diff;if(pred[k]>32767)pred[k]=32767;if(pred[k]<-32768)pred[k]=-32768;
  ix[k]+=idxT[code];if(ix[k]<0)ix[k]=0;if(ix[k]>88)ix[k]=88;
  if(cnt&1){acc|=code<<4;fputc(acc,o);}else acc=code;cnt++;}
 if(cnt&1)fputc(acc,o);fclose(o);return 0;}
'''
open('/tmp/adpcm.c', 'w').write(enc_c)
subprocess.check_call(['gcc', '-O2', '-o', '/tmp/adpcm', '/tmp/adpcm.c'])
entries = []
for nm, (ch, sw, rate, n, raw) in snd.items():
    a = alias.get(nm, nm)
    if a == 'music':
        open('/tmp/m.raw', 'wb').write(raw)
        subprocess.check_call(['/tmp/adpcm', str(ch), '/tmp/m.raw', '/tmp/m.adp'])
        data = open('/tmp/m.adp', 'rb').read()
        entries.append((a, struct.pack('<4sIIII', b'SND1', rate, ch, n, 1) + data))
    else:
        entries.append((a, struct.pack('<4sIIII', b'SND1', rate, ch, n, 0) + raw))
write_pack(OUT + 'sound.pak', entries)
print('sound', os.path.getsize(OUT + 'sound.pak') / 1e6)
