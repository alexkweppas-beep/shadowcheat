#pragma once
#include <string>

const std::string UI_HTML = R"__HTML__(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>shadowcheats.cc</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Inter:ital,wght@0,300;0,400;0,500;0,600;0,700;1,400&display=swap" rel="stylesheet">
<style>
/* ===== RESET & BASE ===== */
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
html,body{height:100%;overflow:hidden;background-color:rgba(0,0,0,0);}
body{
  background:transparent;font-family:'Inter',system-ui,sans-serif;
  -webkit-font-smoothing:antialiased;
  display:grid;place-items:center;
  user-select:none; /* Запрет выделения текста */
}

/* ===== AURORA BACKGROUND ===== */
.aurora{
  position:fixed;inset:0;pointer-events:none;overflow:hidden;
  filter:blur(0);
}
.aurora-beam{
  position:absolute;
  border-radius:50%;
  mix-blend-mode:screen;
  opacity:0;
  animation:aurora-fade 8s ease-in-out infinite;
}
.b1{
  width:900px;height:180px;
  background:linear-gradient(90deg,transparent,rgba(120,180,255,.55),rgba(80,220,200,.4),rgba(180,100,255,.35),transparent);
  top:8%;left:-10%;transform:rotate(-12deg);
  animation-delay:0s;
}
.b2{
  width:800px;height:140px;
  background:linear-gradient(90deg,transparent,rgba(60,120,255,.45),rgba(140,80,255,.4),rgba(220,120,200,.3),transparent);
  top:14%;left:5%;transform:rotate(-10deg);
  animation-delay:1.4s;
}
.b3{
  width:700px;height:100px;
  background:linear-gradient(90deg,transparent,rgba(100,200,255,.3),rgba(60,180,220,.4),transparent);
  top:22%;left:20%;transform:rotate(-8deg);
  animation-delay:2.8s;
}
@keyframes aurora-fade{
  0%,100%{opacity:0;transform:rotate(var(--r,-12deg)) translateX(0)}
  30%,70%{opacity:1;transform:rotate(var(--r,-12deg)) translateX(40px)}
}
.b1{--r:-12deg} .b2{--r:-10deg} .b3{--r:-8deg}

/* ===== WINDOW ===== */
.app{
  position:relative;z-index:10;
  width:860px;height:540px;
  background:#000000;
  border-radius:15px;
  box-shadow: 0 15px 35px rgba(0,0,0,.6);
  overflow:hidden;
  border: 1px solid #000000;
  transform: translateZ(0); /* Fix border-radius overflow anti-aliasing leak */
  display:flex;flex-direction:column;
  -webkit-app-region:drag; /* Позволяет таскать окно */
}

/* red close dot */
.close-dot{
  position:absolute;top:14px;right:16px;z-index:100;
  width:12px;height:12px;border-radius:50%;
  background:#ff5f57;
  box-shadow:0 0 0 1px rgba(0,0,0,.12);
  cursor:pointer;-webkit-app-region:no-drag;
  transition:filter .15s;
}
.close-dot:hover{filter:brightness(1.2)}

/* ===== SCREEN SYSTEM ===== */
.screen{position:absolute;inset:0;display:flex;transition:opacity .35s ease,transform .35s ease}
.screen.hidden{opacity:0;pointer-events:none;transform:scale(.98)}
.screen.visible{opacity:1;pointer-events:all;transform:scale(1)}

/* ===== LOGIN SCREEN ===== */
#screen-login{background:#000000}

