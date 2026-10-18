/*
  easyEEZYbotARM - ESP8266 WiFi Web Control
  ------------------------------------------
  Based on the architecture of https://github.com/meisben/easyEEZYbotARM
  Original: Python (kinematics) --serial--> Arduino (servo control)
  This version: Phone browser (kinematics, in JS) --WiFi--> ESP8266 (servo control)

  HARDWARE ASSUMED (same as the original repo's reference build):
    - ESP8266 (NodeMCU / Wemos D1 mini etc.)
    - PCA9685 16-channel PWM/servo driver board, connected over I2C
    - 4x hobby servos plugged into PCA9685 channels 0-3:
        channel 0 -> end effector / gripper servo
        channel 1 -> q1 (base rotation)
        channel 2 -> q2 (main arm)
        channel 3 -> q3 (horarm)
    - PCA9685 wiring to NodeMCU: VCC->3V3 (logic), GND->GND, SDA->D2, SCL->D1
      (Servo power (V+) on the PCA9685 must come from its own 5-6V supply,
       NOT from the ESP8266 3V3 pin - servos draw far too much current for that)

  WHAT THIS SKETCH DOES:
    1. Starts the ESP8266 as its own WiFi Access Point (no router / laptop needed)
    2. Serves a mobile-friendly control web page (embedded below, no SD card/LittleFS needed)
    3. Exposes a simple HTTP endpoint /move?q1=..&q2=..&q3=..&ee=..
       which takes KINEMATIC joint angles (degrees, same convention as the
       python library), converts them to physical SERVO angles using the
       exact same formulas as EEZYbotARM_Mk2.map_kinematicsToServoAngles(),
       and drives the servos via the PCA9685.

  CALIBRATION:
    - SERVOMIN / SERVOMAX below are a generic starting point for standard
      hobby servos on a 50Hz PCA9685 signal. If your arm's motion looks off,
      calibrate them (see "Improving the accuracy" section of the original
      README) and adjust SERVOMIN/SERVOMAX, or add the linear correction
      per-servo the way the original python code does for q1.

  PLATFORMIO PROJECT LAYOUT:
    your-project/
      platformio.ini      <- board/framework/lib_deps config
      src/
        main.cpp          <- this file
    ESP8266WiFi and ESP8266WebServer ship with the espressif8266 platform
    automatically. Adafruit PWM Servo Driver Library is pulled in via
    lib_deps in platformio.ini - no manual Library Manager step needed.
    Just run `pio run -t upload` (or the PlatformIO IDE upload button).
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

// ------------------------------------------------------------------
// WiFi Access Point settings - phone connects to THIS network
// ------------------------------------------------------------------
const char *AP_SSID = "EEZYbotARM";
const char *AP_PASS = "eezybot123"; // must be 8+ characters, change if you like

// ------------------------------------------------------------------
// PCA9685 / servo settings
// ------------------------------------------------------------------
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);

#define SERVO_FREQ 50 // standard hobby servo update rate (Hz)
#define SERVOMIN 150  // pulse length count for 0 degrees  (calibrate this)
#define SERVOMAX 600  // pulse length count for 180 degrees (calibrate this)

#define CH_EE 0 // end effector / gripper
#define CH_Q1 1 // base rotation
#define CH_Q2 2 // main arm
#define CH_Q3 3 // horarm

ESP8266WebServer server(80);

// ------------------------------------------------------------------
// Convert a servo angle in degrees (0-180) to a PCA9685 PWM count
// ------------------------------------------------------------------
int angleToPulse(float deg)
{
  deg = constrain(deg, 0.0, 180.0);
  return (int)((deg / 180.0) * (SERVOMAX - SERVOMIN) + SERVOMIN);
}

void setServoDeg(uint8_t channel, float deg)
{
  int pulse = angleToPulse(deg);
  Serial.printf("[SERVO] channel=%d  deg=%.2f  -> pulse=%d\n", channel, deg, pulse);
  pwm.setPWM(channel, 0, pulse);
}

// ------------------------------------------------------------------
// EXACT same mapping as EEZYbotARM_Mk2.map_kinematicsToServoAngles()
// in the original python library (kinematic_model.py)
// ------------------------------------------------------------------
float mapQ1ToServo(float q1)
{
  return (-2.0497 * q1) + 91.726; // from experimentation, per original repo
}

float mapQ2ToServo(float q2)
{
  return 180.0 - q2;
}

float mapQ3ToServo(float q2, float q3)
{
  float q3_a = 180.0 - (-q3);
  return q2 - 45.0 + q3_a;
}

// ------------------------------------------------------------------
// Web page (served directly from flash, no filesystem needed)
// ------------------------------------------------------------------
const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>EEZYbotARM Control</title>
<style>
  :root{ --bg:#12151a; --panel:#1c2028; --accent:#4fc3f7; --text:#eef2f6; --sub:#8b93a1; }
  *{box-sizing:border-box;}
  body{margin:0;background:var(--bg);color:var(--text);font-family:-apple-system,Segoe UI,Roboto,sans-serif;padding:12px;padding-bottom:40px;}
  h1{font-size:1.2rem;margin:4px 0 12px;}
  .panel{background:var(--panel);border-radius:12px;padding:14px;margin-bottom:12px;}
  .tabs{display:flex;gap:8px;margin-bottom:12px;}
  .tab{flex:1;text-align:center;padding:10px;border-radius:10px;background:var(--panel);color:var(--sub);font-weight:600;}
  .tab.active{background:var(--accent);color:#08222b;}
  .row{display:flex;justify-content:space-between;align-items:center;margin:10px 0 2px;font-size:.9rem;color:var(--sub);}
  input[type=range]{width:100%;height:36px;}
  input[type=number]{width:100%;padding:10px;border-radius:8px;border:1px solid #333;background:#0e1116;color:var(--text);font-size:1rem;}
  .grid3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;}
  button{width:100%;padding:14px;border:0;border-radius:10px;background:var(--accent);color:#08222b;font-weight:700;font-size:1rem;margin-top:10px;}
  button:active{opacity:.8;}
  button:disabled{background:#3a4048;color:#7a828d;}
  .hide{display:none;}
  #canvasWrap{background:#0e1116;border-radius:10px;padding:6px;}
  canvas{width:100%;height:220px;display:block;}
  #status{font-size:.8rem;color:var(--sub);margin-top:8px;white-space:pre-wrap;}
  select{width:100%;padding:10px;border-radius:8px;background:#0e1116;color:var(--text);border:1px solid #333;}
</style>
</head>
<body>

<h1>EEZYbotARM &middot; WiFi Control</h1>

<div class="panel" id="canvasWrap">
  <div class="row"><span>Top view &middot; tap to aim (left/right + reach)</span></div>
  <canvas id="topCanvas" width="600" height="260"></canvas>
  <div class="row" style="margin-top:14px"><span>Side view &middot; tap to set height</span></div>
  <canvas id="sideCanvas" width="600" height="260"></canvas>
</div>

<div class="tabs">
  <div class="tab active" id="tabJoint">Joint control</div>
  <div class="tab" id="tabCart">X / Y / Z</div>
</div>

<div class="panel" id="panelJoint">
  <div class="row"><span>q1 - base rotation</span><span id="q1v">0</span></div>
  <input type="range" id="q1" min="-20" max="30" step="1" value="0">
  <div class="row"><span>q2 - main arm</span><span id="q2v">90</span></div>
  <input type="range" id="q2" min="39" max="120" step="1" value="90">
  <div class="row"><span>q3 - horarm</span><span id="q3v">-100</span></div>
  <input type="range" id="q3" min="-140" max="-70" step="1" value="-100">
  <div class="row"><span>gripper</span><span id="eev">90</span></div>
  <input type="range" id="ee" min="10" max="100" step="1" value="90">
</div>

<div class="panel hide" id="panelCart">
  <p style="margin:0 0 4px;font-size:.9rem;color:var(--sub)">
    Tap the <strong>top view</strong> above to aim left/right and set reach,
    then tap the <strong>side view</strong> to set height. The dot turns
    green when the point is reachable.
  </p>
  <button id="goCart" disabled>Go to position</button>
</div>

<button id="sendBtn">Send to arm</button>
<div id="status" class="panel">Not connected yet</div>

<script>
// ---- Link lengths (mm), Mk1 only ----
const L = { L1:61, L2:80, L3:80, L4:57 };

// ---- Forward kinematics, ported line-for-line from forwardKinematics() ----
function forwardKinematics(q1,q2,q3){
  const d2r = Math.PI/180;
  const q1r=q1*d2r, q2r=q2*d2r, q3r=q3*d2r;
  const x = Math.cos(q1r)*(Math.cos(q2r+q3r)*L.L3 + Math.cos(q2r)*L.L2) + L.L4*Math.cos(q1r);
  const y = Math.sin(q1r)*(Math.cos(q2r+q3r)*L.L3 + Math.cos(q2r)*L.L2) + L.L4*Math.sin(q1r);
  const z = L.L1 + Math.sin(q2r)*L.L2 + Math.sin(q2r+q3r)*L.L3;
  return {x,y,z};
}

// ---- Inverse kinematics, ported line-for-line from inverseKinematics() ----
function inverseKinematics(x,y,z){
  const q1 = Math.atan2(y,x);
  const x4 = x - L.L4*Math.cos(q1);
  const y4 = y - L.L4*Math.sin(q1);
  const z4 = z;
  const z1 = L.L1;
  const z14 = z4 - z1;
  const xy4 = Math.sqrt(x4*x4 + y4*y4);
  const vSide = Math.sqrt(z14*z14 + xy4*xy4);
  const q3 = -(Math.PI - Math.acos((L.L2*L.L2 + L.L3*L.L3 - vSide*vSide)/(2*L.L2*L.L3)));
  const q2a = Math.atan2(z14, xy4);
  const q2b = Math.acos((vSide*vSide + L.L2*L.L2 - L.L3*L.L3)/(2*vSide*L.L2));
  const q2 = q2a + q2b;
  const r2d = 180/Math.PI;
  return { q1:q1*r2d, q2:q2*r2d, q3:q3*r2d };
}

// ---- Joint limits, exactly as in checkErrorJointLimits() / q3CalcLimits() ----
const Q1_MIN=-20, Q1_MAX=30, Q2_MIN=39, Q2_MAX=120;
function q3Limits(q2){
  return { min: -0.6755*q2 - 70.768, max: -0.7165*q2 - 13.144 };
}
function clampToLimits(q1,q2,q3){
  q1 = Math.min(Q1_MAX, Math.max(Q1_MIN, q1));
  q2 = Math.min(Q2_MAX, Math.max(Q2_MIN, q2));
  const lim = q3Limits(q2);
  q3 = Math.min(lim.max, Math.max(lim.min, q3));
  return {q1,q2,q3};
}

// ---- Workspace boundary check ----
// Returns whether (x,y,z) is actually reachable by the physical arm:
//  1. the triangle formed by L2/L3 must be able to close (acos domain check)
//  2. the resulting q1,q2,q3 must fall inside the real joint limits
// This mirrors what checkErrorJointLimits() guards against in the python lib,
// but here it's used to warn the person BEFORE anything is sent, rather than
// silently clamping to the nearest reachable pose.
function checkReachable(x,y,z){
  const q1 = Math.atan2(y,x);
  const x4 = x - L.L4*Math.cos(q1);
  const y4 = y - L.L4*Math.sin(q1);
  const z1 = L.L1;
  const z14 = z - z1;
  const xy4 = Math.sqrt(x4*x4 + y4*y4);
  const vSide = Math.sqrt(z14*z14 + xy4*xy4);

  // Triangle inequality: the point must lie within the annulus the
  // L2/L3 linkage can physically span (|L2-L3| <= vSide <= L2+L3)
  const minSpan = Math.abs(L.L2 - L.L3);
  const maxSpan = L.L2 + L.L3;
  if (vSide > maxSpan) return { reachable:false, reason:`Too far away (arm fully extended reaches ~${maxSpan.toFixed(0)}mm along that line, target needs ${vSide.toFixed(0)}mm)` };
  if (vSide < minSpan) return { reachable:false, reason:`Too close to the base column for the elbow to fold that tight` };

  const q3 = -(Math.PI - Math.acos((L.L2*L.L2 + L.L3*L.L3 - vSide*vSide)/(2*L.L2*L.L3)));
  const q2a = Math.atan2(z14, xy4);
  const q2b = Math.acos((vSide*vSide + L.L2*L.L2 - L.L3*L.L3)/(2*vSide*L.L2));
  const q2 = q2a + q2b;
  const r2d = 180/Math.PI;
  const q1d=q1*r2d, q2d=q2*r2d, q3d=q3*r2d;

  if (q1d < Q1_MIN || q1d > Q1_MAX) return { reachable:false, reason:`q1 (base) would need ${q1d.toFixed(1)}\u00b0, outside [${Q1_MIN},${Q1_MAX}]\u00b0` };
  if (q2d < Q2_MIN || q2d > Q2_MAX) return { reachable:false, reason:`q2 (main arm) would need ${q2d.toFixed(1)}\u00b0, outside [${Q2_MIN},${Q2_MAX}]\u00b0` };
  const lim = q3Limits(q2d);
  if (q3d < lim.min || q3d > lim.max) return { reachable:false, reason:`q3 (horarm) would need ${q3d.toFixed(1)}\u00b0, outside [${lim.min.toFixed(1)},${lim.max.toFixed(1)}]\u00b0 for that q2` };

  return { reachable:true, q1:q1d, q2:q2d, q3:q3d };
}

// ---- UI elements ----
const el = id => document.getElementById(id);
const q1El=el('q1'), q2El=el('q2'), q3El=el('q3'), eeEl=el('ee');
const q1v=el('q1v'), q2v=el('q2v'), q3v=el('q3v'), eev=el('eev');
const statusEl = el('status');
const topCanvas = el('topCanvas'), topCtx = topCanvas.getContext('2d');
const sideCanvas = el('sideCanvas'), sideCtx = sideCanvas.getContext('2d');

function currentAngles(){
  return {
    q1: parseFloat(q1El.value),
    q2: parseFloat(q2El.value),
    q3: parseFloat(q3El.value),
    ee: parseFloat(eeEl.value)
  };
}

// ---- Shared geometry for each canvas (used for both drawing AND tap->world conversion) ----
function sideGeom(){
  const w=sideCanvas.width, h=sideCanvas.height;
  const maxReach = L.L1 + L.L2 + L.L3; // tallest the arm can ever be
  const scale = Math.min((w*0.8)/(L.L2+L.L3+L.L4), (h*0.82)/maxReach);
  return { w, h, originX: w*0.10, originY: h*0.94, scale };
}
function topGeom(){
  const w=topCanvas.width, h=topCanvas.height;
  const maxR = L.L2+L.L3+L.L4;
  const scale = Math.min((w*0.42)/maxR, (h*0.42)/maxR);
  return { w, h, originX: w*0.12, originY: h*0.5, scale };
}

// ---- Side view: radial distance (r) vs height (z) ----
function drawSide(q1,q2,q3){
  const {w,h,originX,originY,scale} = sideGeom();
  sideCtx.clearRect(0,0,w,h);
  const d2r = Math.PI/180;
  const q2r=q2*d2r, q3r=q3*d2r;

  const p0 = {r:0, z:0};
  const p1 = {r:0, z:L.L1};
  const p2 = {r:Math.cos(q2r)*L.L2, z:L.L1+Math.sin(q2r)*L.L2};
  const p3 = {r:p2.r+Math.cos(q2r+q3r)*L.L3, z:p2.z+Math.sin(q2r+q3r)*L.L3};
  const p4 = {r:p3.r+L.L4, z:p3.z};
  const toScreen = p => ({ x: originX + p.r*scale, y: originY - p.z*scale });
  const pts = [p0,p1,p2,p3,p4].map(toScreen);

  sideCtx.strokeStyle="#333"; sideCtx.beginPath();
  sideCtx.moveTo(0, originY); sideCtx.lineTo(w, originY); sideCtx.stroke();

  // reach envelope: outer = fully extended, inner = fully folded (0 for Mk1, since L2==L3)
  const cx = originX, cy = originY - L.L1*scale;
  const outerR = (L.L2+L.L3+L.L4)*scale;
  const innerR = (Math.abs(L.L2-L.L3)+L.L4)*scale;
  sideCtx.strokeStyle="rgba(79,195,247,0.25)";
  sideCtx.beginPath(); sideCtx.arc(cx, cy, outerR, Math.PI, 1.7*Math.PI); sideCtx.stroke();
  if (innerR > 1){
    sideCtx.strokeStyle="rgba(79,195,247,0.15)";
    sideCtx.beginPath(); sideCtx.arc(cx, cy, innerR, Math.PI, 1.7*Math.PI); sideCtx.stroke();
  }

  if (window.__target){
    const t = window.__target;
    const r = Math.sqrt(t.x*t.x + t.y*t.y);
    const tp = toScreen({r, z:t.z});
    sideCtx.fillStyle = t.reachable ? "#66bb6a" : "#ef5350";
    sideCtx.beginPath(); sideCtx.arc(tp.x, tp.y, 8, 0, 7); sideCtx.stroke(); sideCtx.fill();
  }

  sideCtx.lineWidth=6; sideCtx.strokeStyle="#4fc3f7"; sideCtx.lineCap="round";
  for(let i=0;i<pts.length-1;i++){
    sideCtx.beginPath(); sideCtx.moveTo(pts[i].x, pts[i].y); sideCtx.lineTo(pts[i+1].x, pts[i+1].y); sideCtx.stroke();
  }
  sideCtx.fillStyle="#eef2f6";
  pts.forEach(p=>{ sideCtx.beginPath(); sideCtx.arc(p.x,p.y,5,0,7); sideCtx.fill(); });
  sideCtx.fillStyle="#ffb74d";
  sideCtx.beginPath(); sideCtx.arc(pts[4].x,pts[4].y,7,0,7); sideCtx.fill();
}

// ---- Top view: x (forward reach) vs y (left/right), bird's-eye ----
function drawTop(q1,q2,q3){
  const {w,h,originX,originY,scale} = topGeom();
  topCtx.clearRect(0,0,w,h);
  const d2r = Math.PI/180;

  // centerline + base marker
  topCtx.strokeStyle="#333"; topCtx.beginPath();
  topCtx.moveTo(originX, 0); topCtx.lineTo(originX, h); topCtx.stroke();

  // reachable sector: q1 in [Q1_MIN, Q1_MAX], radius in [innerR, outerR]
  const outerR = (L.L2+L.L3+L.L4)*scale;
  const innerR = Math.abs(L.L2-L.L3)*scale;
  const a1 = -Q1_MAX*d2r, a2 = -Q1_MIN*d2r; // screen angle flips sign vs world y
  topCtx.fillStyle="rgba(79,195,247,0.10)";
  topCtx.beginPath();
  topCtx.moveTo(originX + innerR*Math.cos(a1), originY + innerR*Math.sin(a1));
  topCtx.arc(originX, originY, outerR, a1, a2);
  topCtx.arc(originX, originY, innerR, a2, a1, true);
  topCtx.closePath(); topCtx.fill();
  topCtx.strokeStyle="rgba(79,195,247,0.3)"; topCtx.stroke();

  // current arm heading (top-down projection of current joint angles)
  const q1r=q1*d2r, q2r=q2*d2r, q3r=q3*d2r;
  const rCur = Math.cos(q2r)*L.L2 + Math.cos(q2r+q3r)*L.L3 + L.L4;
  const curX = originX + rCur*scale*Math.cos(q1r);
  const curY = originY - rCur*scale*Math.sin(q1r);
  topCtx.strokeStyle="#4fc3f7"; topCtx.lineWidth=4; topCtx.lineCap="round";
  topCtx.beginPath(); topCtx.moveTo(originX, originY); topCtx.lineTo(curX, curY); topCtx.stroke();
  topCtx.fillStyle="#ffb74d"; topCtx.beginPath(); topCtx.arc(curX, curY, 7, 0, 7); topCtx.fill();

  // base
  topCtx.fillStyle="#eef2f6"; topCtx.beginPath(); topCtx.arc(originX, originY, 5, 0, 7); topCtx.fill();

  // target marker
  if (window.__target){
    const t = window.__target;
    const tx = originX + t.x*scale, ty = originY - t.y*scale;
    topCtx.fillStyle = t.reachable ? "#66bb6a" : "#ef5350";
    topCtx.beginPath(); topCtx.arc(tx, ty, 8, 0, 7); topCtx.stroke(); topCtx.fill();
  }
}

function drawScene(){
  const a = currentAngles();
  drawSide(a.q1,a.q2,a.q3);
  drawTop(a.q1,a.q2,a.q3);
}

function syncLabelsAndDraw(){
  const a = currentAngles();
  q1v.textContent=a.q1; q2v.textContent=a.q2; q3v.textContent=a.q3; eev.textContent=a.ee;
  drawScene();
  const pos = forwardKinematics(a.q1,a.q2,a.q3);
  statusEl.textContent = `x:${pos.x.toFixed(1)}  y:${pos.y.toFixed(1)}  z:${pos.z.toFixed(1)} mm`;
}

[q1El,q2El,q3El,eeEl].forEach(inp => inp.addEventListener('input', syncLabelsAndDraw));

// ---- tabs ----
el('tabJoint').addEventListener('click', ()=>{
  el('tabJoint').classList.add('active'); el('tabCart').classList.remove('active');
  el('panelJoint').classList.remove('hide'); el('panelCart').classList.add('hide');
});
el('tabCart').addEventListener('click', ()=>{
  el('tabCart').classList.add('active'); el('tabJoint').classList.remove('active');
  el('panelCart').classList.remove('hide'); el('panelJoint').classList.add('hide');
});

// ---- Tap-to-aim target picking ----
// window.__target holds {x,y,z,reachable}; window.__theta remembers the last
// aimed direction so a side-view tap (which only carries r and z) can still
// reconstruct a full x,y,z point without losing the left/right aim.
window.__theta = 0;

function updateTargetPoint(x, y, z){
  const check = checkReachable(x,y,z);
  window.__target = { x, y, z, reachable: check.reachable };
  drawScene();
  const goBtn = el('goCart');
  if (check.reachable){
    goBtn.disabled = false;
    statusEl.textContent = `Reachable \u2713  (would need q1:${check.q1.toFixed(1)}\u00b0 q2:${check.q2.toFixed(1)}\u00b0 q3:${check.q3.toFixed(1)}\u00b0)`;
  } else {
    goBtn.disabled = true;
    statusEl.textContent = "Outside workspace \u2717  " + check.reason;
  }
}

function tapXY(evt, canvasEl){
  const rect = canvasEl.getBoundingClientRect();
  const src = evt.touches ? evt.touches[0] : evt;
  const scaleX = canvasEl.width/rect.width, scaleY = canvasEl.height/rect.height;
  return { px: (src.clientX-rect.left)*scaleX, py: (src.clientY-rect.top)*scaleY };
}

topCanvas.addEventListener('click', evt=>{
  const {originX,originY,scale} = topGeom();
  const {px,py} = tapXY(evt, topCanvas);
  const x = (px-originX)/scale;
  const y = (originY-py)/scale;
  window.__theta = Math.atan2(y,x);
  const z = window.__target ? window.__target.z : (L.L1+40);
  updateTargetPoint(x,y,z);
});

sideCanvas.addEventListener('click', evt=>{
  const {originX,originY,scale} = sideGeom();
  const {px,py} = tapXY(evt, sideCanvas);
  const r = Math.max(0, (px-originX)/scale);
  const z = (originY-py)/scale;
  const x = r*Math.cos(window.__theta);
  const y = r*Math.sin(window.__theta);
  updateTargetPoint(x,y,z);
});

el('goCart').addEventListener('click', ()=>{
  if (!window.__target || !window.__target.reachable) return;
  const t = window.__target;
  const check = checkReachable(t.x,t.y,t.z);
  if (!check.reachable){
    statusEl.textContent = "Outside workspace \u2717  " + check.reason;
    return;
  }
  q1El.value=check.q1.toFixed(1); q2El.value=check.q2.toFixed(1); q3El.value=check.q3.toFixed(1);
  syncLabelsAndDraw();
  sendToArm();
});

// ---- Send current joint angles to the ESP8266 ----
function sendToArm(){
  const a = currentAngles();
  const url = `/move?q1=${a.q1}&q2=${a.q2}&q3=${a.q3}&ee=${a.ee}`;
  statusEl.textContent = "Sending...";
  fetch(url).then(r=>r.json()).then(d=>{
    statusEl.textContent = "OK  " + JSON.stringify(d);
  }).catch(err=>{
    statusEl.textContent = "Error talking to arm: " + err;
  });
}
el('sendBtn').addEventListener('click', sendToArm);

syncLabelsAndDraw();
</script>
</body>
</html>
)HTMLPAGE";

// ------------------------------------------------------------------
// HTTP handlers
// ------------------------------------------------------------------
void handleRoot()
{
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleMove()
{
  Serial.println("---------------------------------------------");
  Serial.print("[HTTP] /move request from ");
  Serial.println(server.client().remoteIP());

  if (!server.hasArg("q1") || !server.hasArg("q2") || !server.hasArg("q3"))
  {
    Serial.println("[HTTP] REJECTED - missing q1/q2/q3 argument");
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing q1/q2/q3\"}");
    return;
  }

  float q1 = server.arg("q1").toFloat();
  float q2 = server.arg("q2").toFloat();
  float q3 = server.arg("q3").toFloat();
  float ee = server.hasArg("ee") ? server.arg("ee").toFloat() : 90.0;
  Serial.printf("[HTTP] received  q1=%.2f  q2=%.2f  q3=%.2f  ee=%.2f\n", q1, q2, q3, ee);

  // Convert kinematic angles -> physical servo angles (same formulas as python lib)
  float servo_q1 = mapQ1ToServo(q1);
  float servo_q2 = mapQ2ToServo(q2);
  float servo_q3 = mapQ3ToServo(q2, q3);
  Serial.printf("[MAP]  servo_q1=%.2f  servo_q2=%.2f  servo_q3=%.2f  servo_ee=%.2f\n", servo_q1, servo_q2, servo_q3, ee);

  setServoDeg(CH_Q1, servo_q1);
  setServoDeg(CH_Q2, servo_q2);
  setServoDeg(CH_Q3, servo_q3);
  setServoDeg(CH_EE, ee);
  Serial.println("[PCA9685] setPWM calls issued for all 4 channels");

  String resp = "{\"ok\":true,\"servo_q1\":" + String(servo_q1, 2) +
                ",\"servo_q2\":" + String(servo_q2, 2) +
                ",\"servo_q3\":" + String(servo_q3, 2) +
                ",\"servo_ee\":" + String(ee, 2) + "}";
  server.send(200, "application/json", resp);
  Serial.println("[HTTP] response sent to phone");
}

void handleNotFound()
{
  server.send(404, "text/plain", "Not found");
}

// ------------------------------------------------------------------
// I2C bus scanner - run once at boot. This is the single most useful
// diagnostic for "servos don't move": if the PCA9685 doesn't show up
// here, nothing downstream (mapping, PWM, servos) can possibly work,
// no matter how correct the rest of the code is.
// ------------------------------------------------------------------
void scanI2CBus()
{
  Serial.println("[I2C] Scanning bus...");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++)
  {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0)
    {
      Serial.printf("[I2C] Device found at 0x%02X", addr);
      if (addr == 0x40) Serial.print("  <-- this is your PCA9685 (default address)");
      Serial.println();
      found++;
    }
  }
  if (found == 0)
  {
    Serial.println("[I2C] !!! NO DEVICES FOUND !!!");
    Serial.println("[I2C] Check: SDA on D2, SCL on D1, PCA9685 VCC to 3V3, common GND wired.");
  }
  else
  {
    Serial.printf("[I2C] Scan complete, %d device(s) found.\n", found);
  }
}

// ------------------------------------------------------------------
// Setup / loop
// ------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== easyEEZYbotARM ESP8266 booting ===");

  Wire.begin(D2, D1); // SDA=D2, SCL=D1 on most NodeMCU boards
  Serial.println("[I2C] Wire.begin(D2, D1) done");
  scanI2CBus();

  pwm.begin();
  pwm.setPWMFreq(SERVO_FREQ);
  Serial.println("[PCA9685] pwm.begin() + setPWMFreq(50) done");
  delay(10);

  // Move to a safe home position within joint limits (q1=0, q2=90, q3=-100)
  Serial.println("[HOME] Moving to home position...");
  setServoDeg(CH_Q1, mapQ1ToServo(0));
  setServoDeg(CH_Q2, mapQ2ToServo(90));
  setServoDeg(CH_Q3, mapQ3ToServo(90, -100));
  setServoDeg(CH_EE, 90);
  Serial.println("[HOME] Home position commands sent");
  Serial.println("[HOME] If servos did not physically move just now, this is a POWER");
  Serial.println("[HOME] problem (PCA9685 V+ / servo supply), not a software problem -");
  Serial.println("[HOME] the I2C commands above are being sent regardless of servo power.");
  delay(500);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("Access point started. Connect to WiFi '");
  Serial.print(AP_SSID);
  Serial.println("' then browse to:");
  Serial.print("http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/move", handleMove);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("[HTTP] Web server started on port 80");
  Serial.println("=== Setup complete, ready for requests ===");
}

void loop()
{
  server.handleClient();
}
