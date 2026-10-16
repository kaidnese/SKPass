# -*- coding: utf-8 -*-
import os
import subprocess

OUT_DIR = r"E:\workplace\esp32\MyProject\SKPass\docs\icons\vector_set"
os.makedirs(OUT_DIR, exist_ok=True)

# 1. 方案 A: Fluent 智能卡 (Fluent Smart Pass)
svg_a = '''<svg width="512" height="512" viewBox="0 0 512 512" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <radialGradient id="bgGlow" cx="50%" cy="30%" r="70%">
      <stop offset="0%" stop-color="#1e3a8a"/>
      <stop offset="100%" stop-color="#0a0f1d"/>
    </radialGradient>
    <linearGradient id="cardGrad" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#3b82f6"/>
      <stop offset="50%" stop-color="#1d4ed8"/>
      <stop offset="100%" stop-color="#172554"/>
    </linearGradient>
    <linearGradient id="goldChip" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#fef08a"/>
      <stop offset="50%" stop-color="#eab308"/>
      <stop offset="100%" stop-color="#ca8a04"/>
    </linearGradient>
    <filter id="cardShadow" x="-20%" y="-20%" width="150%" height="150%">
      <feDropShadow dx="0" dy="16" stdDeviation="20" flood-color="#000000" flood-opacity="0.6"/>
      <feDropShadow dx="0" dy="4" stdDeviation="6" flood-color="#2563eb" flood-opacity="0.3"/>
    </filter>
    <filter id="neonGlow" x="-30%" y="-30%" width="160%" height="160%">
      <feDropShadow dx="0" dy="0" stdDeviation="6" flood-color="#38bdf8" flood-opacity="0.8"/>
    </filter>
  </defs>

  <!-- Base Squircle -->
  <rect x="24" y="24" width="464" height="464" rx="104" fill="url(#bgGlow)"/>
  <rect x="24" y="24" width="464" height="464" rx="104" fill="none" stroke="#3b82f6" stroke-opacity="0.3" stroke-width="2"/>

  <!-- Subtle Ambient Circuit Lines in background -->
  <path d="M 60 180 L 140 180 L 180 220" fill="none" stroke="#1e40af" stroke-opacity="0.25" stroke-width="2"/>
  <circle cx="180" cy="220" r="3" fill="#38bdf8" fill-opacity="0.4"/>
  <path d="M 450 340 L 370 340 L 330 300" fill="none" stroke="#1e40af" stroke-opacity="0.25" stroke-width="2"/>
  <circle cx="330" cy="300" r="3" fill="#38bdf8" fill-opacity="0.4"/>

  <!-- Main Smart Card (Floating with angle) -->
  <g transform="translate(256,260) rotate(-7) translate(-175,-110)" filter="url(#cardShadow)">
    <!-- Card Body -->
    <rect x="0" y="0" width="350" height="220" rx="18" fill="url(#cardGrad)"/>
    <rect x="0" y="0" width="350" height="220" rx="18" fill="none" stroke="#93c5fd" stroke-opacity="0.4" stroke-width="2"/>
    
    <!-- Glossy Highlight across card -->
    <path d="M 0 0 L 190 0 L 100 220 L 0 220 Z" fill="#ffffff" fill-opacity="0.08"/>

    <!-- Gold Chip on Card -->
    <g transform="translate(42, 75)">
      <rect x="0" y="0" width="64" height="50" rx="8" fill="url(#goldChip)" stroke="#a16207" stroke-width="1.5"/>
      <!-- Chip Contact Lines -->
      <line x1="0" y1="25" x2="64" y2="25" stroke="#854d0e" stroke-width="1.5"/>
      <line x1="22" y1="0" x2="22" y2="50" stroke="#854d0e" stroke-width="1.5"/>
      <line x1="42" y1="0" x2="42" y2="50" stroke="#854d0e" stroke-width="1.5"/>
      <rect x="22" y="16" width="20" height="18" rx="4" fill="#ca8a04" stroke="#854d0e" stroke-width="1"/>
    </g>

    <!-- Card Brand & Details -->
    <text x="44" y="185" font-family="Segoe UI, sans-serif" font-weight="700" font-size="20" fill="#ffffff" letter-spacing="2">SK PASS</text>
    <text x="44" y="202" font-family="Segoe UI, sans-serif" font-weight="500" font-size="10" fill="#93c5fd" opacity="0.8">NFC / SMART CARD / EMULATION</text>

    <!-- Glowing Contactless Radio Waves on top right of card -->
    <g transform="translate(270, 70)" filter="url(#neonGlow)">
      <path d="M 0 0 A 24 24 0 0 1 0 40" fill="none" stroke="#ffffff" stroke-width="4.5" stroke-linecap="round"/>
      <path d="M 12 -12 A 40 40 0 0 1 12 52" fill="none" stroke="#38bdf8" stroke-width="4.5" stroke-linecap="round"/>
      <path d="M 24 -24 A 56 56 0 0 1 24 64" fill="none" stroke="#38bdf8" stroke-width="4" stroke-linecap="round" stroke-opacity="0.75"/>
    </g>
  </g>
</svg>'''

