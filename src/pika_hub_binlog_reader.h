//  Copyright (c) 2017-present The pika_hub Authors.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#ifndef SRC_PIKA_HUB_BINLOG_READER_H_
#define SRC_PIKA_HUB_BINLOG_READER_H_

#include <string>
#include <vector>

#include "src/pika_hub_common.h"
#include "rocksutil/log_reader.h"
#include "rocksutil/env.h"

class BinlogManager;
class BinlogReader {
 public:
  BinlogReader(rocksutil::log::Reader* reader,
     const std::string& log_path,
     uint64_t number,
     rocksutil::Env* env,
     BinlogManager* manager,
     bool exit_at_end)
  : reader_(reader), log_path_(log_path),
  number_(number),
  env_(env), manager_(manager),
  exit_at_end_(exit_at_end),
  should_exit_(false) {
    reporter_.status = &status_;
  }

  ~BinlogReader() {
    delete reader_;
  }

  rocksutil::Status ReadRecord(std::vector<BinlogFields>* result);

  bool IsEOF() {
    return reader_->IsEOF();
  }
  void GetOffset(uint64_t* number, uint64_t* offset);

  void set_reader(rocksutil::log::Reader* reader) {
    reader_ = reader;
  }
  rocksutil::log::Reader::LogReporter* reporter() {
    return &reporter_;
  }

  void StopRead();

 private:
  bool TryToRollFile();
  static void DecodeBinlogContent(const rocksutil::Slice& content,
      std::vector<BinlogFields>* result);
  rocksutil::log::Reader* reader_;
  std::string log_path_;
  uint64_t number_;
  rocksutil::Env* env_;
  BinlogManager* manager_;
  bool exit_at_end_;
  bool should_exit_;
  rocksutil::Status status_;
  rocksutil::log::Reader::LogReporter reporter_;
};

extern BinlogReader* CreateBinlogReader(const std::string& log_path,
    rocksutil::Env* env, uint64_t number, uint64_t offset,
    BinlogManager* manager,
    bool ret_at_end);

#endif  // SRC_PIKA_HUB_BINLOG_READER_H_
