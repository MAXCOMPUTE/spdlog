// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#pragma once

#include <spdlog/common.h>
#include <spdlog/details/file_helper.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/details/os.h>
#include <spdlog/details/circular_q.h>
#include <spdlog/details/synchronous_factory.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <map>

namespace spdlog {
namespace sinks {

/*
 * Generator of daily log file names in format basename.YYYY-MM-DD.ext
 */
struct daily_filename_calculator
{
    // Create filename for the form basename_YYYY-MM-DD
    static filename_t calc_filename(const filename_t &filename, const tm &now_tm)
    {
        filename_t basename, ext;
        std::tie(basename, ext) = details::file_helper::split_by_extension(filename);
        return fmt::format(
            SPDLOG_FILENAME_T("{}{}{:04d}-{:02d}-{:02d}{}"), basename, filename_prefix_symbol(), now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday, ext);
    }

    // Extract the YYYY-MM-DD suffix from the file name that was based on base_filename and calculated using calc_filename.
    static filename_t extract_date_suffix(const filename_t &base_filename, const filename_t &filename)
    {
        const filename_t base_filename_no_ext = std::get<0>(details::file_helper::split_by_extension(base_filename));

        const filename_t prefix = base_filename_no_ext + filename_prefix_symbol();

        const bool starts_with_base_filename = filename.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), filename.begin());

        if(starts_with_base_filename)
        {
            const filename_t filename_no_ext = std::get<0>(details::file_helper::split_by_extension(filename));
            const filename_t::size_type last_prefix_symbol = filename_no_ext.find_last_of(filename_prefix_symbol());

            if(last_prefix_symbol < filename_no_ext.size())
            {
                return filename_no_ext.substr(last_prefix_symbol + 1);
            }
        }

        return SPDLOG_FILENAME_T("");
    }

    static std::map<filename_t, filename_t> calc_dates_to_filenames(const filename_t &base_filename)
    {
        const filename_t dir = details::os::dir_name(base_filename);
        const std::vector<filename_t> dir_files = details::os::get_directory_files(dir);

        // lexicographical order ensures files are sorted by created date.
        std::map<filename_t, filename_t> dates_to_filenames;

        for(const filename_t& dir_file : dir_files)
        {
            const filename_t date_suffix = daily_filename_calculator::extract_date_suffix(base_filename, dir_file);
            if(!date_suffix.empty())
            {
                dates_to_filenames[date_suffix] = dir_file;
            }
        }

        return dates_to_filenames;
    }

    static filename_t filename_prefix_symbol()
    {
        return SPDLOG_FILENAME_T("_");
    }
};

/*
 * Rotating file sink based on date.
 * If truncate != false , the created file will be truncated.
 * If max_files > 0, retain only the last max_files and delete previous.
 */
template<typename Mutex, typename FileNameCalc = daily_filename_calculator>
class daily_file_sink final : public base_sink<Mutex>
{
public:
    // create daily file sink which rotates on given time
    // initial_file_tp - useful for testing especially when we want to verify the delete_old_files_on_init behaviour
    daily_file_sink(filename_t base_filename, int rotation_hour, int rotation_minute, bool truncate = false, uint16_t max_files = 0, bool delete_old_files_on_init = false,
    log_clock::time_point initial_file_tp = log_clock::now())
        : base_filename_(std::move(base_filename))
        , rotation_h_(rotation_hour)
        , rotation_m_(rotation_minute)
        , truncate_(truncate)
        , max_files_(max_files)
        , filenames_q_()
    {
        if (rotation_hour < 0 || rotation_hour > 23 || rotation_minute < 0 || rotation_minute > 59)
        {
            throw_spdlog_ex("daily_file_sink: Invalid rotation time in ctor");
        }

        auto filename = FileNameCalc::calc_filename(base_filename_, now_tm(initial_file_tp));
        file_helper_.open(filename, truncate_);
        rotation_tp_ = next_rotation_tp_();

        if (max_files_ > 0)
        {
            init_filenames_q_(delete_old_files_on_init);
        }
    }

    filename_t filename()
    {
        std::lock_guard<Mutex> lock(base_sink<Mutex>::mutex_);
        return file_helper_.filename();
    }

protected:
    void sink_it_(const details::log_msg &msg) override
    {
        auto time = msg.time;
        bool should_rotate = time >= rotation_tp_;
        if (should_rotate)
        {
            auto filename = FileNameCalc::calc_filename(base_filename_, now_tm(time));
            file_helper_.open(filename, truncate_);
            rotation_tp_ = next_rotation_tp_();
        }
        memory_buf_t formatted;
        base_sink<Mutex>::formatter_->format(msg, formatted);
        file_helper_.write(formatted);

        // Do the cleaning only at the end because it might throw on failure.
        if (should_rotate && max_files_ > 0)
        {
            delete_old_();
        }
    }

