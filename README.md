<div align="center">

# 🎬 GrandFraps

[![C++](https://img.shields.io/badge/C%2B%2B-23-blue?logo=cplusplus)](https://en.cppreference.com/)
[![CMake](https://img.shields.io/badge/CMake-3.20%2B-064F8C?logo=cmake)](https://cmake.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x86-0078D4?logo=windows)](https://www.microsoft.com/windows)
[![SAMP](https://img.shields.io/badge/SA--MP-0.3.7-orange)](#)
[![FFmpeg](https://img.shields.io/badge/FFmpeg-libavcodec-green?logo=ffmpeg)](https://ffmpeg.org/)
[![License](https://img.shields.io/badge/License-See%20LICENSE-yellow)](LICENSE.txt)

**DLL-мод для записи геймплея GTA SA + SA-MP прямо из процесса игры**

**[🇷🇺 Русский](#-русский) · [🇬🇧 English](#-english) · [🇩🇪 Deutsch](#-deutsch)**

---

> 📹 **Демонстрация работы:** [смотреть на YouTube](https://youtu.be/XXXXXXXXXXXXXXX)

</div>

---

## 🇷🇺 Русский

DLL-мод для **GTA San Andreas + SA-MP 0.3.7**, который записывает геймплей в MP4 прямо изнутри игрового процесса. Никаких сторонних программ захвата — рекордер хукает D3D9, захватывает кадры из бэкбуфера, кодирует через FFmpeg (libx264 / libx265 / NVENC) и пишет результат на диск. Встроенный **ImGui-оверлей** управляется через чат-команду `/fraps`.

### 📋 Содержание

- [Возможности](#-возможности)
- [Как это работает](#-как-это-работает)
- [Зависимости](#-зависимости)
- [Сборка](#-сборка)
- [Установка](#-установка)
- [Использование](#-использование)
- [Структура проекта](#-структура-проекта)

---

### 🚀 Возможности

| Модуль | Описание |
|--------|----------|
| 🎮 **Захват D3D9** | Хук `IDirect3DDevice9::Present` — кадры снимаются прямо с бэкбуфера без стороннего ПО |
| 🎞 **Видеокодеки** | x264, x265 (программные) и NVENC (аппаратный GPU-кодировщик NVIDIA) |
| 🔊 **Захват аудио** | Опциональная запись системного звука через WASAPI с настройкой частоты и каналов |
| 🖥 **ImGui-оверлей** | Встроенный интерфейс с тёмной темой (electric indigo), открывается по `/fraps` |
| ⚙️ **Гибкая настройка** | CRF (0–51), FPS (10–240), пресеты x264 p1–p7, разрешение, путь к файлу |
| 📝 **Логирование** | Потокобезопасный файловый логгер с уровнями INFO / DEBUG / ERR |
| 🔁 **Многопоточность** | Захват, конвертация YUV и кодирование работают в отдельных потоках с ограниченными очередями |
| 🎯 **Поддержка R1 и R3** | Работает с обоими лэйаутами SAMP классов (0.3.7 R1 и R3) |

---

### ⚙️ Как это работает

```
GTA SA + SA-MP
    │
    │  DLL-инъекция в gta_sa.exe (x86)
    ▼
GrandFraps (fraps.dll)
    │
    ├── SAMP::Init()                    ← регистрация коллбэков
    ├── D3DPresentHook()                ← рендер ImGui + захват кадра в очередь
    ├── D3DResetHook()                  ← сброс DX9 объектов при ресете устройства
    │
    └── VideoWriter
         ├── SurfacePool               ← пул IDirect3DSurface9 для быстрого захвата
         ├── VideoEncoder              ← поток: RawFrame → YUV → libavcodec → MP4
         └── AudioCapture + Encoder    ← поток: WASAPI loopback → AAC → MP4
```

---

### 📦 Зависимости

| Библиотека | Назначение |
|------------|------------|
| FFmpeg (avcodec, avformat, avutil, swresample, swscale) | Кодирование и мультиплексирование видео/аудио |
| libx264 | Программный кодировщик H.264 |
| x265 | Программный кодировщик H.265 |
| NVENC / nvcuvid | Аппаратный кодировщик NVIDIA |
| Intel Media SDK (libmfx) | Аппаратный кодировщик Intel |
| OpenCL | GPU-конвертация пикселей |
| DirectX 9 SDK (June 2010) | Захват бэкбуфера D3D9 |
| ImGui | Оверлей-интерфейс |
| zlib, bzip2, lzma, snappy | Утилиты сжатия |
| libssl / libcrypto (OpenSSL) | Криптография (RakNet) |
| opus | Аудиокодек |
| libxml2 | XML-утилиты |
| CUDA Toolkit 12.x | Заголовки NVENC/CUVID |
| plugin-sdk | Определения типов GTA SA / SA-MP |
| kthook | Движок x86 inline-хуков |
| memwrapper | Утилиты патчинга памяти |

---

### 🛠 Сборка

#### Требования

- Visual Studio 2022 (toolset v143)
- CMake 3.20+
- vcpkg с триплетом `x86-windows-static`
- FFmpeg (статические либы, x86 Windows)
- DirectX SDK June 2010
- CUDA Toolkit 12.x
- [plugin-sdk](https://github.com/CyberMor/sampapi)

#### Конфигурация CMake

```bat
cmake -B build -G "Visual Studio 17 2022" -A Win32 ^
  -DVCPKG_DIR="C:/vcpkg/installed/x86-windows-static" ^
  -DFFMPEG_DIR="C:/ffmpeg" ^
  -DCUDA_TOOLKIT_ROOT_DIR="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6" ^
  -DDXSDK_DIR="C:/DXSDK" ^
  -DPLUGIN_SDK_DIR="C:/plugin-sdk"
```

> ⚠️ Цель всегда **Win32 (x86)**. SA-MP — 32-битный процесс, DLL обязана совпадать.

#### Сборка

```bat
cmake --build build --config Release
```

Результат: `build/Release/fraps.dll`

#### Legacy (Visual Studio)

Можно открыть `SAMP_API_ONE_HEADER.vcxproj` напрямую в VS 2022. Перед сборкой нужно исправить хардкодные пути к SDK в файле проекта.

---

### 📥 Установка

1. Собери или скачай `fraps.dll`.
2. Положи DLL в любое удобное место.
3. Инжектируй её в `gta_sa.exe` **после того, как SA-MP загрузился** — подойдёт любой DLL-инжектор (Xenos, LoadLibrary-инжектор и т.д.).
4. После загрузки в чате появится сообщение:

   ```
   GrandFraps loaded — type /fraps to open
   ```

---

### 🎮 Использование

1. Открой оверлей командой `/fraps` в чате SA-MP.
2. Настрой путь к файлу, разрешение, FPS, CRF и пресет кодировщика.
3. Нажми **Start recording** — кнопка становится красной во время записи.
4. Нажми **Stop recording** (или выгрузи DLL) чтобы завершить и записать файл.
5. Видео будет по указанному пути (по умолчанию: `GrandFraps/video.mp4` относительно папки с игрой).

**Параметры по умолчанию:**

| Параметр | Значение |
|----------|----------|
| Путь | `GrandFraps/video.mp4` |
| Разрешение | 1920 × 1080 |
| FPS | 60 |
| CRF | 15 |
| Пресет | `p4` |
| Аудио | выключено |
| Частота дискретизации | 44 100 Гц |
| Каналов | 2 (стерео) |

---

### 📁 Структура проекта

```
├── main.cpp / main.h          — DllMain, ImGui-оверлей, SAMP-коллбэки
├── VideoWriter.cpp/.h         — верхний уровень: инициализация, start/stop, захват кадра
├── VideoEncoder.cpp/.h        — поток: raw кадры → H.264/H.265
├── AudioEncoder.cpp/.h        — поток: PCM → AAC
├── AudioCapture.cpp/.h        — поток захвата WASAPI loopback
├── BufferPool.cpp/.h          — переиспользуемый пул буферов
├── SurfacePool.cpp/.h         — пул IDirect3DSurface9
├── FPSLimiter.cpp/.h          — высокоточный ограничитель FPS
├── Logger.cpp/.h              — потокобезопасный файловый логгер
├── ErrorCodes.h               — HRESULT-константы для ошибок FFmpeg
├── FrameData.h                — структуры RawFrame / YuvFrame / AudioFrame
├── SAMP/                      — заголовки SAMP API (лэйауты классов R1 и R3)
├── imgui/                     — Dear ImGui + бэкенды DX9 / Win32
├── kthook/                    — библиотека inline-хуков
├── memwrapper/                — утилиты работы с памятью
├── notify/                    — библиотека toast-уведомлений ImGui
└── CMakeLists.txt             — скрипт сборки CMake
```

---
---

## 🇬🇧 English

A DLL mod for **GTA San Andreas + SA-MP 0.3.7** that records gameplay to MP4 directly from inside the game process. No external capture software needed — the recorder hooks D3D9, grabs frames from the back buffer, encodes them via FFmpeg (libx264 / libx265 / NVENC) and writes the result to disk. A built-in **ImGui overlay** is controlled via the `/fraps` chat command.

### 📋 Table of Contents

- [Features](#-features)
- [How It Works](#-how-it-works)
- [Dependencies](#-dependencies)
- [Building](#-building)
- [Installation](#-installation)
- [Usage](#-usage)
- [Project Structure](#-project-structure)

---

### 🚀 Features

| Module | Description |
|--------|-------------|
| 🎮 **D3D9 Capture** | Hook on `IDirect3DDevice9::Present` — frames taken directly from the back buffer |
| 🎞 **Video Codecs** | x264, x265 (software) and NVENC (NVIDIA GPU hardware encoder) |
| 🔊 **Audio Capture** | Optional system audio recording via WASAPI with configurable sample rate and channels |
| 🖥 **ImGui Overlay** | Built-in dark-themed UI (electric indigo accent), toggled with `/fraps` |
| ⚙️ **Flexible Settings** | CRF (0–51), FPS (10–240), x264 presets p1–p7, resolution, output path |
| 📝 **Logging** | Thread-safe file logger with INFO / DEBUG / ERR levels |
| 🔁 **Multithreading** | Capture, YUV conversion and encoding run on dedicated threads with bounded queues |
| 🎯 **R1 & R3 Support** | Works with both SAMP class layouts (0.3.7 R1 and R3) |

---

### ⚙️ How It Works

```
GTA SA + SA-MP
    │
    │  DLL injection into gta_sa.exe (x86)
    ▼
GrandFraps (fraps.dll)
    │
    ├── SAMP::Init()                    ← register callbacks
    ├── D3DPresentHook()                ← render ImGui + push frame to queue
    ├── D3DResetHook()                  ← release DX9 objects on device reset
    │
    └── VideoWriter
         ├── SurfacePool               ← IDirect3DSurface9 pool for fast capture
         ├── VideoEncoder              ← thread: RawFrame → YUV → libavcodec → MP4
         └── AudioCapture + Encoder    ← thread: WASAPI loopback → AAC → MP4
```

---

### 📦 Dependencies

| Library | Purpose |
|---------|---------|
| FFmpeg (avcodec, avformat, avutil, swresample, swscale) | Video/audio encoding and muxing |
| libx264 | H.264 software encoder |
| x265 | H.265 software encoder |
| NVENC / nvcuvid | NVIDIA hardware encoder |
| Intel Media SDK (libmfx) | Intel hardware encoder |
| OpenCL | GPU-accelerated pixel conversion |
| DirectX 9 SDK (June 2010) | D3D9 back-buffer capture |
| ImGui | In-game overlay UI |
| zlib, bzip2, lzma, snappy | Compression utilities |
| libssl / libcrypto (OpenSSL) | Crypto (RakNet) |
| opus | Audio codec |
| libxml2 | XML utilities |
| CUDA Toolkit 12.x | NVENC/CUVID headers |
| plugin-sdk | GTA SA / SAMP type definitions |
| kthook | x86 inline hook engine |
| memwrapper | Memory patch helpers |

---

### 🛠 Building

#### Prerequisites

- Visual Studio 2022 (v143 toolset)
- CMake 3.20+
- vcpkg with `x86-windows-static` triplet
- FFmpeg build for x86 Windows (static libs)
- DirectX SDK June 2010
- CUDA Toolkit 12.x
- [plugin-sdk](https://github.com/CyberMor/sampapi)

#### CMake Configuration

```bat
cmake -B build -G "Visual Studio 17 2022" -A Win32 ^
  -DVCPKG_DIR="C:/vcpkg/installed/x86-windows-static" ^
  -DFFMPEG_DIR="C:/ffmpeg" ^
  -DCUDA_TOOLKIT_ROOT_DIR="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6" ^
  -DDXSDK_DIR="C:/DXSDK" ^
  -DPLUGIN_SDK_DIR="C:/plugin-sdk"
```

> ⚠️ The target is always **Win32 (x86)**. SA-MP is a 32-bit process and the injected DLL must match.

#### Build

```bat
cmake --build build --config Release
```

Output: `build/Release/fraps.dll`

#### Legacy (Visual Studio)

You can open `SAMP_API_ONE_HEADER.vcxproj` directly in VS 2022. Before building, fix the hardcoded SDK paths inside the project file.

---

### 📥 Installation

1. Build or download `fraps.dll`.
2. Place the DLL anywhere on your machine.
3. Inject it into `gta_sa.exe` **after SA-MP has loaded** — use any DLL injector (Xenos, LoadLibrary injector, etc.).
4. Once loaded, a chat message will appear:

   ```
   GrandFraps loaded — type /fraps to open
   ```

---

### 🎮 Usage

1. Open the overlay with `/fraps` in SA-MP chat.
2. Set the output path, resolution, FPS, CRF and encoder preset.
3. Click **Start recording** — the button turns red while recording is active.
4. Click **Stop recording** (or unload the DLL) to finalize and write the file.
5. Find the MP4 at the configured output path (default: `GrandFraps/video.mp4` relative to the GTA SA directory).

**Default configuration:**

| Parameter | Value |
|-----------|-------|
| Output path | `GrandFraps/video.mp4` |
| Resolution | 1920 × 1080 |
| FPS | 60 |
| CRF | 15 |
| Preset | `p4` |
| Audio | disabled |
| Sample rate | 44 100 Hz |
| Channels | 2 (stereo) |

---

### 📁 Project Structure

```
├── main.cpp / main.h          — DllMain, ImGui overlay, SAMP callbacks
├── VideoWriter.cpp/.h         — top-level recorder: init, start/stop, frame capture
├── VideoEncoder.cpp/.h        — thread: raw frames → H.264/H.265
├── AudioEncoder.cpp/.h        — thread: PCM → AAC
├── AudioCapture.cpp/.h        — WASAPI loopback capture thread
├── BufferPool.cpp/.h          — reusable buffer pool
├── SurfacePool.cpp/.h         — IDirect3DSurface9 pool
├── FPSLimiter.cpp/.h          — high-resolution frame rate limiter
├── Logger.cpp/.h              — thread-safe file logger
├── ErrorCodes.h               — HRESULT constants for FFmpeg errors
├── FrameData.h                — RawFrame / YuvFrame / AudioFrame structs
├── SAMP/                      — SAMP API headers (R1 and R3 class layouts)
├── imgui/                     — Dear ImGui + DX9 / Win32 backends
├── kthook/                    — inline hook library
├── memwrapper/                — memory utilities
├── notify/                    — ImGui toast notification library
└── CMakeLists.txt             — CMake build script
```

---
---

## 🇩🇪 Deutsch

Ein DLL-Mod für **GTA San Andreas + SA-MP 0.3.7**, der Gameplay direkt aus dem Spielprozess heraus als MP4 aufzeichnet. Keine externe Capture-Software nötig — der Recorder hookt D3D9, greift Frames vom Back Buffer, kodiert sie via FFmpeg (libx264 / libx265 / NVENC) und schreibt das Ergebnis auf die Festplatte. Ein eingebautes **ImGui-Overlay** wird über den Chat-Befehl `/fraps` gesteuert.

### 📋 Inhaltsverzeichnis

- [Funktionen](#-funktionen)
- [Funktionsweise](#-funktionsweise)
- [Abhängigkeiten](#-abhngigkeiten)
- [Kompilieren](#-kompilieren)
- [Installation](#-installation)
- [Verwendung](#-verwendung)
- [Projektstruktur](#-projektstruktur)

---

### 🚀 Funktionen

| Modul | Beschreibung |
|-------|--------------|
| 🎮 **D3D9-Capture** | Hook auf `IDirect3DDevice9::Present` — Frames direkt vom Back Buffer |
| 🎞 **Video-Codecs** | x264, x265 (Software) und NVENC (NVIDIA GPU Hardware-Encoder) |
| 🔊 **Audio-Capture** | Optionale Systemton-Aufnahme via WASAPI mit konfigurierbarer Samplerate und Kanälen |
| 🖥 **ImGui-Overlay** | Eingebaute dunkle UI (Electric-Indigo-Akzent), aktiviert mit `/fraps` |
| ⚙️ **Flexible Einstellungen** | CRF (0–51), FPS (10–240), x264-Presets p1–p7, Auflösung, Ausgabepfad |
| 📝 **Logging** | Thread-sicherer Datei-Logger mit INFO / DEBUG / ERR Stufen |
| 🔁 **Multithreading** | Capture, YUV-Konvertierung und Kodierung laufen in dedizierten Threads mit begrenzten Queues |
| 🎯 **R1 & R3 Support** | Kompatibel mit beiden SAMP-Class-Layouts (0.3.7 R1 und R3) |

---

### ⚙️ Funktionsweise

```
GTA SA + SA-MP
    │
    │  DLL-Injektion in gta_sa.exe (x86)
    ▼
GrandFraps (fraps.dll)
    │
    ├── SAMP::Init()                    ← Callbacks registrieren
    ├── D3DPresentHook()                ← ImGui rendern + Frame in Queue schieben
    ├── D3DResetHook()                  ← DX9-Objekte beim Device-Reset freigeben
    │
    └── VideoWriter
         ├── SurfacePool               ← IDirect3DSurface9-Pool für schnellen Capture
         ├── VideoEncoder              ← Thread: RawFrame → YUV → libavcodec → MP4
         └── AudioCapture + Encoder    ← Thread: WASAPI loopback → AAC → MP4
```

---

### 📦 Abhängigkeiten

| Bibliothek | Zweck |
|------------|-------|
| FFmpeg (avcodec, avformat, avutil, swresample, swscale) | Video/Audio-Kodierung und Muxing |
| libx264 | H.264 Software-Encoder |
| x265 | H.265 Software-Encoder |
| NVENC / nvcuvid | NVIDIA Hardware-Encoder |
| Intel Media SDK (libmfx) | Intel Hardware-Encoder |
| OpenCL | GPU-beschleunigte Pixelkonvertierung |
| DirectX 9 SDK (June 2010) | D3D9 Back-Buffer-Capture |
| ImGui | In-Game Overlay-UI |
| zlib, bzip2, lzma, snappy | Kompressionsutilities |
| libssl / libcrypto (OpenSSL) | Kryptographie (RakNet) |
| opus | Audio-Codec |
| libxml2 | XML-Utilities |
| CUDA Toolkit 12.x | NVENC/CUVID-Header |
| plugin-sdk | GTA SA / SAMP Typdefinitionen |
| kthook | x86 Inline-Hook-Engine |
| memwrapper | Memory-Patch-Helfer |

---

### 🛠 Kompilieren

#### Voraussetzungen

- Visual Studio 2022 (Toolset v143)
- CMake 3.20+
- vcpkg mit `x86-windows-static` Triplet
- FFmpeg (statische Libs, x86 Windows)
- DirectX SDK June 2010
- CUDA Toolkit 12.x
- [plugin-sdk](https://github.com/CyberMor/sampapi)

#### CMake-Konfiguration

```bat
cmake -B build -G "Visual Studio 17 2022" -A Win32 ^
  -DVCPKG_DIR="C:/vcpkg/installed/x86-windows-static" ^
  -DFFMPEG_DIR="C:/ffmpeg" ^
  -DCUDA_TOOLKIT_ROOT_DIR="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6" ^
  -DDXSDK_DIR="C:/DXSDK" ^
  -DPLUGIN_SDK_DIR="C:/plugin-sdk"
```

> ⚠️ Das Ziel ist immer **Win32 (x86)**. SA-MP ist ein 32-Bit-Prozess, die injizierte DLL muss übereinstimmen.

#### Build

```bat
cmake --build build --config Release
```

Ausgabe: `build/Release/fraps.dll`

#### Legacy (Visual Studio)

`SAMP_API_ONE_HEADER.vcxproj` kann direkt in VS 2022 geöffnet werden. Vor dem Build müssen die hartkodierten SDK-Pfade im Projektfile angepasst werden.

---

### 📥 Installation

1. `fraps.dll` kompilieren oder herunterladen.
2. Die DLL an einem beliebigen Ort ablegen.
3. In `gta_sa.exe` injizieren **nachdem SA-MP geladen wurde** — beliebiger DLL-Injektor (Xenos, LoadLibrary-Injektor usw.).
4. Nach dem Laden erscheint im Chat:

   ```
   GrandFraps loaded — type /fraps to open
   ```

---

### 🎮 Verwendung

1. Overlay mit `/fraps` im SA-MP-Chat öffnen.
2. Ausgabepfad, Auflösung, FPS, CRF und Encoder-Preset einstellen.
3. **Start recording** klicken — der Button wird rot während der Aufnahme.
4. **Stop recording** klicken (oder DLL entladen) um die Datei abzuschließen.
5. Die MP4 liegt am konfigurierten Pfad (Standard: `GrandFraps/video.mp4` relativ zum GTA SA-Verzeichnis).

**Standardkonfiguration:**

| Parameter | Wert |
|-----------|------|
| Ausgabepfad | `GrandFraps/video.mp4` |
| Auflösung | 1920 × 1080 |
| FPS | 60 |
| CRF | 15 |
| Preset | `p4` |
| Audio | deaktiviert |
| Samplerate | 44 100 Hz |
| Kanäle | 2 (Stereo) |

---

### 📁 Projektstruktur

```
├── main.cpp / main.h          — DllMain, ImGui-Overlay, SAMP-Callbacks
├── VideoWriter.cpp/.h         — Top-Level-Recorder: Init, Start/Stop, Frame-Capture
├── VideoEncoder.cpp/.h        — Thread: Raw-Frames → H.264/H.265
├── AudioEncoder.cpp/.h        — Thread: PCM → AAC
├── AudioCapture.cpp/.h        — WASAPI-Loopback-Capture-Thread
├── BufferPool.cpp/.h          — Wiederverwendbarer Buffer-Pool
├── SurfacePool.cpp/.h         — IDirect3DSurface9-Pool
├── FPSLimiter.cpp/.h          — Hochauflösender FPS-Begrenzer
├── Logger.cpp/.h              — Thread-sicherer Datei-Logger
├── ErrorCodes.h               — HRESULT-Konstanten für FFmpeg-Fehler
├── FrameData.h                — RawFrame / YuvFrame / AudioFrame Structs
├── SAMP/                      — SAMP API Header (R1 und R3 Class-Layouts)
├── imgui/                     — Dear ImGui + DX9 / Win32 Backends
├── kthook/                    — Inline-Hook-Bibliothek
├── memwrapper/                — Memory-Utilities
├── notify/                    — ImGui Toast-Benachrichtigungsbibliothek
└── CMakeLists.txt             — CMake Build-Skript
```

---

<div align="center">

**GrandFraps** — made with ❤️ for the SA-MP community

</div>
