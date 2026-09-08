"""ECHO recognition server — runs on your laptop, not the ESP32."""
from flask import Flask, render_template_string, jsonify, request
import requests, os, json, datetime, math
ESP32_IP = "10.83.86.172"          # update if the board's IP changes
PEOPLE_FILE = "people.json"
PLACES_FILE = "places.json"
FACE_THRESHOLD = 0.52              # lower = stricter. 0.5-0.6 is the useful range
PLACE_THRESHOLD = 0.45
app = Flask(__name__)
def load(path, default):
    try:
        with open(path, encoding="utf-8") as f: return json.load(f)
    except Exception: return default
def save(path, data):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))
def find_person(desc):
    best, bd = None, 999.0
    for p in load(PEOPLE_FILE, {"people": []})["people"]:
        for known in p.get("descriptors", []):
            d = dist(desc, known)
            if d < bd: bd, best = d, p
    return (best, bd) if best and bd <= FACE_THRESHOLD else (None, bd)
def meta_now():
    try:
        r = requests.get(f"http://{ESP32_IP}/meta", timeout=6); r.raise_for_status()
        return r.json()
    except Exception as e:
        return {"time": datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                "location_aps": [], "meta_error": str(e)}
def place_now(meta):
    aps = {a["ssid"] for a in meta.get("location_aps", []) if a.get("ssid")}
    if not aps: return None
    best, bs = None, 0.0
    for pl in load(PLACES_FILE, {"places": []})["places"]:
        known = {a["ssid"] for a in pl.get("aps", []) if a.get("ssid")}
        if not known: continue
        s = len(aps & known) / len(aps | known)
        if s > bs: bs, best = s, pl["name"]
    return best if bs >= PLACE_THRESHOLD else None
