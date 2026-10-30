import json, subprocess, sys, collections
repo, path, out = sys.argv[1], sys.argv[2], sys.argv[3]
log = subprocess.run(["git","-C",repo,"log","--format=%h %ad","--date=format:%Y-%m","--",path],capture_output=True,text=True).stdout.split("\n")
months = {}
for line in log:
    if not line: continue
    h,m = line.split()
    months.setdefault(m,h)   # newest commit of each month (log is newest-first)
first = {}
info = {}
for m in sorted(months):
    raw = subprocess.run(["git","-C",repo,"show",f"{months[m]}:{path}"],capture_output=True).stdout
    try: d = json.loads(raw)
    except Exception as e: print("skip",m,e,file=sys.stderr); continue
    for k,v in d.items():
        if k not in first:
            first[k]=m
            info[k]={f:v.get(f) for f in ("name","land","area","dungeon","rarity","element","guide_id") if f in v}
    print(m,len(d),file=sys.stderr)
json.dump({k:{"first_seen":first[k],**info[k]} for k in first}, open(out,"w"), indent=0, ensure_ascii=False)
