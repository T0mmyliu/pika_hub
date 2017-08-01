//  Copyright (c) 2017-present The pika_hub Authors.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#include "src/pika_hub_binlog_manager.h"
#include "src/pika_hub_common.h"
#include <string>
#include <vector>
#include <cstdint>

void AbstractFileAndUpdate(const std::string& filename,
    uint64_t* largest, uint64_t* smallest) {
  std::string prefix = filename.substr(0, strlen(kBinlogPrefix));
  if (prefix == kBinlogPrefix) {
    char* ptr;
    uint64_t num =
        strtoul(filename.data() + strlen(kBinlogPrefix), &ptr, 10);
    if (num > *largest) {
      *largest = num;
    }
    if (num < *smallest) {
      *smallest = num;
    }
  }
}

BinlogWriter* BinlogManager::AddWriter() {
  return CreateBinlogWriter(log_path_, number_,
      env_, this);
}

BinlogReader* BinlogManager::AddReader(uint64_t number,
    uint64_t offset, bool ret_at_end) {
  return CreateBinlogReader(log_path_, env_,
      number, offset, this, ret_at_end);
}

void BinlogManager::UpdateWriterOffset(uint64_t number,
    uint64_t offset) {
  number_ = number;
  offset_ = offset;
}

void BinlogManager::GetWriterOffset(uint64_t* number,
    uint64_t* offset) {
  *number = number_;
  *offset = offset_;
}

rocksutil::Status BinlogManager::RecoverLruCache(int64_t* nums) {
  BinlogReader* reader = AddReader(smallest_, 0, true);
  if (reader == nullptr) {
    return rocksutil::Status::NotFound();
  }
  Info(info_log_, "RecoverLruCache from binlog_%d to binlog_%d",
      smallest_, number_ - 1);
  *nums = 0;
  rocksutil::Status s;
  std::vector<BinlogFields> result;
  while (true) {
    s = reader->ReadRecord(&result);
    if (s.IsCorruption() && s.ToString() == "Corruption: Exit") {
      return rocksutil::Status::OK();
    } else if (!s.ok()) {
      return s;
    }

    /*
     * Recover is called before server start, so we could check & insert
     * lru cache without lock here;
     */
    for (auto iter = result.begin(); iter != result.end(); iter++) {
      rocksutil::Cache::Handle* handle = lru_cache_->Lookup(iter->key);
      if (handle) {
        int32_t _exec_time = static_cast<CacheEntity*>(
            lru_cache_->Value(handle))->exec_time;
        if (iter->exec_time <= _exec_time) {
          lru_cache_->Release(handle);
          continue;
        }
        lru_cache_->Release(handle);
      }
      CacheEntity* entity = new CacheEntity(iter->server_id,
          iter->exec_time);
      lru_cache_->Insert(iter->key, entity, 1,
          &BinlogWriter::CacheEntityDeleter);
      (*nums)++;
    }
  }
  delete reader;

  return rocksutil::Status::OK();
}

BinlogManager* CreateBinlogManager(const std::string& log_path,
    rocksutil::Env* env, std::shared_ptr<rocksutil::Logger> info_log) {
  std::vector<std::string> result;
  rocksutil::Status s = env->GetChildren(log_path, &result);

  if (!s.ok()) {
    return nullptr;
  }

  uint64_t largest = 0;
  uint64_t smallest = UINT64_MAX;
  for (auto& file : result) {
    AbstractFileAndUpdate(file, &largest, &smallest);
  }

  if (largest - smallest + 1 > kMaxRecoverNums) {
    smallest = largest + 1 - kMaxRecoverNums;
  }

  largest++;

  return new BinlogManager(log_path, env, largest, smallest, info_log);
}
