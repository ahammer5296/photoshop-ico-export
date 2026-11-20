# Photoshop ICO Export

UXP-плагин для Photoshop (24.2+): сохраняет открытый документ как многоразмерный `.ico` (PNG внутри ICO).

## Установка
1. Откройте Adobe UXP Developer Tool.
2. *Add Plugin* → выберите `ico-export/manifest.json` → *Load*.
3. В Photoshop: *Plugins → ICO Export*.

## Нативный плагин для File → Save As (`ico-format/`)
Плагин формата файла на Photoshop SDK (только запись, RGB 8 бит, прозрачность, PNG-слои 16–256 px).

Сборка (нужен Photoshop SDK и Xcode):
```bash
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer ico-format/build.sh /путь/к/pluginsdk
```
Установка: скопировать `ico-format/ICOFormat.plugin` в `/Applications/Adobe Photoshop <версия>/Plug-ins/ICO Format/` и перезапустить Photoshop.