# 2. 方案 B: 街机硬核通行卡 (Arcade Cyber Pass - Aime/BanaPassport 风格)
svg_b = '''<svg width="512" height="512" viewBox="0 0 512 512" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <linearGradient id="arcadeBg" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#0f172a"/>
      <stop offset="50%" stop-color="#1e1e38"/>
      <stop offset="100%" stop-color="#090d16"/>
    </linearGradient>
    <linearGradient id="aimeStripe" x1="0%" y1="0%" x2="100%" y2="0%">
      <stop offset="0%" stop-color="#f59e0b"/>
      <stop offset="35%" stop-color="#06b6d4"/>
      <stop offset="100%" stop-color="#3b82f6"/>
    </linearGradient>
    <linearGradient id="cardMetallic" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#1e293b"/>
      <stop offset="100%" stop-color="#0f172a"/>
    </linearGradient>
    <filter id="arcadeGlow" x="-20%" y="-20%" width="140%" height="140%">
      <feDropShadow dx="0" dy="0" stdDeviation="10" flood-color="#06b6d4" flood-opacity="0.6"/>
    </filter>
  </defs>

  <rect x="24" y="24" width="464" height="464" rx="104" fill="url(#arcadeBg)"/>
  <rect x="24" y="24" width="464" height="464" rx="104" fill="none" stroke="#38bdf8" stroke-opacity="0.25" stroke-width="2"/>

  <!-- Arcade Machine / Reader Frame -->
  <g transform="translate(66, 126)">
    <!-- Base Card Frame -->
    <rect x="0" y="0" width="380" height="260" rx="20" fill="url(#cardMetallic)" stroke="#334155" stroke-width="3"/>
    <rect x="6" y="6" width="368" height="248" rx="16" fill="none" stroke="#0ea5e9" stroke-opacity="0.4" stroke-width="1.5"/>

    <!-- High-speed Arcade Stripe -->
    <path d="M 0 35 L 380 35 L 380 50 L 0 50 Z" fill="url(#aimeStripe)"/>

    <!-- NFC Target Crosshair / Scanner Emblem -->
    <g transform="translate(190, 140)">
      <circle cx="0" cy="0" r="56" fill="none" stroke="#06b6d4" stroke-opacity="0.2" stroke-width="2"/>
      <circle cx="0" cy="0" r="42" fill="none" stroke="#06b6d4" stroke-opacity="0.5" stroke-width="2" stroke-dasharray="6,4"/>
      <circle cx="0" cy="0" r="28" fill="#0ea5e9" fill-opacity="0.1" stroke="#38bdf8" stroke-width="3"/>
      
      <!-- Radio Wave Arcs inside target -->
      <path d="M -10 -12 A 16 16 0 0 1 10 -12" fill="none" stroke="#ffffff" stroke-width="3" stroke-linecap="round"/>
      <path d="M -16 -18 A 24 24 0 0 1 16 -18" fill="none" stroke="#38bdf8" stroke-width="3" stroke-linecap="round"/>
      <circle cx="0" cy="4" r="5" fill="#38bdf8" filter="url(#arcadeGlow)"/>
    </g>

    <!-- Arcade Pass Title -->
    <text x="36" y="98" font-family="Arial Black, Impact, sans-serif" font-size="28" fill="#ffffff" letter-spacing="3">SK·PASS</text>
    <rect x="36" y="106" width="60" height="4" fill="#f59e0b" rx="2"/>
    <text x="36" y="128" font-family="Consolas, monospace" font-size="12" fill="#94a3b8" letter-spacing="1">ARCADE &amp; NFC SYSTEM</text>

    <!-- Bottom Data Barcode representation -->
    <g transform="translate(36, 218)">
      <rect x="0" y="0" width="4" height="20" fill="#64748b"/>
      <rect x="7" y="0" width="8" height="20" fill="#64748b"/>
      <rect x="18" y="0" width="3" height="20" fill="#64748b"/>
      <rect x="24" y="0" width="10" height="20" fill="#64748b"/>
      <rect x="38" y="0" width="5" height="20" fill="#64748b"/>
      <rect x="46" y="0" width="2" height="20" fill="#64748b"/>
      <rect x="52" y="0" width="7" height="20" fill="#64748b"/>
      <text x="70" y="16" font-family="Consolas, monospace" font-size="13" fill="#38bdf8" letter-spacing="3">0000-AIME-8888</text>
    </g>

    <text x="290" y="234" font-family="Segoe UI, sans-serif" font-weight="700" font-size="12" fill="#f59e0b">TOUCH</text>
  </g>
</svg>'''

