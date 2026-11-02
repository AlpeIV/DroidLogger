//
// Created by Alpe on 21/7/2023.
//


//Maybe use #pragma once

#ifndef DROIDLOGGER_H
#define DROIDLOGGER_H



#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>



#include <android/log.h>


#ifndef DROIDLOG_TAG
#define DROIDLOG_TAG ("DroidLog")
#endif




namespace droidlog {

    namespace detail {

        template <typename T, typename = void>
        struct has_char_data_size : std::false_type {};

        template <typename T>
        struct has_char_data_size<T, std::void_t<
                decltype(std::declval<const T&>().data()),
                decltype(std::declval<const T&>().size())>>
                : std::integral_constant<bool,
                        std::is_convertible<decltype(std::declval<const T&>().data()), const char*>::value &&
                        std::is_convertible<decltype(std::declval<const T&>().size()), std::size_t>::value> {};

        template <typename T, typename = void>
        struct has_c_str : std::false_type {};

        template <typename T>
        struct has_c_str<T, std::void_t<decltype(std::declval<const T&>().c_str())>>
                : std::is_convertible<decltype(std::declval<const T&>().c_str()), const char*> {};

    }

    class LogStream {
    public:
        explicit LogStream(android_LogPriority priority, const char* tag = DROIDLOG_TAG)
                : priority_(priority) {

            const char* source = tag ? tag : DROIDLOG_TAG;
            if (!source) source = "";
            std::size_t len = std::strlen(source);
            if (len >= sizeof(tag_)) len = sizeof(tag_) - 1;
            std::memcpy(tag_, source, len);
            tag_[len] = '\0';
        }

        ~LogStream() {
            flush();
            std::free(buffer_);
        }

        LogStream(const LogStream&) = delete;
        LogStream& operator=(const LogStream&) = delete;
        LogStream(LogStream&&) = delete;
        LogStream& operator=(LogStream&&) = delete;

        LogStream& write(const char* data, std::size_t size) {
            if (!data || !size) return *this;
            while (size) {
                const char* newline = static_cast<const char*>(std::memchr(data, '\n', size));
                const std::size_t segment = newline ? static_cast<std::size_t>(newline - data) : size;
                append(data, segment);
                if (!newline) break;
                emit(true); 
                data += segment + 1;
                size -= segment + 1;
            }
            return *this;
        }

        LogStream& flush() {
            emit(false);
            return *this;
        }

        using Manipulator = LogStream& (*)(LogStream&);
        LogStream& operator<<(Manipulator fn) { return fn(*this); }

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
        LogStream& operator<<(bool b) { return *this << (boolalpha_ ? (b ? "true" : "false") : (b ? "1" : "0")); }

        template <typename T, std::enable_if_t<detail::has_char_data_size<T>::value, int> = 0>
        LogStream& operator<<(const T& text) {
            formatted(text.data(), static_cast<std::size_t>(text.size()));
            return *this;
        }
        template <typename T, std::enable_if_t<!detail::has_char_data_size<T>::value && detail::has_c_str<T>::value, int> = 0>
        LogStream& operator<<(const T& text) { return *this << text.c_str(); }

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

        template <typename T, std::enable_if_t<std::is_enum<T>::value, int> = 0>
        LogStream& operator<<(T value) {
            return *this << static_cast<typename std::underlying_type<T>::type>(value);
        }

        LogStream& operator<<(float v)       { return floating(static_cast<long double>(v)); }
        LogStream& operator<<(double v)      { return floating(static_cast<long double>(v)); }
        LogStream& operator<<(long double v) { return floating(v); }

        LogStream& operator<<(const void* ptr) {
            return pointer_value(reinterpret_cast<std::uintptr_t>(ptr));
        }

        template <typename Function,
                std::enable_if_t<std::is_function<Function>::value, int> = 0>
        LogStream& operator<<(Function* fn) {
            return pointer_value(reinterpret_cast<std::uintptr_t>(fn));
        }

        LogStream& operator<<(std::nullptr_t) { return pointer_value(0); }

        void set_base(int base) { base_ = base; }
        void set_boolalpha(bool enabled) { boolalpha_ = enabled; }
        void set_showbase(bool enabled) { showbase_ = enabled; }
        void set_uppercase(bool enabled) { uppercase_ = enabled; }
        void set_precision(int value) { precision_ = value < 0 ? 0 : (value > 1000 ? 1000 : value); }
        void set_width(int value) { width_ = value < 0 ? 0 : value; }
        void set_fill(char value) { fill_ = value; }
        void set_float_style(int style) { float_style_ = style; }

