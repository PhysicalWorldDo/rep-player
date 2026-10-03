# 播放器图标

`rep-player.png` 使用 Codex 内置 imagegen 工具生成（2026-10-03），未使用客户端图像作为参考。`rep-player.ico` 保留 Alpha，包含 16、24、32、48、64、128、256 像素版本。EXE 与窗口使用同一 ICO。

生成提示词：

```text
Use case: logo-brand
Asset type: Windows application icon for a native REP replay player
Primary request: Create a polished, original square app icon for a replay and visual-effects player. A bold luminous play triangle inside a single circular replay arrow, on a compact dark graphite rounded-square tile. Subtle cyan and amber accents suggest animated energy. Simple strong silhouette readable at 16 and 32 pixels, generous safe margin, frontal flat icon composition with restrained dimensional shading. Transparent canvas outside the rounded tile.
Constraints: one standalone icon only, no lettering, no text, no watermark, no existing game or company logo, no characters, no tiny decorative details. True alpha transparency outside the tile.
```

重新转换（Python + Pillow，仅开发时需要）：

```powershell
python -c "from PIL import Image; Image.open('assets/icon/rep-player.png').save('assets/icon/rep-player.ico', format='ICO', sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])"
```