# 3. 方案 C: 硬件射频天线线圈 + 晶圆微核心 (PCB Antenna Coil & MCU)
svg_c = '''<svg width="512" height="512" viewBox="0 0 512 512" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <radialGradient id="pcbBg" cx="50%" cy="50%" r="70%">
      <stop offset="0%" stop-color="#0f2b38"/>
      <stop offset="70%" stop-color="#081820"/>
      <stop offset="100%" stop-color="#030a0d"/>
    </radialGradient>
    <linearGradient id="goldTrace" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#38bdf8"/>
      <stop offset="50%" stop-color="#0284c7"/>
      <stop offset="100%" stop-color="#0369a1"/>
    </linearGradient>
    <linearGradient id="chipMatte" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#27272a"/>
      <stop offset="100%" stop-color="#18181b"/>
    </linearGradient>
    <filter id="coreGlow" x="-30%" y="-30%" width="160%" height="160%">
      <feDropShadow dx="0" dy="0" stdDeviation="12" flood-color="#0ea5e9" flood-opacity="0.9"/>
    </filter>
  </defs>

  <rect x="24" y="24" width="464" height="464" rx="104" fill="url(#pcbBg)"/>
  <rect x="24" y="24" width="464" height="464" rx="104" fill="none" stroke="#0ea5e9" stroke-opacity="0.3" stroke-width="2"/>

  <!-- Concentric NFC Antenna Coils (Concentric rounded squares) -->
  <g fill="none" stroke="url(#goldTrace)" stroke-linecap="round">
    <rect x="80" y="80" width="352" height="352" rx="48" stroke-width="4.5" opacity="0.95"/>
    <rect x="100" y="100" width="312" height="312" rx="40" stroke-width="4" opacity="0.8"/>
    <rect x="120" y="120" width="272" height="272" rx="32" stroke-width="3.5" opacity="0.65"/>
    <rect x="140" y="140" width="232" height="232" rx="24" stroke-width="3" opacity="0.5"/>
  </g>

  <!-- Corner Via test pads -->
  <g fill="#38bdf8" opacity="0.8">
    <circle cx="80" cy="120" r="4.5"/>
    <circle cx="432" cy="120" r="4.5"/>
    <circle cx="80" cy="392" r="4.5"/>
    <circle cx="432" cy="392" r="4.5"/>
  </g>

  <!-- Central Microcontroller / PN7150 Chip -->
  <g transform="translate(196, 196)" filter="url(#coreGlow)">
    <!-- Chip Body -->
    <rect x="0" y="0" width="120" height="120" rx="14" fill="url(#chipMatte)" stroke="#52525b" stroke-width="2"/>
    
    <!-- Chip Pins on 4 sides -->
    <g fill="#a1a1aa">
      <!-- Top pins -->
      <rect x="25" y="-8" width="6" height="8" rx="1"/>
      <rect x="45" y="-8" width="6" height="8" rx="1"/>
      <rect x="65" y="-8" width="6" height="8" rx="1"/>
      <rect x="85" y="-8" width="6" height="8" rx="1"/>
      <!-- Bottom pins -->
      <rect x="25" y="120" width="6" height="8" rx="1"/>
      <rect x="45" y="120" width="6" height="8" rx="1"/>
      <rect x="65" y="120" width="6" height="8" rx="1"/>
      <rect x="85" y="120" width="6" height="8" rx="1"/>
      <!-- Left pins -->
      <rect x="-8" y="25" width="8" height="6" rx="1"/>
      <rect x="-8" y="45" width="8" height="6" rx="1"/>
      <rect x="-8" y="65" width="8" height="6" rx="1"/>
      <rect x="-8" y="85" width="8" height="6" rx="1"/>
      <!-- Right pins -->
      <rect x="120" y="25" width="8" height="6" rx="1"/>
      <rect x="120" y="45" width="8" height="6" rx="1"/>
      <rect x="120" y="65" width="8" height="6" rx="1"/>
      <rect x="120" y="85" width="8" height="6" rx="1"/>
    </g>

    <!-- Dot index 1 -->
    <circle cx="18" cy="18" r="4" fill="#71717a"/>

    <!-- Glowing NFC wireless icon embossed on chip center -->
    <g transform="translate(60, 60)">
      <circle cx="0" cy="0" r="6" fill="#38bdf8"/>
      <path d="M -9 -9 A 14 14 0 0 1 9 -9" fill="none" stroke="#38bdf8" stroke-width="3" stroke-linecap="round"/>
      <path d="M -16 -16 A 24 24 0 0 1 16 -16" fill="none" stroke="#38bdf8" stroke-width="3" stroke-linecap="round"/>
    </g>

    <text x="60" y="98" font-family="Consolas, monospace" font-size="11" font-weight="700" fill="#38bdf8" text-anchor="middle" letter-spacing="1">SK·NFC</text>
  </g>
</svg>'''

