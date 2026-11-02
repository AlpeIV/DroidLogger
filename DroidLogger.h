/**
 * @file DroidLogger.h
 * @brief Lightweight, header-only Android logging with C++-style streaming.
 *
 * Created by Alpe on 21/7/2023.
 *
 * Requirements: C++17, the Android NDK, and Android's `log` library.
 * Include this header directly; no .cpp implementation is needed.
 *
 * Quick start:
 *   #define DROIDLOG_TAG "MyApp"  // Optional; define before the include.
 *   #include "DroidLogger.h"
 *   LOGI("Connected client #", 42);
 *   droidlog::dbgout << "Value: " << 3.14 << droidlog::endl;
 *
 * This library does not depend on std::ostream or std::iostream.
 */

#ifndef DROIDLOGGER_H
#define DROIDLOGGER_H


// Small C/C++ utility headers only; no iostream or streambuf machinery.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>


// The native Android logcat API (__android_log_write and priority values).
#include <android/log.h>


// Default logcat tag; override with #define DROIDLOG_TAG "..." before inclusion.
#ifndef DROIDLOG_TAG
#define DROIDLOG_TAG ("DroidLog")
#endif





// Inline variables, if constexpr, and fold expressions require C++17.
namespace droidlog {

    namespace detail {

        // Detection idiom for string-like values with contiguous character data.
        // This supports std::string/string_view without including their headers.

        template <typename T, typename = void>
        struct has_char_data_size : std::false_type {};

        template <typename T>
        struct has_char_data_size<T, std::void_t<
                decltype(std::declval<const T&>().data()),
                decltype(std::declval<const T&>().size())>>
                : std::integral_constant<bool,
                        std::is_convertible<decltype(std::declval<const T&>().data()), const char*>::value &&
                        std::is_convertible<decltype(std::declval<const T&>().size()), std::size_t>::value> {};

        // Fallback for string-like values exposing c_str() but not data()/size().
        template <typename T, typename = void>
        struct has_c_str : std::false_type {};

        template <typename T>
        struct has_c_str<T, std::void_t<decltype(std::declval<const T&>().c_str())>>
                : std::is_convertible<decltype(std::declval<const T&>().c_str()), const char*> {};

    }

    /**
     * @brief Non-copyable log stream that emits messages to Android logcat.
     *
     * Text is buffered until a newline, explicit flush, buffer-size limit, or
     * destruction. A dedicated stream instance should not be shared between
     * threads; the predefined severity streams are thread_local.
     *
     * Formatting manipulators are intentionally lightweight and independent
     * of std::ios_base. Most formatting state persists until changed, except
     * width (setw), which only affects the next formatted insertion.
     */
    class LogStream {
    public:
        /** Create a stream for a priority and optional logcat tag. */
        explicit LogStream(android_LogPriority priority, const char* tag = DROIDLOG_TAG)
                : priority_(priority) {

            // Own the tag rather than holding a possibly short-lived pointer.
            const char* source = tag ? tag : DROIDLOG_TAG;
            if (!source) source = "";
            std::size_t len = std::strlen(source);
            if (len >= sizeof(tag_)) len = sizeof(tag_) - 1;
            std::memcpy(tag_, source, len);
            tag_[len] = '\0';
        }

        // An ephemeral stream flushes its last (unterminated) line here.
        ~LogStream() {
            flush();
            std::free(buffer_);
        }

        // Neither the owned buffer nor formatting state is transferable.
        LogStream(const LogStream&) = delete;
        LogStream& operator=(const LogStream&) = delete;
        LogStream(LogStream&&) = delete;
        LogStream& operator=(LogStream&&) = delete;

