# -*- coding: utf-8 -*-
import os

OUT_DIR = r"E:\workplace\esp32\MyProject\SKPass\docs\icons\sk_set"
os.makedirs(OUT_DIR, exist_ok=True)

# 1. SK-1: 双色折叠莫比乌斯流线 (Möbius Ribbon Flow)
svg_sk1 = '''<svg width="512" height="512" viewBox="0 0 512 512" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <radialGradient id="bg1" cx="50%" cy="30%" r="75%">
      <stop offset="0%" stop-color="#1e293b"/>
      <stop offset="100%" stop-color="#090d16"/>
    </radialGradient>
    <linearGradient id="ribbon1" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#38bdf8"/>
      <stop offset="60%" stop-color="#2563eb"/>
      <stop offset="100%" stop-color="#1d4ed8"/>
    </linearGradient>
    <linearGradient id="ribbon2" x1="0%" y1="100%" x2="100%" y2="0%">
      <stop offset="0%" stop-color="#60a5fa"/>
      <stop offset="100%" stop-color="#a855f7"/>
    </linearGradient>
    <filter id="glow1" x="-20%" y="-20%" width="140%" height="140%">
      <feDropShadow dx="0" dy="12" stdDeviation="16" flood-color="#2563eb" flood-opacity="0.55"/>
    </filter>
  </defs>

  <rect x="24" y="24" width="464" height="464" rx="108" fill="url(#bg1)"/>
  <rect x="24" y="24" width="464" height="464" rx="108" fill="none" stroke="#38bdf8" stroke-opacity="0.25" stroke-width="2"/>

  <!-- SK Monogram Ribbon -->
  <g transform="translate(100, 105)" filter="url(#glow1)">
    <!-- 'S' Continuous Flow -->
    <path d="M 115 50
             C 45 50, 15 95, 15 145
             C 15 205, 110 205, 110 255
             C 110 285, 75 295, 35 280"
          fill="none" stroke="url(#ribbon1)" stroke-width="32" stroke-linecap="round"/>

    <!-- 'K' Stem shared/adjacent -->
    <path d="M 160 50 L 160 290" fill="none" stroke="#ffffff" stroke-width="32" stroke-linecap="round"/>

    <!-- 'K' Top Wing & NFC integration -->
    <path d="M 185 170 L 275 60" fill="none" stroke="url(#ribbon2)" stroke-width="32" stroke-linecap="round"/>
    <!-- 'K' Bottom Wing -->
    <path d="M 185 175 L 280 290" fill="none" stroke="url(#ribbon1)" stroke-width="32" stroke-linecap="round"/>

    <!-- Dynamic Wireless NFC Arcs emanating from K's upper wing -->
    <g transform="translate(245, 80)">
      <path d="M 30 -15 A 32 32 0 0 1 55 15" fill="none" stroke="#38bdf8" stroke-width="9" stroke-linecap="round"/>
      <path d="M 52 -35 A 60 60 0 0 1 95 15" fill="none" stroke="#38bdf8" stroke-width="9" stroke-linecap="round" stroke-opacity="0.6"/>
    </g>
  </g>

  <text x="256" y="445" font-family="Segoe UI, sans-serif" font-weight="800" font-size="22" fill="#ffffff" letter-spacing="8" text-anchor="middle">SKPASS</text>
</svg>'''

