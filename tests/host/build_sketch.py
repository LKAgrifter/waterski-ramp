import sys,re,os
d,out,main=sys.argv[1:4]
files=[main+'.ino']+sorted(f for f in os.listdir(d) if f.endswith('.ino') and f!=main+'.ino')
src=''.join('#line 1 "%s"\n'%f+open(os.path.join(d,f)).read()+'\n' for f in files)
protos=[]
for m in re.finditer(r'^(void|bool|float|int|uint8_t|uint16_t|LadderButton)\s+\w+\s*\([^;{)]*\)(?=\s*(?://[^\n]*)?\s*\{)',src,re.M):
    if '=' in m.group(0): continue
    protos.append(m.group(0).strip()+';')
i=src.index('void setup()')
open(out,'w').write(src[:i]+'\n'.join(protos)+'\n'+src[i:])