# 4. 方案 D: 极简现代 "SK" 科技几何 Monogram (Sleek Geometric Monogram)
svg_d = '''<svg width="512" height="512" viewBox="0 0 512 512" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <radialGradient id="monoBg" cx="40%" cy="30%" r="80%">
      <stop offset="0%" stop-color="#1e1b4b"/>
      <stop offset="50%" stop-color="#0f172a"/>
      <stop offset="100%" stop-color="#020617"/>
    </radialGradient>
    <linearGradient id="sGrad" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#38bdf8"/>
      <stop offset="100%" stop-color="#2563eb"/>
    </linearGradient>
    <linearGradient id="kGrad" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#60a5fa"/>
      <stop offset="100%" stop-color="#818cf8"/>
    </linearGradient>
    <filter id="brandGlow" x="-20%" y="-20%" width="140%" height="140%">
      <feDropShadow dx="0" dy="8" stdDeviation="16" flood-color="#2563eb" flood-opacity="0.5"/>
    </filter>
  </defs>

  <rect x="24" y="24" width="464" height="464" rx="104" fill="url(#monoBg)"/>
  <rect x="24" y="24" width="464" height="464" rx="104" fill="none" stroke="#6366f1" stroke-opacity="0.3" stroke-width="2"/>

  <!-- Logo Mark: Modern 'S' and 'K' integrated with Contactless waves -->
  <g transform="translate(100, 106)" filter="url(#brandGlow)">
    <!-- Stylized Modern 'S' Loop (Left side) -->
    <path d="M 120 40 
             C 50 40, 20 80, 20 130 
             C 20 190, 110 190, 110 240 
             C 110 270, 70 280, 40 260" 
          fill="none" stroke="url(#sGrad)" stroke-width="32" stroke-linecap="round"/>

    <!-- Integrated Stylized 'K' Stem & Wings (Right side) -->
    <line x1="160" y1="40" x2="160" y2="270" stroke="#ffffff" stroke-width="30" stroke-linecap="round"/>
    
    <!-- K Upper Diagonal turning into NFC signal -->
    <path d="M 175 140 L 260 50" stroke="url(#kGrad)" stroke-width="28" stroke-linecap="round"/>
    <!-- K Lower Diagonal -->
    <path d="M 175 160 L 270 270" stroke="url(#kGrad)" stroke-width="28" stroke-linecap="round"/>

    <!-- Concentric Contactless Arcs on top right representing Wireless Pass -->
    <g transform="translate(230, 70)">
      <path d="M 30 -10 A 30 30 0 0 1 60 20" fill="none" stroke="#38bdf8" stroke-width="10" stroke-linecap="round"/>
      <path d="M 50 -30 A 60 60 0 0 1 95 20" fill="none" stroke="#38bdf8" stroke-width="10" stroke-linecap="round" stroke-opacity="0.6"/>
    </g>
  </g>

  <!-- Clean Bottom Subtitle -->
  <text x="256" y="440" font-family="Segoe UI, sans-serif" font-weight="700" font-size="22" fill="#ffffff" letter-spacing="6" text-anchor="middle">SKPASS</text>
</svg>'''

