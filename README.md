<div align="center">

# 🤖 DroidLogger

### C++ streams meet Android logcat. Without the iostream baggage.

![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)
![Android NDK](https://img.shields.io/badge/Android-NDK-3DDC84?style=for-the-badge&logo=android&logoColor=white)
![Header Only](https://img.shields.io/badge/HEADER-ONLY-7C3AED?style=for-the-badge)

**One header. Familiar `<<` syntax. Real Android logging.**

</div>

---

```cpp
#define DROIDLOG_TAG "MyNativeApp"
#include "DroidLogger.h"

void on_connected(int client_id) {
    LOGI("🚀 Client #", client_id, " connected!");
    LOGD("flags=", droidlog::showbase, droidlog::hex, 255,
         droidlog::dec, droidlog::noshowbase);
    LOGW("connection secure: ", droidlog::boolalpha, true,
         droidlog::noboolalpha);
}
```

```text
I/MyNativeApp: 🚀 Client #42 connected!
D/MyNativeApp: flags=0xff
W/MyNativeApp: connection secure: true
```

> **Why DroidLogger?** Get readable, composable C++ logging without bringing in `<iostream>`, `<ostream>`, `<sstream>`, or the stream-buffer machinery.

## ✨ Highlights

- **Drop-in header:** add `DroidLogger.h`; no implementation `.cpp` files.
- **Native logcat output:** uses Android's `__android_log_write` and its priorities.
- **Two ways to log:** compact `LOGI(...)` macros or chainable `LogStream` objects.
- **Thread-local streams:** separate pending messages and formatting per thread and level.
- **Stream-friendly values:** numbers, booleans, enums, pointers, C strings, `std::string`, `std::string_view`, and similar character containers.
- **Formatting controls:** hex/dec/oct, base prefixes, precision, alignment padding, uppercase, floating-point styles, and more.
- **Line-aware output:** emits on newline, flush, or destruction; splits long messages into smaller logcat entries.
- **No iostream dependency:** lightweight formatting without constructing standard C++ stream objects.

## ⚡ Installation

**Requirements:** Android NDK toolchain, **C++17 or newer**, and Android's `log` library.

1. Copy `DroidLogger.h` into your native source or include directory.
2. Enable C++17 in your build.
3. Link your native target to `log`.

For **CMake**:

```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# After your add_library(...) or add_executable(...):
target_link_libraries(your_native_target PRIVATE log)
```

Then:

```cpp
#define DROIDLOG_TAG "Engine"  // Optional: must be set before the header.
#include "DroidLogger.h"

void initialize_engine() {
    LOGI("Native code is alive!");
}
```

No separate DroidLogger library target is needed. If you build without CMake, link Android's `liblog` (typically `-llog`).

## 🎮 Usage

### 1. Level macros — the quick path

```cpp
LOGV("Verbose detail");
LOGD("loading asset #", 7);
LOGI("ready in ", 18, " ms");
LOGW("cache hit rate low: ", 0.25);
LOGE("failed with code ", -12);
LOGF("fatal-priority event");

// Choose a level at runtime:
LOGX(ANDROID_LOG_INFO, "Dynamic priority!");
```

**Arguments are joined without added spaces or punctuation.** Supply your own separators (`" "`, `" = "`, `", "`, etc.). Each macro call appends a newline, emitting the message immediately.

| Macro | Android priority |
|---|---|
| `LOGV(...)` | `ANDROID_LOG_VERBOSE` |
| `LOGD(...)` | `ANDROID_LOG_DEBUG` |
| `LOGI(...)` | `ANDROID_LOG_INFO` |
| `LOGW(...)` | `ANDROID_LOG_WARN` |
| `LOGE(...)` | `ANDROID_LOG_ERROR` |
| `LOGF(...)` | `ANDROID_LOG_FATAL` |
| `LOGU(...)` | `ANDROID_LOG_UNKNOWN` |
| `LOGDEFAULT(...)` | `ANDROID_LOG_DEFAULT` |
| `LOGS(...)` | `ANDROID_LOG_SILENT` |
| `LOGX(level, ...)` | Supplied Android priority |

`LOGF` selects the FATAL priority but **does not itself abort the process**. `LOGS` uses Android's SILENT priority, so it generally will not appear in normal logcat output.

### 2. Stream syntax — the flexible path

```cpp
// Persistent, per-thread streams. A newline emits the message.
droidlog::infout << "Loaded " << 12 << " textures" << droidlog::endl;
droidlog::errout << "Error code: " << -2 << droidlog::endl;

// Explicit flush sends a partial line.
droidlog::dbgout << "Starting job..." << droidlog::flush;

// Temporary stream: its destructor flushes automatically.
droidlog::cout(ANDROID_LOG_INFO, "Network")
    << "Connected in " << 18 << " ms";
```

Use `droidlog::cout(priority, tag)` for a custom tag **per log statement**. The tag is copied into the stream, so it does not need to outlive the call.

### 3. Format like a pro

```cpp
using namespace droidlog;

LOGD("hex: ", showbase, hex, 48879, dec, noshowbase);     // hex: 0xbeef
LOGD("oct: ", showbase, oct, 64, dec, noshowbase);         // oct: 0100
LOGI("bool: ", boolalpha, true, noboolalpha);             // bool: true
LOGI("fixed: ", fixed, setprecision(2), 3.14159,
     defaultfloat, setprecision(6));                       // fixed: 3.14
LOGD("padded: [", setfill('0'), setw(5), 42,
     setfill(' '), "]");                                  // padded: [00042]
```

Formatting manipulators are **DroidLogger's own** versions, not `std::hex`, `std::setw`, or `std::endl`.

| Manipulator | Behavior |
|---|---|
| `hex`, `dec`, `oct` | Integer base (16, 10, 8) |
| `showbase`, `noshowbase` | Show/hide `0x` or octal `0` prefixes |
| `uppercase`, `nouppercase` | Upper/lowercase hex digits and float output |
| `boolalpha`, `noboolalpha` | `true`/`false` vs. `1`/`0` |
| `fixed`, `scientific`, `defaultfloat` | Floating-point presentation |
| `setprecision(n)` | Floating-point precision; clamped to 0–1000 |
| `setw(n)` | Minimum width of the next value only |
| `setfill(c)` | Left-padding character for width formatting |
| `endl` | Write newline and emit the complete line |
| `flush` | Emit pending content without adding a newline |

**Formatting state persists** on the thread-local stream until you change it, except for `setw`, which is consumed by the next formatted value. The examples above reset options so later logs remain predictable.

### 4. String and enum support

```cpp
#include <string>
#include <string_view>

enum class Mode { Idle = 0, Running = 2 };

std::string user = "Ada";
std::string_view action = "joined";
LOGI(user, " ", action, " (mode ", Mode::Running, ")");
```

String-like types with `data()`/`size()` or `c_str()` are supported if their character pointers are compatible with `const char*`. Ordinary C-string pointers are supported too; a null C-string pointer is displayed as `(null)`.

## 🔍 How messages flow

```text
LOGI(...)  ────┐
               ├──► LogStream ──► newline? ──► Android logcat
stream << ... ┘          │
                         ├──► flush() ────────► Android logcat
                         ├──► 3500-byte chunk► Android logcat
                         └──► destructor ─────► Android logcat
```

Each `LogStream` owns a lazily allocated text buffer. Embedded newlines produce distinct log entries; intentional blank lines are emitted. Very long lines are split into chunks of at most **3,500 bytes**, and an allocation-failure fallback logs smaller chunks directly.

## 🧠 Good to know

- **Android only:** the header includes `<android/log.h>` and requires Android's native logging library.
- **UTF-8 is passed through:** the logger does not validate or transform text encoding.
- **Text, not binary data:** Android's log writer accepts NUL-terminated strings; embedded `\0` bytes cannot be displayed as ordinary message text.
- **No implicit separators:** `LOGI("A", 1)` emits `A1`, not `A 1`.
- **No log filtering API:** DroidLogger forwards entries to Android; visibility depends on platform/logcat filtering. Calls still evaluate their arguments.
- **Stateful streams:** manipulators persist for each thread-local severity stream. For one-off formatting or an isolated tag, consider `droidlog::cout(...)`.
- **Log tags:** an owned tag buffer keeps at most **127 bytes**; longer tags are truncated.
- **Custom stream objects:** `LogStream` is non-copyable and non-movable; do not concurrently share one stream instance between threads.

## 🛠️ Tips

Filter by tag with logcat, for example:

```bash
adb logcat -s MyNativeApp:V
```

Want separate channels? Use distinct tags:

```cpp
droidlog::cout(ANDROID_LOG_DEBUG, "Renderer") << "Frame submitted";
droidlog::cout(ANDROID_LOG_INFO, "Audio") << "Device ready";
```

---

<div align="center">

**Small header. Big debugging energy.** 🚀

Built for native Android code that wants expressive logging, not a heavyweight stream subsystem.

</div>
