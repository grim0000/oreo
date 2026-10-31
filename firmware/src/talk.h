// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
#pragma once
// Talk through the robot: the phone's microphone plays on the robot's speaker (the RC screen's
// mic button). Browsers give a page the microphone only when it's "secure" - https, or http the
// browser was told to trust (Chrome: chrome://flags "Insecure origins treated as secure"). So as
// well as the WebSocket on the normal server, the robot runs a small HTTPS server (port 443, a
// self-signed certificate: tls_cert.h) with its own talk page: https://<robot>/ - the browser warns
// once ("not private"), accept it for the robot. The audio: 16 kHz 16-bit mono PCM (aud::talkFeed).
#include <lwip/sockets.h>
#include <esp_tls.h>
#include <esp_https_server.h>
#include "mbedtls/platform.h"
#include "tls_cert.h"

httpd_handle_t tls_httpd = nullptr;

// WebSocket /talk: binary frames = PCM; text "vol=0.7" (the one volume), "gain=2" (voice level),
// "get" -> {"master":..,"talkGain":..}
esp_err_t talk_ws_handler(httpd_req_t* req) {
  if (req->method == HTTP_GET) return ESP_OK;  // the handshake
  httpd_ws_frame_t f;
  memset(&f, 0, sizeof(f));
  esp_err_t e = httpd_ws_recv_frame(req, &f, 0);  // the length first
  if (e != ESP_OK) return e;
  if (f.len == 0) return ESP_OK;
  if (f.len > 16384) return ESP_FAIL;
  uint8_t* b = (uint8_t*)heap_caps_malloc(f.len + 1, MALLOC_CAP_SPIRAM);
  if (!b) return ESP_ERR_NO_MEM;
  f.payload = b;
  e = httpd_ws_recv_frame(req, &f, f.len);
  if (e == ESP_OK && f.type == HTTPD_WS_TYPE_BINARY) {
    aud::talkFeed((const int16_t*)b, f.len / 2);
  } else if (e == ESP_OK && f.type == HTTPD_WS_TYPE_TEXT) {
    b[f.len] = 0;
    const char* s = (const char*)b;
    if (!strncmp(s, "vol=", 4)) {
      aud::masterVol = constrain(atof(s + 4), 0.0, 1.0);
      prefPut("volume", (int32_t)lroundf(aud::masterVol * 100), false);
    } else if (!strncmp(s, "gain=", 5)) {
      aud::talkGain = constrain(atof(s + 5), 0.5, 6.0);
    }
    char o[80];
    const int n = snprintf(o, sizeof(o), "{\"master\":%.2f,\"talkGain\":%.1f,\"talk\":%d}", (float)aud::masterVol, (float)aud::talkGain, aud::talkOn ? 1 : 0);
    httpd_ws_frame_t r;
    memset(&r, 0, sizeof(r));
    r.type = HTTPD_WS_TYPE_TEXT;
    r.payload = (uint8_t*)o;
    r.len = n;
    httpd_ws_send_frame(req, &r);
  }
  free(b);
  return e;
}