        /**
         * Append exactly `size` bytes of text, splitting at newlines.
         * Each newline emits a separate log entry (including blank lines).
         * Bytes after the final newline remain pending until flushed.
         */
        LogStream& write(const char* data, std::size_t size) {
            if (!data || !size) return *this;
            while (size) {
                // Search within the supplied byte count, not via strlen().
                const char* newline = static_cast<const char*>(std::memchr(data, '\n', size));
                const std::size_t segment = newline ? static_cast<std::size_t>(newline - data) : size;
                append(data, segment);
                if (!newline) break;
                emit(true); // Preserve intentional empty lines.
                data += segment + 1;
                size -= segment + 1;
            }
            return *this;
        }

        /** Emit buffered content immediately, unless the buffer is empty. */
        LogStream& flush() {
            emit(false);
            return *this;
        }

        // Enables stream syntax such as `stream << droidlog::hex`.
        using Manipulator = LogStream& (*)(LogStream&);
        LogStream& operator<<(Manipulator fn) { return fn(*this); }

        // Null C-string pointers are printed as "(null)".
        LogStream& operator<<(const char* text) {
            if (!text) text = "(null)";
            formatted(text, std::strlen(text));
            return *this;
        }
        LogStream& operator<<(char* text) { return *this << static_cast<const char*>(text); }
        LogStream& operator<<(char ch) { formatted(&ch, 1); return *this; }
        LogStream& operator<<(signed char ch) { return *this << static_cast<char>(ch); }
        LogStream& operator<<(unsigned char ch) { return *this << static_cast<char>(ch); }
        LogStream& operator<<(const signed char* s) {
            return *this << reinterpret_cast<const char*>(s);
        }
        LogStream& operator<<(const unsigned char* s) {
            return *this << reinterpret_cast<const char*>(s);
        }
        // Boolean output defaults to 0/1, or true/false after boolalpha.
        LogStream& operator<<(bool b) { return *this << (boolalpha_ ? (b ? "true" : "false") : (b ? "1" : "0")); }

        // Accepts std::string, std::string_view, and similar char-based string types
        // without including or depending on their headers.
        template <typename T, std::enable_if_t<detail::has_char_data_size<T>::value, int> = 0>
        LogStream& operator<<(const T& text) {
            formatted(text.data(), static_cast<std::size_t>(text.size()));
            return *this;
        }
        // Use c_str() only when the sized character-data overload is absent.
        template <typename T, std::enable_if_t<!detail::has_char_data_size<T>::value && detail::has_c_str<T>::value, int> = 0>
        LogStream& operator<<(const T& text) { return *this << text.c_str(); }

        // Integer conversion is performed locally: no iostream allocations.
        // Signed negatives receive a '-' only in decimal; other bases show
        // their unsigned representation, matching common stream behavior.
        template <typename T, std::enable_if_t<
                std::is_integral<T>::value && !std::is_same<T, bool>::value &&
                !std::is_same<T, char>::value && !std::is_same<T, signed char>::value &&
                !std::is_same<T, unsigned char>::value, int> = 0>
        LogStream& operator<<(T n) {
            using U = typename std::make_unsigned<T>::type;
            U number = static_cast<U>(n);
            bool negative = false;
            if constexpr (std::is_signed<T>::value) {
                negative = (base_ == 10 && n < 0);
                if (negative) number = static_cast<U>(U{0} - number);
            }
            unsigned long long u = static_cast<unsigned long long>(number);
            const char* digits = uppercase_ ? "0123456789ABCDEF" : "0123456789abcdef";
            // Collect least-significant digits first, then reverse below.
            char reversed[65];
            std::size_t count = 0;
            do {
                reversed[count++] = digits[u % static_cast<unsigned>(base_)];
                u /= static_cast<unsigned>(base_);
            } while (u);

            char result[70];
            std::size_t pos = 0;
            if (negative) result[pos++] = '-';
            if (showbase_ && number != 0 && base_ == 16) {
                result[pos++] = '0'; result[pos++] = uppercase_ ? 'X' : 'x';
            } else if (showbase_ && number != 0 && base_ == 8) {
                result[pos++] = '0';
            }
            while (count) result[pos++] = reversed[--count];
            formatted(result, pos);
            return *this;
        }

