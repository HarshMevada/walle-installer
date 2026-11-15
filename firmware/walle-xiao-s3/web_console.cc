#include "web_console.h"

#include "audio_codec.h"
#include "audio_diag.h"
#include "config.h"
#include "motor_controller.h"

#include <esp_camera.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#define TAG "WebConsole"

namespace {
const char kIndexHtml[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Chitu</title>
<style>
:root{--bg:#0e1a1f;--panel:#152830;--line:#24414c;--t:#e8f1f2;--m:#93aab0;--eye:#5ee7f7;--warn:#ff8a5c}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;background:var(--bg);color:var(--t);font:15px system-ui,Segoe UI,Roboto,Arial,sans-serif;user-select:none}
main{max-width:520px;margin:0 auto;padding:10px}
header{display:flex;justify-content:space-between;align-items:center;margin:4px 2px 10px}
h1{font-size:20px;margin:0;letter-spacing:.08em}h1 b{color:var(--eye)}
.pill{font-size:12px;padding:4px 10px;border-radius:99px;border:1px solid var(--line);color:var(--m)}
.pill.on{color:#10161a;background:var(--eye);border-color:var(--eye)}
.cam{position:relative;background:#000;border-radius:14px;overflow:hidden;aspect-ratio:4/3;border:1px solid var(--line)}
.cam img{width:100%;height:100%;object-fit:cover;display:block}
.cam .msg{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;color:var(--m)}
.dist{position:absolute;left:8px;bottom:8px;background:rgba(0,0,0,.6);padding:4px 10px;border-radius:99px;font-size:13px}
.dist.near{background:var(--warn);color:#000}
.modes{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin:10px 0}
.modes button{padding:12px;border-radius:12px;border:1px solid var(--line);background:var(--panel);color:var(--t);font-size:15px;font-weight:600}
.modes button.act{background:var(--eye);color:#10161a;border-color:var(--eye)}
.pad{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin:6px auto;max-width:330px}
.pad button{aspect-ratio:1;border-radius:16px;border:1px solid var(--line);background:var(--panel);color:var(--t);font-size:28px;touch-action:none}
.pad button:active,.pad button.held{background:var(--eye);color:#10161a}
.pad .stop{background:#5a1d1d;border-color:#8a2c2c;font-size:15px;font-weight:700}
.pad .blank{visibility:hidden}
.row{display:flex;align-items:center;gap:10px;margin:12px 4px;color:var(--m);font-size:14px}
.row input{flex:1;accent-color:var(--eye)}
.row span{width:42px;text-align:right;color:var(--t)}
.info{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-top:6px}
.info div{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:8px;text-align:center;font-size:12px;color:var(--m)}
.info b{display:block;color:var(--t);font-size:15px;margin-top:2px}
footer{color:var(--m);font-size:11px;text-align:center;margin:14px 0 4px}
</style></head><body><main>
<header><h1>CHI<b>TU</b></h1><span id="conn" class="pill">connecting…</span></header>
<div class="cam"><img id="cam" alt=""><div id="cammsg" class="msg">Camera starting…</div><div id="dist" class="dist">— cm</div></div>
<div class="modes"><button id="mManual" class="act">Manual</button><button id="mAuto">Auto roam</button></div>
<div class="pad">
<button class="blank"></button><button data-d="forward">▲</button><button class="blank"></button>
<button data-d="left">◀</button><button class="stop" id="stop">STOP</button><button data-d="right">▶</button>
<button class="blank"></button><button data-d="backward">▼</button><button class="blank"></button>
</div>
<div class="row">Speed<input id="speed" type="range" min="30" max="100" value="70"><span id="speedv">70</span></div>
<div class="row">Volume<input id="vol" type="range" min="0" max="100" value="80"><span id="volv">80</span></div>
<div class="info"><div>State<b id="state">—</b></div><div>Wi-Fi<b id="rssi">—</b></div><div>Obstacle<b id="obs">—</b></div>
<div>Mic level<b id="mic">—</b></div><div style="grid-column:span 2">Wake word<b id="wake">—</b></div></div>
<footer>Chitu web console · same Wi-Fi only · two-way audio coming next</footer>
</main><script>
const $=id=>document.getElementById(id);
const cmd=q=>fetch('/cmd?'+q).catch(()=>{});
let held=null,timer=null;
function send(){if(held)cmd('a=drive&d='+held+'&s='+$('speed').value)}
function press(b){release();held=b.dataset.d;b.classList.add('held');setMode(false,true);send();timer=setInterval(send,200)}
function release(){if(!held)return;clearInterval(timer);timer=null;held=null;document.querySelectorAll('.held').forEach(e=>e.classList.remove('held'));cmd('a=stop')}
document.querySelectorAll('.pad [data-d]').forEach(b=>{
 b.addEventListener('pointerdown',e=>{e.preventDefault();b.setPointerCapture(e.pointerId);press(b)});
 ['pointerup','pointercancel','lostpointercapture'].forEach(ev=>b.addEventListener(ev,release));});
$('stop').onclick=()=>{release();setMode(false);cmd('a=stop')};
function setMode(auto,quiet){$('mAuto').classList.toggle('act',auto);$('mManual').classList.toggle('act',!auto);if(!quiet)cmd('a=roam&on='+(auto?1:0))}
$('mAuto').onclick=()=>setMode(true);$('mManual').onclick=()=>setMode(false);
$('speed').oninput=e=>$('speedv').textContent=e.target.value;
$('vol').oninput=e=>$('volv').textContent=e.target.value;
$('vol').onchange=e=>cmd('a=volume&v='+e.target.value);
document.addEventListener('visibilitychange',()=>{if(document.hidden)release()});
// camera
const cam=$('cam');function startCam(){cam.src='http://'+location.hostname+':81/stream?t='+Date.now()}
cam.onload=()=>$('cammsg').style.display='none';
cam.onerror=()=>{$('cammsg').style.display='flex';$('cammsg').textContent='Camera unavailable – retrying…';setTimeout(startCam,3000)};
startCam();
// status
let fails=0,volSet=false;
async function poll(){try{const r=await fetch('/status',{cache:'no-store'});const s=await r.json();fails=0;
 $('conn').textContent='online';$('conn').className='pill on';
 const mm=s.m.distance_mm;const d=$('dist');
 if(mm<0){d.textContent='no sensor';d.className='dist'}else{d.textContent=(mm>=2000?'>200':Math.round(mm/10))+' cm';d.className='dist'+(mm<300?' near':'')}
 $('state').textContent=s.m.roaming?'roaming':(s.m.moving?'moving':'idle');
 $('obs').textContent=s.m.obstacle_stop?'blocked':'clear';
 $('rssi').textContent=s.rssi+' dBm';
 $('mic').textContent=s.mic+(s.mic_clip?' CLIP':'');$('wake').textContent=s.wake;
 if(!held){$('mAuto').classList.toggle('act',s.m.roaming);$('mManual').classList.toggle('act',!s.m.roaming)}
 if(!volSet&&s.volume>=0){$('vol').value=s.volume;$('volv').textContent=s.volume;volSet=true}
}catch(e){if(++fails>2){$('conn').textContent='offline';$('conn').className='pill'}}
setTimeout(poll,700)}
poll();
</script></body></html>)HTML";

constexpr const char* kStreamContentType = "multipart/x-mixed-replace;boundary=chitu-frame";
constexpr const char* kStreamBoundary = "\r\n--chitu-frame\r\n";
constexpr int kStreamMaxFps = 12;

bool QueryValue(const char* query, const char* key, char* out, size_t out_len) {
    return httpd_query_key_value(query, key, out, out_len) == ESP_OK;
}
}  // namespace

std::string WebConsole::Url() const {
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip = {};
    if (netif == nullptr || esp_netif_get_ip_info(netif, &ip) != ESP_OK || ip.ip.addr == 0) {
        return "";
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "http://" IPSTR "/", IP2STR(&ip.ip));
    return buf;
}

void WebConsole::Start(MotorController* motors, AudioCodec* codec) {
    if (control_server_ != nullptr) {
        return;  // already running (Wi-Fi reconnects keep the same servers)
    }
    motors_ = motors;
    codec_ = codec;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = WEB_CONSOLE_PORT;
    cfg.ctrl_port = 32780;
    cfg.max_open_sockets = 5;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 6144;
    if (httpd_start(&control_server_, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start control server");
        control_server_ = nullptr;
        return;
    }
    httpd_uri_t index = {.uri = "/", .method = HTTP_GET, .handler = HandleIndex, .user_ctx = this};
    httpd_uri_t cmd = {.uri = "/cmd", .method = HTTP_GET, .handler = HandleCmd, .user_ctx = this};
    httpd_uri_t status = {.uri = "/status", .method = HTTP_GET, .handler = HandleStatus, .user_ctx = this};
    httpd_register_uri_handler(control_server_, &index);
    httpd_register_uri_handler(control_server_, &cmd);
    httpd_register_uri_handler(control_server_, &status);

    httpd_config_t scfg = HTTPD_DEFAULT_CONFIG();
    scfg.server_port = WEB_STREAM_PORT;
    scfg.ctrl_port = 32781;
    scfg.max_open_sockets = 3;
    scfg.lru_purge_enable = true;
    scfg.stack_size = 6144;
    if (httpd_start(&stream_server_, &scfg) == ESP_OK) {
        httpd_uri_t stream = {.uri = "/stream", .method = HTTP_GET, .handler = HandleStream, .user_ctx = this};
        httpd_register_uri_handler(stream_server_, &stream);
    } else {
        ESP_LOGE(TAG, "Failed to start camera stream server");
        stream_server_ = nullptr;
    }
    ESP_LOGI(TAG, "Web console running at %s", Url().c_str());
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

    bool ok = true;
    if (strcmp(action, "drive") == 0) {
        char dir[12] = {0}, speed_s[8] = {0};
        QueryValue(query, "d", dir, sizeof(dir));
        int speed = QueryValue(query, "s", speed_s, sizeof(speed_s)) ? atoi(speed_s) : 70;
        speed = std::clamp(speed, 1, 100);
        self->motors_->StopRoam();
        // Short duration: the page re-sends every 200 ms while a button is held,
        // so the robot stops by itself if the phone disconnects (dead-man switch).
        ok = self->motors_->DriveNamed(dir, speed, 600);
    } else if (strcmp(action, "stop") == 0) {
        self->motors_->Stop();
    } else if (strcmp(action, "roam") == 0) {
        char on[4] = {0};
        QueryValue(query, "on", on, sizeof(on));
        if (on[0] == '1') {
            ok = self->motors_->StartRoam(ROAM_MAX_MINUTES);
        } else {
            self->motors_->Stop();
        }
    } else if (strcmp(action, "volume") == 0) {
        char v[8] = {0};
        if (self->codec_ != nullptr && QueryValue(query, "v", v, sizeof(v))) {
            self->codec_->SetOutputVolume(std::clamp(atoi(v), 0, 100));
        }
    } else {
        ok = false;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

esp_err_t WebConsole::HandleStatus(httpd_req_t* req) {
    auto* self = static_cast<WebConsole*>(req->user_ctx);
    int rssi = 0;
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        rssi = ap.rssi;
    }
    int volume = self->codec_ ? self->codec_->output_volume() : -1;
    std::string json = "{\"m\":" + self->motors_->StatusJson() + ",\"rssi\":" + std::to_string(rssi) +
                       ",\"volume\":" + std::to_string(volume) +
                       ",\"camera\":" + (esp_camera_sensor_get() ? "true" : "false") +
                       ",\"mic\":" + std::to_string(audio_diag::mic_rms.load()) +
                       ",\"mic_clip\":" + (audio_diag::mic_clipped > 0 ? "true" : "false") +
                       ",\"wake\":\"" + audio_diag::WakeWords() + "\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json.c_str(), json.size());
}

esp_err_t WebConsole::HandleStream(httpd_req_t* req) {
    auto* self = static_cast<WebConsole*>(req->user_ctx);
    if (esp_camera_sensor_get() == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera not available");
        return ESP_FAIL;
    }
    // Newest viewer wins: an older stream (e.g. a reloaded page) ends itself
    int my_generation = ++self->stream_generation_;

    httpd_resp_set_type(req, kStreamContentType);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    char part[96];
    const int64_t frame_us = 1000000 / kStreamMaxFps;
    esp_err_t res = ESP_OK;
    while (res == ESP_OK && self->stream_generation_ == my_generation) {
        int64_t start = esp_timer_get_time();
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb == nullptr) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (fb->format == PIXFORMAT_JPEG) {
            int n = snprintf(part, sizeof(part),
                             "%sContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                             kStreamBoundary, (unsigned)fb->len);
            res = httpd_resp_send_chunk(req, part, n);
            if (res == ESP_OK) {
                res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
            }
        }
        esp_camera_fb_return(fb);
        int64_t spent = esp_timer_get_time() - start;
        if (spent < frame_us) {
            vTaskDelay(pdMS_TO_TICKS((frame_us - spent) / 1000));
        }
    }
    if (res == ESP_OK) {
        httpd_resp_send_chunk(req, nullptr, 0);
    }
    return ESP_OK;
}
