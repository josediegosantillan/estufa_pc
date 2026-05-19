#include "network_services.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_netif_ip_addr.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "mdns.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "app_board.h"
#include "app_config.h"
#include "app_state.h"
#include "relay_control.h"
#include "temperature_control.h"
#include "esp_timer.h"

static EventGroupHandle_t wifi_event_group = NULL;
static httpd_handle_t web_server_handle = NULL;
static esp_ip4_addr_t sta_ipv4_addr = {0};
static bool mdns_started = false;

static esp_err_t mdns_start_if_needed(void)
{
    if (mdns_started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(mdns_init(), TAG, "mdns init failed");
    ESP_RETURN_ON_ERROR(mdns_hostname_set("estufa"), TAG, "mdns hostname failed");
    ESP_RETURN_ON_ERROR(mdns_instance_name_set("Estufa ESP32"), TAG, "mdns instance failed");

    mdns_started = true;
    ESP_LOGI(TAG, "mdns listo en http://estufa.local/");
    return ESP_OK;
}

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    /* head + CSS */
    httpd_resp_sendstr_chunk(req,
        "<!doctype html><html lang='es'><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Estufa ESP32</title>"
        "<style>"
        ":root{--bg:#0f1117;--panel:#1a1d26;--border:#2a2d3a;--text:#e2e8f0;--muted:#64748b;"
        "--blue:#3b82f6;--cyan:#06b6d4;--green:#10b981;--red:#ef4444;--amber:#f59e0b;--purple:#8b5cf6;}"
        "*{box-sizing:border-box;margin:0;padding:0}"
        "body{background:#0f1117;color:#e2e8f0;font-family:'Segoe UI',system-ui,sans-serif;min-height:100vh}"
        ".wrap{max-width:1000px;margin:0 auto;padding:20px 16px 40px}"
        ".hdr{display:flex;justify-content:space-between;align-items:center;"
        "margin-bottom:24px;padding-bottom:16px;border-bottom:1px solid #2a2d3a}"
        ".hdr h1{font-size:22px;font-weight:700;letter-spacing:-.02em}"
        ".hdr h1 span{color:#06b6d4}"
        ".badge{display:flex;align-items:center;gap:6px;font-size:13px;color:#64748b}"
        ".dot{width:8px;height:8px;border-radius:50%;background:#10b981;"
        "box-shadow:0 0 0 3px rgba(16,185,129,.2);animation:pulse 2s infinite}"
        "@keyframes pulse{0%,100%{opacity:1}50%{opacity:.5}}"
        ".grid{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin-bottom:20px}"
        ".card{background:#1a1d26;border:1px solid #2a2d3a;border-radius:10px;padding:16px}"
        ".card .lbl{font-size:11px;text-transform:uppercase;letter-spacing:.08em;color:#64748b;margin-bottom:8px}"
        ".card .val{font-size:28px;font-weight:700;line-height:1}"
        ".card .sub{font-size:12px;color:#64748b;margin-top:4px}"
        ".c-blue .val{color:#3b82f6}.c-cyan .val{color:#06b6d4}"
        ".c-green .val{color:#10b981}.c-red .val{color:#ef4444}"
        ".c-amber .val{color:#f59e0b}.c-purple .val{color:#8b5cf6}"
        ".bar{height:6px;background:#2a2d3a;border-radius:3px;margin-top:10px;overflow:hidden}"
        ".bar-fill{height:100%;border-radius:3px;transition:width .4s ease}"
        ".chart-card{background:#1a1d26;border:1px solid #2a2d3a;border-radius:10px;padding:20px;margin-bottom:20px}"
        ".chart-hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:16px}"
        ".chart-hdr h2{font-size:15px;font-weight:600}"
        ".chart-meta{font-size:12px;color:#64748b}"
        ".relay-btn{width:100%;padding:14px;border:0;border-radius:8px;font-size:16px;font-weight:600;"
        "cursor:pointer;transition:all .2s ease;margin-bottom:20px}"
        ".relay-on{background:rgba(239,68,68,.15);color:#ef4444;border:1px solid rgba(239,68,68,.3)}"
        ".relay-off{background:rgba(16,185,129,.15);color:#10b981;border:1px solid rgba(16,185,129,.3)}"
        ".relay-btn:hover{filter:brightness(1.2)}.relay-btn:disabled{opacity:.4;cursor:not-allowed}"
        ".form-card{background:#1a1d26;border:1px solid #2a2d3a;border-radius:10px;padding:20px}"
        ".form-card h2{font-size:15px;font-weight:600;margin-bottom:16px;padding-bottom:12px;border-bottom:1px solid #2a2d3a}"
        ".fields{display:grid;grid-template-columns:repeat(2,1fr);gap:14px}"
        ".field label{display:block;font-size:12px;color:#64748b;margin-bottom:6px;letter-spacing:.04em}"
        ".field input{width:100%;background:#0d1018;border:1px solid #2a2d3a;border-radius:6px;"
        "padding:9px 11px;color:#e2e8f0;font:inherit;font-size:14px}"
        ".field input:focus{outline:none;border-color:#06b6d4;box-shadow:0 0 0 3px rgba(6,182,212,.1)}"
        ".field input:disabled{opacity:.5}"
        ".toggle-row{display:flex;align-items:center;justify-content:space-between;"
        "padding:10px 0;border-top:1px solid #2a2d3a;margin-top:4px}"
        ".toggle-row label{font-size:14px;color:#e2e8f0}"
        "input[type=checkbox]{width:18px;height:18px;accent-color:#06b6d4}"
        ".actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:18px}"
        ".btn{padding:10px 16px;border-radius:6px;font:inherit;font-size:14px;font-weight:500;"
        "cursor:pointer;text-decoration:none;display:inline-block;border:0}"
        ".btn-primary{background:#06b6d4;color:#0f1117}"
        ".btn-ghost{background:transparent;color:#e2e8f0;border:1px solid #2a2d3a}"
        ".btn:hover{filter:brightness(1.1)}"
        "@media(max-width:680px){.grid{grid-template-columns:repeat(2,1fr)}.fields{grid-template-columns:1fr}}"
        "@media(max-width:420px){.grid{grid-template-columns:1fr}.card .val{font-size:24px}}"
        "</style></head>"
    );

    /* body + header */
    httpd_resp_sendstr_chunk(req,
        "<body><div class='wrap'>"
        "<header class='hdr'>"
        "<h1>Estufa <span>ESP32</span></h1>"
        "<div class='badge'><span class='dot'></span>estufa.local &mdash; live</div>"
        "</header>"
    );

    /* stats grid — dynamic */
    {
        char t1_str[12] = "--";
        char t2_str[12] = "--";
        char rssi_str[8] = "--";
        wifi_ap_record_t ap_info = {0};
        uint64_t uptime_s = esp_timer_get_time() / 1000000ULL;
        uint32_t uptime_h = (uint32_t)(uptime_s / 3600U);
        uint32_t uptime_m = (uint32_t)((uptime_s % 3600U) / 60U);
        uint32_t uptime_ss = (uint32_t)(uptime_s % 60U);
        uint32_t motor_pct = (motor_pwm_duty * 100U) / MOTOR_PWM_DUTY_MAX;

        if (temperature_valid[0]) {
            snprintf(t1_str, sizeof(t1_str), "%.1f", last_temperature_c[0]);
        }
        if (temperature_valid[1]) {
            snprintf(t2_str, sizeof(t2_str), "%.1f", last_temperature_c[1]);
        }
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            snprintf(rssi_str, sizeof(rssi_str), "%d", (int)ap_info.rssi);
        }

        const char *relay_color = relay_state ? "c-green" : "c-red";
        const char *relay_label = relay_state ? "ON" : "OFF";
        const char *relay_sub = relay_overheat_lockout ? "proteccion termica" :
                                (relay_state ? "activo" : "inactivo");

        char stats[1024];
        snprintf(stats, sizeof(stats),
            "<section class='grid'>"
            "<div class='card c-blue'><div class='lbl'>T1 Frente</div>"
            "<div class='val' id='t1v'>%s</div><div class='sub'>&#176;C</div></div>"
            "<div class='card c-cyan'><div class='lbl'>T2 Superior</div>"
            "<div class='val' id='t2v'>%s</div><div class='sub'>&#176;C</div></div>"
            "<div class='card %s'><div class='lbl'>Rele</div>"
            "<div class='val' id='relayVal'>%s</div>"
            "<div class='sub' id='relaySub'>%s</div></div>"
            "<div class='card c-amber'><div class='lbl'>Motor PWM</div>"
            "<div class='val' id='motorVal'>%" PRIu32 "%%</div>"
            "<div class='bar'><div class='bar-fill' id='motorFill'"
            " style='width:%" PRIu32 "%%;background:linear-gradient(90deg,#f59e0b,#ef4444)'></div></div></div>"
            "<div class='card c-purple'><div class='lbl'>WiFi RSSI</div>"
            "<div class='val' id='rssiVal'>%s</div><div class='sub'>dBm</div></div>"
            "<div class='card'><div class='lbl'>Uptime</div>"
            "<div class='val' id='uptimeVal' style='font-size:20px'>"
            "%02" PRIu32 ":%02" PRIu32 ":%02" PRIu32 "</div>"
            "<div class='sub'>h:m:s</div></div>"
            "</section>",
            t1_str, t2_str,
            relay_color, relay_label, relay_sub,
            motor_pct, motor_pct,
            rssi_str,
            uptime_h, uptime_m, uptime_ss);
        httpd_resp_sendstr_chunk(req, stats);
    }

    /* chart container */
    httpd_resp_sendstr_chunk(req,
        "<div class='chart-card'>"
        "<div class='chart-hdr'>"
        "<h2>Temperatura historica</h2>"
        "<span class='chart-meta'>ultimos 3 min &mdash; muestras cada 3 s</span>"
        "</div>"
        "<canvas id='tempChart' height='100'></canvas>"
        "</div>"
    );

    /* relay button */
    {
        char relay_btn[128];
        snprintf(relay_btn, sizeof(relay_btn),
            "<button id='relayBtn' class='relay-btn %s' onclick='toggleRelay()' data-state='%s'>%s</button>",
            relay_state ? "relay-on" : "relay-off",
            relay_state ? "1" : "0",
            relay_state ? "Apagar rele" : "Encender rele");
        httpd_resp_sendstr_chunk(req, relay_btn);
    }

    /* config form — dynamic */
    {
        char ip_str[16] = "sin_ip";
        char sensor_str[16];
        snprintf(sensor_str, sizeof(sensor_str), "%s",
                 ds18b20_sensor_count > 1 ? DS18B20_SENSOR2_LABEL : DS18B20_SENSOR1_LABEL);
        if (sta_ipv4_addr.addr != 0) {
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&sta_ipv4_addr));
        }

        httpd_resp_sendstr_chunk(req,
            "<div class='form-card'><h2>Configuracion</h2>"
            "<form action='/set' method='get'><div class='fields'>");

        char fields[1024];
        snprintf(fields, sizeof(fields),
            "<div class='field'><label>Motor arranca (&#176;C)</label>"
            "<input name='motor_off_c' type='number' step='.1' value='%.1f'></div>"
            "<div class='field'><label>Motor maximo (&#176;C)</label>"
            "<input name='motor_max_c' type='number' step='.1' value='%.1f'></div>"
            "<div class='field'><label>PWM minimo (%%)</label>"
            "<input name='motor_pwm_min_pct' type='number' min='0' max='100' value='%u'></div>"
            "<div class='field'><label>Corte rele (&#176;C)</label>"
            "<input name='relay_cutoff_c' type='number' step='.1' value='%.1f'></div>"
            "<div class='field'><label>Rearme rele (&#176;C)</label>"
            "<input name='relay_resume_c' type='number' step='.1' value='%.1f'></div>"
            "<div class='field'><label>Buzzer alerta (&#176;C)</label>"
            "<input name='buzzer_warning_c' type='number' step='.1' value='%.1f'></div>"
            "<div class='field'><label>IP</label><input value='%s' disabled></div>"
            "<div class='field'><label>Sensor</label><input value='%s' disabled></div>",
            app_config.motor_temp_off_c, app_config.motor_temp_max_c,
            app_config.motor_pwm_min_pct,
            app_config.relay_cutoff_c, app_config.relay_resume_c,
            app_config.buzzer_warning_c,
            ip_str, sensor_str);
        httpd_resp_sendstr_chunk(req, fields);

        httpd_resp_sendstr_chunk(req,
            "</div><div class='toggle-row'><label>Buzzer habilitado</label>");
        httpd_resp_sendstr_chunk(req,
            app_config.buzzer_enabled
            ? "<input name='buzzer_enabled' type='checkbox' value='1' checked>"
            : "<input name='buzzer_enabled' type='checkbox' value='1'>");
        httpd_resp_sendstr_chunk(req,
            "<input type='hidden' name='buzzer_enabled' value='0'></div>"
            "<div class='actions'>"
            "<button class='btn btn-primary' type='submit'>Guardar cambios</button>"
            "<a class='btn btn-ghost' href='/set?reset=1'>Restaurar defaults</a>"
            "<a class='btn btn-ghost' href='/status'>Ver JSON</a>"
            "</div></form></div>");
    }

    /* Chart.js + JavaScript */
    httpd_resp_sendstr_chunk(req,
        "<script src='https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js'></script>"
        "<script>"
        "const MAX=60,labels=[],t1Data=[],t2Data=[];"
        "let chart=null,relayBusy=false;"
        "function initChart(){"
        "const ctx=document.getElementById('tempChart').getContext('2d');"
        "chart=new Chart(ctx,{"
        "type:'line',"
        "data:{labels:labels,datasets:["
        "{label:'T1 Frente',data:t1Data,borderColor:'#3b82f6',"
        "backgroundColor:'rgba(59,130,246,.08)',tension:.3,pointRadius:0,borderWidth:2},"
        "{label:'T2 Superior',data:t2Data,borderColor:'#06b6d4',"
        "backgroundColor:'rgba(6,182,212,.08)',tension:.3,pointRadius:0,borderWidth:2}"
        "]},"
        "options:{responsive:true,animation:{duration:300},"
        "plugins:{legend:{labels:{color:'#94a3b8',font:{size:12}}}},"
        "scales:{"
        "x:{ticks:{color:'#475569',maxTicksLimit:8},grid:{color:'rgba(255,255,255,.04)'}},"
        "y:{ticks:{color:'#475569'},grid:{color:'rgba(255,255,255,.04)'},"
        "title:{display:true,text:'\\u00b0C',color:'#64748b'}}}"
        "}});"
        "}"
        "function pushPoint(t1,t2){"
        "const now=new Date().toLocaleTimeString('es-AR',{hour:'2-digit',minute:'2-digit',second:'2-digit'});"
        "labels.push(now);t1Data.push(t1);t2Data.push(t2);"
        "if(labels.length>MAX){labels.shift();t1Data.shift();t2Data.shift();}"
        "chart.update();"
        "}"
        "function g(id){return document.getElementById(id);}"
        "function updateStats(d){"
        "if(d.t1_c!==null&&g('t1v'))g('t1v').textContent=d.t1_c.toFixed(1);"
        "if(d.t2_c!==null&&g('t2v'))g('t2v').textContent=d.t2_c.toFixed(1);"
        "const rv=g('relayVal');"
        "if(rv){rv.textContent=d.relay==='on'?'ON':'OFF';"
        "rv.parentElement.className='card '+(d.relay==='on'?'c-green':'c-red');}"
        "const rs=g('relaySub');"
        "if(rs)rs.textContent=d.relay_overheat?'proteccion termica':(d.relay==='on'?'activo':'inactivo');"
        "const btn=g('relayBtn');"
        "if(btn&&!relayBusy){"
        "btn.dataset.state=d.relay==='on'?'1':'0';"
        "btn.textContent=d.relay==='on'?'Apagar rele':'Encender rele';"
        "btn.className='relay-btn '+(d.relay==='on'?'relay-on':'relay-off');"
        "btn.disabled=!!d.relay_overheat;}"
        "if(g('motorVal'))g('motorVal').textContent=d.motor_pwm_pct+'%';"
        "const mf=g('motorFill');if(mf)mf.style.width=d.motor_pwm_pct+'%';"
        "if(g('rssiVal'))g('rssiVal').textContent=d.rssi;"
        "if(g('uptimeVal')){"
        "const s=d.uptime_s,h=Math.floor(s/3600),m=Math.floor((s%3600)/60),ss=s%60;"
        "g('uptimeVal').textContent=String(h).padStart(2,'0')+':'"
        "+String(m).padStart(2,'0')+':'+String(ss).padStart(2,'0');}"
        "}"
        "async function refresh(){"
        "try{const r=await fetch('/status',{cache:'no-store'});"
        "if(!r.ok)return;"
        "const d=await r.json();"
        "updateStats(d);pushPoint(d.t1_c,d.t2_c);"
        "}catch(e){console.warn('poll',e);}"
        "}"
        "async function toggleRelay(){"
        "const btn=g('relayBtn');"
        "if(!btn||relayBusy)return;"
        "relayBusy=true;btn.disabled=true;"
        "const wantOn=btn.dataset.state==='0'?1:0;"
        "try{const r=await fetch('/relay?state='+wantOn,{cache:'no-store'});"
        "const d=await r.json();"
        "btn.dataset.state=d.relay==='on'?'1':'0';"
        "btn.textContent=d.relay==='on'?'Apagar rele':'Encender rele';"
        "btn.className='relay-btn '+(d.relay==='on'?'relay-on':'relay-off');"
        "}catch(e){console.warn('relay',e);}"
        "finally{relayBusy=false;btn.disabled=false;}"
        "}"
        "document.addEventListener('DOMContentLoaded',function(){"
        "initChart();refresh();setInterval(refresh,3000);"
        "});"
        "</script>"
    );

    httpd_resp_sendstr_chunk(req, "</div></body></html>");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    char message[384] = {0};
    char temp_text[16] = "null";
    char t1_text[16] = "null";
    char t2_text[16] = "null";
    const char *sensor_name = ds18b20_sensor_count > 1 ? DS18B20_SENSOR2_LABEL : DS18B20_SENSOR1_LABEL;
    float control_temp = 0.0f;
    bool control_valid = false;

    if (ds18b20_sensor_count > 1) {
        control_valid = temperature_valid[1];
        control_temp = last_temperature_c[1];
    } else if (ds18b20_sensor_count > 0) {
        control_valid = temperature_valid[0];
        control_temp = last_temperature_c[0];
    }

    if (control_valid) {
        snprintf(temp_text, sizeof(temp_text), "%.2f", control_temp);
    }
    if (temperature_valid[0]) {
        snprintf(t1_text, sizeof(t1_text), "%.2f", last_temperature_c[0]);
    }
    if (temperature_valid[1]) {
        snprintf(t2_text, sizeof(t2_text), "%.2f", last_temperature_c[1]);
    }

    wifi_ap_record_t ap_info = {0};
    int rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = (int)ap_info.rssi;
    }
    uint64_t uptime_s = esp_timer_get_time() / 1000000ULL;

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    snprintf(message,
             sizeof(message),
             "{\"relay\":\"%s\",\"relay_overheat\":%s,"
             "\"motor_pwm_pct\":%" PRIu32 ",\"motor_pwm_duty\":%" PRIu32 ","
             "\"sensor\":\"%s\",\"sensor_temp_c\":%s,"
             "\"t1_valid\":%s,\"t2_valid\":%s,"
             "\"t1_c\":%s,\"t2_c\":%s,"
             "\"rssi\":%d,\"uptime_s\":%" PRIu64 "}",
             relay_state ? "on" : "off",
             relay_overheat_lockout ? "true" : "false",
             (motor_pwm_duty * 100U) / MOTOR_PWM_DUTY_MAX,
             motor_pwm_duty,
             sensor_name,
             temp_text,
             temperature_valid[0] ? "true" : "false",
             temperature_valid[1] ? "true" : "false",
             t1_text, t2_text,
             rssi, uptime_s);
    return httpd_resp_sendstr(req, message);
}

