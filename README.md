# pcmplay

一个面向 Windows x64 的轻量级 PCM / WAV 音频播放器，纯 C 实现（Win32 API +
WASAPI），无任何第三方依赖。

## 功能

- **WAV 播放**：自动解析 RIFF/WAVE 头，支持 u8 / s16 / s24 / s32 / f32，
  支持多声道与任意采样率（由系统音频引擎自动重采样）。
- **裸 PCM 播放**：无文件头的 `.pcm` 裸流可手动指定采样率、声道数与样本格式。
- **交互式控制台 UI**：进度条、时间显示、音量条、暂停/恢复、任意位置拖动
  （seek）、音量调节（0–200%，超 100% 时软件增益）。
- **多文件顺序播放**。

## 构建需要

- Windows 10/11 x64（WASAPI 共享模式 + `AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM`）
- MSVC（VS2017 或更新）或 MinGW-w64 GCC
- CMake 3.15+（可选，也可以直接用 `build.bat`）

## 构建

### 方式一：MSVC 命令行

在「x64 Native Tools Command Prompt for VS」中执行：

```bat
cd Claw\pcm-player
build.bat
```

### 方式二：CMake

```bat
cd Claw\pcm-player
cmake -B build -A x64
cmake --build build --config Release
```

产物：`pcmplay.exe`（`build.bat` 直接输出在项目根目录，CMake 输出在
`build\Release\` 下）。

## 用法

```text
pcmplay [options] <file> [more files...]

输入选项（裸 PCM 文件）:
  -r, --raw              强制按无文件头的裸 PCM 解析
  --rate <hz>            采样率（默认 44100）
  --channels <n>         声道数（默认 2）
  --format <fmt>         样本格式 u8/s16/s24/s32/f32（默认 s16）

播放选项:
  -s, --start <time>     起始位置，如 12.5、1:30、1h2m3s、500ms
  -t, --duration <time>  只播放这么长
  -v, --volume <pct>     初始音量百分比 0-200（默认 100）
  -q, --quiet            无 UI，直接播放到结束

其他:
  -h, --help             帮助
  -V, --version          版本
```

示例：

```bat
pcmplay music.wav
pcmplay --volume 80 -s 1:30 music.wav
pcmplay -r --rate 16000 --channels 1 --format s16 record.pcm
pcmplay -q -t 30s test.wav
```

## 交互按键

| 按键              | 功能            |
| ----------------- | --------------- |
| `空格` / `p`      | 暂停 / 恢复     |
| `←` / `→`         | 后退 / 前进 5 秒 |
| `PgUp` / `PgDn`   | 后退 / 前进 30 秒|
| `↑` / `↓`         | 音量 -/+ 5%     |
| `Home` / `End`    | 跳到开头 / 结尾  |
| `r`               | 重新播放当前文件 |
| `n`               | 下一个文件      |
| `q` / `Esc` / `Ctrl+C` | 退出      |

## 目录结构

```text
pcm-player/
├── CMakeLists.txt
├── build.bat          # MSVC 一键构建脚本
└── src/
    ├── main.c         # 命令行解析、播放循环、交互 UI
    ├── out.c/.h       # WASAPI 共享模式输出（事件驱动渲染线程）
    ├── source.c/.h    # WAV 解析与裸 PCM 数据源
    ├── audio.c/.h     # 样本格式描述与格式/增益转换
    ├── console.c/.h   # Win32 控制台辅助（颜色、光标、非阻塞按键）
    └── util.c/.h      # 日志、时间解析与格式化
```

## 实现说明

- 输出走 **WASAPI 共享模式**：渲染线程以事件回调方式向系统音频引擎喂数据，
  通过 `AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM` 让引擎完成采样率/格式转换，
  播放器自身只按源格式推送帧。
- 音量通过 `audio_convert_gain()` 在填充回调中做软件增益（带削波），范围
  0–200%，100% 以上即数字放大。
- 播放位置 = 已填充帧数 - 设备中尚未播放的帧数（`GetCurrentPadding`），
  seek 时通过 `IAudioClient::Reset` 清空设备缓冲，保证声音立即跳转。