# 5. 方案 E: 手机卡模拟 & 数字通行盾牌 (Digital Card Emulation & Shield)
svg_e = '''<svg width="512" height="512" viewBox="0 0 512 512" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <radialGradient id="shieldBg" cx="50%" cy="40%" r="70%">
      <stop offset="0%" stop-color="#022c22"/>
      <stop offset="50%" stop-color="#064e3b"/>
      <stop offset="100%" stop-color="#021a14"/>
    </radialGradient>
    <linearGradient id="shieldGrad" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#10b981"/>
      <stop offset="50%" stop-color="#059669"/>
      <stop offset="100%" stop-color="#047857"/>
    </linearGradient>
    <filter id="greenGlow" x="-20%" y="-20%" width="140%" height="140%">
      <feDropShadow dx="0" dy="12" stdDeviation="16" flood-color="#10b981" flood-opacity="0.5"/>
    </filter>
  </defs>

  <rect x="24" y="24" width="464" height="464" rx="104" fill="#021410"/>
  <rect x="24" y="24" width="464" height="464" rx="104" fill="none" stroke="#10b981" stroke-opacity="0.3" stroke-width="2"/>

  <!-- Shield Shape for Security & Emulation Pass -->
  <g transform="translate(106, 76)" filter="url(#greenGlow)">
    <!-- Shield Outline -->
    <path d="M 150 10 
             L 280 60 
             C 280 200, 200 290, 150 330 
             C 100 290, 20 200, 20 60 
             Z" 
          fill="url(#shieldGrad)" stroke="#6ee7b7" stroke-width="4"/>

    <!-- Subtle Inner Shield Inset -->
    <path d="M 150 35 
             L 255 75 
             C 255 190, 190 265, 150 300 
             C 110 265, 45 190, 45 75 
             Z" 
          fill="#064e3b" fill-opacity="0.6"/>

    <!-- Smartphone + Card Fusion Silhouette (Card Emulation) -->
    <g transform="translate(100, 95)">
      <!-- Phone outline -->
      <rect x="0" y="0" width="100" height="150" rx="16" fill="#022c22" stroke="#a7f3d0" stroke-width="3"/>
      <!-- Home indicator line -->
      <line x1="35" y1="138" x2="65" y2="138" stroke="#a7f3d0" stroke-width="3" stroke-linecap="round"/>
      
      <!-- Smart Card Emerging/Projecting from Phone -->
      <g transform="translate(20, 25)">
        <rect x="0" y="0" width="85" height="55" rx="6" fill="#10b981" stroke="#ffffff" stroke-width="2"/>
        <rect x="10" y="16" width="18" height="15" rx="3" fill="#fef08a"/>
        <path d="M 60 14 A 12 12 0 0 1 60 36" fill="none" stroke="#ffffff" stroke-width="3" stroke-linecap="round"/>
        <path d="M 70 8 A 20 20 0 0 1 70 42" fill="none" stroke="#ffffff" stroke-width="3" stroke-linecap="round"/>
      </g>

      <!-- Transmission radio beacon -->
      <circle cx="50" cy="98" r="8" fill="#34d399"/>
      <path d="M 32 98 A 18 18 0 0 1 68 98" fill="none" stroke="#34d399" stroke-width="3" stroke-linecap="round"/>
      <path d="M 22 98 A 28 28 0 0 1 78 98" fill="none" stroke="#34d399" stroke-width="3" stroke-linecap="round"/>
    </g>
  </g>

  <text x="256" y="445" font-family="Segoe UI, sans-serif" font-weight="700" font-size="20" fill="#a7f3d0" letter-spacing="4" text-anchor="middle">CARD EMULATION</text>
</svg>'''