static esp_err_t set_handler(httpd_req_t *req)
{
    char query[256] = {0};
    char value[32] = {0};
    app_config_t next = app_config;

    if (httpd_req_get_url_query_len(req) > 0) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_req_get_url_query_str(req, query, sizeof(query)));
    }

    if (httpd_query_key_value(query, "reset", value, sizeof(value)) == ESP_OK && strcmp(value, "1") == 0) {
        config_set_defaults(&next);
    } else {
        if (httpd_query_key_value(query, "motor_off_c", value, sizeof(value)) == ESP_OK) {
            next.motor_temp_off_c = strtof(value, NULL);
        }
        if (httpd_query_key_value(query, "motor_max_c", value, sizeof(value)) == ESP_OK) {
            next.motor_temp_max_c = strtof(value, NULL);
        }
        if (httpd_query_key_value(query, "motor_pwm_min_pct", value, sizeof(value)) == ESP_OK) {
            next.motor_pwm_min_pct = (uint8_t)strtoul(value, NULL, 10);
        }
        if (httpd_query_key_value(query, "relay_cutoff_c", value, sizeof(value)) == ESP_OK) {
            next.relay_cutoff_c = strtof(value, NULL);
        }
        if (httpd_query_key_value(query, "relay_resume_c", value, sizeof(value)) == ESP_OK) {
            next.relay_resume_c = strtof(value, NULL);
        }
        if (httpd_query_key_value(query, "buzzer_warning_c", value, sizeof(value)) == ESP_OK) {
            next.buzzer_warning_c = strtof(value, NULL);
        }
        if (httpd_query_key_value(query, "buzzer_enabled", value, sizeof(value)) == ESP_OK) {
            next.buzzer_enabled = strcmp(value, "0") != 0;
        }
    }

    if (config_validate(&next) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "config invalida");
    }

    app_config = next;
    ESP_ERROR_CHECK_WITHOUT_ABORT(config_save());
    ESP_LOGI(TAG,
             "config web: motor_off=%.1f motor_max=%.1f motor_min=%u%% relay_cutoff=%.1f relay_resume=%.1f buzzer_warn=%.1f buzzer=%s",
             app_config.motor_temp_off_c,
             app_config.motor_temp_max_c,
             app_config.motor_pwm_min_pct,
             app_config.relay_cutoff_c,
             app_config.relay_resume_c,
             app_config.buzzer_warning_c,
             app_config.buzzer_enabled ? "on" : "off");

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t relay_handler(httpd_req_t *req)
{
    char query[32] = {0};
    char value[8] = {0};

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    if (httpd_req_get_url_query_len(req) > 0) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_req_get_url_query_str(req, query, sizeof(query)));
    }

    if (httpd_query_key_value(query, "state", value, sizeof(value)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"error\":\"missing state\"}");
    }

    bool want_on = strcmp(value, "1") == 0;

    if (want_on && relay_overheat_lockout) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "{\"error\":\"relay_overheat_lockout\",\"relay\":\"off\"}");
    }

    relay_apply(want_on);
    notify_relay_state();
    ESP_LOGI(TAG, "relay web: %s", want_on ? "on" : "off");

    char resp[80];
    snprintf(resp, sizeof(resp),
             "{\"relay\":\"%s\",\"relay_overheat\":%s}",
             relay_state ? "on" : "off",
             relay_overheat_lockout ? "true" : "false");
    return httpd_resp_sendstr(req, resp);
}