    void flush_() override
    {
        file_helper_.flush();
    }

private:
    void init_filenames_q_(bool delete_old_file_on_init)
    {
        using details::os::path_exists;
        using details::os::remove_if_exists;

        filenames_q_ = details::circular_q<filename_t>(static_cast<size_t>(max_files_));

        // Because the map key is the date in yyyy-mm--dd, it is sorted by lexicographical order.
        const std::map<filename_t, filename_t> dates_to_filenames = daily_filename_calculator::calc_dates_to_filenames(base_filename_);

        const auto first_valid_file_pos = max_files_ > 0 && dates_to_filenames.size() > max_files_ ? dates_to_filenames.size() - max_files_ : 0;

        auto first_valid_file_iter = dates_to_filenames.begin();

        if(first_valid_file_pos > 0)
        {
            std::advance(first_valid_file_iter, first_valid_file_pos);
        }

        std::vector<filename_t> recent_files;
        for(auto iter = first_valid_file_iter; iter != dates_to_filenames.end(); ++iter)
        {
            recent_files.emplace_back(iter->second);
        }

        for (auto iter = recent_files.begin(); iter != recent_files.end(); ++iter)
        {
            if(path_exists(*iter))
            {
                filenames_q_.push_back(std::move(*iter));
            }
        }

        if(max_files_ > 0 && delete_old_file_on_init)
        {
            for(auto iter = dates_to_filenames.begin(); iter != first_valid_file_iter; ++iter)
            {
                remove_if_exists(iter->second);
            }
        }
    }

    tm now_tm(log_clock::time_point tp)
    {
        time_t tnow = log_clock::to_time_t(tp);
        return spdlog::details::os::localtime(tnow);
    }

    log_clock::time_point next_rotation_tp_()
    {
        auto now = log_clock::now();
        tm date = now_tm(now);
        date.tm_hour = rotation_h_;
        date.tm_min = rotation_m_;
        date.tm_sec = 0;
        auto rotation_time = log_clock::from_time_t(std::mktime(&date));
        if (rotation_time > now)
        {
            return rotation_time;
        }
        return {rotation_time + std::chrono::hours(24)};
    }

    // Delete the file N rotations ago.
    // Throw spdlog_ex on failure to delete the old file.
    void delete_old_()
    {
        using details::os::filename_to_str;
        using details::os::remove_if_exists;

        filename_t current_file = filename();
        if (filenames_q_.full())
        {
            auto old_filename = std::move(filenames_q_.front());
            filenames_q_.pop_front();
            bool ok = remove_if_exists(old_filename) == 0;
            if (!ok)
            {
                filenames_q_.push_back(std::move(current_file));
                throw_spdlog_ex("Failed removing daily file " + filename_to_str(old_filename), errno);
            }
        }
        filenames_q_.push_back(std::move(current_file));
    }

    filename_t base_filename_;
    int rotation_h_;
    int rotation_m_;
    log_clock::time_point rotation_tp_;
    details::file_helper file_helper_;
    bool truncate_;
    uint16_t max_files_;
    details::circular_q<filename_t> filenames_q_;
};

using daily_file_sink_mt = daily_file_sink<std::mutex>;
using daily_file_sink_st = daily_file_sink<details::null_mutex>;

} // namespace sinks

//
// factory functions
//
template<typename Factory = spdlog::synchronous_factory>
inline std::shared_ptr<logger> daily_logger_mt(
    const std::string &logger_name, const filename_t &filename, int hour = 0, int minute = 0, bool truncate = false, uint16_t max_files = 0, bool delete_old_files_on_init = false,
    log_clock::time_point initial_file_tp = log_clock::now())
{
    return Factory::template create<sinks::daily_file_sink_mt>(logger_name, filename, hour, minute, truncate, max_files, delete_old_files_on_init, initial_file_tp);
}

template<typename Factory = spdlog::synchronous_factory>
inline std::shared_ptr<logger> daily_logger_st(
    const std::string &logger_name, const filename_t &filename, int hour = 0, int minute = 0, bool truncate = false, uint16_t max_files = 0, bool delete_old_files_on_init = false,
    log_clock::time_point initial_file_tp = log_clock::now())
{
    return Factory::template create<sinks::daily_file_sink_st>(logger_name, filename, hour, minute, truncate, max_files, delete_old_files_on_init, initial_file_tp);
}
} // namespace spdlog