        // Enums print as their underlying integral values.
        template <typename T, std::enable_if_t<std::is_enum<T>::value, int> = 0>
        LogStream& operator<<(T value) {
            return *this << static_cast<typename std::underlying_type<T>::type>(value);
        }

        // Floating-point output is delegated to snprintf (see floating()).
        LogStream& operator<<(float v)       { return floating(static_cast<long double>(v)); }
        LogStream& operator<<(double v)      { return floating(static_cast<long double>(v)); }
        LogStream& operator<<(long double v) { return floating(v); }

        // Object pointers are logged as hexadecimal addresses.
        LogStream& operator<<(const void* ptr) {
            return pointer_value(reinterpret_cast<std::uintptr_t>(ptr));
        }

        // Function pointers do not implicitly convert to const void*. Without this
        // overload, overload resolution selects operator<<(bool), printing 1 or true.
        // Android NDK targets support representing function addresses as uintptr_t.
        template <typename Function,
                std::enable_if_t<std::is_function<Function>::value, int> = 0>
        LogStream& operator<<(Function* fn) {
            return pointer_value(reinterpret_cast<std::uintptr_t>(fn));
        }

        LogStream& operator<<(std::nullptr_t) { return pointer_value(0); }

        // Backing setters for the custom manipulators defined below.
        // set_base expects 8, 10, or 16 (as provided by oct/dec/hex).
        // Precision is clamped to [0, 1000] to bound formatting requests.
        void set_base(int base) { base_ = base; }
        void set_boolalpha(bool enabled) { boolalpha_ = enabled; }
        void set_showbase(bool enabled) { showbase_ = enabled; }
        void set_uppercase(bool enabled) { uppercase_ = enabled; }
        void set_precision(int value) { precision_ = value < 0 ? 0 : (value > 1000 ? 1000 : value); }
        void set_width(int value) { width_ = value < 0 ? 0 : value; }
        void set_fill(char value) { fill_ = value; }
        void set_float_style(int style) { float_style_ = style; }

    private:
        // Temporarily force hex + prefix, restoring the caller's base flags.
        LogStream& pointer_value(std::uintptr_t address) {
            if (!address) return *this << "0x0";
            const int old_base = base_;
            const bool old_showbase = showbase_;
            base_ = 16;
            showbase_ = true;
            *this << address;
            base_ = old_base;
            showbase_ = old_showbase;
            return *this;
        }

        // Keep each logcat entry comfortably below Android's normal size limit.
        // Longer lines are split into multiple entries without truncating them.
        static constexpr std::size_t kMaxMessageBytes = 3500;
        android_LogPriority priority_;
        char tag_[128]{}; // Owned, null-terminated; up to 127 tag bytes.
        char* buffer_ = nullptr; // Lazily allocated; room for the trailing NUL.
        std::size_t pending_ = 0; // Number of bytes not yet sent to logcat.
        int base_ = 10;
        int precision_ = 6;
        int width_ = 0;
        int float_style_ = 0; // 0 = general, 1 = fixed, 2 = scientific
        char fill_ = ' ';
        bool boolalpha_ = false;
        bool showbase_ = false;
        bool uppercase_ = false;

        // Android's logging API consumes a NUL-terminated message.
        // include_empty is true for explicit blank lines, false for flush().
        void emit(bool include_empty) {
            if (!include_empty && pending_ == 0) return;
            if (buffer_) buffer_[pending_] = '\0';
            __android_log_write(static_cast<int>(priority_), tag_, buffer_ ? buffer_ : "");
            pending_ = 0;
        }

