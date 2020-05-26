/*
 * This content is released under the MIT License as specified in https://raw.githubusercontent.com/gabime/spdlog/master/LICENSE
 */
#include "includes.h"

TEST_CASE("daily_logger with dateonly calculator", "[daily_logger]")
{
    using sink_type = spdlog::sinks::daily_file_sink<std::mutex, spdlog::sinks::daily_filename_calculator>;

    prepare_logdir();

    // calculate filename (time based)
    std::string basename = "test_logs/daily_dateonly";
    std::tm tm = spdlog::details::os::localtime();
    spdlog::memory_buf_t w;
    fmt::format_to(w, "{}_{:04d}-{:02d}-{:02d}", basename, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);

    auto logger = spdlog::create<sink_type>("logger", basename, 0, 0);
    for (int i = 0; i < 10; ++i)
    {

        logger->info("Test message {}", i);
    }
    logger->flush();

    auto filename = fmt::to_string(w);
    require_message_count(filename, 10);
}

struct custom_daily_file_name_calculator
{
    static spdlog::filename_t calc_filename(const spdlog::filename_t &basename, const tm &now_tm)
    {
        spdlog::memory_buf_t w;
        fmt::format_to(w, "{}{:04d}{:02d}{:02d}", basename, now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday);
        return fmt::to_string(w);
    }

    static std::map<spdlog::filename_t, spdlog::filename_t> calc_dates_to_filenames(const spdlog::filename_t &base_filename)
    {
        return {};
    }
};

TEST_CASE("daily_logger with custom calculator", "[daily_logger]")
{
    using sink_type = spdlog::sinks::daily_file_sink<std::mutex, custom_daily_file_name_calculator>;

    prepare_logdir();

    // calculate filename (time based)
    std::string basename = "test_logs/daily_dateonly";
    std::tm tm = spdlog::details::os::localtime();
    spdlog::memory_buf_t w;
    fmt::format_to(w, "{}{:04d}{:02d}{:02d}", basename, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);

    auto logger = spdlog::create<sink_type>("logger", basename, 0, 0);
    for (int i = 0; i < 10; ++i)
    {
        logger->info("Test message {}", i);
    }

    logger->flush();

    auto filename = fmt::to_string(w);
    require_message_count(filename, 10);
}

/*
 * File name calculations
 */

TEST_CASE("rotating_file_sink::calc_filename1", "[rotating_file_sink]]")
{
    auto filename = spdlog::sinks::rotating_file_sink_st::calc_filename("rotated.txt", 3);
    REQUIRE(filename == "rotated.3.txt");
}

TEST_CASE("rotating_file_sink::calc_filename2", "[rotating_file_sink]]")
{
    auto filename = spdlog::sinks::rotating_file_sink_st::calc_filename("rotated", 3);
    REQUIRE(filename == "rotated.3");
}

TEST_CASE("rotating_file_sink::calc_filename3", "[rotating_file_sink]]")
{
    auto filename = spdlog::sinks::rotating_file_sink_st::calc_filename("rotated.txt", 0);
    REQUIRE(filename == "rotated.txt");
}

// regex supported only from gcc 4.9 and above
#if defined(_MSC_VER) || !(__GNUC__ <= 4 && __GNUC_MINOR__ < 9)

#include <regex>

TEST_CASE("daily_file_sink::daily_filename_calculator", "[daily_file_sink]]")
{
    // daily_YYYY-MM-DD_hh-mm.txt
    auto filename = spdlog::sinks::daily_filename_calculator::calc_filename("daily.txt", spdlog::details::os::localtime());
    // date regex based on https://www.regular-expressions.info/dates.html
    std::regex re(R"(^daily_(19|20)\d\d-(0[1-9]|1[012])-(0[1-9]|[12][0-9]|3[01])\.txt$)");
    std::smatch match;
    REQUIRE(std::regex_match(filename, match, re));
}
#endif

TEST_CASE("daily_file_sink::daily_filename_calculator::extract_date_suffix", "[daily_file_sink]]")
{
    auto now = spdlog::details::os::localtime();

    auto basename = "daily.txt";

    auto filename = spdlog::sinks::daily_filename_calculator::calc_filename(basename, now);

    auto date_suffix = spdlog::sinks::daily_filename_calculator::extract_date_suffix(basename, filename);

    auto expected_suffix = fmt::format(SPDLOG_FILENAME_T("{:04d}-{:02d}-{:02d}"), now.tm_year + 1900, now.tm_mon + 1, now.tm_mday);

    REQUIRE(date_suffix == expected_suffix);
}

TEST_CASE("daily_file_sink::daily_filename_calculator::extract_date_suffix2", "[daily_file_sink]]")
{
    auto date_suffix = spdlog::sinks::daily_filename_calculator::extract_date_suffix("basename", "filename");

    REQUIRE(date_suffix == "");
}

/* Test removal of old files */
static spdlog::details::log_msg create_msg(std::chrono::seconds offset)
{
    using spdlog::log_clock;
    spdlog::details::log_msg msg{"test", spdlog::level::info, "Hello Message"};
    msg.time = log_clock::now() + offset;
    return msg;
}

static void test_rotate(int days_to_run, uint16_t max_days, uint16_t expected_n_files)
{
    using spdlog::log_clock;
    using spdlog::details::log_msg;
    using spdlog::sinks::daily_file_sink_st;

    prepare_logdir();

    std::string basename = "test_logs/daily_rotate.txt";
    daily_file_sink_st sink{basename, 2, 30, true, max_days};

    // simulate messages with 24 intervals

    for (int i = 0; i < days_to_run; i++)
    {
        auto offset = std::chrono::seconds{24 * 3600 * i};
        sink.log(create_msg(offset));
    }

    REQUIRE(count_files("test_logs") == static_cast<size_t>(expected_n_files));
}

TEST_CASE("daily_logger rotate", "[daily_file_sink]")
{
    int days_to_run = 1;
    test_rotate(days_to_run, 0, 1);
    test_rotate(days_to_run, 1, 1);
    test_rotate(days_to_run, 3, 1);
    test_rotate(days_to_run, 10, 1);

    days_to_run = 10;
    test_rotate(days_to_run, 0, 10);
    test_rotate(days_to_run, 1, 1);
    test_rotate(days_to_run, 3, 3);
    test_rotate(days_to_run, 9, 9);
    test_rotate(days_to_run, 10, 10);
    test_rotate(days_to_run, 11, 10);
    test_rotate(days_to_run, 20, 10);
}

TEST_CASE("daily_logger should delete oldest file on init", "[daily_file_sink]")
{
    using spdlog::log_clock;
    using spdlog::details::log_msg;
    using spdlog::sinks::daily_file_sink_st;

    prepare_logdir();

    std::string basename = "test_logs/daily_rotate.txt";

    daily_file_sink_st sink{basename, 2, 30, false, 8, true};

    // simulate messages with 24 intervals. create more than max files of second sink below
    for (int i = 0; i < 8; i++)
    {
        auto offset = std::chrono::seconds{24 * 3600 * i};
        sink.log(create_msg(offset));
    }

    REQUIRE(count_files("test_logs") == static_cast<size_t>(8));

    // This will create a 9th file in logdir, therefore the ctor wil delete the oldest two files.
    auto initial_file_tp = log_clock::now() + std::chrono::seconds{24 * 3600 * 8};

    // Second sink should load existing files in the logdir and delete the oldest because the max files count is 7
    daily_file_sink_st sink2{basename, 2, 30, false, 7, true, initial_file_tp};

    REQUIRE(count_files("test_logs") == static_cast<size_t>(7));
}