# 2. SK-2: 几何咬合卡片与负空间 (Geometric Interlocking Card)
svg_sk2 = '''<svg width="512" height="512" viewBox="0 0 512 512" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <linearGradient id="bg2" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#0f172a"/>
      <stop offset="50%" stop-color="#1e1b4b"/>
      <stop offset="100%" stop-color="#090d16"/>
    </linearGradient>
    <linearGradient id="blueCore" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#06b6d4"/>
      <stop offset="50%" stop-color="#3b82f6"/>
      <stop offset="100%" stop-color="#1d4ed8"/>
    </linearGradient>
    <filter id="glow2" x="-20%" y="-20%" width="140%" height="140%">
      <feDropShadow dx="0" dy="10" stdDeviation="15" flood-color="#0ea5e9" flood-opacity="0.5"/>
    </filter>
  </defs>

  <rect x="24" y="24" width="464" height="464" rx="108" fill="url(#bg2)"/>
  <rect x="24" y="24" width="464" height="464" rx="108" fill="none" stroke="#6366f1" stroke-opacity="0.3" stroke-width="2"/>

  <!-- Interlocking SK Shape inside an invisible smartcard proportion -->
  <g transform="translate(106, 110)" filter="url(#glow2)">
    <!-- 'S' as two interlocking rounded bracket hooks -->
    <path d="M 120 50 L 60 50 A 40 40 0 0 0 20 90 L 20 120 A 40 40 0 0 0 60 160 L 90 160 A 40 40 0 0 1 130 200 L 130 230 A 40 40 0 0 1 90 270 L 20 270"
          fill="none" stroke="url(#blueCore)" stroke-width="30" stroke-linecap="round" stroke-linejoin="round"/>

    <!-- 'K' formed by a vertical column + two angled pass blades -->
    <line x1="175" y1="50" x2="175" y2="270" stroke="#ffffff" stroke-width="30" stroke-linecap="round"/>
    
    <!-- K Upper Blade -->
    <path d="M 195 150 L 275 60" stroke="url(#blueCore)" stroke-width="30" stroke-linecap="round"/>
    <!-- K Lower Blade -->
    <path d="M 195 170 L 275 270" stroke="url(#blueCore)" stroke-width="30" stroke-linecap="round"/>

    <!-- Central Microchip Gold Accent Node -->
    <rect x="160" y="145" width="30" height="30" rx="6" fill="#f59e0b" stroke="#ffffff" stroke-width="3"/>
  </g>

  <text x="256" y="442" font-family="Segoe UI, sans-serif" font-weight="700" font-size="20" fill="#93c5fd" letter-spacing="6" text-anchor="middle">SK · PASS</text>
</svg>'''

# 3. SK-3: 纯几何极简圆环与射频切片 (Pure Circle & Radar Arc)
svg_sk3 = '''<svg width="512" height="512" viewBox="0 0 512 512" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <radialGradient id="bg3" cx="50%" cy="50%" r="70%">
      <stop offset="0%" stop-color="#172554"/>
      <stop offset="70%" stop-color="#0f172a"/>
      <stop offset="100%" stop-color="#020617"/>
    </radialGradient>
    <linearGradient id="cyanArc" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#38bdf8"/>
      <stop offset="100%" stop-color="#2563eb"/>
    </linearGradient>
    <filter id="glow3" x="-20%" y="-20%" width="140%" height="140%">
      <feDropShadow dx="0" dy="8" stdDeviation="14" flood-color="#38bdf8" flood-opacity="0.6"/>
    </filter>
  </defs>

  <rect x="24" y="24" width="464" height="464" rx="108" fill="url(#bg3)"/>
  <rect x="24" y="24" width="464" height="464" rx="108" fill="none" stroke="#38bdf8" stroke-opacity="0.3" stroke-width="2"/>

  <!-- Circular Bauhaus Geometric S & K -->
  <g transform="translate(116, 116)" filter="url(#glow3)">
    <!-- Top arc of S -->
    <path d="M 120 40 A 55 55 0 0 0 20 85 A 55 55 0 0 0 90 140"
          fill="none" stroke="url(#cyanArc)" stroke-width="28" stroke-linecap="round"/>
    <!-- Bottom arc of S -->
    <path d="M 90 140 A 55 55 0 0 1 160 195 A 55 55 0 0 1 60 240"
          fill="none" stroke="url(#cyanArc)" stroke-width="28" stroke-linecap="round"/>

    <!-- K's Clean Linear Architecture -->
    <line x1="160" y1="40" x2="160" y2="240" stroke="#ffffff" stroke-width="28" stroke-linecap="round"/>

    <!-- K Radiating Radar Arcs (Top Wing = 1st NFC arc, Bottom Wing = geometric cut) -->
    <path d="M 175 130 A 70 70 0 0 1 255 55" fill="none" stroke="#38bdf8" stroke-width="28" stroke-linecap="round"/>
    <path d="M 175 150 L 260 235" fill="none" stroke="url(#cyanArc)" stroke-width="28" stroke-linecap="round"/>

    <!-- Concentric 2nd & 3rd Radar Wave Arcs -->
    <path d="M 210 110 A 110 110 0 0 1 275 40" fill="none" stroke="#38bdf8" stroke-width="9" stroke-linecap="round" stroke-opacity="0.8"/>
  </g>

  <text x="256" y="438" font-family="Segoe UI, sans-serif" font-weight="700" font-size="22" fill="#ffffff" letter-spacing="6" text-anchor="middle">SKPASS</text>
</svg>'''