        // Append a chunk to the per-stream buffer, emitting full chunks.
        // On allocation failure, log small chunks directly as a fallback.
        void append(const char* data, std::size_t size) {
            if (!size) return;
            if (!buffer_) {
                buffer_ = static_cast<char*>(std::malloc(kMaxMessageBytes + 1));
                if (!buffer_) { // Emergency fallback if allocation fails.
                    char chunk[256];
                    while (size) {
                        const std::size_t n = size < sizeof(chunk) ? size : sizeof(chunk) - 1;
                        std::memcpy(chunk, data, n);
                        chunk[n] = '\0';
                        __android_log_write(static_cast<int>(priority_), tag_, chunk);
                        data += n;
                        size -= n;
                    }
                    return;
                }
            }
            while (size) {
                const std::size_t capacity = kMaxMessageBytes - pending_;
                const std::size_t n = size < capacity ? size : capacity;
                std::memcpy(buffer_ + pending_, data, n);
                pending_ += n;
                data += n;
                size -= n;
                if (pending_ == kMaxMessageBytes) emit(false);
            }
        }

        // Apply pending minimum field width and fill, then write the value.
        // Width is consumed even when no padding is necessary.
        void formatted(const char* data, std::size_t size) {
            const std::size_t pad = width_ > 0 && static_cast<std::size_t>(width_) > size
                                    ? static_cast<std::size_t>(width_) - size : 0;
            width_ = 0; // Like std::setw: applies only to the next insertion.
            for (std::size_t i = 0; i < pad; ++i) append(&fill_, 1);
            write(data, size);
        }

        // Format general/fixed/scientific floats using printf precision rules.
        // Most values fit on the stack; large formatted results use the heap.
        LogStream& floating(long double value) {
            const char* fmt = nullptr;
            if (float_style_ == 1) fmt = uppercase_ ? "%.*LF" : "%.*Lf";
            else if (float_style_ == 2) fmt = uppercase_ ? "%.*LE" : "%.*Le";
            else fmt = uppercase_ ? "%.*LG" : "%.*Lg";

            char local[256];
            const int size = std::snprintf(local, sizeof(local), fmt, precision_, value);
            if (size < 0) return *this;
            if (static_cast<std::size_t>(size) < sizeof(local)) {
                formatted(local, static_cast<std::size_t>(size));
            } else {
                char* large = static_cast<char*>(std::malloc(static_cast<std::size_t>(size) + 1));
                if (large) {
                    std::snprintf(large, static_cast<std::size_t>(size) + 1, fmt, precision_, value);
                    formatted(large, static_cast<std::size_t>(size));
                    std::free(large);
                } else {
                    formatted(local, sizeof(local) - 1);
                }
            }
            return *this;
        }
    };

    // Custom manipulators: similar syntax to std::hex / std::setw, but these
    // act on LogStream only and never instantiate an iostream.
    inline LogStream& endl(LogStream& s)        { s.write("\n", 1); return s; }
    inline LogStream& flush(LogStream& s)       { return s.flush(); }
    inline LogStream& hex(LogStream& s)         { s.set_base(16); return s; }
    inline LogStream& dec(LogStream& s)         { s.set_base(10); return s; }
    inline LogStream& oct(LogStream& s)         { s.set_base(8); return s; }
    inline LogStream& boolalpha(LogStream& s)   { s.set_boolalpha(true); return s; }
    inline LogStream& noboolalpha(LogStream& s) { s.set_boolalpha(false); return s; }
    inline LogStream& showbase(LogStream& s)    { s.set_showbase(true); return s; }
    inline LogStream& noshowbase(LogStream& s)  { s.set_showbase(false); return s; }
    inline LogStream& uppercase(LogStream& s)  { s.set_uppercase(true); return s; }
    inline LogStream& nouppercase(LogStream& s) { s.set_uppercase(false); return s; }
    inline LogStream& fixed(LogStream& s)       { s.set_float_style(1); return s; }
    inline LogStream& scientific(LogStream& s)  { s.set_float_style(2); return s; }
    inline LogStream& defaultfloat(LogStream& s){ s.set_float_style(0); return s; }

