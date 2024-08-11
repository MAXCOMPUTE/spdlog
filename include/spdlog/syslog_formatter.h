#pragma once

#include <spdlog/common.h>
#include <spdlog/formatter.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/details/log_msg.h>

#include <memory>
#include <string>

namespace spdlog {

namespace facility {
    enum facility_enum : int {
        kernel = 0,
        user = 1,
        mail = 2,
        daemons = 3,
        security = 4,
        syslogd = 5,
        printer = 6,
        network = 7,
        uucp = 8,
        clock = 9,
        security2 = 10,
        ftp = 11,
        ntp = 12,
        audit = 13,
        alert = 14,
        clock2 = 15,
        local0 = 16,
        local1 = 17,
        local2 = 18,
        local3 = 19,
        local4 = 20,
        local5 = 21,
        local6 = 22,
        local7 = 23
    };
}

class SPDLOG_API syslog_formatter : public formatter {
public:
    explicit syslog_formatter(int facility, std::string hostname, std::string appname);

    syslog_formatter(const syslog_formatter &other) = delete;
    syslog_formatter &operator=(const syslog_formatter &other) = delete;

    std::unique_ptr<formatter> clone() const override;
    void format(const details::log_msg &msg, memory_buf_t &dest) override;

private:
    int facility_;
    std::string hostname_;
    std::string appname_;
    spdlog::pattern_formatter pattern_formatter_;
};

} // namespace spdlog

#ifdef SPDLOG_HEADER_ONLY
    #include "syslog_formatter-inl.h"
#endif