# 4. SK-4: 街机动感斜切折角 (Cyber Dynamic Blade / Arcade Precision)
svg_sk4 = '''<svg width="512" height="512" viewBox="0 0 512 512" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <linearGradient id="bg4" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#020617"/>
      <stop offset="50%" stop-color="#0f172a"/>
      <stop offset="100%" stop-color="#1e1e38"/>
    </linearGradient>
    <linearGradient id="bladeGrad" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#00f2fe"/>
      <stop offset="50%" stop-color="#4facfe"/>
      <stop offset="100%" stop-color="#6366f1"/>
    </linearGradient>
    <filter id="glow4" x="-20%" y="-20%" width="140%" height="140%">
      <feDropShadow dx="0" dy="10" stdDeviation="15" flood-color="#4facfe" flood-opacity="0.6"/>
    </filter>
  </defs>

  <rect x="24" y="24" width="464" height="464" rx="108" fill="url(#bg4)"/>
  <rect x="24" y="24" width="464" height="464" rx="108" fill="none" stroke="#4facfe" stroke-opacity="0.3" stroke-width="2"/>

  <!-- High-speed Angled Chamfered Monogram -->
  <g transform="translate(100, 110)" filter="url(#glow4)">
    <!-- 'S' Dynamic 45-degree Angled Blocks -->
    <path d="M 120 40 L 40 40 L 20 80 L 100 80 L 120 120 L 40 160 L 20 200 L 100 240 L 120 240 L 140 200 L 60 200 L 40 160 L 120 120 Z"
          fill="url(#bladeGrad)" fill-rule="evenodd" opacity="0.95"/>

    <!-- 'K' Forward Slashing Blades -->
    <polygon points="160,40 185,40 185,240 160,240" fill="#ffffff"/>
    <polygon points="185,130 250,55 280,55 205,140" fill="url(#bladeGrad)"/>
    <polygon points="205,140 280,225 250,240 185,155" fill="url(#bladeGrad)"/>

    <!-- Fast NFC Radiating Signal Arcs from K's top -->
    <g transform="translate(265, 55)">
      <circle cx="0" cy="0" r="4" fill="#00f2fe"/>
      <path d="M 14 -12 A 20 20 0 0 1 22 12" fill="none" stroke="#00f2fe" stroke-width="4.5" stroke-linecap="round"/>
      <path d="M 28 -24 A 38 38 0 0 1 40 22" fill="none" stroke="#00f2fe" stroke-width="4.5" stroke-linecap="round" stroke-opacity="0.6"/>
    </g>
  </g>

  <text x="256" y="440" font-family="Consolas, sans-serif" font-weight="900" font-size="22" fill="#ffffff" letter-spacing="6" text-anchor="middle">SK · PASS</text>
</svg>'''

