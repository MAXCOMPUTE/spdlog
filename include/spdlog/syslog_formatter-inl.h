#pragma once

#ifndef SPDLOG_HEADER_ONLY
    #include <spdlog/syslog_formatter.h>
#endif

#include <spdlog/details/fmt_helper.h>

namespace spdlog {
namespace details {

namespace severity {
    enum severity_enum : int {
        emergency = 0,
        alert = 1,
        critical = 2,
        error = 3,
        warning = 4,
        notice = 5,
        informational = 6,
        debug = 7,
    };
}

} // namespace details

SPDLOG_INLINE syslog_formatter::syslog_formatter(int facility, std::string hostname, std::string appname)
    : facility_(facility),
      hostname_(std::move(hostname)),
      appname_(std::move(appname)),
      pattern_formatter_("%Y-%m-%dT%H:%M:%SZ", pattern_time_type::utc, "") {
    pattern_formatter_.need_localtime();
}

SPDLOG_INLINE std::unique_ptr<formatter> syslog_formatter::clone() const {
    auto cloned = details::make_unique<syslog_formatter>(facility_, hostname_, appname_);
#if defined(__GNUC__) && __GNUC__ < 5
    return std::move(cloned);
#else
    return cloned;
#endif
}

SPDLOG_INLINE void syslog_formatter::format(const details::log_msg &msg, memory_buf_t &dest) {
    details::severity::severity_enum severity;

    switch (msg.level) {
        case level::critical:
            severity = details::severity::critical;

        case level::err:
            severity = details::severity::error;
            break;

        case level::warn:
            severity = details::severity::warning;
            break;

        case level::info:
            severity = details::severity::informational;
            break;

        default:
            severity = details::severity::debug;
            break;
    }

    dest.push_back('<');
    details::fmt_helper::append_int((facility_ * 8) + severity, dest);
    dest.push_back('>');
    dest.push_back('1');

    dest.push_back(' ');
    pattern_formatter_.format(msg, dest);

    dest.push_back(' ');
    if (hostname_.empty()) {
        dest.push_back('-');
    } else {
        details::fmt_helper::append_string_view(hostname_, dest);
    }

    dest.push_back(' ');
    if (appname_.empty()) {
        dest.push_back('-');
    } else {
        details::fmt_helper::append_string_view(appname_, dest);
    }
    
    dest.push_back(' ');
    details::fmt_helper::append_int(details::os::pid(), dest);

    dest.push_back(' ');
    if (msg.logger_name.size() == 0) {
        dest.push_back('-');
    } else {
        details::fmt_helper::append_string_view(msg.logger_name, dest);
    }

    dest.push_back(' ');
    dest.push_back('-'); // nil structured data

    dest.push_back(' ');
    details::fmt_helper::append_string_view(msg.payload, dest);
}

} // namespace spdlog