static esp_err_t web_server_start(void)
{
    if (web_server_handle) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 10240;
    config.lru_purge_enable = true;

    ESP_RETURN_ON_ERROR(httpd_start(&web_server_handle, &config), TAG, "http start failed");

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t set_uri = {
        .uri = "/set",
        .method = HTTP_GET,
        .handler = set_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t relay_uri = {
        .uri = "/relay",
        .method = HTTP_GET,
        .handler = relay_handler,
        .user_ctx = NULL,
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(web_server_handle, &root_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(web_server_handle, &status_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(web_server_handle, &set_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(web_server_handle, &relay_uri));
    ESP_LOGI(TAG, "web server listo stack=%d", config.stack_size);
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (wifi_sta_credentials_present()) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        sta_ipv4_addr.addr = 0;
        if (wifi_sta_credentials_present()) {
            ESP_LOGW(TAG, "wifi desconectado reason=%d, reintentando", event ? event->reason : -1);
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        sta_ipv4_addr = event->ip_info.ip;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "wifi ip=" IPSTR, IP2STR(&event->ip_info.ip));
        ESP_ERROR_CHECK_WITHOUT_ABORT(mdns_start_if_needed());
        ESP_ERROR_CHECK_WITHOUT_ABORT(web_server_start());
    }
}

static void log_wifi_radio_state(const char *label)
{
    uint8_t self_mac[6] = {0};
    uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    int8_t tx_power = 0;

    esp_err_t mac_err = esp_wifi_get_mac(WIFI_IF_STA, self_mac);
    esp_err_t channel_err = esp_wifi_get_channel(&primary, &second);
    esp_err_t tx_err = esp_wifi_get_max_tx_power(&tx_power);

    if (mac_err == ESP_OK && channel_err == ESP_OK && tx_err == ESP_OK) {
        ESP_LOGI(TAG,
                 "%s self=" MACSTR " channel=%u tx_power_raw=%d approx_dbm=%d",
                 label,
                 MAC2STR(self_mac),
                 primary,
                 tx_power,
                 tx_power / 4);
        return;
    }

    ESP_LOGW(TAG,
             "%s mac_err=%s channel_err=%s tx_err=%s",
             label,
             esp_err_to_name(mac_err),
             esp_err_to_name(channel_err),
             esp_err_to_name(tx_err));
}

static bool is_authorized_peer(const uint8_t *mac)
{
    return mac &&
           (memcmp(mac, EDGE_AGENT_MAC_1, sizeof(EDGE_AGENT_MAC_1)) == 0 ||
            memcmp(mac, EDGE_AGENT_MAC_2, sizeof(EDGE_AGENT_MAC_2)) == 0);
}

static void espnow_send_text_reply(const uint8_t *dst_addr, const char *text)
{
    if (!dst_addr || !text) {
        return;
    }

    esp_err_t err = esp_now_send(dst_addr, (const uint8_t *)text, strlen(text));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "reply failed: %s", esp_err_to_name(err));
    }
}

void notify_relay_state(void)
{
    const char *message = relay_state ? "event relay=on" : "event relay=off";
    espnow_send_text_reply(EDGE_AGENT_MAC_1, message);
    espnow_send_text_reply(EDGE_AGENT_MAC_2, message);
    maybe_report_status(true);
}

void espnow_send_status_reply(const uint8_t *dst_addr)
{
    char message[96] = {0};
    char temp1_text[16] = "na";
    char temp2_text[16] = "na";

    if (!dst_addr) {
        return;
    }

    if (temperature_valid[0]) {
        snprintf(temp1_text, sizeof(temp1_text), "%.2f", last_temperature_c[0]);
    }
    if (temperature_valid[1]) {
        snprintf(temp2_text, sizeof(temp2_text), "%.2f", last_temperature_c[1]);
    }

    if (ds18b20_sensor_count > 1) {
        snprintf(message,
                 sizeof(message),
                 "status t1_frente=%s t2_superior=%s relay=%s",
                 temp1_text,
                 temp2_text,
                 relay_state ? "on" : "off");
    } else if (temperature_valid[0]) {
        snprintf(message,
                 sizeof(message),
                 "status temp=%.2f relay=%s",
                 last_temperature_c[0],
                 relay_state ? "on" : "off");
    } else {
        snprintf(message,
                 sizeof(message),
                 "status temp=na relay=%s",
                 relay_state ? "on" : "off");
    }

    ESP_LOGI(TAG, "espnow status tx to " MACSTR ": %s", MAC2STR(dst_addr), message);
    espnow_send_text_reply(dst_addr, message);
}

static void espnow_broadcast_status(void)
{
    espnow_send_status_reply(EDGE_AGENT_MAC_1);
    espnow_send_status_reply(EDGE_AGENT_MAC_2);
}

void maybe_report_status(bool force)
{
    TickType_t now = xTaskGetTickCount();
    bool relay_changed = !status_snapshot_valid || relay_state != last_reported_relay_state;
    bool temp_changed = false;
    bool heartbeat_due = !status_snapshot_valid ||
                         (now - last_status_report_tick) >= ESPNOW_STATUS_HEARTBEAT_INTERVAL;

    for (size_t i = 0; i < ds18b20_sensor_count; ++i) {
        if (!temperature_valid[i]) {
            continue;
        }

        if (!status_snapshot_valid) {
            temp_changed = true;
            break;
        }

        float delta = last_temperature_c[i] - last_reported_temperature_c[i];
        if (delta < 0.0f) {
            delta = -delta;
        }
        if (delta >= ESPNOW_STATUS_TEMP_DELTA_C) {
            temp_changed = true;
            break;
        }
    }

    if (!force && !relay_changed && !temp_changed && !heartbeat_due) {
        return;
    }

    espnow_broadcast_status();
    status_snapshot_valid = true;
    last_reported_relay_state = relay_state;
    last_status_report_tick = now;
    for (size_t i = 0; i < ds18b20_sensor_count; ++i) {
        if (temperature_valid[i]) {
            last_reported_temperature_c[i] = last_temperature_c[i];
        }
    }
}

void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_sta() ? ESP_OK : ESP_ERR_NO_MEM);

    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(wifi_event_group ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));

    wifi_config_t wifi_cfg = {0};
    snprintf((char *)wifi_cfg.sta.ssid, sizeof(wifi_cfg.sta.ssid), "%s", WIFI_STA_SSID);
    snprintf((char *)wifi_cfg.sta.password, sizeof(wifi_cfg.sta.password), "%s", WIFI_STA_PASSWORD);
    wifi_cfg.sta.threshold.authmode = WIFI_STA_PASSWORD[0] != '\0' ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    wifi_cfg.sta.pmf_cfg.capable = true;
    wifi_cfg.sta.pmf_cfg.required = false;

    if (wifi_sta_credentials_present()) {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(80));
    if (!wifi_sta_credentials_present()) {
        ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
        ESP_LOGW(TAG, "wifi ssid vacio: web local deshabilitada, solo esp-now en canal %u", ESPNOW_CHANNEL);
    }
    log_wifi_radio_state("wifi ready");
}