# 5. SK-5: 闭合芯片轮廓与负空间穿透 (Negative Space Chip & Pass)
svg_sk5 = '''<svg width="512" height="512" viewBox="0 0 512 512" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <radialGradient id="bg5" cx="50%" cy="40%" r="75%">
      <stop offset="0%" stop-color="#1e3a8a"/>
      <stop offset="60%" stop-color="#0f172a"/>
      <stop offset="100%" stop-color="#020617"/>
    </radialGradient>
    <linearGradient id="tileGrad" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#2563eb"/>
      <stop offset="100%" stop-color="#1d4ed8"/>
    </linearGradient>
    <filter id="glow5" x="-20%" y="-20%" width="140%" height="140%">
      <feDropShadow dx="0" dy="12" stdDeviation="16" flood-color="#2563eb" flood-opacity="0.7"/>
    </filter>
  </defs>

  <rect x="24" y="24" width="464" height="464" rx="108" fill="url(#bg5)"/>
  <rect x="24" y="24" width="464" height="464" rx="108" fill="none" stroke="#3b82f6" stroke-opacity="0.3" stroke-width="2"/>

  <!-- Rounded App Card Container with Negative Space SK -->
  <g transform="translate(116, 100)" filter="url(#glow5)">
    <!-- Base Solid Card Tile -->
    <rect x="0" y="0" width="280" height="280" rx="48" fill="url(#tileGrad)" stroke="#60a5fa" stroke-width="3"/>

    <!-- Glossy Highlight Diagonal -->
    <path d="M 0 0 L 160 0 L 0 160 Z" fill="#ffffff" fill-opacity="0.12"/>

    <!-- Negative Space / Pure White 'S' & 'K' Stencil -->
    <g fill="none" stroke="#ffffff" stroke-width="24" stroke-linecap="round" stroke-linejoin="round">
      <!-- 'S' Stencil -->
      <path d="M 105 75 
               L 65 75 
               A 25 25 0 0 0 40 100 
               L 40 120 
               A 25 25 0 0 0 65 145 
               L 85 145 
               A 25 25 0 0 1 110 170 
               L 110 190 
               A 25 25 0 0 1 85 215 
               L 45 215"/>

      <!-- 'K' Stencil -->
      <line x1="145" y1="75" x2="145" y2="215"/>
      <path d="M 155 145 L 210 75"/>
      <path d="M 155 145 L 210 215"/>
    </g>

    <!-- Glowing NFC Arcs cut into the top right corner of the tile -->
    <g transform="translate(215, 60)">
      <path d="M 12 0 A 18 18 0 0 1 12 36" fill="none" stroke="#fef08a" stroke-width="6" stroke-linecap="round"/>
      <path d="M 26 -10 A 34 34 0 0 1 26 46" fill="none" stroke="#fef08a" stroke-width="6" stroke-linecap="round" stroke-opacity="0.75"/>
    </g>
  </g>

  <text x="256" y="445" font-family="Segoe UI, sans-serif" font-weight="800" font-size="20" fill="#ffffff" letter-spacing="6" text-anchor="middle">SKPASS</text>
</svg>'''

