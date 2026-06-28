import os,glob,json,subprocess,urllib.request,time
C=os.path.join(os.path.dirname(__file__),"corpus"); O=os.path.join(os.path.dirname(__file__),"out")
tok=json.loads(subprocess.check_output(
    ["security","find-generic-password","-s","Claude Code-credentials","-w"]))["claudeAiOauth"]["accessToken"]
MODEL="claude-opus-4-8"
def claude_tokens(text):
    body=json.dumps({"model":MODEL,"messages":[{"role":"user","content":text}]}).encode()
    req=urllib.request.Request("https://api.anthropic.com/v1/messages/count_tokens",data=body,headers={
        "authorization":"Bearer "+tok,"anthropic-version":"2023-06-01",
        "anthropic-beta":"oauth-2025-04-20","content-type":"application/json"})
    for a in range(5):
        try: return json.load(urllib.request.urlopen(req))["input_tokens"]
        except urllib.error.HTTPError as e:
            if a==4 or e.code not in (429,500,503,529): raise
            time.sleep(2*(a+1))
base=claude_tokens(".")-1  # "." = 1 token; isolate wrapper
rows=[]
for jf in sorted(glob.glob(os.path.join(C,"*.json"))):
    n=os.path.basename(jf)[:-5]; tf=os.path.join(O,n+".toon")
    jc=claude_tokens(open(jf).read())-base; tc=claude_tokens(open(tf).read())-base
    rows.append((n,jc,tc)); time.sleep(0.3)
print(f"# Claude tokenizer ({MODEL}), wrapper baseline={base} tok subtracted")
print(f"{'surface':<24}{'json':>8}{'toon':>8}{'Δ':>8}{'reduction':>10}")
sj=st=0
for n,jc,tc in rows:
    sj+=jc; st+=tc
    print(f"{n:<24}{jc:>8}{tc:>8}{jc-tc:>8}{100*(jc-tc)/jc:>9.1f}%")
print("-"*58)
print(f"{'TOTAL (all surfaces)':<24}{sj:>8}{st:>8}{sj-st:>8}{100*(sj-st)/sj:>9.1f}%")
FIX={'commands','formats','formula-functions','json-schema','redline-schema'}
fj=sum(jc for n,jc,tc in rows if n in FIX); ft=sum(tc for n,jc,tc in rows if n in FIX)
print(f"{'  fixed/help only':<24}{fj:>8}{ft:>8}{fj-ft:>8}{100*(fj-ft)/fj:>9.1f}%")