    private:
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

        static constexpr std::size_t kMaxMessageBytes = 3500;
        android_LogPriority priority_;
        char tag_[128]{}; 
        char* buffer_ = nullptr;
        std::size_t pending_ = 0;
        int base_ = 10;
        int precision_ = 6;
        int width_ = 0;
        int float_style_ = 0;
        char fill_ = ' ';
        bool boolalpha_ = false;
        bool showbase_ = false;
        bool uppercase_ = false;

        void emit(bool include_empty) {
            if (!include_empty && pending_ == 0) return;
            if (buffer_) buffer_[pending_] = '\0';
            __android_log_write(static_cast<int>(priority_), tag_, buffer_ ? buffer_ : "");
            pending_ = 0;
        }

        void append(const char* data, std::size_t size) {
            if (!size) return;
            if (!buffer_) {
                buffer_ = static_cast<char*>(std::malloc(kMaxMessageBytes + 1));
                if (!buffer_) {
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

        void formatted(const char* data, std::size_t size) {
            const std::size_t pad = width_ > 0 && static_cast<std::size_t>(width_) > size
                                    ? static_cast<std::size_t>(width_) - size : 0;
            width_ = 0;
            for (std::size_t i = 0; i < pad; ++i) append(&fill_, 1);
            write(data, size);
        }

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

    struct SetWidth { int value; };
    struct SetPrecision { int value; };
    struct SetFill { char value; };
    inline SetWidth setw(int n) { return {n}; }
    inline SetPrecision setprecision(int n) { return {n}; }
    inline SetFill setfill(char c) { return {c}; }
    inline LogStream& operator<<(LogStream& s, SetWidth x) { s.set_width(x.value); return s; }
    inline LogStream& operator<<(LogStream& s, SetPrecision x) { s.set_precision(x.value); return s; }
    inline LogStream& operator<<(LogStream& s, SetFill x) { s.set_fill(x.value); return s; }

    inline thread_local LogStream unknownout{ANDROID_LOG_UNKNOWN};
    inline thread_local LogStream defaultout{ANDROID_LOG_DEFAULT};
    inline thread_local LogStream verbout{ANDROID_LOG_VERBOSE};
    inline thread_local LogStream dbgout{ANDROID_LOG_DEBUG};
    inline thread_local LogStream infout{ANDROID_LOG_INFO};
    inline thread_local LogStream warnout{ANDROID_LOG_WARN};
    inline thread_local LogStream errout{ANDROID_LOG_ERROR};
    inline thread_local LogStream fatalout{ANDROID_LOG_FATAL};
    inline thread_local LogStream silentout{ANDROID_LOG_SILENT};

    inline LogStream cout(android_LogPriority priority, const char* tag = DROIDLOG_TAG) {
        return LogStream(priority, tag);
    }

}



#include <utility>



//

namespace droidlog::detail {

    template<typename Stream, typename... Args>
    inline void log(Stream&& stream, Args&&... args) {
        ((stream << std::forward<Args>(args)), ...);
        stream << droidlog::endl;
    }
}


#define LOGV(...) ::droidlog::detail::log(::droidlog::verbout, __VA_ARGS__)
#define LOGD(...) ::droidlog::detail::log(::droidlog::dbgout,  __VA_ARGS__)
#define LOGI(...) ::droidlog::detail::log(::droidlog::infout, __VA_ARGS__)
#define LOGW(...) ::droidlog::detail::log(::droidlog::warnout, __VA_ARGS__)
#define LOGE(...) ::droidlog::detail::log(::droidlog::errout, __VA_ARGS__)
#define LOGF(...) ::droidlog::detail::log(::droidlog::fatalout, __VA_ARGS__)

#define LOGU(...) ::droidlog::detail::log(::droidlog::unknownout, __VA_ARGS__)
#define LOGDEFAULT(...) ::droidlog::detail::log(::droidlog::defaultout, __VA_ARGS__)
#define LOGS(...) ::droidlog::detail::log(::droidlog::silentout, __VA_ARGS__)

#define LOGX(level, ...) ::droidlog::detail::log(::droidlog::cout(level), __VA_ARGS__)



#endif //DROIDLOGGER_H
