# -*- coding: utf-8 -*-
import os
import subprocess
from PIL import Image

ROOT = r"E:\workplace\esp32\MyProject\SKPass"
SVG_PATH = os.path.join(ROOT, r"docs\icons\sk_set\sk1_mobius_flow.svg")
HTML_PATH = os.path.join(ROOT, r"desktop\build\icon_1024.html")
PNG_1024 = os.path.join(ROOT, r"desktop\build\icon_1024.png")

with open(SVG_PATH, "r", encoding="utf-8") as f:
    svg_content = f.read()

# Make 1024x1024 render page
html_content = f'''<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <style>
    * {{ margin: 0; padding: 0; box-sizing: border-box; }}
    body {{ background: transparent; display: flex; align-items: center; justify-content: center; width: 1024px; height: 1024px; }}
    svg {{ width: 1024px; height: 1024px; }}
  </style>
</head>
<body>
{svg_content}
</body>
</html>'''

with open(HTML_PATH, "w", encoding="utf-8") as f:
    f.write(html_content)

edge = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
cmd = [
    edge,
    "--headless=new",
    "--disable-gpu",
    f"--screenshot={PNG_1024}",
    "--window-size=1024,1024",
    f"file:///{HTML_PATH.replace(os.sep, '/')}"
]
print("Rendering 1024x1024 PNG with Edge...")
subprocess.run(cmd, check=True)

# Generate multi-resolution ICO with Pillow
img = Image.open(PNG_1024).convert("RGBA")

# Sizes for high-quality Windows ICO
sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]

targets = [
    os.path.join(ROOT, r"desktop\src\app.ico"),
    os.path.join(ROOT, r"desktop\src\app.png"),
    os.path.join(ROOT, r"docs\icons\SKPass.ico"),
    os.path.join(ROOT, r"docs\icons\SKPass.png"),
]

for t in targets:
    if t.endswith(".ico"):
        img.save(t, format="ICO", sizes=sizes)
        print(f"Generated ICO: {t}")
    elif t.endswith(".png"):
        img_256 = img.resize((256, 256), Image.Resampling.LANCZOS)
        img_256.save(t, format="PNG")
        print(f"Generated PNG: {t}")

print("Icon generation completed successfully.")
