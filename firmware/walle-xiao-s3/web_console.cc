#include "web_console.h"

#include "application.h"
#include "audio_codec.h"
#include "audio_diag.h"
#include "chitu_cert.h"
#include "config.h"
#include "device_state_machine.h"
#include "motor_controller.h"
#include "pet_mode.h"
#include "remote_audio.h"
#include "settings.h"

#include <esp_camera.h>
#include <esp_heap_caps.h>
#include <esp_https_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#define TAG "WebConsole"

namespace {

// WebSocket binary frame types (first byte)
constexpr uint8_t kWsMicIn = 0x01;     // phone -> robot: 16 kHz int16 mono
constexpr uint8_t kWsVideo = 0x10;     // robot -> phone: JPEG frame
constexpr uint8_t kWsVoice = 0x20;     // robot -> phone: Chitu's voice, output sample rate
constexpr uint8_t kWsRoom = 0x21;      // robot -> phone: room sound, 16 kHz
constexpr int kVideoIntervalMs = 110;  // ~9 fps
constexpr size_t kRxMax = 8192;

const char kIndexHtml[] = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Chitu</title><style>
:root{--bg:#10161a;--panel:#1b242a;--line:#2b3840;--t:#e8eef2;--m:#8fa3ae;--eye:#7fe3ff;--warn:#ffb347}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;background:var(--bg);color:var(--t);font:15px system-ui,-apple-system,Segoe UI,Roboto,sans-serif;user-select:none;-webkit-user-select:none}
main{max-width:480px;margin:0 auto;padding:10px 12px 24px}
header{display:flex;align-items:center;justify-content:space-between;margin:2px 0 10px}
h1{font-size:22px;letter-spacing:3px;margin:0}h1 b{color:var(--eye)}
.pill{font-size:12px;padding:4px 10px;border-radius:20px;background:var(--panel);border:1px solid var(--line);color:var(--m)}
.pill.on{color:#10161a;background:var(--eye);border-color:var(--eye)}
.cam{position:relative;background:#000;border-radius:14px;overflow:hidden;aspect-ratio:4/3;border:1px solid var(--line)}
.cam img{width:100%;height:100%;object-fit:cover;display:block}
.msg{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;color:var(--m);font-size:14px;text-align:center;padding:20px}
.dist{position:absolute;left:8px;bottom:8px;background:#000a;padding:3px 8px;border-radius:8px;font-size:13px}
.dist.near{color:var(--warn)}
h2{font-size:13px;color:var(--m);font-weight:600;letter-spacing:1px;text-transform:uppercase;margin:16px 4px 8px}
.btns{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.btns button,.tog{padding:11px 8px;border-radius:12px;border:1px solid var(--line);background:var(--panel);color:var(--t);font-size:14px;font-weight:600}
.btns button.act,.tog.act{background:var(--eye);color:#10161a;border-color:var(--eye)}
.talk{width:100%;padding:18px;border-radius:16px;border:1px solid var(--line);background:var(--panel);color:var(--t);font-size:18px;font-weight:700;touch-action:none}
.talk.held{background:#e0405a;border-color:#e0405a;color:#fff}
.pad{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin:6px auto;max-width:300px}
.pad button{aspect-ratio:1;border-radius:16px;border:1px solid var(--line);background:var(--panel);color:var(--t);font-size:26px;touch-action:none}
.pad button.held{background:var(--eye);color:#10161a}
.pad .stop{background:#5a1d1d;border-color:#8a2c2c;font-size:14px;font-weight:700}
.pad .blank{visibility:hidden}
.row{display:flex;align-items:center;gap:10px;margin:10px 4px;color:var(--m);font-size:14px}
.row input[type=range]{flex:1;accent-color:var(--eye)}.row span{width:36px;text-align:right;color:var(--t)}
.say{display:flex;gap:8px}.say input{flex:1;min-width:0;padding:12px;border-radius:12px;border:1px solid var(--line);background:var(--panel);color:var(--t);font-size:16px;user-select:text;-webkit-user-select:text}
.say button{padding:0 16px;border-radius:12px;border:0;background:var(--eye);color:#10161a;font-weight:700;font-size:15px}
.emo{display:flex;flex-wrap:wrap;gap:4px;margin:8px 0}.emo button{font-size:22px;background:none;border:0;padding:2px 4px}
.chk{display:flex;align-items:center;gap:8px;color:var(--m);font-size:14px;margin:6px 4px}
.info{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-top:10px}
.info div{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:8px;text-align:center;font-size:12px;color:var(--m)}
.info b{display:block;color:var(--t);font-size:14px;margin-top:2px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.note{color:var(--warn);font-size:13px;margin:8px 4px;display:none}
footer{color:var(--m);font-size:11px;text-align:center;margin:16px 0 4px}
</style></head><body><main>
<header><h1>CHI<b>TU</b></h1><span id="conn" class="pill">connecting…</span></header>
<div class="cam"><img id="cam" alt=""><div id="cammsg" class="msg">Connecting to camera…</div><div id="dist" class="dist">— cm</div></div>
<div class="btns" style="margin-top:8px;grid-template-columns:1fr 1fr 1fr"><button id="cOn" class="act">Camera</button><button id="cFlip">Flip ↕</button><button id="cMirror">Mirror ↔</button></div>

<h2>Voice</h2>
<button id="talk" class="talk">🎤 Hold to talk to Chitu</button>
<div class="note" id="micnote"></div>
<div class="btns" style="margin-top:8px">
<button id="tPhone" class="tog act">🔊 Chitu on phone</button><button id="tSpk" class="tog act">🤖 Robot speaker</button>
<button id="tRoom" class="tog">👂 Hear the room</button><button id="tMic" class="tog">🎙️ Phone mic always</button></div>

<h2>Drive</h2>
<div class="btns"><button id="mManual" class="act">Manual</button><button id="mAuto">Auto roam</button></div>
<div class="pad">
<button class="blank"></button><button data-d="forward">▲</button><button class="blank"></button>
<button data-d="left">◀</button><button class="stop" id="stop">STOP</button><button data-d="right">▶</button>
<button class="blank"></button><button data-d="backward">▼</button><button class="blank"></button></div>
<div class="row">Speed<input id="speed" type="range" min="30" max="100" value="70"><span id="speedv">70</span></div>

<h2>Message on Chitu's screen</h2>
<div class="say"><input id="text" maxlength="120" placeholder="Type a message… 😀" enterkeyhint="send"><button id="send">Send</button></div>
<div class="emo" id="emo"></div>
<label class="chk"><input type="checkbox" id="sayit"> Chitu also says it out loud</label>

<h2>Pet</h2>
<div class="btns"><button id="tPet" class="tog act">🐾 Pet mode</button><button id="tChat" class="tog act">💬 Chatty</button></div>
<div class="row">Volume<input id="vol" type="range" min="0" max="100" value="80"><span id="volv">80</span></div>
<div class="info"><div>State<b id="state">—</b></div><div>Wi-Fi<b id="rssi">—</b></div><div>Obstacle<b id="obs">—</b></div>
<div>Mic<b id="mic">—</b></div><div style="grid-column:span 2">Wake word<b id="wake">—</b></div></div>
<footer>Chitu web console · same Wi-Fi only</footer>
</main><script>
const $=id=>document.getElementById(id);
const cmd=q=>fetch('/cmd?'+q).catch(()=>{});
// ---------- audio ----------
let ac=null,rates={ir:16000,or:24000};
function AC(){if(!ac){const C=window.AudioContext||window.webkitAudioContext;ac=new C()}if(ac.state==='suspended')ac.resume();return ac}
document.addEventListener('pointerdown',()=>AC(),{once:true});
const players={v:{next:0},r:{next:0}};
function play(view,rate,pl){if(!ac||ac.state!=='running')return;const n=(view.byteLength-1)>>1;if(n<=0)return;
 const b=ac.createBuffer(1,n,rate),ch=b.getChannelData(0);for(let i=0;i<n;i++)ch[i]=view.getInt16(1+i*2,true)/32768;
 const s=ac.createBufferSource();s.buffer=b;s.connect(ac.destination);const now=ac.currentTime;
 if(pl.next<now+0.03||pl.next>now+0.8)pl.next=now+0.15;s.start(pl.next);pl.next+=b.duration}
let mic=null,holding=false,micAlways=false,dsAcc=0,dsSum=0,dsCnt=0;
async function micStart(){const c=AC();if(mic)return true;
 if(!navigator.mediaDevices||!window.isSecureContext){note('Microphone needs the secure page (https). Open https://'+location.hostname+'/');return false}
 try{const st=await navigator.mediaDevices.getUserMedia({audio:{echoCancellation:true,noiseSuppression:true,autoGainControl:true}});
  const src=c.createMediaStreamSource(st),p=c.createScriptProcessor(2048,1,1);
  p.onaudioprocess=e=>{if(!(holding||micAlways)||!ws||ws.readyState!==1)return;const d=e.inputBuffer.getChannelData(0),step=c.sampleRate/16000,out=[];
   for(let i=0;i<d.length;i++){dsSum+=d[i];dsCnt++;if(++dsAcc>=step){dsAcc-=step;out.push(dsSum/dsCnt);dsSum=0;dsCnt=0}}
   const buf=new DataView(new ArrayBuffer(1+out.length*2));buf.setUint8(0,1);
   for(let i=0;i<out.length;i++){let v=Math.max(-1,Math.min(1,out[i]*1.4));buf.setInt16(1+i*2,v*32767,true)}ws.send(buf.buffer)};
  src.connect(p);p.connect(c.destination);mic={st,p};note('');return true}
 catch(e){note('Microphone blocked: allow microphone access for this page in the browser settings.');return false}}
function note(t){$('micnote').textContent=t;$('micnote').style.display=t?'block':'none'}
const talk=$('talk');
talk.addEventListener('pointerdown',async e=>{e.preventDefault();talk.setPointerCapture(e.pointerId);if(!await micStart())return;holding=true;talk.classList.add('held');talk.textContent='🔴 Listening… release to send';cmd('a=talk&on=1')});
['pointerup','pointercancel','lostpointercapture'].forEach(ev=>talk.addEventListener(ev,()=>{if(!holding)return;talk.classList.remove('held');talk.textContent='🎤 Hold to talk to Chitu';setTimeout(()=>{holding=false;cmd('a=talk&on=0')},350)}));
function tog(id,key,init,cb){const b=$(id);let on=init;b.classList.toggle('act',on);b.onclick=async()=>{AC();on=!on;if(cb&&!(await cb(on))){on=!on;return}b.classList.toggle('act',on);cmd('a='+key+'&on='+(on?1:0))};return{get:()=>on,set:v=>{on=v;b.classList.toggle('act',v)}}}
const tPhone=tog('tPhone','audio',true),tSpk=tog('tSpk','spk',true),tRoom=tog('tRoom','room',false);
const tMic=tog('tMic','mic',false,async on=>{if(on&&!await micStart())return false;micAlways=on;return true});
const tPet=tog('tPet','pet',true),tChat=tog('tChat','chatty',true);
// ---------- websocket: camera + audio ----------
let ws=null,camUrl=null;const cam=$('cam');
function connect(){ws=new WebSocket('wss://'+location.host+'/ws');ws.binaryType='arraybuffer';
 ws.onopen=()=>{$('cammsg').textContent='Camera starting…';cmd('a=audio&on='+(tPhone.get()?1:0));cmd('a=spk&on='+(tSpk.get()?1:0));cmd('a=room&on='+(tRoom.get()?1:0));cmd('a=video&on='+(camOn?1:0))};
 ws.onmessage=e=>{if(typeof e.data==='string')return;const v=new DataView(e.data),t=v.getUint8(0);
  if(t===0x10){const u=URL.createObjectURL(new Blob([new Uint8Array(e.data,1)],{type:'image/jpeg'}));cam.onload=()=>{if(camUrl)URL.revokeObjectURL(camUrl);camUrl=u};cam.src=u;$('cammsg').style.display='none'}
  else if(t===0x20)play(v,rates.or,players.v);else if(t===0x21)play(v,rates.ir,players.r)};
 ws.onclose=()=>{$('cammsg').style.display='flex';$('cammsg').textContent='Reconnecting…';setTimeout(connect,2000)}}
connect();setInterval(()=>{if(ws&&ws.readyState===1)ws.send('p')},1000);
let camOn=true;$('cOn').onclick=()=>{camOn=!camOn;$('cOn').classList.toggle('act',camOn);cmd('a=video&on='+(camOn?1:0));if(!camOn){$('cammsg').style.display='flex';$('cammsg').textContent='Camera off'}};
let camV=0,camH=0;$('cFlip').onclick=()=>{camV^=1;cmd('a=cam&v='+camV)};$('cMirror').onclick=()=>{camH^=1;cmd('a=cam&h='+camH)};
// ---------- drive ----------
let held=null,timer=null;
function send(){if(held)cmd('a=drive&d='+held+'&s='+$('speed').value)}
function press(b){release();held=b.dataset.d;b.classList.add('held');setMode(false,true);send();timer=setInterval(send,200)}
function release(){if(!held)return;clearInterval(timer);timer=null;held=null;document.querySelectorAll('.pad .held').forEach(e=>e.classList.remove('held'));cmd('a=stop')}
document.querySelectorAll('.pad [data-d]').forEach(b=>{b.addEventListener('pointerdown',e=>{e.preventDefault();b.setPointerCapture(e.pointerId);press(b)});
 ['pointerup','pointercancel','lostpointercapture'].forEach(ev=>b.addEventListener(ev,release))});
$('stop').onclick=()=>{release();setMode(false);cmd('a=stop')};
function setMode(auto,quiet){$('mAuto').classList.toggle('act',auto);$('mManual').classList.toggle('act',!auto);if(!quiet)cmd('a=roam&on='+(auto?1:0))}
$('mAuto').onclick=()=>setMode(true);$('mManual').onclick=()=>setMode(false);
$('speed').oninput=e=>$('speedv').textContent=e.target.value;
$('vol').oninput=e=>$('volv').textContent=e.target.value;$('vol').onchange=e=>cmd('a=volume&v='+e.target.value);
document.addEventListener('visibilitychange',()=>{if(document.hidden){release();holding=false}});
// ---------- messages ----------
'😀😂😍🥰😘😎🤔😴😢😡😱🤖👍👋🙏❤🔥🎉⭐🍕🐶🌙'.match(/\p{Extended_Pictographic}/gu).forEach(c=>{const b=document.createElement('button');b.textContent=c;b.onclick=()=>{$('text').value+=c;$('text').focus()};$('emo').appendChild(b)});
function sendMsg(){let t=$('text').value.trim();if(!t)return;t=t.replace(/[️‍]|[\u{1F3FB}-\u{1F3FF}]/gu,'');
 fetch('/msg?say='+($('sayit').checked?1:0),{method:'POST',body:t}).then(r=>{if(r.ok)$('text').value=''})}
$('send').onclick=sendMsg;$('text').addEventListener('keydown',e=>{if(e.key==='Enter')sendMsg()});
// ---------- status ----------
let fails=0,volSet=false,synced=false;
async function poll(){try{const r=await fetch('/status',{cache:'no-store'});const s=await r.json();fails=0;
 $('conn').textContent='online';$('conn').className='pill on';rates.ir=s.ir;rates.or=s.or;camV=s.cam_v|0;camH=s.cam_h|0;
 const mm=s.m.distance_mm,d=$('dist');
 if(mm<0){d.textContent='no sensor';d.className='dist'}else{d.textContent=(mm>=2000?'>200':Math.round(mm/10))+' cm';d.className='dist'+(mm<300?' near':'')}
 $('state').textContent=s.m.roaming?'roaming':(s.m.dancing?'dancing':(s.m.moving?'moving':s.st));
 $('obs').textContent=s.m.obstacle_stop?'blocked':'clear';$('rssi').textContent=s.rssi+' dBm';
 $('mic').textContent=s.phone_mic?'phone':(s.mic+(s.mic_clip?' CLIP':''));$('wake').textContent=s.wake;
 if(!held){$('mAuto').classList.toggle('act',s.m.roaming);$('mManual').classList.toggle('act',!s.m.roaming)}
 if(!volSet&&s.volume>=0){$('vol').value=s.volume;$('volv').textContent=s.volume;volSet=true}
 if(!synced){tPet.set(s.pet);tChat.set(s.chatty);synced=true}
}catch(e){if(++fails>2){$('conn').textContent='offline';$('conn').className='pill'}}
setTimeout(poll,800)}
poll();
</script></body></html>)HTML";

bool QueryValue(const char* query, const char* key, char* out, size_t out_len) {
    return httpd_query_key_value(query, key, out, out_len) == ESP_OK;
}

bool QueryOn(const char* query) {
    char on[4] = {0};
    return QueryValue(query, "on", on, sizeof(on)) && on[0] == '1';
}

std::string JsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if ((unsigned char)c < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}
}  // namespace

std::string WebConsole::Url() const {
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip = {};
    if (netif == nullptr || esp_netif_get_ip_info(netif, &ip) != ESP_OK || ip.ip.addr == 0) {
        return "";
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "https://" IPSTR "/", IP2STR(&ip.ip));
    return buf;
}

bool WebConsole::RecentlyUsed() const {
    return client_fd_.load() >= 0 ||
           esp_timer_get_time() - last_use_us_.load() < 60LL * 1000 * 1000;
}

void WebConsole::Start(MotorController* motors, AudioCodec* codec, PetMode* pet) {
    if (secure_server_ != nullptr) {
        return;  // already running (Wi-Fi reconnects keep the same servers)
    }
    motors_ = motors;
    codec_ = codec;
    pet_ = pet;
    if (!remote_audio::Init()) {
        ESP_LOGE(TAG, "No memory for phone audio buffers");
    }
    rx_buf_ = static_cast<uint8_t*>(heap_caps_malloc(kRxMax, MALLOC_CAP_SPIRAM));

    // Secure server: the page, commands and the camera/audio WebSocket
    httpd_ssl_config_t ssl = HTTPD_SSL_CONFIG_DEFAULT();
    ssl.servercert = reinterpret_cast<const uint8_t*>(kChituCertPem);
    ssl.servercert_len = sizeof(kChituCertPem);
    ssl.prvtkey_pem = reinterpret_cast<const uint8_t*>(kChituKeyPem);
    ssl.prvtkey_len = sizeof(kChituKeyPem);
    ssl.port_secure = WEB_CONSOLE_PORT_SECURE;
    ssl.httpd.ctrl_port = 32780;
    ssl.httpd.max_open_sockets = 5;
    ssl.httpd.max_uri_handlers = 8;
    ssl.httpd.lru_purge_enable = true;
    ssl.httpd.recv_wait_timeout = 10;
    ssl.httpd.send_wait_timeout = 10;
    if (httpd_ssl_start(&secure_server_, &ssl) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start the secure web console");
        secure_server_ = nullptr;
        return;
    }
    httpd_uri_t index = {.uri = "/", .method = HTTP_GET, .handler = HandleIndex, .user_ctx = this};
    httpd_uri_t cmd = {.uri = "/cmd", .method = HTTP_GET, .handler = HandleCmd, .user_ctx = this};
    httpd_uri_t status = {.uri = "/status", .method = HTTP_GET, .handler = HandleStatus, .user_ctx = this};
    httpd_uri_t msg = {.uri = "/msg", .method = HTTP_POST, .handler = HandleMsg, .user_ctx = this};
    httpd_uri_t ws = {.uri = "/ws", .method = HTTP_GET, .handler = HandleWs, .user_ctx = this};
    ws.is_websocket = true;
    httpd_register_uri_handler(secure_server_, &index);
    httpd_register_uri_handler(secure_server_, &cmd);
    httpd_register_uri_handler(secure_server_, &status);
    httpd_register_uri_handler(secure_server_, &msg);
    httpd_register_uri_handler(secure_server_, &ws);

    // Plain http://<ip>/ just sends the phone to the secure page
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = WEB_CONSOLE_PORT;
    cfg.ctrl_port = 32781;
    cfg.max_open_sockets = 2;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 3584;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    if (httpd_start(&redirect_server_, &cfg) == ESP_OK) {
        httpd_uri_t any = {.uri = "/*", .method = HTTP_GET, .handler = HandleRedirect, .user_ctx = this};
        httpd_register_uri_handler(redirect_server_, &any);
    } else {
        redirect_server_ = nullptr;
    }

    xTaskCreate([](void* arg) { static_cast<WebConsole*>(arg)->MediaLoop(); }, "chitu_media", 4096,
                this, 4, nullptr);
    ESP_LOGI(TAG, "Web console running at %s", Url().c_str());
}

esp_err_t WebConsole::HandleRedirect(httpd_req_t* req) {
    char host[64] = {0};
    std::string location = "https://";
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) == ESP_OK) {
        char* colon = strchr(host, ':');
        if (colon != nullptr) *colon = '\0';
        location += host;
        location += "/";
    } else {
        location = static_cast<WebConsole*>(req->user_ctx)->Url();
    }
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", location.c_str());
    httpd_resp_set_type(req, "text/html");
    std::string body = "<a href=\"" + location + "\">Open Chitu console</a>";
    return httpd_resp_send(req, body.c_str(), body.size());
}

esp_err_t WebConsole::HandleIndex(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, kIndexHtml, sizeof(kIndexHtml) - 1);
}

esp_err_t WebConsole::HandleCmd(httpd_req_t* req) {
    auto* self = static_cast<WebConsole*>(req->user_ctx);
    char query[128] = {0};
    char action[16] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        !QueryValue(query, "a", action, sizeof(action))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing a=");
        return ESP_FAIL;
    }
    self->last_use_us_ = esp_timer_get_time();
    if (self->pet_ != nullptr) {
        self->pet_->Poke();
    }

    bool ok = true;
    auto& app = Application::GetInstance();
    if (strcmp(action, "drive") == 0) {
        char dir[12] = {0}, speed_s[8] = {0};
        QueryValue(query, "d", dir, sizeof(dir));
        int speed = QueryValue(query, "s", speed_s, sizeof(speed_s)) ? atoi(speed_s) : 70;
        speed = std::clamp(speed, 1, 100);
        self->motors_->StopRoam();
        self->motors_->StopDance();
        // Short duration: the page re-sends every 200 ms while a button is held,
        // so the robot stops by itself if the phone disconnects (dead-man switch).
        ok = self->motors_->DriveNamed(dir, speed, 600);
    } else if (strcmp(action, "stop") == 0) {
        self->motors_->Stop();
    } else if (strcmp(action, "roam") == 0) {
        if (QueryOn(query)) {
            ok = self->motors_->StartRoam(ROAM_MAX_MINUTES);
        } else {
            self->motors_->Stop();
        }
    } else if (strcmp(action, "volume") == 0) {
        char v[8] = {0};
        if (self->codec_ != nullptr && QueryValue(query, "v", v, sizeof(v))) {
            self->codec_->SetOutputVolume(std::clamp(atoi(v), 0, 100));
        }
    } else if (strcmp(action, "talk") == 0) {
        // Push-to-talk from the phone: the phone mic feeds the AI while held
        if (QueryOn(query)) {
            app.StartListening();
        } else {
            app.StopListening();
        }
    } else if (strcmp(action, "audio") == 0) {
        remote_audio::phone_listen = QueryOn(query);
    } else if (strcmp(action, "spk") == 0) {
        remote_audio::robot_speaker = QueryOn(query);
    } else if (strcmp(action, "room") == 0) {
        remote_audio::room_listen = QueryOn(query);
    } else if (strcmp(action, "mic") == 0) {
        // "Phone mic always": nothing to switch here, the phone simply keeps streaming
    } else if (strcmp(action, "video") == 0) {
        self->video_on_ = QueryOn(query);
    } else if (strcmp(action, "pet") == 0) {
        if (self->pet_ != nullptr) self->pet_->SetEnabled(QueryOn(query));
    } else if (strcmp(action, "chatty") == 0) {
        if (self->pet_ != nullptr) self->pet_->SetChatty(QueryOn(query));
    } else if (strcmp(action, "cam") == 0) {
        // Camera orientation: v=vertical flip, h=horizontal mirror (0/1), saved to NVS
        sensor_t* s = esp_camera_sensor_get();
        char v[4] = {0}, h[4] = {0};
        if (s == nullptr) {
            ok = false;
        } else {
            Settings settings("chitu", true);
            if (QueryValue(query, "v", v, sizeof(v))) {
                int on = v[0] == '1' ? 1 : 0;
                s->set_vflip(s, on);
                settings.SetInt("cam_vflip", on);
            }
            if (QueryValue(query, "h", h, sizeof(h))) {
                int on = h[0] == '1' ? 1 : 0;
                s->set_hmirror(s, on);
                settings.SetInt("cam_hmirror", on);
            }
        }
    } else {
        ok = false;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

esp_err_t WebConsole::HandleMsg(httpd_req_t* req) {
    auto* self = static_cast<WebConsole*>(req->user_ctx);
    char query[32] = {0};
    char say[4] = {0};
    bool say_it = httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
                  QueryValue(query, "say", say, sizeof(say)) && say[0] == '1';
    int len = std::min<int>(req->content_len, 480);
    std::string text(len, '\0');
    int got = 0;
    while (got < len) {
        int r = httpd_req_recv(req, text.data() + got, len - got);
        if (r <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no text");
            return ESP_FAIL;
        }
        got += r;
    }
    self->last_use_us_ = esp_timer_get_time();
    if (self->pet_ != nullptr) {
        self->pet_->Poke();
    }
    if (!text.empty() && self->on_message_) {
        self->on_message_(text, say_it);
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t WebConsole::HandleStatus(httpd_req_t* req) {
    auto* self = static_cast<WebConsole*>(req->user_ctx);
    int rssi = 0;
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        rssi = ap.rssi;
    }
    int volume = self->codec_ ? self->codec_->output_volume() : -1;
    sensor_t* cam = esp_camera_sensor_get();
    auto state = Application::GetInstance().GetDeviceState();
    std::string json =
        "{\"m\":" + self->motors_->StatusJson() + ",\"rssi\":" + std::to_string(rssi) +
        ",\"volume\":" + std::to_string(volume) + ",\"camera\":" + (cam ? "true" : "false") +
        ",\"cam_v\":" + std::to_string(cam ? cam->status.vflip : 0) +
        ",\"cam_h\":" + std::to_string(cam ? cam->status.hmirror : 0) +
        ",\"mic\":" + std::to_string(audio_diag::mic_rms.load()) +
        ",\"mic_clip\":" + (audio_diag::mic_clipped > 0 ? "true" : "false") +
        ",\"phone_mic\":" + (remote_audio::PhoneMicActive() ? "true" : "false") +
        ",\"ir\":" + std::to_string(self->codec_ ? self->codec_->input_sample_rate() : 16000) +
        ",\"or\":" + std::to_string(self->codec_ ? self->codec_->output_sample_rate() : 24000) +
        ",\"pet\":" + (self->pet_ && self->pet_->enabled() ? "true" : "false") +
        ",\"chatty\":" + (self->pet_ && self->pet_->chatty() ? "true" : "false") +
        ",\"st\":\"" + JsonEscape(DeviceStateMachine::GetStateName(state)) + "\"" +
        ",\"wake\":\"" + JsonEscape(audio_diag::WakeWords()) + "\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json.c_str(), json.size());
}

// ---------------------------------------------------------------------------
// WebSocket: camera + audio
// ---------------------------------------------------------------------------
esp_err_t WebConsole::HandleWs(httpd_req_t* req) {
    auto* self = static_cast<WebConsole*>(req->user_ctx);
    int fd = httpd_req_to_sockfd(req);
    if (req->method == HTTP_GET) {
        // Handshake done: the newest phone takes over
        int old = self->client_fd_.exchange(fd);
        if (old >= 0 && old != fd) {
            httpd_sess_trigger_close(self->secure_server_, old);
        }
        self->last_rx_us_ = esp_timer_get_time();
        ESP_LOGI(TAG, "Phone connected (socket %d)", fd);
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {};
    if (httpd_ws_recv_frame(req, &frame, 0) != ESP_OK) {
        return ESP_FAIL;
    }
    if (frame.len > kRxMax || self->rx_buf_ == nullptr) {
        return ESP_FAIL;
    }
    frame.payload = self->rx_buf_;
    if (frame.len > 0 && httpd_ws_recv_frame(req, &frame, frame.len) != ESP_OK) {
        return ESP_FAIL;
    }
    if (fd == self->client_fd_.load()) {
        self->last_rx_us_ = esp_timer_get_time();
    }
    if (frame.type == HTTPD_WS_TYPE_BINARY && frame.len > 1 && frame.payload[0] == kWsMicIn) {
        remote_audio::phone_mic.Push(frame.payload + 1, (frame.len - 1) & ~1u);
        remote_audio::last_phone_mic_us = esp_timer_get_time();
    }
    return ESP_OK;
}

bool WebConsole::EnsureTx(size_t len) {
    if (tx_buf_ != nullptr && tx_cap_ >= len + 1) {
        return true;
    }
    if (tx_buf_ != nullptr) heap_caps_free(tx_buf_);
    tx_cap_ = len + 1 + 8 * 1024;
    tx_buf_ = static_cast<uint8_t*>(heap_caps_malloc(tx_cap_, MALLOC_CAP_SPIRAM));
    if (tx_buf_ == nullptr) {
        tx_cap_ = 0;
        return false;
    }
    return true;
}

bool WebConsole::SendTx(int fd, size_t len) {
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_BINARY;
    frame.payload = tx_buf_;
    frame.len = len + 1;
    // Queued to the server task (TLS is not shared between tasks) and waits until sent
    return httpd_ws_send_data(secure_server_, fd, &frame) == ESP_OK;
}

bool WebConsole::SendBinary(int fd, uint8_t type, const void* data, size_t len) {
    if (!EnsureTx(len)) {
        return false;
    }
    tx_buf_[0] = type;
    memcpy(tx_buf_ + 1, data, len);
    return SendTx(fd, len);
}

void WebConsole::DropClient(const char* why) {
    int fd = client_fd_.exchange(-1);
    if (fd >= 0) {
        ESP_LOGI(TAG, "Phone disconnected (%s)", why);
        // only close it if that socket is still our WebSocket (fd numbers get reused)
        if (httpd_ws_get_fd_info(secure_server_, fd) == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_sess_trigger_close(secure_server_, fd);
        }
    }
    remote_audio::ResetRouting();  // robot mic + speaker back to normal
    video_on_ = true;
}

void WebConsole::MediaLoop() {
    // up to ~330 ms of audio per message so audio keeps up while video frames are sent
    constexpr size_t kPcmBytes = 16000;
    auto* pcm = static_cast<int16_t*>(heap_caps_malloc(kPcmBytes, MALLOC_CAP_SPIRAM));
    if (pcm == nullptr) {
        ESP_LOGE(TAG, "No memory for media task");
        vTaskDelete(nullptr);
        return;
    }
    int64_t last_frame_us = 0;
    while (true) {
        int fd = client_fd_.load();
        if (fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int64_t now = esp_timer_get_time();
        if (httpd_ws_get_fd_info(secure_server_, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
            DropClient("closed");
            continue;
        }
        if (now - last_rx_us_.load() > 5LL * 1000 * 1000) {
            DropClient("no heartbeat");
            continue;
        }
        bool sent = false;
        bool ok = true;

        // Audio first: it is small and latency matters
        size_t n = remote_audio::voice_out.Pop(pcm, kPcmBytes);
        if (n > 0) {
            ok = SendBinary(fd, kWsVoice, pcm, n);
            sent = true;
        }
        n = remote_audio::room_out.Pop(pcm, kPcmBytes);
        if (ok && n > 0) {
            ok = SendBinary(fd, kWsRoom, pcm, n);
            sent = true;
        }

        if (ok && video_on_ && (now - last_frame_us) / 1000 >= kVideoIntervalMs &&
            esp_camera_sensor_get() != nullptr) {
            camera_fb_t* fb = esp_camera_fb_get();
            if (fb != nullptr) {
                last_frame_us = now;
                if (fb->format == PIXFORMAT_JPEG && fb->len > 0) {
                    // copy, then return the buffer at once so AI photos never wait on Wi-Fi
                    size_t len = fb->len;
                    bool fits = EnsureTx(len);
                    if (fits) {
                        tx_buf_[0] = kWsVideo;
                        memcpy(tx_buf_ + 1, fb->buf, len);
                    }
                    esp_camera_fb_return(fb);
                    if (fits) {
                        ok = SendTx(fd, len);
                    }
                } else {
                    esp_camera_fb_return(fb);
                }
                sent = true;
            }
        }
        if (!ok) {
            DropClient("send failed");
            continue;
        }
        if (!sent) {
            vTaskDelay(pdMS_TO_TICKS(15));
        }
    }
}
