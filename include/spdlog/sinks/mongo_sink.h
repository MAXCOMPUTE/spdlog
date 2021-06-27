// Copyright(c) 2015-present, Gabi Melman & spdlog contributors.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#pragma once

//
// Custom sink for mongodb
// Building and using requires mongocxx library.
// For building mongocxx library check the url below
// http://mongocxx.org/mongocxx-v3/installation/
// 

#include "spdlog/common.h"
#include "spdlog/details/log_msg.h"
#include "spdlog/pattern_formatter.h"
#include "spdlog/sinks/base_sink.h"
#include <spdlog/details/synchronous_factory.h>

#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/types.hpp>
#include <bsoncxx/view_or_value.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>

namespace spdlog {
namespace sinks {
template <typename Mutex> class mongo_sink : public base_sink<Mutex> {
public:
  mongo_sink(const std::string &db_name, const std::string &collection_name,
             const std::string &uri = "mongodb://localhost:27017") {
    try {
      client_ = std::make_unique<mongocxx::client>(mongocxx::uri{uri});
      db_name_ = db_name;
      coll_name_ = collection_name;
      set_pattern("%v");
    } catch (const std::exception &e) {
      throw spdlog_ex("Error opening database");
    }
  }

  ~mongo_sink() { flush_(); }

protected:
  void sink_it_(const details::log_msg &msg) {
    using bsoncxx::builder::stream::document;
    using bsoncxx::builder::stream::finalize;

    if (client_ != nullptr) {
      memory_buf_t formatted;
      base_sink<Mutex>::formatter_->format(msg, formatted);
      auto doc = document{}
                 << "timestamp" << bsoncxx::types::b_date(msg.time) << "level"
                 << level::to_string_view(msg.level).data() << "message"
                 << std::string(formatted.begin(), formatted.end())
                 << "logger_name"
                 << std::string(msg.logger_name.begin(), msg.logger_name.end())
                 << "thread_id" << static_cast<int>(msg.thread_id) << finalize;
      client_->database(db_name_).collection(coll_name_).insert_one(doc.view());
    }
  }

  void flush_() {}

  void set_pattern_(const std::string &pattern) {
    formatter_ = std::unique_ptr<spdlog::pattern_formatter>(
        new spdlog::pattern_formatter(pattern, pattern_time_type::local, ""));
  }

  void set_formatter_(std::unique_ptr<spdlog::formatter> sink_formatter) {}

private:
  static mongocxx::instance instance_;
  std::string db_name_;
  std::string coll_name_;
  std::unique_ptr<mongocxx::client> client_ = nullptr;
};
mongocxx::instance mongo_sink<std::mutex>::instance_{};

#include "spdlog/details/null_mutex.h"
#include <mutex>
using mongo_sink_mt = mongo_sink<std::mutex>;
using mongo_sink_st = mongo_sink<spdlog::details::null_mutex>;

template <typename Factory = spdlog::synchronous_factory>
inline std::shared_ptr<logger>
mongo_logger_mt(const std::string &logger_name, const std::string &db_name,
                const std::string &collection_name,
                const std::string &uri = "mongodb://localhost:27017") {
  return Factory::template create<sinks::mongo_sink_mt>(logger_name, db_name,
                                                        collection_name, uri);
}

template <typename Factory = spdlog::synchronous_factory>
inline std::shared_ptr<logger>
mongo_logger_st(const std::string &logger_name, const std::string &db_name,
                const std::string &collection_name,
                const std::string &uri = "mongodb://localhost:27017") {
  return Factory::template create<sinks::mongo_sink_st>(logger_name, db_name,
                                                        collection_name, uri);
}
} // namespace sinks
} // namespace spdlog