// The talk page (also the reference for the controller's RC mic button, which does the same)
const char TALK_PAGE[] = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<meta name="apple-mobile-web-app-capable" content="yes"><title>Talk through the robot</title><style>
:root{color-scheme:dark}*{box-sizing:border-box}
body{margin:0;background:#101014;color:#eee;font-family:system-ui,-apple-system,sans-serif;display:flex;flex-direction:column;align-items:center;min-height:100vh;padding:16px;gap:14px}
h1{font-size:19px;margin:8px 0 0}
.mic{width:180px;height:180px;border-radius:50%;border:none;background:radial-gradient(circle at 35% 30%,#ffa36b,#e0582a);color:#fff;font-size:64px;
 box-shadow:0 8px 30px rgba(255,120,60,.35);touch-action:none;user-select:none;-webkit-user-select:none;-webkit-touch-callout:none;transition:transform .1s}
.mic:active{transform:scale(.96)}
.mic.on{background:radial-gradient(circle at 35% 30%,#86ffab,#1fa04a);box-shadow:0 0 0 12px rgba(80,255,140,.16),0 8px 30px rgba(60,255,120,.35)}
.meter{width:240px;max-width:90vw;height:10px;background:#26262b;border-radius:5px;overflow:hidden}
.meter i{display:block;height:100%;width:0;background:linear-gradient(90deg,#4ade80,#facc15,#f87171);transition:width .08s}
.row{display:flex;gap:10px;align-items:center;font-size:14px;color:#ccc;flex-wrap:wrap;justify-content:center}
.row input[type=range]{width:180px;max-width:55vw}
.seg{display:inline-flex;border:1px solid #555;border-radius:9px;overflow:hidden}
.seg button{background:#26262b;color:#ccc;border:none;border-right:1px solid #444;padding:8px 12px;font-size:13px}
.seg button:last-child{border-right:none}.seg button.on{background:#ff8c42;color:#111;font-weight:bold}
#msg{font-size:13px;color:#aaa;text-align:center;max-width:330px;min-height:34px}a{color:#ffb27a;font-size:14px}
</style></head><body>
<h1>&#127908; Talk through the robot</h1>
<div id="msg">Hold the button and speak</div>
<button class="mic" id="mic">&#127908;</button>
<div class="meter"><i id="lvl"></i></div>
<div class="row"><span class="seg"><button id="mHold" class="on">Hold to talk</button><button id="mLock">Tap on / off</button></span></div>
<div class="row"><span class="seg"><button id="vN" class="on">My voice</button><button id="vR">Robot voice</button></span></div>
<div class="row">Voice <input type="range" id="gain" min="0.5" max="5" step="0.1" value="2"></div>
<div class="row">Volume <input type="range" id="vol" min="0" max="1" step="0.01" value="0.7"></div>
<a id="back" href="/">Back to the controller</a>
<script>
const $=id=>document.getElementById(id);
$('back').href='/';
let ws=null,ctx=null,stream=null,proc=null,talking=false,lock=false,robot=false,pos=0,ph=0;
const msg=t=>$('msg').textContent=t;
function wsOpen(){
  if(ws&&ws.readyState<=1)return;
  ws=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/talk');
  ws.binaryType='arraybuffer';
  ws.onopen=()=>{ws.send('get');if(!talking)msg(lock?'Tap the button to talk':'Hold the button and speak');};
  ws.onclose=()=>{ws=null;if(talking)msg('Reconnecting...');setTimeout(wsOpen,1000);};
  ws.onmessage=e=>{try{const j=JSON.parse(e.data);if(j.master!==undefined&&document.activeElement!==$('vol'))$('vol').value=j.master;if(j.talkGain!==undefined&&document.activeElement!==$('gain'))$('gain').value=j.talkGain;}catch(x){}};
}
async function micOn(){
  if(!navigator.mediaDevices||!navigator.mediaDevices.getUserMedia){msg('No microphone on this page - open it as https://'+location.hostname+'/');return false;}
  if(!stream){
    try{stream=await navigator.mediaDevices.getUserMedia({audio:{echoCancellation:true,noiseSuppression:true,autoGainControl:true}});}
    catch(e){msg('Microphone not allowed: '+e.message);return false;}
    ctx=new(window.AudioContext||window.webkitAudioContext)();
    const src=ctx.createMediaStreamSource(stream);
    proc=ctx.createScriptProcessor(2048,1,1);
    src.connect(proc);proc.connect(ctx.destination);
    proc.onaudioprocess=onAudio;
  }
  if(ctx.state==='suspended')await ctx.resume();
  return true;
}
function onAudio(e){
  const x=e.inputBuffer.getChannelData(0);
  e.outputBuffer.getChannelData(0).fill(0);
  let pk=0;for(let i=0;i<x.length;i++)pk=Math.max(pk,Math.abs(x[i]));
  $('lvl').style.width=(talking?Math.min(100,pk*140):0)+'%';
  if(!talking||!ws||ws.readyState!==1)return;
  const r=ctx.sampleRate/16000,out=[];
  for(;pos<x.length-1;pos+=r){
    const i=Math.floor(pos),f=pos-i;
    let v=x[i]*(1-f)+x[i+1]*f;
    if(robot){v*=1.7*Math.sin(ph);ph+=2*Math.PI*50/16000;}
    out.push(v);
  }
  pos-=x.length-1;
  const pcm=new Int16Array(out.length);
  for(let i=0;i<out.length;i++)pcm[i]=Math.max(-32767,Math.min(32767,out[i]*32767));
  if(ws.bufferedAmount<24000)ws.send(pcm.buffer);
}
async function start(){if(!(await micOn()))return;wsOpen();talking=true;$('mic').classList.add('on');msg('Talking - the robot is saying it');}
function stop(){talking=false;$('mic').classList.remove('on');msg(lock?'Tap the button to talk':'Hold the button and speak');}
$('mic').addEventListener('pointerdown',e=>{e.preventDefault();if(lock){talking?stop():start();}else start();});
['pointerup','pointercancel','pointerleave'].forEach(n=>$('mic').addEventListener(n,()=>{if(!lock&&talking)stop();}));
$('mic').addEventListener('contextmenu',e=>e.preventDefault());
const seg=(a,b,f)=>{$(a).onclick=()=>{$(a).classList.add('on');$(b).classList.remove('on');f(true);};$(b).onclick=()=>{$(b).classList.add('on');$(a).classList.remove('on');f(false);};};
seg('mHold','mLock',h=>{lock=!h;stop();});
seg('vN','vR',n=>{robot=!n;});
$('gain').oninput=()=>{if(ws&&ws.readyState===1)ws.send('gain='+$('gain').value);};
$('vol').oninput=()=>{if(ws&&ws.readyState===1)ws.send('vol='+$('vol').value);};
wsOpen();
</script></body></html>)HTML";

esp_err_t talk_page_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, TALK_PAGE, sizeof(TALK_PAGE) - 1);
}

// TLS memory in PSRAM: every connection needs ~40 KB of buffers, and internal RAM is tight
// (mbedTLS is built to use internal RAM only; its allocator can be swapped at run time)
static void* tlsCalloc(size_t n, size_t size) {
  // all of it (the small pieces too: ~5 KB internal per connection otherwise, and the camera and
  // the WiFi need that memory)
  void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_calloc(n, size, MALLOC_CAP_8BIT);
  return p;
}
static void tlsFree(void* p) { heap_caps_free(p); }

// The HTTPS server (port 443): the whole controller again, secure - so the RC screen can use the
// phone's microphone. main.cpp registers every handler on it too (startServer), plus the video
// as a WebSocket (/vws: a secure page can't read the plain :81 stream). /talk.html is the small
// talk-only page. Up to 6 connections, kept open (a new one costs ~1 s of TLS handshake); their
// TLS buffers are in PSRAM.
// TCP keep-alive probes on every connection (all three servers): a phone that left, or a test that
// ended without closing, is noticed in ~11 s and its socket freed. Without them kept-alive secure
// connections stay open forever - and the robot has 16 sockets for everything.
static void sockKeepAlive(int fd) {
  int on = 1, idle = 5, intvl = 2, cnt = 3;
  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
}
static esp_err_t httpOpenKeepAlive(httpd_handle_t, int fd) {  // httpd open_fn (plain servers)
  sockKeepAlive(fd);
  return ESP_OK;
}
static void tlsUserCb(esp_https_server_user_cb_arg_t* a) {  // after a secure connection's handshake
  int fd = -1;
  if (a && a->tls && esp_tls_get_conn_sockfd((esp_tls_t*)a->tls, &fd) == ESP_OK && fd >= 0) sockKeepAlive(fd);
}

void startTlsServer() {
  if (tls_httpd) return;
  mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree);
  httpd_ssl_config_t c = HTTPD_SSL_CONFIG_DEFAULT();
  // this robot's own certificate (made on first boot, tls_cert.h)
  if (!tlsid::get(&c.cacert_pem, &c.cacert_len, &c.prvtkey_pem, &c.prvtkey_len)) return;
  c.httpd.ctrl_port = 32770;  // 32768 / 32769: the other two servers
  c.httpd.max_open_sockets = 5;  // kept open (keep-alive): the page's fetches + video + mic (lwIP has 16 for all)
  c.httpd.max_uri_handlers = 48;
  c.httpd.lru_purge_enable = true;
  c.httpd.stack_size = 10240;
  // A new connection's TLS handshake is ~1 s of number crunching: on core 0, so it never holds up
  // loop() (core 1: legs, sensors, OLED)
  c.httpd.core_id = 0;
  c.httpd.uri_match_fn = httpd_uri_match_wildcard;  // "/ai/*"
  c.user_cb = tlsUserCb;  // keep-alive probes: dead connections go
  if (httpd_ssl_start(&tls_httpd, &c) != ESP_OK) {
    tls_httpd = nullptr;
    xlogSys("HTTPS talk server did not start (free heap %u)", ESP.getFreeHeap());
    return;
  }
  httpd_uri_t page = {};
  page.uri = "/talk.html";
  page.method = HTTP_GET;
  page.handler = talk_page_handler;
  httpd_register_uri_handler(tls_httpd, &page);
}