# 6. 方案 F: 暗黑极客赛博霓虹 (Cyber Neon Minimalist)
svg_f = '''<svg width="512" height="512" viewBox="0 0 512 512" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <filter id="cyanGlow" x="-30%" y="-30%" width="160%" height="160%">
      <feDropShadow dx="0" dy="0" stdDeviation="8" flood-color="#00f2fe" flood-opacity="0.9"/>
      <feDropShadow dx="0" dy="0" stdDeviation="20" flood-color="#4facfe" flood-opacity="0.6"/>
    </filter>
  </defs>

  <!-- Deep Obsidian Black Background -->
  <rect x="24" y="24" width="464" height="464" rx="104" fill="#080c14"/>
  <rect x="24" y="24" width="464" height="464" rx="104" fill="none" stroke="#00f2fe" stroke-opacity="0.3" stroke-width="1.5"/>

  <!-- Isometric/Angled Minimalist Neon Card Wireframe -->
  <g transform="translate(256,245) rotate(-10) translate(-160,-100)" filter="url(#cyanGlow)">
    <!-- Outer Card Glow Edge -->
    <rect x="0" y="0" width="320" height="200" rx="16" fill="#0d1527" fill-opacity="0.75" stroke="#00f2fe" stroke-width="3.5"/>
    
    <!-- Neon Chip Wireframe -->
    <g transform="translate(40, 70)">
      <rect x="0" y="0" width="55" height="45" rx="6" fill="#06182c" stroke="#38bdf8" stroke-width="2.5"/>
      <line x1="0" y1="22" x2="55" y2="22" stroke="#38bdf8" stroke-width="2"/>
      <line x1="20" y1="0" x2="20" y2="45" stroke="#38bdf8" stroke-width="2"/>
      <line x1="36" y1="0" x2="36" y2="45" stroke="#38bdf8" stroke-width="2"/>
    </g>

    <!-- Concentric Radio Beam Arcs -->
    <g transform="translate(250, 65)">
      <circle cx="0" cy="35" r="5" fill="#00f2fe"/>
      <path d="M 0 15 A 20 20 0 0 1 0 55" fill="none" stroke="#00f2fe" stroke-width="4" stroke-linecap="round"/>
      <path d="M 12 0 A 38 38 0 0 1 12 70" fill="none" stroke="#00f2fe" stroke-width="4" stroke-linecap="round"/>
      <path d="M 24 -15 A 56 56 0 0 1 24 85" fill="none" stroke="#00f2fe" stroke-width="3" stroke-linecap="round" stroke-opacity="0.7"/>
    </g>

    <!-- Glowing Tech Font -->
    <text x="40" y="170" font-family="Consolas, monospace" font-size="22" font-weight="700" fill="#ffffff" letter-spacing="4">SK·PASS</text>
  </g>
</svg>'''

icons = [
    ("optionA_fluent_card", "方案 A：Fluent 现代智能卡 (经典蓝调 / 镀金芯片 / 无线射频)", svg_a),
    ("optionB_arcade_pass", "方案 B：街机硬核通行卡 (Aime / BanaPassport 游戏风格)", svg_b),
    ("optionC_antenna_mcu", "方案 C：射频天线线圈与核心晶圆 (PCB 硬件极客风格)", svg_c),
    ("optionD_sk_monogram", "方案 D：极简科技 'SK' 几何徽标 (大厂顶级产品 Logo 风格)", svg_d),
    ("optionE_digital_shield", "方案 E：数字通行证与卡模拟盾牌 (手机模拟卡 / 安全凭证)", svg_e),
    ("optionF_cyber_neon", "方案 F：暗黑赛博霓虹 (高对比黑底 / 电光青发光线条)", svg_f),
]

for name, title, svg in icons:
    svg_path = os.path.join(OUT_DIR, f"{name}.svg")
    html_path = os.path.join(OUT_DIR, f"{name}.html")
    png_path = os.path.join(OUT_DIR, f"{name}.png")
    
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

print("Saved all SVGs and HTMLs.")