    // Value-carrying manipulators, used as `stream << setw(8)` etc.
    struct SetWidth { int value; };
    struct SetPrecision { int value; };
    struct SetFill { char value; };
    inline SetWidth setw(int n) { return {n}; }
    inline SetPrecision setprecision(int n) { return {n}; }
    inline SetFill setfill(char c) { return {c}; }
    inline LogStream& operator<<(LogStream& s, SetWidth x) { s.set_width(x.value); return s; }
    inline LogStream& operator<<(LogStream& s, SetPrecision x) { s.set_precision(x.value); return s; }
    inline LogStream& operator<<(LogStream& s, SetFill x) { s.set_fill(x.value); return s; }

    // One persistent stream per priority *per thread*: no shared line buffers.
    // Formatting flags also remain local to each thread and severity stream.
    inline thread_local LogStream unknownout{ANDROID_LOG_UNKNOWN};
    inline thread_local LogStream defaultout{ANDROID_LOG_DEFAULT};
    inline thread_local LogStream verbout{ANDROID_LOG_VERBOSE};
    inline thread_local LogStream dbgout{ANDROID_LOG_DEBUG};
    inline thread_local LogStream infout{ANDROID_LOG_INFO};
    inline thread_local LogStream warnout{ANDROID_LOG_WARN};
    inline thread_local LogStream errout{ANDROID_LOG_ERROR};
    inline thread_local LogStream fatalout{ANDROID_LOG_FATAL};
    inline thread_local LogStream silentout{ANDROID_LOG_SILENT};

    /**
     * Create a temporary stream with a custom priority and optional tag.
     * Its destructor flushes at the end of the full expression:
     * `droidlog::cout(ANDROID_LOG_INFO, "Networking") << "Connected";`
     */
    inline LogStream cout(android_LogPriority priority, const char* tag = DROIDLOG_TAG) {
        return LogStream(priority, tag);
    }

} // namespace droidlog


// Shared implementation behind LOGV/LOGD/... and LOGX macros.
namespace droidlog::detail {

    // Concatenate all arguments with no implicit spaces, then terminate the
    // line. LOGX can pass a temporary; the other macros use thread-local streams.
    template<typename Stream, typename... Args>
    inline void log(Stream&& stream, Args&&... args) {
        ((stream << std::forward<Args>(args)), ...);
        stream << droidlog::endl;
    }
}  // namespace droidlog::detail


// Convenient printf-free, stream-style logging macros.
// Example: LOGI("answer=", 42);  // Emits "answer=42" with INFO priority.
// Arguments are concatenated exactly; add separators yourself.
// Standard Android logging levels:
#define LOGV(...) ::droidlog::detail::log(::droidlog::verbout, __VA_ARGS__)
#define LOGD(...) ::droidlog::detail::log(::droidlog::dbgout,  __VA_ARGS__)
#define LOGI(...) ::droidlog::detail::log(::droidlog::infout, __VA_ARGS__)
#define LOGW(...) ::droidlog::detail::log(::droidlog::warnout, __VA_ARGS__)
#define LOGE(...) ::droidlog::detail::log(::droidlog::errout, __VA_ARGS__)
#define LOGF(...) ::droidlog::detail::log(::droidlog::fatalout, __VA_ARGS__)

// Other NDK priorities (DEFAULT/UNKNOWN/SILENT may have platform-specific
// filtering behavior; SILENT is generally not displayed by logcat).
#define LOGU(...) ::droidlog::detail::log(::droidlog::unknownout, __VA_ARGS__)
#define LOGDEFAULT(...) ::droidlog::detail::log(::droidlog::defaultout, __VA_ARGS__)
#define LOGS(...) ::droidlog::detail::log(::droidlog::silentout, __VA_ARGS__)

// Select the priority per call; uses a temporary stream that auto-flushes.
#define LOGX(level, ...) ::droidlog::detail::log(::droidlog::cout(level), __VA_ARGS__)



#endif // DROIDLOGGER_H