def ago(ts):
    try: t = datetime.datetime.strptime(ts, "%Y-%m-%d %H:%M:%S")
    except Exception: return ts
    s = (datetime.datetime.now() - t).total_seconds()
    if s < 90: return "just now"
    if s < 3600: return f"{int(s//60)} minutes ago"
    if s < 86400: return f"{int(s//3600)} hours ago"
    d = int(s // 86400)
    return "yesterday" if d == 1 else (f"{d} days ago" if d < 30 else f"{d//30} months ago")
HTML = """
<!doctype html><title>ECHO</title>
<style>
body{font-family:system-ui,sans-serif;background:#0f1115;color:#e8eaed;margin:0;padding:24px;
     display:flex;gap:24px;justify-content:center;align-items:flex-start;flex-wrap:wrap}
h1{font-size:18px;margin:0 0 14px}
#wrap{position:relative;display:inline-block;line-height:0}
img{width:480px;max-width:100%;border-radius:10px}
canvas{position:absolute;top:0;left:0;border-radius:10px}
.panel{background:#181b22;border:1px solid #2a2f3a;border-radius:10px;padding:16px;
       margin-top:14px;max-width:420px}
.label{font-size:11px;text-transform:uppercase;letter-spacing:.08em;color:#9aa0aa;margin-bottom:8px}
#who{font-size:26px;font-weight:650;margin-bottom:6px}
#detail{font-size:14px;color:#9aa0aa;line-height:1.6}
input{background:#11141a;color:#e8eaed;border:1px solid #2a2f3a;border-radius:7px;
      padding:9px 11px;width:100%;box-sizing:border-box;margin-bottom:8px;font:inherit}
button{background:#6ee7a8;color:#0b0e13;border:none;border-radius:7px;padding:9px 15px;
       font:inherit;font-weight:600;cursor:pointer}
button:disabled{opacity:.4;cursor:default}
#status{font-size:12px;color:#9aa0aa;margin-top:10px}
#log{font-size:11px;color:#6b7280;margin-top:10px;max-height:130px;overflow-y:auto;line-height:1.7}
</style>
<div>
  <h1>ECHO — live</h1>
  <div id="wrap"><img id="feed" crossorigin="anonymous"><canvas id="overlay"></canvas></div>
  <div id="status">loading recognition models...</div>
</div>
<div>
  <div class="panel"><div class="label">Who is this</div>
    <div id="who">—</div><div id="detail">Point the camera at someone.</div></div>
  <div class="panel"><div class="label">Enrol the face on screen</div>
    <input id="nameInput" placeholder="Name (e.g. Ashutosh Rath)">
    <input id="relInput" placeholder="Relationship (e.g. son, doctor)">
    <button id="enrolBtn" disabled>Enrol this face</button></div>
  <div class="panel"><div class="label">Name this place</div>
    <input id="placeInput" placeholder="Place name (e.g. Home, Lab)">
    <button id="placeBtn">Save current place</button></div>
  <div id="log"></div>
</div>
<script src="https://cdn.jsdelivr.net/npm/face-api.js@0.22.2/dist/face-api.min.js"></script>
<script>
const IP=" {{ ip }} ".trim(), img=document.getElementById('feed'),
      cv=document.getElementById('overlay'), st=document.getElementById('status'),
      lg=document.getElementById('log'), who=document.getElementById('who'),
      det=document.getElementById('detail'), enrolBtn=document.getElementById('enrolBtn');
let desc=null, lastId=null, lastAt=0, busy=false;
const OPTS=new faceapi.TinyFaceDetectorOptions({inputSize:416, scoreThreshold:0.35});
function log(m){const d=document.createElement('div');
  d.textContent=new Date().toLocaleTimeString()+'  '+m; lg.prepend(d);}
async function boot(){
  const U='https://cdn.jsdelivr.net/gh/justadudewhohacks/face-api.js@master/weights';
  await faceapi.nets.tinyFaceDetector.loadFromUri(U);
  await faceapi.nets.faceLandmark68Net.loadFromUri(U);
  await faceapi.nets.faceRecognitionNet.loadFromUri(U);
  st.textContent='models ready — watching'; tick();
}
function tick(){ img.src='http://'+IP+'/snapshot?_='+Date.now(); }
img.onload=async()=>{
  // canvas bitmap must match the camera's REAL resolution, because that is the
  // coordinate space face-api reports boxes in. CSS then scales it to match the
  // displayed image. Using img.width here puts the box on your shoulder.
  cv.width=img.naturalWidth; cv.height=img.naturalHeight;
  cv.style.width=img.clientWidth+'px'; cv.style.height=img.clientHeight+'px';
  const ctx=cv.getContext('2d'); ctx.clearRect(0,0,cv.width,cv.height);
  let r=null;
  try{ r=await faceapi.detectSingleFace(img,OPTS)
        .withFaceLandmarks().withFaceDescriptor(); }
  catch(e){ st.textContent='detection error: '+e; }
  if(!r){ desc=null; if(!busy) enrolBtn.disabled=true;
    st.textContent='no face in view'; who.textContent='—';
    det.textContent='Point the camera at someone.'; return setTimeout(tick,400); }
  const b=r.detection.box; ctx.strokeStyle='#6ee7a8'; ctx.lineWidth=3;
  ctx.strokeRect(b.x,b.y,b.width,b.height);
  desc=Array.from(r.descriptor); if(!busy) enrolBtn.disabled=false;
  try{
    const res=await fetch('/api/identify',{method:'POST',
      headers:{'Content-Type':'application/json'},body:JSON.stringify({descriptor:desc})});
    const d=await res.json();
    if(d.known){
      who.textContent=d.name; det.innerHTML=d.detail;
      st.textContent='recognised (distance '+d.distance.toFixed(2)+')';
      const n=Date.now();
      if(d.id!==lastId||n-lastAt>30000){ lastId=d.id; lastAt=n;
        fetch('/api/encounter',{method:'POST',headers:{'Content-Type':'application/json'},
          body:JSON.stringify({id:d.id})}).then(r=>r.json())
          .then(x=>{if(x.ok)log('logged meeting with '+d.name+(x.place?' at '+x.place:''));});}
    } else {
      who.textContent='Unknown';
      det.textContent="I don't know this person yet. Type a name and press Enrol.";
      st.textContent='unknown face (closest '+d.distance.toFixed(2)+')';
    }
  }catch(e){ st.textContent='identify failed: '+e; }
  setTimeout(tick,400);
};
img.onerror=()=>{ st.textContent='could not reach ESP32 at '+IP; setTimeout(tick,2000); };
enrolBtn.onclick=async()=>{
  const n=document.getElementById('nameInput').value.trim();
  if(!n) return alert('Type a name first.');
  if(!desc) return alert('No face on screen.');
  busy=true; enrolBtn.disabled=true; enrolBtn.textContent='saving...';
  try{
    const res=await fetch('/api/enrol',{method:'POST',headers:{'Content-Type':'application/json'},
      body:JSON.stringify({name:n,relationship:document.getElementById('relInput').value.trim(),
      descriptor:desc})});
    const d=await res.json();
    if(d.ok){ log(d.again?('added another angle for '+n):('enrolled '+n));
      document.getElementById('nameInput').value='';
      document.getElementById('relInput').value=''; }
    else log('enrol failed: '+d.error);
  }catch(e){ log('enrol failed: '+e); }
  busy=false; enrolBtn.textContent='Enrol this face';
};
document.getElementById('placeBtn').onclick=async()=>{
  const btn=document.getElementById('placeBtn');
  const n=document.getElementById('placeInput').value.trim();
  if(!n) return alert('Type a place name first.');
  btn.disabled=true; btn.textContent='reading WiFi...';
  try{
    const res=await fetch('/api/place',{method:'POST',headers:{'Content-Type':'application/json'},
      body:JSON.stringify({name:n})});
    const d=await res.json();
    if(d.ok){ log('saved place "'+n+'" ('+d.n+' networks)');
      document.getElementById('placeInput').value=''; }
    else log('save place failed: '+d.error);
  }catch(e){ log('save place failed: '+e); }
  btn.disabled=false; btn.textContent='Save current place';
};
boot();
</script>
"""
@app.route('/')
def index(): return render_template_string(HTML, ip=ESP32_IP)
@app.route('/api/identify', methods=['POST'])
def identify():
    d = request.json.get("descriptor")
    if not d: return jsonify({"known": False, "distance": 9.99})
    p, dd = find_person(d)
    if not p: return jsonify({"known": False, "distance": dd})
    enc = p.get("encounters", [])
    bits = []
    if p.get("relationship"): bits.append(f"Your <b>{p['relationship']}</b>.")
    if p.get("first_met_place"):
        bits.append(f"First met at <b>{p['first_met_place']}</b>, {ago(p.get('first_met',''))}.")
    elif p.get("first_met"): bits.append(f"First met {ago(p['first_met'])}.")
    bits.append(f"You've met <b>{len(enc)}</b> time{'s' if len(enc)!=1 else ''}.")
    if len(enc) > 1:
        pv = enc[-2]; w = f" at {pv['place']}" if pv.get("place") else ""
        bits.append(f"Last time was {ago(pv['time'])}{w}.")
    return jsonify({"known": True, "id": p["id"], "name": p["name"],
                    "distance": dd, "detail": "<br>".join(bits)})
@app.route('/api/enrol', methods=['POST'])
def enrol():
    b = request.json; name = (b.get("name") or "").strip(); d = b.get("descriptor")
    if not name or not d: return jsonify({"ok": False, "error": "name and face required"})
    store = load(PEOPLE_FILE, {"people": []}); m = meta_now(); pl = place_now(m)
    ex = next((p for p in store["people"] if p["name"].lower() == name.lower()), None)
    if ex:
        ex.setdefault("descriptors", []).append(d); save(PEOPLE_FILE, store)
        return jsonify({"ok": True, "again": True})
    store["people"].append({"id": f"p{len(store['people'])+1}", "name": name,
        "relationship": (b.get("relationship") or "").strip(), "descriptors": [d],
        "first_met": m.get("time"), "first_met_place": pl,
        "encounters": [{"time": m.get("time"), "place": pl, "note": "first meeting"}]})
    save(PEOPLE_FILE, store)
    return jsonify({"ok": True, "again": False})
@app.route('/api/encounter', methods=['POST'])
def encounter():
    store = load(PEOPLE_FILE, {"people": []})
    p = next((x for x in store["people"] if x["id"] == request.json.get("id")), None)
    if not p: return jsonify({"ok": False, "error": "unknown id"})
    m = meta_now(); pl = place_now(m)
    p.setdefault("encounters", []).append({"time": m.get("time"), "place": pl, "note": ""})
    save(PEOPLE_FILE, store)
    return jsonify({"ok": True, "place": pl})
@app.route('/api/place', methods=['POST'])
def place():
    name = (request.json.get("name") or "").strip()
    if not name: return jsonify({"ok": False, "error": "name required"})
    m = meta_now(); aps = m.get("location_aps", [])
    if not aps: return jsonify({"ok": False, "error": "no WiFi list " + m.get("meta_error", "")})
    store = load(PLACES_FILE, {"places": []})
    store["places"] = [p for p in store["places"] if p["name"].lower() != name.lower()]
    store["places"].append({"name": name, "aps": aps, "saved": m.get("time")})
    save(PLACES_FILE, store)
    return jsonify({"ok": True, "n": len(aps)})
@app.route('/api/people')
def people():
    return jsonify({"people": [{"name": p["name"], "relationship": p.get("relationship"),
        "first_met": p.get("first_met"), "place": p.get("first_met_place"),
        "times_met": len(p.get("encounters", [])), "samples": len(p.get("descriptors", []))}
        for p in load(PEOPLE_FILE, {"people": []})["people"]]})
if __name__ == '__main__':
    app.run(host='127.0.0.1', port=5000, debug=True)