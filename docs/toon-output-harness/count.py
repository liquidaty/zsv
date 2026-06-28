import os,glob,tiktoken,json
C=os.path.join(os.path.dirname(__file__),"corpus"); O=os.path.join(os.path.dirname(__file__),"out")
o2=tiktoken.get_encoding("o200k_base"); cl=tiktoken.get_encoding("cl100k_base")
# verify lossless round-trip is not something we can do without a toon2json; instead assert json parses
rows=[]
for jf in sorted(glob.glob(os.path.join(C,"*.json"))):
    name=os.path.basename(jf)[:-5]; tf=os.path.join(O,name+".toon")
    j=open(jf).read(); t=open(tf).read()
    json.loads(j)  # sanity: source is valid json
    rows.append((name,len(o2.encode(j)),len(o2.encode(t)),len(cl.encode(j)),len(cl.encode(t))))
def line(n,j2,t2,jc,tc):
    p2=100*(j2-t2)/j2; pc=100*(jc-tc)/jc
    return f"{n:<24}{j2:>8}{t2:>8}{j2-t2:>7}{p2:>6.1f}%   {jc:>8}{tc:>8}{pc:>6.1f}%"
print(f"{'surface':<24}{'json':>8}{'toon':>8}{'Δ':>7}{'%':>7}   {'json':>8}{'toon':>8}{'%':>7}")
print(f"{'':<24}{'--- o200k_base ---':>30}   {'--- cl100k_base ---':>31}")
sj2=st2=sjc=stc=0
FIXED={'commands','formats','formula-functions','json-schema','redline-schema'}
for r in rows:
    print(line(*r)); sj2+=r[1];st2+=r[2];sjc+=r[3];stc+=r[4]
print("-"*78)
print(line("TOTAL (all surfaces)",sj2,st2,sjc,stc))
fj2=sum(r[1] for r in rows if r[0] in FIXED); ft2=sum(r[2] for r in rows if r[0] in FIXED)
fjc=sum(r[3] for r in rows if r[0] in FIXED); ftc=sum(r[4] for r in rows if r[0] in FIXED)
print(line("  fixed/help only",fj2,ft2,fjc,ftc))