# 6. SK-6: 极光柔光渐变折带 (Aurora Gradient Loop)
svg_sk6 = '''<svg width="512" height="512" viewBox="0 0 512 512" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <linearGradient id="bg6" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#0f172a"/>
      <stop offset="40%" stop-color="#1e1b4b"/>
      <stop offset="100%" stop-color="#090d16"/>
    </linearGradient>
    <linearGradient id="aurora1" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#38bdf8"/>
      <stop offset="35%" stop-color="#818cf8"/>
      <stop offset="70%" stop-color="#c084fc"/>
      <stop offset="100%" stop-color="#f472b6"/>
    </linearGradient>
    <linearGradient id="aurora2" x1="0%" y1="100%" x2="100%" y2="0%">
      <stop offset="0%" stop-color="#06b6d4"/>
      <stop offset="100%" stop-color="#3b82f6"/>
    </linearGradient>
    <filter id="glow6" x="-20%" y="-20%" width="140%" height="140%">
      <feDropShadow dx="0" dy="12" stdDeviation="18" flood-color="#818cf8" flood-opacity="0.5"/>
    </filter>
  </defs>

  <rect x="24" y="24" width="464" height="464" rx="108" fill="url(#bg6)"/>
  <rect x="24" y="24" width="464" height="464" rx="108" fill="none" stroke="#818cf8" stroke-opacity="0.3" stroke-width="2"/>

  <!-- Fluid Aurora Loop 'SK' -->
  <g transform="translate(100, 110)" filter="url(#glow6)">
    <!-- Fluid 'S' Ribbon -->
    <path d="M 125 50
             C 45 45, 20 85, 20 135
             C 20 195, 115 190, 115 245
             C 115 285, 75 290, 35 275"
          fill="none" stroke="url(#aurora1)" stroke-width="32" stroke-linecap="round"/>

    <!-- 'K' Vertical Axis -->
    <line x1="160" y1="50" x2="160" y2="280" stroke="#ffffff" stroke-width="32" stroke-linecap="round"/>

    <!-- 'K' Floating Upper Wing & Connected Signal -->
    <path d="M 175 160 L 265 65" fill="none" stroke="url(#aurora1)" stroke-width="30" stroke-linecap="round"/>
    <!-- 'K' Lower Grounding Wing -->
    <path d="M 175 170 L 270 280" fill="none" stroke="url(#aurora2)" stroke-width="30" stroke-linecap="round"/>

    <!-- Stylized Wireless Contactless Arcs on Upper Corner -->
    <g transform="translate(235, 70)">
      <path d="M 28 -12 A 28 28 0 0 1 50 18" fill="none" stroke="#38bdf8" stroke-width="9" stroke-linecap="round"/>
      <path d="M 48 -32 A 56 56 0 0 1 88 18" fill="none" stroke="#f472b6" stroke-width="9" stroke-linecap="round" stroke-opacity="0.75"/>
    </g>
  </g>

  <text x="256" y="445" font-family="Segoe UI, sans-serif" font-weight="700" font-size="22" fill="#ffffff" letter-spacing="8" text-anchor="middle">SKPASS</text>
</svg>'''

monograms = [
    ("sk1_mobius_flow", "SK-1: 双色莫比乌斯流线 (柔顺 S 环抱 + 刚劲 K 射频)", svg_sk1),
    ("sk2_interlock_card", "SK-2: 几何咬合卡片与金色微芯片 (微拟物结构感)", svg_sk2),
    ("sk3_circle_radar", "SK-3: 包豪斯极简圆环与雷达切片 (极客圆弧比例)", svg_sk3),
    ("sk4_cyber_blade", "SK-4: 街机极客斜切折角 (45° 疾速斜切块面)", svg_sk4),
    ("sk5_stencil_tile", "SK-5: 经典实体卡片负空间印章 (小尺寸最清晰极简)", svg_sk5),
    ("sk6_aurora_ribbon", "SK-6: 极光柔光渐变折带 (Apple/Fluent 柔光高级感)", svg_sk6),
]

for name, title, svg in monograms:
    svg_path = os.path.join(OUT_DIR, f"{name}.svg")
    html_path = os.path.join(OUT_DIR, f"{name}.html")
    with open(svg_path, "w", encoding="utf-8") as f:
        f.write(svg)
    html_content = f'''<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <style>
    * {{ margin: 0; padding: 0; box-sizing: border-box; }}
    body {{ background: transparent; display: flex; align-items: center; justify-content: center; width: 512px; height: 512px; }}
  </style>
</head>
<body>
{svg}
</body>
</html>'''
    with open(html_path, "w", encoding="utf-8") as f:
        f.write(html_content)

print("Saved all SK Monograms.")