static void espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    if (!tx_info) {
        return;
    }

    ESP_LOGI(TAG, "tx to " MACSTR " status=%s",
             MAC2STR(tx_info->des_addr),
             status == ESP_NOW_SEND_SUCCESS ? "ok" : "fail");
}

static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    if (!recv_info || !recv_info->src_addr || !data || len <= 0) {
        return;
    }

    if (!is_authorized_peer(recv_info->src_addr)) {
        ESP_LOGW(TAG, "ignoring rx from unauthorized peer " MACSTR,
                 MAC2STR(recv_info->src_addr));
        return;
    }

    int rssi = recv_info->rx_ctrl ? recv_info->rx_ctrl->rssi : 0;
    ESP_LOGI(TAG, "rx from " MACSTR " len=%d rssi=%d text=%.*s",
             MAC2STR(recv_info->src_addr), len, rssi, len, (const char *)data);

    if (len == 4 && memcmp(data, "hola", 4) == 0) {
        uint8_t self_mac[6] = {0};
        ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, self_mac));
        char reply[32] = {0};
        snprintf(reply, sizeof(reply), "ack desde " MACSTR, MAC2STR(self_mac));
        espnow_send_text_reply(recv_info->src_addr, reply);
        espnow_send_status_reply(recv_info->src_addr);
        return;
    }

    if ((len == 7 && memcmp(data, "rele on", 7) == 0) ||
        (len == 8 && memcmp(data, "relay on", 8) == 0)) {
        espnow_send_text_reply(recv_info->src_addr, "err local_only");
        return;
    }

    if ((len == 8 && memcmp(data, "rele off", 8) == 0) ||
        (len == 9 && memcmp(data, "relay off", 9) == 0)) {
        espnow_send_text_reply(recv_info->src_addr, "err local_only");
        return;
    }

    if ((len == 11 && memcmp(data, "rele toggle", 11) == 0) ||
        (len == 12 && memcmp(data, "relay toggle", 12) == 0)) {
        espnow_send_text_reply(recv_info->src_addr, "err local_only");
        return;
    }

    if ((len == 11 && memcmp(data, "rele status", 11) == 0) ||
        (len == 12 && memcmp(data, "relay status", 12) == 0)) {
        espnow_send_status_reply(recv_info->src_addr);
        return;
    }

    espnow_send_text_reply(recv_info->src_addr, "err invalid_cmd");
}

void espnow_init_peer(void)
{
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, EDGE_AGENT_MAC_1, 6);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;

    if (!esp_now_is_peer_exist(EDGE_AGENT_MAC_1)) {
        ESP_ERROR_CHECK(esp_now_add_peer(&peer));
    }

    memcpy(peer.peer_addr, EDGE_AGENT_MAC_2, 6);
    if (!esp_now_is_peer_exist(EDGE_AGENT_MAC_2)) {
        ESP_ERROR_CHECK(esp_now_add_peer(&peer));
    }
}
