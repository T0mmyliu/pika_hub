//  Copyright (c) 2017-present The pika_hub Authors.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#include <string>
#include <thread>
#include <vector>

#include "src/pika_hub_binlog_sender.h"
#include "src/pika_hub_common.h"
#include "src/pika_hub_binlog_manager.h"
#include "pink/include/pink_cli.h"
#include "pink/include/redis_cli.h"
#include "slash/include/slash_status.h"
#include "rocksutil/cache.h"

void BinlogSender::UpdateSendOffset() {
  {
  rocksutil::MutexLock l(pika_mutex_);
  auto iter = pika_servers_->find(server_id_);
  if (iter != pika_servers_->end()) {
    reader_->GetOffset(&iter->second.send_number, &iter->second.send_offset);
  }
  }
}
void* BinlogSender::ThreadMain() {
  rocksutil::Status read_status;
  pink::PinkCli* cli = nullptr;
  pink::RedisCmdArgsType args;
  std::string str_cmd;
  std::string tmp_str;
  slash::Status s;
  std::vector<BinlogFields> result;
  while (!should_stop()) {
    if (cli == nullptr) {
      cli = pink::NewRedisCli();
      cli->set_connect_timeout(1500);
      if ((cli->Connect(ip_, port_+ 1100)).ok()) {
        cli->set_send_timeout(3000);
        cli->set_recv_timeout(3000);
        Info(info_log_, "BinlogSender[%d] Connect to %s:%d success", server_id_,
            ip_.c_str(), port_);
        {
        rocksutil::MutexLock l(pika_mutex_);
        auto iter = pika_servers_->find(server_id_);
        if (iter != pika_servers_->end()) {
          iter->second.send_fd = cli->fd();
        }
        }
      } else {
        Error(info_log_, "BinlogSender[%d] Connect to %s:%d failed", server_id_,
            ip_.c_str(), port_);
        delete cli;
        cli = nullptr;
      }
      std::this_thread::sleep_for(std::chrono::seconds(2));
      continue;
    }

    if (str_cmd.size() != 0) {
      s = cli->Send(&str_cmd);
      if (!s.ok()) {
        Error(info_log_, "BinlogSender[%d] Send to %s:%d failed", server_id_,
            ip_.c_str(), port_);
        {
        rocksutil::MutexLock l(pika_mutex_);
        auto iter = pika_servers_->find(server_id_);
        if (iter != pika_servers_->end()) {
          iter->second.send_fd = -1;
        }
        }
        delete cli;
        cli = nullptr;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        continue;
      } else {
//        UpdateSendOffset();
      }
      args.clear();
      str_cmd.clear();
    }

//    read_status = reader_->ReadRecord(&op, &key, &value,
//        &server_id, &exec_time);
    read_status = reader_->ReadRecord(&result);
    if (read_status.ok()) {
//      Info(info_log_,
//          "op: %d, key: %s, value: %s, server_id: %d, exec_time: %d",
//          op, key.c_str(), value.c_str(), server_id, exec_time);
      for (auto iter = result.begin(); iter != result.end();
            iter++) {
        if (server_id_ == iter->server_id) {
//          UpdateSendOffset();
          continue;
        }

        rocksutil::Cache::Handle* handle = manager_->lru_cache()->Lookup(
            iter->key);
        if (handle) {
          int32_t _exec_time = static_cast<CacheEntity*>(
              manager_->lru_cache()->Value(handle))->exec_time;
          if (iter->exec_time < _exec_time) {
//            UpdateSendOffset();
            manager_->lru_cache()->Release(handle);
            continue;
          }
        } else {
          Error(info_log_, "BinlogSender[%d] check LRU: %s is not in cache",
              server_id_, iter->key.c_str());
//          UpdateSendOffset();
          continue;
        }
        manager_->lru_cache()->Release(handle);

        switch (iter->op) {
          case kSetOPCode:
            args.push_back("set");
            break;
        }

        args.push_back(iter->key);

        switch (iter->op) {
          case kSetOPCode:
            args.push_back(iter->value);
            break;
        }

        pink::SerializeRedisCommand(args, &tmp_str);
        str_cmd.append(tmp_str);
        args.clear();
      }
      UpdateSendOffset();
    } else if (read_status.IsCorruption() &&
            read_status.ToString() == "Corruption: Exit") {
      Info(info_log_, "BinlogSender[%d] Reader exit", server_id_);
    } else {
      {
      rocksutil::MutexLock l(pika_mutex_);
      auto iter = pika_servers_->find(server_id_);
      if (iter != pika_servers_->end()) {
        iter->second.send_fd = -2;
        iter->second.sender = nullptr;
      }
      }
      Error(info_log_, "BinlogSender[%d] ReadRecord, error: %s",
          server_id_, read_status.ToString().c_str());
      break;
    }
  }
  delete cli;
  return nullptr;
}