/* left: lavender hero */
.login-hero{
  width:340px;flex:0 0 340px;
  background:linear-gradient(145deg,#050505 0%,#0f0f0f 45%,#000000 100%);
  padding:28px 32px;
  display:flex;flex-direction:column;
  position:relative;overflow:hidden;
}
.login-hero::after{
  content:'';position:absolute;bottom:-60px;right:-60px;
  width:300px;height:300px;border-radius:50%;
  background:rgba(150,160,170,.1);pointer-events:none;
}
.hero-top{display:flex;align-items:center;justify-content:space-between;margin-bottom:auto}
.hero-badge{
  font-size:10.5px;font-weight:600;letter-spacing:.1em;
  color:rgba(200,210,220,.8);background:rgba(150,160,170,.12);border:1px solid rgba(150,160,170,.25);
  padding:4px 10px;border-radius:100px;
}
.hero-ver{font-size:12px;color:rgba(255,255,255,.25);font-weight:500}
.hero-title{
  font-size:38px;font-weight:700;line-height:1.12;letter-spacing:-.03em;
  color:#fff;margin:auto 0 32px;
}
.hero-link{
  display:flex;align-items:center;gap:7px;
  font-size:13px;font-weight:500;color:rgba(129,140,248,.6);
  text-decoration:none;cursor:pointer;-webkit-app-region:no-drag;
  transition:color .15s;
}
.hero-link:hover{color:rgba(165,180,252,1)}
.hero-link svg{width:14px;height:14px;opacity:.6}

/* right: form */
.login-form-panel{
  flex:1;display:flex;flex-direction:column;
  align-items:center;justify-content:center;
  padding:40px 52px;
}
.form-title{font-size:21px;font-weight:650;color:#e8e9f4;margin-bottom:6px;letter-spacing:-.02em}
.form-sub{font-size:13.5px;color:rgba(255,255,255,.35);margin-bottom:28px}

.field{
  width:100%;position:relative;margin-bottom:12px;
}
.field input{
  width:100%;height:46px;padding:0 40px 0 40px;
  border:1.5px solid rgba(255,255,255,.09);border-radius:10px;
  font-size:14px;font-family:inherit;color:#e8e9f4;
  background:rgba(255,255,255,.05);outline:none;
  transition:border-color .18s,background .18s,box-shadow .18s;
  -webkit-app-region:no-drag; /* Позволяет кликать в поле */
}
.field input:focus{
  border-color:#6b7280;background:#000000;
  box-shadow:0 0 0 4px rgba(107,114,128,.15);
}
.field input::placeholder{color:rgba(255,255,255,.2)}
.fi-left,.fi-right{
  position:absolute;top:50%;transform:translateY(-50%);
  width:16px;height:16px;color:rgba(255,255,255,.2);pointer-events:none;
  transition:color .18s;
}
.fi-left{left:13px}
.fi-right{right:13px;cursor:pointer;pointer-events:all}
.field input:focus~.fi-left{color:rgba(150,160,170,.9)}

.forgot{align-self:flex-start;font-size:12.5px;color:rgba(255,255,255,.35);cursor:pointer;margin-bottom:18px;-webkit-app-region:no-drag}
.forgot:hover{color:rgba(180,190,200,.8)}

.btn-primary,.btn-secondary{
  width:100%;height:44px;border-radius:10px;border:0;
  font-size:14.5px;font-weight:600;font-family:inherit;
  cursor:pointer;transition:filter .15s,transform .1s;
  -webkit-app-region:no-drag;
}
.btn-primary{
  background:linear-gradient(145deg,#1c2023 0%,#252b2e 50%,#161b1e 100%);color:#fff;margin-bottom:10px;
  box-shadow:0 8px 24px -8px rgba(0,0,0,.5);border:1px solid rgba(150,160,170,.12);
}
.btn-primary:hover{filter:brightness(1.08)}
.btn-primary:active{transform:scale(.98)}
.btn-secondary{
  background:rgba(255,255,255,.06);color:rgba(255,255,255,.7);
}
.btn-secondary:hover{background:rgba(255,255,255,.1)}
.btn-secondary:active{transform:scale(.98)}

/* loading dots inside btn */
.btn-dots{display:none;align-items:center;justify-content:center;gap:5px}
.btn-primary.loading .btn-label{display:none}
.btn-primary.loading .btn-dots{display:flex}
.btn-dot{width:6px;height:6px;border-radius:50%;background:#000000}
.btn-dot:nth-child(1){animation:db .8s ease-in-out infinite}
.btn-dot:nth-child(2){animation:db .8s ease-in-out .15s infinite}
.btn-dot:nth-child(3){animation:db .8s ease-in-out .3s infinite}
@keyframes db{0%,80%,100%{transform:scale(.8);opacity:.5}40%{transform:scale(1.2);opacity:1}}


/* ===== DASHBOARD SCREEN ===== */
#screen-dash{background:#000000;flex-direction:row}

/* sidebar */
.sidebar{
  width:210px;flex:0 0 210px;
  background:#000000;
  border-right:1px solid rgba(255,255,255,.07);
  display:flex;flex-direction:column;
  padding:0;
}
.sidebar-header{
  padding:18px 20px 14px;
  display:flex;align-items:center;justify-content:space-between;
  border-bottom:1px solid rgba(255,255,255,.07);
}
.sidebar-title{font-size:13px;font-weight:650;color:#e8e9f4;letter-spacing:-.01em}
.lang-toggle{display:flex;gap:2px;background:rgba(255,255,255,.06);border-radius:7px;padding:3px}
.lang-btn{
  padding:3px 8px;border-radius:5px;font-size:11.5px;font-weight:600;
  color:rgba(255,255,255,.3);cursor:pointer;transition:all .15s;-webkit-app-region:no-drag;
  border:0;background:transparent;font-family:inherit;
}
.lang-btn.active{background:#000000;color:#e8e9f4;box-shadow:0 1px 3px rgba(0,0,0,.1)}

.product-list{flex:1;overflow-y:auto;padding:8px 0}
.product-list::-webkit-scrollbar{width:4px}
.product-list::-webkit-scrollbar-thumb{background:rgba(255,255,255,.1);border-radius:2px}

.product-item{
  display:flex;align-items:center;gap:10px;
  padding:10px 20px;cursor:pointer;-webkit-app-region:no-drag;
  border-radius:0;transition:background .12s;
  border-left:2.5px solid transparent;
}
.product-item:hover{background:rgba(255,255,255,.04)}
.product-item.active{
  background:rgba(107,114,128,.1);
  border-left-color:#6b7280;
}
.product-icon{
  width:36px;height:36px;border-radius:8px;
  display:grid;place-items:center;flex:0 0 36px;
}
.product-icon svg{width:20px;height:20px}
.pi-apex{background:#fff3f3} .pi-apex svg{color:#e03}
.pi-rage{background:transparent} .pi-rage img{filter:drop-shadow(0 0 4px rgba(255,200,0,.3))}
.pi-altv{background:transparent} .pi-altv img{filter:drop-shadow(0 0 4px rgba(40,180,40,.3))}
.pi-dev {background:#fff0f8} .pi-dev  svg{color:#e06}

.pi-van {background:#f0fffe} .pi-van  svg{color:#0aa}

.product-info{flex:1;min-width:0}
.product-name{font-size:13.5px;font-weight:600;color:#e8e9f4;letter-spacing:-.01em}
.product-status{font-size:11.5px;font-weight:500;margin-top:1px}
.st-ok{color:#4ade80}.st-warn{color:#fb923c}
.product-time{display:none}

.sidebar-footer{
  padding:14px 20px;
  border-top:1px solid rgba(255,255,255,.07);
  display:flex;align-items:center;justify-content:space-between;
}
.user-info{display:flex;align-items:center;gap:8px}
.user-avatar{
  width:28px;height:28px;border-radius:50%;
  background:linear-gradient(135deg,#3a4044,#252b2e);
  display:grid;place-items:center;
  font-size:11px;font-weight:700;color:#fff;
}
.user-name{font-size:12.5px;font-weight:600;color:#333}
.key-btn{
  display:flex;align-items:center;gap:5px;
  font-size:12px;font-weight:600;color:#9ca3af;
  background:none;border:0;cursor:pointer;font-family:inherit;
  -webkit-app-region:no-drag;
}
.key-btn svg{width:12px;height:12px}

/* detail panel */
.detail-panel{
  flex:1;display:flex;flex-direction:column;
  background:linear-gradient(170deg,#0a0a0a 0%,#000000 100%);
  overflow:hidden;
}
.detail-header{
  padding:22px 28px 18px;
  border-bottom:1px solid rgba(255,255,255,.07);
}
.detail-name{font-size:22px;font-weight:700;color:#e8e9f4;letter-spacing:-.03em}
.detail-meta{
  display:flex;align-items:center;gap:8px;margin-top:8px;
}
.badge{
  font-size:11.5px;font-weight:600;padding:3px 9px;border-radius:100px;
}
.badge-ok{background:rgba(74,222,128,.1);color:#4ade80} 
.badge-active{background:rgba(107,114,128,.15);color:#9ca3af}
.badge-date{background:transparent;color:rgba(255,255,255,.28);font-weight:500;padding:0}

.changelog{flex:1;overflow-y:auto;padding:18px 28px}
.changelog::-webkit-scrollbar{width:4px}
.changelog::-webkit-scrollbar-thumb{background:rgba(255,255,255,.08);border-radius:2px}
.cl-item{margin-bottom:20px}
.cl-header{display:flex;align-items:center;gap:10px;margin-bottom:8px}
.cl-ver{
  font-size:12px;font-weight:700;background:#3a4044;color:#d1d5db;
  padding:2px 8px;border-radius:6px;
}
.cl-date{font-size:12px;color:rgba(255,255,255,.3)}
.cl-notes{list-style:none;padding:0}
.cl-notes li{
  font-size:13px;color:rgba(255,255,255,.55);padding:3px 0 3px 14px;position:relative;
}
.cl-notes li::before{content:'•';position:absolute;left:0;color:#aab}
.product-desc{
  font-size:13.5px;line-height:1.65;color:rgba(255,255,255,.45);
  font-weight:400;margin-top:4px;letter-spacing:.01em;
}

.detail-actions{
  padding:16px 28px;
  border-top:1.5px solid rgba(107,114,128,.15);
}
.toggle-row{
  display:flex;align-items:center;justify-content:space-between;
  margin-bottom:14px;
}
.toggle-label{font-size:13px;font-weight:500;color:#555}
.toggle-switch{
  position:relative;width:38px;height:22px;
  -webkit-app-region:no-drag;
}
.toggle-switch input{opacity:0;width:0;height:0}
.toggle-slider{
  position:absolute;inset:0;border-radius:100px;
  background:rgba(255,255,255,.08);cursor:pointer;
  transition:background .18s;
}
.toggle-slider::after{
  content:'';position:absolute;left:3px;top:3px;
  width:16px;height:16px;border-radius:50%;background:#000000;
  box-shadow:0 1px 4px rgba(0,0,0,.2);
  transition:transform .18s;
}
.toggle-switch input:checked~.toggle-slider{background:#4b5563}
.toggle-switch input:checked~.toggle-slider::after{transform:translateX(16px)}

.btn-launch{
  width:100%;height:44px;border-radius:10px;border:0;
  background:linear-gradient(135deg,#3a4044,#252b2e);color:#fff;
  font-size:14.5px;font-weight:700;font-family:inherit;
  display:flex;align-items:center;justify-content:center;gap:9px;
  cursor:pointer;-webkit-app-region:no-drag;
  box-shadow:0 8px 24px -8px rgba(0,0,0,.4);
  transition:filter .15s,transform .1s;
}
.btn-launch:hover{filter:brightness(1.08)}
.btn-launch:active{transform:scale(.98)}
.btn-launch svg{width:16px;height:16px}
.btn-auto-clear{
  width:100%;height:38px;border-radius:10px;border:1px solid rgba(255,255,255,.22);
  background:rgba(255,255,255,.09);color:rgba(255,255,255,.85);
  font-size:13px;font-weight:600;font-family:inherit;
  cursor:pointer;margin-bottom:10px;-webkit-app-region:no-drag;
  letter-spacing:.04em;transition:background .15s,color .15s,border-color .15s;
}
.btn-auto-clear:hover{background:rgba(255,255,255,.15);color:#fff;border-color:rgba(255,255,255,.35);}
.btn-auto-clear:active{transform:scale(.98)}

/* ===== TRANSITIONS ===== */
[data-in]{opacity:0;transform:translateY(10px)}
.fade-in{animation:fin .4s ease forwards}
@keyframes fin{to{opacity:1;transform:translateY(0)}}

/* ===== LIGHT THEME ===== */
body.light-theme {
  --bg: #f0f0f2;
  --panel-bg: #ffffff;
  --text: #1a1a2e;
  --text-muted: rgba(30,30,60,.45);
  --border: rgba(0,0,0,.1);
  --field-bg: #f5f5f8;
  --field-border: rgba(0,0,0,.13);
  --field-focus-border: #c0392b;
  --field-focus-shadow: rgba(192,57,43,.12);
  --item-hover: rgba(0,0,0,.04);
  --item-active: rgba(192,57,43,.08);
  --sidebar-bg: #fafafa;
  --detail-bg: #f5f5f8;
  --divider: rgba(192,57,43,.12);
}
body.light-theme .app{background:#f0f0f2; border-color: rgba(0,0,0,0.1);}
body.light-theme #screen-login{background:#f0f0f2}
body.light-theme #screen-dash{background:#f0f0f2}
body.light-theme .login-form-panel{background:#ffffff}
body.light-theme .form-title{color:#1a1a2e}
body.light-theme .form-sub{color:rgba(30,30,60,.5)}
body.light-theme .field input{
  background:#f5f5f8;border-color:rgba(0,0,0,.13);color:#1a1a2e;
}
body.light-theme .field input:focus{
  background:#fff;border-color:#6b7280;box-shadow:0 0 0 4px rgba(107,114,128,.12);
}
body.light-theme .field input::placeholder{color:rgba(30,30,60,.3)}
body.light-theme .field svg{color:rgba(30,30,60,.35)}
body.light-theme .forgot{color:rgba(30,30,60,.4)}
body.light-theme .sidebar{
  background:#ffffff;border-right:1px solid rgba(0,0,0,.07);
}
body.light-theme .sidebar-header{border-bottom-color:rgba(0,0,0,.07)}
body.light-theme .product-item:hover{background:rgba(0,0,0,.04)}
body.light-theme .product-item.active{background:rgba(107,114,128,.1)}
body.light-theme .product-name{color:#1a1a2e}
body.light-theme .detail-panel{background:#f5f5f8}
body.light-theme .detail-header{border-bottom-color:rgba(0,0,0,.07)}
body.light-theme .detail-title{color:#1a1a2e}
body.light-theme .detail-name{color:#111 !important;font-weight:700}
body.light-theme .detail-actions{border-top-color:rgba(0,0,0,.08)}
body.light-theme .toggle-label{color:rgba(30,30,60,.7)}
body.light-theme .cl-date{color:rgba(30,30,60,.4)}
body.light-theme .cl-note{color:rgba(30,30,60,.65)}
body.light-theme .user-name{color:#1a1a2e}
body.light-theme .sidebar-footer{border-top-color:rgba(0,0,0,.07)}
body.light-theme .product-desc{color:rgba(30,30,60,.55)}
body.light-theme .sidebar-title{color:#1a1a2e}
body.light-theme .product-name{color:#1a1a2e !important}
body.light-theme .product-status{color:rgba(30,30,60,.5)}
body.light-theme .detail-title{color:#1a1a2e !important}
body.light-theme .badge-ok{background:rgba(74,222,128,.15);color:#16a34a}
body.light-theme .badge-active{background:rgba(107,114,128,.12);color:#4b5563}
body.light-theme .cl-ver{background:#3a4044;color:#fff}
body.light-theme .product-desc{color:rgba(30,30,60,.6) !important}
body.light-theme .btn-auto-clear{
  background:rgba(0,0,0,.07);color:#1a1a2e;border-color:rgba(0,0,0,.18);
}
body.light-theme .btn-auto-clear:hover{
  background:rgba(0,0,0,.12);color:#000;border-color:rgba(0,0,0,.28);
}
body.light-theme .btn-launch{
  background:linear-gradient(135deg,#2d3135,#1a1d1f);color:#fff;
}
body.light-theme .user-avatar{background:linear-gradient(135deg,#3a4044,#252b2e)}
body.light-theme .key-btn{color:#4b5563}
body.light-theme .toggle-label{color:#374151 !important}
body.light-theme .close-dot{background:#e5534b}

/* toggle pill */
.theme-toggle{
  position:absolute;top:12px;right:44px;
  display:flex;align-items:center;gap:7px;
  -webkit-app-region:no-drag;
  z-index:10;
}
.theme-pill{
  width:36px;height:20px;border-radius:100px;
  background:rgba(255,255,255,.1);border:1px solid rgba(255,255,255,.15);
  position:relative;cursor:pointer;transition:background .2s;
  flex-shrink:0;
}
.theme-pill::after{
  content:'';position:absolute;top:3px;left:3px;
  width:12px;height:12px;border-radius:50%;
  background:#fff;transition:transform .2s,background .2s;
}
body.light-theme .theme-pill{
  background:rgba(107,114,128,.3);border-color:rgba(107,114,128,.4);
}
body.light-theme .theme-pill::after{
  transform:translateX(16px);background:#6b7280;
}
.theme-icon{font-size:13px;opacity:.6;user-select:none}
body.light-theme .theme-icon{opacity:.8}

/* ===== GEAR BUTTON ===== */
.gear-btn{
  position:absolute;top:11px;right:44px;
  width:22px;height:22px;border-radius:50%;
  display:grid;place-items:center;
  cursor:pointer;-webkit-app-region:no-drag;
  color:rgba(255,255,255,.45);
  transition:color .15s,transform .3s;
  z-index:20;
}
body.light-theme .gear-btn{color:rgba(0,0,0,.4);}
.gear-btn:hover{color:rgba(255,255,255,.9);transform:rotate(45deg)}
body.light-theme .gear-btn:hover{color:rgba(0,0,0,.85)}

/* ===== GEAR MENU ===== */
.gear-menu{
  position:absolute;top:38px;right:36px;
  background:#0a0a0a;border:1px solid rgba(255,255,255,.1);
  border-radius:12px;padding:6px;
  min-width:190px;
  box-shadow:0 8px 32px rgba(0,0,0,.4);
  z-index:100;display:none;
  -webkit-app-region:no-drag;
  animation:menuIn .15s ease;
}
body.light-theme .gear-menu{
  background:#ffffff;border-color:rgba(0,0,0,.1);
  box-shadow:0 8px 32px rgba(0,0,0,.15);
}
@keyframes menuIn{
  from{opacity:0;transform:translateY(-6px) scale(.96)}
  to{opacity:1;transform:translateY(0) scale(1)}
}
.gear-menu.open{display:block}
.gear-menu-item{
  display:flex;align-items:center;gap:9px;
  padding:9px 10px;border-radius:8px;
  cursor:pointer;transition:background .12s;
  font-size:13px;font-weight:500;color:rgba(255,255,255,.8);
}
body.light-theme .gear-menu-item{color:#1a1a2e}
.gear-menu-item:hover{background:rgba(255,255,255,.07)}
body.light-theme .gear-menu-item:hover{background:rgba(0,0,0,.05)}
.gm-label{flex:1}
.gear-menu-divider{height:1px;background:rgba(255,255,255,.07);margin:3px 4px}
body.light-theme .gear-menu-divider{background:rgba(0,0,0,.08)}

/* gear toggle knob */
.gm-toggle{
  width:32px;height:18px;border-radius:100px;
  background:rgba(255,255,255,.15);position:relative;
  transition:background .2s;flex-shrink:0;
}
body.light-theme .gm-toggle{background:rgba(0,0,0,.15)}
.gm-toggle.on{background:#4b5563}
.gm-toggle-knob{
  position:absolute;top:3px;left:3px;
  width:12px;height:12px;border-radius:50%;
  background:#fff;transition:transform .2s;
}
.gm-toggle.on .gm-toggle-knob{transform:translateX(14px)}

/* lang row */
.lang-row{cursor:default}
.gm-lang-btn{
  font-size:12.5px;font-weight:700;padding:3px 8px;border-radius:6px;
  cursor:pointer;color:rgba(255,255,255,.4);transition:all .15s;
}
body.light-theme .gm-lang-btn{color:rgba(0,0,0,.35)}
.gm-lang-btn.active{background:#4b5563;color:#fff}
body.light-theme .gm-lang-btn.active{background:#2d3135;color:#fff}
.gm-lang-sep{color:rgba(255,255,255,.2);font-size:12px}
body.light-theme .gm-lang-sep{color:rgba(0,0,0,.2)}
</style>
</head>
<body class="light-theme">

<!-- aurora background -->

  <div class="aurora-beam b2"></div>
  <div class="aurora-beam b3"></div>
</div>

<div class="app">
  <!-- universal close dot -->
  <div class="close-dot" id="close-dot"></div>
  <!-- gear icon (shown after login) -->
  <div class="gear-btn" id="gear-btn" style="display:none">
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" width="14" height="14"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg>
  </div>
  <!-- gear dropdown menu -->
  <div class="gear-menu" id="gear-menu">
    <div class="gear-menu-item" id="gm-theme">
      <span class="gm-label" data-ru="Тёмная тема" data-en="Dark theme">Тёмная тема</span>
      <div class="gm-toggle" id="gm-theme-toggle"><div class="gm-toggle-knob"></div></div>
    </div>
    <div class="gear-menu-divider"></div>
    <div class="gear-menu-item" id="gm-clear">
      <span class="gm-label" data-ru="Очистить" data-en="Clear">Очистить</span>
    </div>
    <div class="gear-menu-divider"></div>
    <div class="gear-menu-item" id="gm-logout">
      <span class="gm-label" data-ru="Выйти" data-en="Log out">Выйти</span>
    </div>
    <div class="gear-menu-divider"></div>
    <div class="gear-menu-item lang-row">
      <span class="gm-lang-btn active" id="lang-ru">RU</span>
      <span class="gm-lang-sep">/</span>
      <span class="gm-lang-btn" id="lang-en">EN</span>
    </div>
  </div>

  <!-- ===== LOGIN SCREEN ===== -->
  <div class="screen visible" id="screen-login">
    <!-- hero left -->
    <div class="login-hero">
      <div class="hero-top">
        <span class="hero-badge">shadowcheats.cc</span>
        
      </div>
      <h1 class="hero-title">Ваш центр<br>возможностей.</h1>

    </div>

    <!-- form right -->
    <div class="login-form-panel">
      <div class="form-title">Вход в аккаунт</div>
      <div class="form-sub">Авторизуйтесь, чтобы получить доступ к продуктам.</div>

      <form id="login-form" style="width:100%;display:flex;flex-direction:column;">
        <div class="field">
          <input type="text" id="inp-user" placeholder="Логин" />
          <svg class="fi-left" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/></svg>
        </div>
        <div class="field">
          <input type="password" id="inp-pass" placeholder="Пароль" />
          <svg class="fi-left" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="11" width="18" height="11" rx="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/></svg>
          <svg class="fi-right" id="eye-toggle" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94"/><path d="M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19"/><line x1="1" y1="1" x2="23" y2="23"/></svg>
        </div>
        <span class="forgot">Забыли пароль?</span>
        <button type="submit" class="btn-primary" id="btn-login">
          <span class="btn-label">Войти</span>
          <div class="btn-dots">
            <div class="btn-dot"></div><div class="btn-dot"></div><div class="btn-dot"></div>
          </div>
        </button>

      </form>
    </div>
  </div>

  <!-- ===== DASHBOARD SCREEN ===== -->
  <div class="screen hidden" id="screen-dash">
    <!-- sidebar -->
    <aside class="sidebar">
      <div class="sidebar-header">
        <span class="sidebar-title">Выберите продукт</span>
      </div>

      <div class="product-list" id="product-list">
        <!-- filled by JS -->
      </div>

      <div class="sidebar-footer">
        <div class="user-info">
          <div class="user-avatar" id="user-avatar">V</div>
          <span class="user-name" id="user-name-label">User</span>
        </div>

      </div>
    </aside>

    <!-- detail -->
    <div class="detail-panel" id="detail-panel">
      <!-- filled by JS -->
    </div>
  </div>

  <!-- ===== SUCCESS MODAL ===== -->
  <div class="screen hidden" id="modal-success" style="z-index: 200; background: rgba(0,0,0,0.6); backdrop-filter: blur(5px); align-items: center; justify-content: center; -webkit-app-region:no-drag;">
    <div class="modal-box" style="background: #1c2023; padding: 32px 24px; border-radius: 16px; border: 1px solid rgba(255,255,255,0.1); position: relative; text-align: center; width: 320px; box-shadow: 0 20px 50px rgba(0,0,0,0.5);">
      <div class="modal-close" style="position: absolute; top: 12px; right: 12px; cursor: pointer; color: rgba(255,255,255,0.5); transition: color 0.15s;" onclick="document.getElementById('modal-success').classList.replace('visible','hidden')" onmouseover="this.style.color='#fff'" onmouseout="this.style.color='rgba(255,255,255,0.5)'">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="20" height="20"><line x1="18" y1="6" x2="6" y2="18"></line><line x1="6" y1="6" x2="18" y2="18"></line></svg>
      </div>
      <div style="color: #4ade80; margin-bottom: 16px; display:flex; justify-content:center;">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" width="48" height="48" stroke-linecap="round" stroke-linejoin="round"><path d="M22 11.08V12a10 10 0 1 1-5.93-9.14"></path><polyline points="22 4 12 14.01 9 11.01"></polyline></svg>
      </div>
      <h3 style="color: #fff; font-size: 19px; margin-bottom: 8px; font-weight: 600; letter-spacing: -0.01em;">Запущено успешно</h3>
      <p style="color: rgba(255,255,255,0.55); font-size: 13px; line-height: 1.5;">Продукт успешно инжектирован. Приятной игры!</p>
    </div>
  </div>
</div>

<script>
let PRODUCTS = [
  { id:'external', icon:'external', name:'EXTERNAL', status:'Undetected', st:'ok', time:'28.11.2026 23:19', daysLeft: 0, bought: false,
    description:'Внешняя модификация для GTA V. Работает полностью вне игрового процесса — без инжекции в память и без вмешательства в файлы игры. Обеспечивает расширенные игровые возможности, оставаясь невидимой для античит-систем.',
    descriptionEn:'External modification for GTA V. Runs entirely outside the game process — no memory injection, no file tampering. Provides advanced gameplay features while remaining undetected by anti-cheat systems.'}
];
let authToken = '';

const ICONS = {
  apex:`<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"/></svg>`,
  external:`<img src="external.png" style="width:36px;height:36px;object-fit:contain;display:block;transform:scale(1.7)">`,
  dev:`<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="16 18 22 12 16 6"/><polyline points="8 6 2 12 8 18"/></svg>`,

  van:`<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24"/><line x1="1" y1="1" x2="23" y2="23"/></svg>`,
};

let selectedId = 'external';
let autoClear = true;

// ===== RENDER SIDEBAR =====
function renderSidebar(){
  const list = document.getElementById('product-list');
  const activeProducts = PRODUCTS.filter(p => p.bought);
  
  if (activeProducts.length > 0 && !activeProducts.find(p => p.id === selectedId)) {
    selectedId = activeProducts[0].id;
  }
  
  list.innerHTML = activeProducts.map(p => `
    <div class="product-item${p.id===selectedId?' active':''}" data-id="${p.id}">
      <div class="product-icon pi-${p.icon}">${ICONS[p.icon]}</div>
      <div class="product-info">
        <div class="product-name">${p.name}</div>
        <div class="product-status st-${p.st}">${p.status}</div>
      </div>
    </div>
  `).join('');
  list.querySelectorAll('.product-item').forEach(el=>{
    el.addEventListener('click',()=>{ selectedId=el.dataset.id; renderSidebar(); renderDetail(); });
  });
}

// ===== RENDER DETAIL =====
function renderDetail(){
  const p = PRODUCTS.find(x=>x.id===selectedId);
  const panel = document.getElementById('detail-panel');
  const now = '05.02.2025 17:53:54';
  panel.innerHTML = `
    <div class="detail-header" data-in>
      <div class="detail-name">${p.name}</div>
      <div class="detail-meta">
        <span class="badge badge-ok">${currentLang==='en'?'Undetected':p.status}</span>
        <span class="badge badge-active">${p.daysLeft} d</span>
      </div>
    </div>
    <div class="changelog" data-in>
      <div class="cl-item">
        <div class="cl-header">
          <span class="cl-ver">About</span>
        </div>
        <p class="product-desc">${currentLang==='en' && p.descriptionEn ? p.descriptionEn : p.description}</p>
      </div>
    </div>
    \u003cdiv class="detail-actions" data-in style="display:flex; flex-direction:column; gap:10px;"\u003e

      <!-- === ФАЗА 0: кнопка LAUNCH === -->
      \u003cdiv id="phase-launch"\u003e
        \u003cbutton class="btn-launch" id="btn-launch"\u003e
          \u003cspan id="launch-text" style="display:flex; align-items:center; gap:9px;"\u003e
            \u003csvg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" width="16" height="16"\u003e\u003cpolygon points="5 3 19 12 5 21 5 3" fill="currentColor" stroke="none"/\u003e\u003c/svg\u003e
            Launch
          \u003c/span\u003e
          \u003cdiv class="btn-dots" id="launch-dots" style="display:none;"\u003e
            \u003cdiv class="btn-dot" style="background:#fff"\u003e\u003c/div\u003e\u003cdiv class="btn-dot" style="background:#fff"\u003e\u003c/div\u003e\u003cdiv class="btn-dot" style="background:#fff"\u003e\u003c/div\u003e
          \u003c/div\u003e
        \u003c/button\u003e
      \u003c/div\u003e

      <!-- === ФАЗА 1: "Запустите игру и нажмите INJECT" === -->
      \u003cdiv id="phase-ready" style="display:none;"\u003e
        \u003cdiv style="background:rgba(74,222,128,.08);border:1px solid rgba(74,222,128,.2);border-radius:12px;padding:14px 16px;margin-bottom:10px;"\u003e
          \u003cdiv style="display:flex;align-items:center;gap:10px;margin-bottom:6px;"\u003e
            \u003cspan style="color:#4ade80;font-size:16px;"\u003e✓\u003c/span\u003e
            \u003cspan style="color:#4ade80;font-size:13.5px;font-weight:600;"\u003eДрайвер загружен\u003c/span\u003e
          \u003c/div\u003e
          \u003cp style="color:rgba(255,255,255,.55);font-size:12.5px;line-height:1.5;padding-left:26px;"\u003e
            Запустите игру, поставьте окно на весь экран и нажмите <strong style="color:rgba(255,255,255,.8)"\u003eINJECT\u003c/strong\u003e
          \u003c/p\u003e
        \u003c/div\u003e
        \u003cdiv style="display:flex;gap:8px;"\u003e
          \u003cbutton class="btn-launch" id="btn-inject" style="flex:1;background:linear-gradient(135deg,#166534,#15803d);"\u003e
            \u003cspan id="inject-text" style="display:flex;align-items:center;gap:9px;"\u003e
              \u003csvg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" width="16" height="16"\u003e\u003cpath d="M12 2L2 7l10 5 10-5-10-5z"/\u003e\u003cpath d="M2 17l10 5 10-5"/\u003e\u003cpath d="M2 12l10 5 10-5"/\u003e\u003c/svg\u003e
              Inject
            \u003c/span\u003e
            \u003cdiv class="btn-dots" id="inject-dots" style="display:none;"\u003e
              \u003cdiv class="btn-dot" style="background:#fff"\u003e\u003c/div\u003e\u003cdiv class="btn-dot" style="background:#fff"\u003e\u003c/div\u003e\u003cdiv class="btn-dot" style="background:#fff"\u003e\u003c/div\u003e
            \u003c/div\u003e
          \u003c/button\u003e
          \u003cbutton class="btn-launch" id="btn-cancel-inject" style="width:44px;flex:0 0 44px;background:rgba(239,68,68,.15);border:1px solid rgba(239,68,68,.25);"\u003e
            \u003csvg viewBox="0 0 24 24" fill="none" stroke="#ef4444" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" width="16" height="16"\u003e\u003cline x1="18" y1="6" x2="6" y2="18"/\u003e\u003cline x1="6" y1="6" x2="18" y2="18"/\u003e\u003c/svg\u003e
          \u003c/button\u003e
        \u003c/div\u003e
      \u003c/div\u003e

    \u003c/div\u003e
  `;

  // animate in
  panel.querySelectorAll('[data-in]').forEach((el,i)=>{
    el.style.opacity='0'; el.style.transform='translateY(8px)';
    requestAnimationFrame(()=>{
      el.style.transition=`opacity .3s ease ${i*.07}s, transform .3s ease ${i*.07}s`;
      el.style.opacity='1'; el.style.transform='translateY(0)';
    });
  });

  // ── LAUNCH (Фаза 1) ──
  document.getElementById('btn-launch')?.addEventListener('click', async () => {
    const btn  = document.getElementById('btn-launch');
    const text = document.getElementById('launch-text');
    const dots = document.getElementById('launch-dots');

    btn.disabled = true; btn.style.opacity = '.7';
    text.style.display = 'none';
    dots.style.display = 'flex';

    const user = document.getElementById('inp-user')?.value || authUsername || '';
    const pass = document.getElementById('inp-pass')?.value || authPassword || '';

    let result = 'error:No bridge';
    if (window.shadow) {
      result = await window.shadow.prepare(selectedId, user, pass);
    } else {
      await new Promise(r => setTimeout(r, 1800));
      result = 'ok';
    }

    btn.disabled = false; btn.style.opacity = '1';
    text.style.display = 'flex';
    dots.style.display = 'none';

    if (result === 'ok' || result === '"ok"') {
      // Переход к фазе INJECT
      document.getElementById('phase-launch').style.display = 'none';
      document.getElementById('phase-ready').style.display  = 'block';
    } else {
      // Показать ошибку
      const errMsg = result.replace(/^"error:|"$/g, '').replace(/^error:/, '');
      showToast(errMsg || 'Error', 'error');
    }
  });

  // ── INJECT (Фаза 2) ──
  document.getElementById('btn-inject')?.addEventListener('click', async () => {
    const btn  = document.getElementById('btn-inject');
    const text = document.getElementById('inject-text');
    const dots = document.getElementById('inject-dots');

    btn.disabled = true; btn.style.opacity = '.7';
    text.style.display = 'none';
    dots.style.display = 'flex';

    let result = 'error:No bridge';
    if (window.shadow) {
      result = await window.shadow.inject();
    } else {
      await new Promise(r => setTimeout(r, 1200));
      result = 'ok';
    }

    btn.disabled = false; btn.style.opacity = '1';
    text.style.display = 'flex';
    dots.style.display = 'none';

    if (result === 'ok' || result === '"ok"') {
      // Успешный инжект — просто показываем success modal
      // Unload доступен через меню самого чита
      document.getElementById('phase-ready').style.display = 'none';
      document.getElementById('modal-success').classList.replace('hidden', 'visible');
    } else {
      const errMsg = result.replace(/^"error:|"$/g, '').replace(/^error:/, '');
      showToast(errMsg || 'Inject error', 'error');
    }
  });

  // ── CANCEL INJECT → вернуться к Launch ──
  document.getElementById('btn-cancel-inject')?.addEventListener('click', async () => {
    if (window.shadow) await window.shadow.unload();
    document.getElementById('phase-ready').style.display  = 'none';
    document.getElementById('phase-launch').style.display = 'block';
  });

  // Unload вызывается только при закрытии окна (cpp_close)
  // Кнопки Unload в UI нет — управление идёт через меню чита
}

function setLang(l){
  document.querySelectorAll('.lang-btn').forEach(b=>b.classList.toggle('active', b.textContent===l.toUpperCase()));
}

// ===== LOGIN -> DASH =====
document.getElementById('login-form').addEventListener('submit', async e=>{
  e.preventDefault();
  const btn = document.getElementById('btn-login');
  btn.classList.add('loading'); btn.disabled=true;
  
  // Получаем HWID через IPC
  let hwid = "UNKNOWN";
  if(window.revolt && window.revolt.getHwid) {
    hwid = await window.revolt.getHwid();
  }
  console.log("Login attempt with HWID:", hwid);

  const u = document.getElementById('inp-user').value || 'User';
  const p = document.getElementById('inp-pass').value || '';
  
  try {
    const res = await fetch('https://shadowcheats.cc/api/auth/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username: u, password: p })
    });
    const data = await res.json();
    
    if(data.token) {
      authToken = data.token;
      
      // Fetch subscriptions
      const subRes = await fetch('https://shadowcheats.cc/api/user/subscriptions', {
        headers: { 'Authorization': 'Bearer ' + authToken }
      });
      const subData = await subRes.json();
      
      if (!subData.subscriptions || subData.subscriptions.length === 0) {
        alert("Для использования лоадера купите хотя бы 1 продукт.");
        btn.classList.remove('loading'); btn.disabled=false;
        return;
      }
      
      PRODUCTS.forEach(prod => {
        const found = subData.subscriptions.find(s => s.id === prod.id);
        if(found) {
          prod.bought = true;
          prod.daysLeft = found.daysLeft;
        } else {
          prod.bought = false;
        }
      });
      
      // set username (using typed login directly as requested, removing #5)
      document.getElementById('user-name-label').textContent = u;
      document.getElementById('user-avatar').textContent = u[0].toUpperCase();
      
      // switch screens
      document.getElementById('screen-login').classList.replace('visible','hidden');
      document.getElementById('gear-btn').style.display='grid';
      renderSidebar(); renderDetail();
      document.getElementById('screen-dash').classList.replace('hidden','visible');
    } else {
      alert("Ошибка авторизации");
    }
  } catch (err) {
    console.error(err);
    alert("Сервер недоступен. Запустите бэкенд.");
  }

  btn.classList.remove('loading'); btn.disabled=false;
});

// ===== EYE TOGGLE =====
document.getElementById('eye-toggle')?.addEventListener('click',()=>{
  const i = document.getElementById('inp-pass');
  i.type = i.type==='password'?'text':'password';
});

// ===== CLOSE =====
document.getElementById('close-dot').addEventListener('click',()=>window.revolt?.close());

// ===== LINKS =====
document.querySelector('.hero-badge')?.addEventListener('click', () => {
  if(window.revolt && window.revolt.openLink) window.revolt.openLink('https://shadowcheats.cc');
});
document.querySelector('.forgot')?.addEventListener('click', () => {
  if(window.revolt && window.revolt.openLink) window.revolt.openLink('https://shadowcheats.cc/reset-password');
});

// ===== WINDOW CONTROLS =====
if(window.revolt){
  document.addEventListener('keydown',e=>{ if(e.key==='Escape') window.revolt.close(); });
}

// ===== GEAR MENU =====
const gearBtn  = document.getElementById('gear-btn');
const gearMenu = document.getElementById('gear-menu');
const gmThemeToggle = document.getElementById('gm-theme-toggle');

// open/close
gearBtn.addEventListener('click', e => {
  e.stopPropagation();
  gearMenu.classList.toggle('open');
});
document.addEventListener('click', () => gearMenu.classList.remove('open'));
gearMenu.addEventListener('click', e => e.stopPropagation());

// dark/light theme toggle
let isDark = false; // default = light
function applyTheme(){
  if(isDark){
    document.body.classList.remove('light-theme');
    gmThemeToggle.classList.add('on');
  } else {
    document.body.classList.add('light-theme');
    gmThemeToggle.classList.remove('on');
  }
}
applyTheme();
document.getElementById('gm-theme').addEventListener('click', () => {
  isDark = !isDark;
  applyTheme();
});

// clear
document.getElementById('gm-clear').addEventListener('click', () => {
  autoClear = !autoClear;
  const lbl = document.querySelector('#gm-clear .gm-label');
  if(lbl) lbl.style.opacity = autoClear ? '1' : '.5';
  gearMenu.classList.remove('open');
});

// language
const LANG = {
  ru: {
    selectProduct: 'Выберите продукт',
    undetected: 'Undetected',
    active: 'Active',
    about: 'About',
    launch: 'Launch',
    clear: 'Очистить',
    login: 'Вход в аккаунт',
    loginSub: 'Авторизуйтесь, чтобы получить доступ к продуктам.',
    loginBtn: 'Войти',
    darkTheme: 'Тёмная тема',
    langLabel: 'Язык',
  },
  en: {
    selectProduct: 'Select product',
    undetected: 'Undetected',
    active: 'Active',
    about: 'About',
    launch: 'Launch',
    clear: 'Clear',
    login: 'Sign in',
    loginSub: 'Enter your credentials to access products.',
    loginBtn: 'Sign in',
    darkTheme: 'Dark theme',
    langLabel: 'Language',
  }
};
let currentLang = 'ru';
function applyLang(lang){
  currentLang = lang;
  const L = LANG[lang];
  document.getElementById('lang-ru').classList.toggle('active', lang==='ru');
  document.getElementById('lang-en').classList.toggle('active', lang==='en');
  // update all data-ru/data-en labels
  document.querySelectorAll('[data-ru]').forEach(el => {
    el.textContent = el.getAttribute('data-' + lang);
  });
  // static elements
  const sideTitle = document.querySelector('.sidebar-title');
  if(sideTitle) sideTitle.textContent = L.selectProduct;
  const formTitle = document.querySelector('.form-title');
  if(formTitle) formTitle.textContent = L.login;
  const formSub = document.querySelector('.form-sub');
  if(formSub) formSub.textContent = L.loginSub;
  const loginBtn = document.querySelector('#btn-login .btn-label');
  if(loginBtn) loginBtn.textContent = L.loginBtn;
  const launchBtn = document.querySelector('#btn-launch');
  if(launchBtn){
    const txt = launchBtn.childNodes[launchBtn.childNodes.length-1];
    if(txt && txt.nodeType===3) txt.textContent = ' ' + L.launch;
  }
  // re-render sidebar & detail to update product statuses
  if(typeof renderSidebar === 'function') renderSidebar();
  if(typeof renderDetail === 'function') renderDetail();
}
document.getElementById('lang-ru').addEventListener('click', () => applyLang('ru'));
document.getElementById('lang-en').addEventListener('click', () => applyLang('en'));

// logout
document.getElementById('gm-logout').addEventListener('click', () => {
  gearMenu.classList.remove('open');
  document.getElementById('gear-btn').style.display = 'none';
  document.getElementById('screen-dash').classList.replace('visible','hidden');
  document.getElementById('screen-login').classList.replace('hidden','visible');
  document.getElementById('inp-user').value = '';
  document.getElementById('inp-pass').value = '';
});

</script>
</body>
</html>

)__HTML__";
