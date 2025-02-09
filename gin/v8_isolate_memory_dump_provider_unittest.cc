// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gin/v8_isolate_memory_dump_provider.h"

#include <memory>

#include "base/trace_event/process_memory_dump.h"
#include "base/trace_event/trace_event.h"
#include "gin/public/isolate_holder.h"
#include "gin/test/v8_test.h"

namespace gin {

typedef V8Test V8MemoryDumpProviderTest;

// Checks if the dump provider runs without crashing and dumps root objects.
TEST_F(V8MemoryDumpProviderTest, DumpStatistics) {
  // Sets the track objects flag for dumping object statistics. Since this is
  // not set before V8::InitializePlatform the sizes will not be accurate, but
  // this serves the purpose of this test.
  const char track_objects_flag[] = "--track-gc-object-stats";
  v8::V8::SetFlagsFromString(track_objects_flag,
                             static_cast<int>(strlen(track_objects_flag)));

  base::trace_event::MemoryDumpArgs dump_args = {
      base::trace_event::MemoryDumpLevelOfDetail::DETAILED};
  std::unique_ptr<base::trace_event::ProcessMemoryDump> process_memory_dump(
      new base::trace_event::ProcessMemoryDump(dump_args));
  instance_->isolate_memory_dump_provider_for_testing()->OnMemoryDump(
      dump_args, process_memory_dump.get());
  const base::trace_event::ProcessMemoryDump::AllocatorDumpsMap&
      allocator_dumps = process_memory_dump->allocator_dumps();

  bool did_dump_isolate_stats = false;
  bool did_dump_space_stats = false;
  bool did_dump_objects_stats = false;
  for (const auto& name_dump : allocator_dumps) {
    const std::string& name = name_dump.first;
    if (name.find("v8/main") != std::string::npos) {
      did_dump_isolate_stats = true;
    }
    if (name.find("v8/main/heap") != std::string::npos) {
      did_dump_space_stats = true;
    }
    if (name.find("v8/main/heap_objects") != std::string::npos) {
      did_dump_objects_stats = true;
    }
  }

  ASSERT_TRUE(did_dump_isolate_stats);
  ASSERT_TRUE(did_dump_space_stats);
  ASSERT_TRUE(did_dump_objects_stats);
}

TEST_F(V8MemoryDumpProviderTest, DumpContextStatistics) {
  base::trace_event::MemoryDumpArgs dump_args = {
      base::trace_event::MemoryDumpLevelOfDetail::LIGHT};
  std::unique_ptr<base::trace_event::ProcessMemoryDump> process_memory_dump(
      new base::trace_event::ProcessMemoryDump(dump_args));
  instance_->isolate_memory_dump_provider_for_testing()->OnMemoryDump(
      dump_args, process_memory_dump.get());
  const base::trace_event::ProcessMemoryDump::AllocatorDumpsMap&
      allocator_dumps = process_memory_dump->allocator_dumps();

  bool did_dump_detached_contexts = false;
  bool did_dump_native_contexts = false;
  for (const auto& name_dump : allocator_dumps) {
    const std::string& name = name_dump.first;
    if (name.find("contexts/detached_context") != std::string::npos) {
      did_dump_detached_contexts = true;
    }
    if (name.find("contexts/native_context") != std::string::npos) {
      did_dump_native_contexts = true;
    }
  }

  ASSERT_TRUE(did_dump_detached_contexts);
  ASSERT_TRUE(did_dump_native_contexts);
}

TEST_F(V8MemoryDumpProviderTest, DumpCodeStatistics) {
  // Code stats are disabled unless this category is enabled.
  base::trace_event::TraceLog::GetInstance()->SetEnabled(
      base::trace_event::TraceConfig(
          TRACE_DISABLED_BY_DEFAULT("memory-infra.v8.code_stats"), ""),
      base::trace_event::TraceLog::RECORDING_MODE);

  base::trace_event::MemoryDumpArgs dump_args = {
      base::trace_event::MemoryDumpLevelOfDetail::LIGHT};
  std::unique_ptr<base::trace_event::ProcessMemoryDump> process_memory_dump(
      new base::trace_event::ProcessMemoryDump(dump_args));
  instance_->isolate_memory_dump_provider_for_testing()->OnMemoryDump(
      dump_args, process_memory_dump.get());
  const base::trace_event::ProcessMemoryDump::AllocatorDumpsMap&
      allocator_dumps = process_memory_dump->allocator_dumps();

  bool did_dump_bytecode_size = false;
  bool did_dump_code_size = false;
  bool did_dump_external_scripts_size = false;

  for (const auto& name_dump : allocator_dumps) {
    const std::string& name = name_dump.first;
    if (name.find("code_stats") != std::string::npos) {
      for (const base::trace_event::MemoryAllocatorDump::Entry& entry :
           name_dump.second->entries()) {
        if (entry.name == "bytecode_and_metadata_size") {
          did_dump_bytecode_size = true;
        } else if (entry.name == "code_and_metadata_size") {
          did_dump_code_size = true;
        } else if (entry.name == "external_script_source_size") {
          did_dump_external_scripts_size = true;
        }
      }
    }
  }
  base::trace_event::TraceLog::GetInstance()->SetDisabled();

  ASSERT_TRUE(did_dump_bytecode_size);
  ASSERT_TRUE(did_dump_code_size);
  ASSERT_TRUE(did_dump_external_scripts_size);
}

}  // namespace gin
