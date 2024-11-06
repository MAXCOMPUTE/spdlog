// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#pragma once

#ifdef __ANDROID__

    #include <spdlog/details/fmt_helper.h>
    #include <spdlog/details/null_mutex.h>
    #include <spdlog/details/os.h>
    #include <spdlog/details/synchronous_factory.h>
    #include <spdlog/sinks/base_sink.h>

    #include <android/log.h>
    #include <chrono>
    #include <mutex>
    #include <string>
    #include <thread>
    #include <type_traits>

    #if !defined(SPDLOG_ANDROID_RETRIES)
        #define SPDLOG_ANDROID_RETRIES 2
    #endif

    #if __ANDROID_API__ >= 30
        #define SPDLOG_ANDROID_ENABLE_LOG_WRITE
    #endif

namespace spdlog {
namespace sinks {

/*
 * Android sink
 * (logging using __android_log_write or __android_log_buf_write depending on the specified
 * BufferID)
 */
template <typename Mutex, int BufferID = log_id::LOG_ID_MAIN>
class android_sink final : public base_sink<Mutex> {
public:
    explicit android_sink(std::string tag = "spdlog", bool use_raw_msg = false)
        : tag_(std::move(tag)),
          use_raw_msg_(use_raw_msg) {
        // Available since API level 30
    #ifdef SPDLOG_ANDROID_ENABLE_LOG_WRITE
        write_message_ = (write_log_message_t)dlsym(nullptr, "__android_log_write_log_message");
    #endif
    }

protected:
    void sink_it_(const details::log_msg &msg) override {
        const android_LogPriority priority = convert_to_android_(msg.level);
        memory_buf_t formatted;
        if (use_raw_msg_) {
            details::fmt_helper::append_string_view(msg.payload, formatted);
        } else {
            base_sink<Mutex>::formatter_->format(msg, formatted);
        }
        formatted.push_back('\0');
        const char *msg_output = formatted.data();

    #ifdef SPDLOG_ANDROID_ENABLE_LOG_WRITE
        if (write_message_) {
            __android_log_message logMessage{};
            logMessage.struct_size = sizeof(logMessage);
            logMessage.buffer_id = BufferID;
            logMessage.priority = priority;
            logMessage.tag = tag_.c_str();
            logMessage.file = msg.source.filename;
            logMessage.line = static_cast<uint32_t>(msg.source.line);
            logMessage.message = msg_output;
            write_message_(&logMessage);
            return;
        }
    #endif

        // See system/core/liblog/logger_write.c for explanation of return value
        int ret = android_log(priority, tag_.c_str(), msg_output);
        if (ret == -EPERM) {
            return;  // !__android_log_is_loggable
        }
        int retry_count = 0;
        while ((ret == -11 /*EAGAIN*/) && (retry_count < SPDLOG_ANDROID_RETRIES)) {
            details::os::sleep_for_millis(5);
            ret = android_log(priority, tag_.c_str(), msg_output);
            retry_count++;
        }

        if (ret < 0) {
            throw_spdlog_ex("logging to Android failed", ret);
        }
    }

    void flush_() override {}

private:
    // There might be liblog versions used, that do not support __android_log_buf_write. So we only
    // compile and link against
    // __android_log_buf_write, if user explicitly provides a non-default log buffer. Otherwise,
    // when using the default log buffer, always log via __android_log_write.
    template <int ID = BufferID>
    typename std::enable_if<ID == static_cast<int>(log_id::LOG_ID_MAIN), int>::type android_log(
        int prio, const char *tag, const char *text) {
        return __android_log_write(prio, tag, text);
    }

    template <int ID = BufferID>
    typename std::enable_if<ID != static_cast<int>(log_id::LOG_ID_MAIN), int>::type android_log(
        int prio, const char *tag, const char *text) {
        return __android_log_buf_write(ID, prio, tag, text);
    }

    static android_LogPriority convert_to_android_(spdlog::level::level_enum level) {
        switch (level) {
            case spdlog::level::trace:
                return ANDROID_LOG_VERBOSE;
            case spdlog::level::debug:
                return ANDROID_LOG_DEBUG;
            case spdlog::level::info:
                return ANDROID_LOG_INFO;
            case spdlog::level::warn:
                return ANDROID_LOG_WARN;
            case spdlog::level::err:
                return ANDROID_LOG_ERROR;
            case spdlog::level::critical:
                return ANDROID_LOG_FATAL;
            default:
                return ANDROID_LOG_DEFAULT;
        }
    }

    #ifdef SPDLOG_ANDROID_ENABLE_LOG_WRITE
    typedef void (*write_log_message_t)(struct __android_log_message *log_message);
    #endif

    std::string tag_;
    bool use_raw_msg_;
    #ifdef SPDLOG_ANDROID_ENABLE_LOG_WRITE
    write_log_message_t write_message_;
    #endif
};

using android_sink_mt = android_sink<std::mutex>;
using android_sink_st = android_sink<details::null_mutex>;

template <int BufferId = log_id::LOG_ID_MAIN>
using android_sink_buf_mt = android_sink<std::mutex, BufferId>;
template <int BufferId = log_id::LOG_ID_MAIN>
using android_sink_buf_st = android_sink<details::null_mutex, BufferId>;

}  // namespace sinks

// Create and register android syslog logger

template <typename Factory = spdlog::synchronous_factory>
inline std::shared_ptr<logger> android_logger_mt(const std::string &logger_name,
                                                 const std::string &tag = "spdlog") {
    return Factory::template create<sinks::android_sink_mt>(logger_name, tag);
}

template <typename Factory = spdlog::synchronous_factory>
inline std::shared_ptr<logger> android_logger_st(const std::string &logger_name,
                                                 const std::string &tag = "spdlog") {
    return Factory::template create<sinks::android_sink_st>(logger_name, tag);
}

}  // namespace spdlog

#endif  // __ANDROID__
