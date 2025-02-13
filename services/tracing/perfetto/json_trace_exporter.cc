// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/tracing/perfetto/json_trace_exporter.h"

#include <unordered_map>
#include <utility>

#include "base/format_macros.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "base/trace_event/trace_event.h"
#include "third_party/perfetto/protos/perfetto/trace/chrome/chrome_trace_event.pbzero.h"
#include "third_party/perfetto/protos/perfetto/trace/chrome/chrome_trace_packet.pb.h"

using TraceEvent = base::trace_event::TraceEvent;

namespace tracing {

namespace {

const size_t kTraceEventBufferSizeInBytes = 100 * 1024;

void AppendProtoArrayAsJSON(std::string* out,
                            const perfetto::protos::ChromeTracedValue& array);

void AppendProtoValueAsJSON(std::string* out,
                            const perfetto::protos::ChromeTracedValue& value) {
  base::trace_event::TraceEvent::TraceValue json_value;
  if (value.has_int_value()) {
    json_value.as_int = value.int_value();
    TraceEvent::AppendValueAsJSON(TRACE_VALUE_TYPE_INT, json_value, out);
  } else if (value.has_double_value()) {
    json_value.as_double = value.double_value();
    TraceEvent::AppendValueAsJSON(TRACE_VALUE_TYPE_DOUBLE, json_value, out);
  } else if (value.has_bool_value()) {
    json_value.as_bool = value.bool_value();
    TraceEvent::AppendValueAsJSON(TRACE_VALUE_TYPE_BOOL, json_value, out);
  } else if (value.has_string_value()) {
    json_value.as_string = value.string_value().c_str();
    TraceEvent::AppendValueAsJSON(TRACE_VALUE_TYPE_STRING, json_value, out);
  } else if (value.has_nested_type()) {
    if (value.nested_type() == perfetto::protos::ChromeTracedValue::ARRAY) {
      AppendProtoArrayAsJSON(out, value);
      return;
    } else if (value.nested_type() ==
               perfetto::protos::ChromeTracedValue::DICT) {
      AppendProtoDictAsJSON(out, value);
    } else {
      NOTREACHED();
    }
  } else {
    NOTREACHED();
  }
}

void AppendProtoArrayAsJSON(std::string* out,
                            const perfetto::protos::ChromeTracedValue& array) {
  out->append("[");

  bool is_first_entry = true;
  for (auto& value : array.array_values()) {
    if (!is_first_entry) {
      out->append(",");
    } else {
      is_first_entry = false;
    }

    AppendProtoValueAsJSON(out, value);
  }

  out->append("]");
}

const char* GetStringFromStringTable(
    const std::unordered_map<int, std::string>& string_table,
    int index) {
  auto it = string_table.find(index);
  DCHECK(it != string_table.end());

  return it->second.c_str();
}

void OutputJSONFromArgumentValue(
    const perfetto::protos::ChromeTraceEvent::Arg& arg,
    std::string* out) {
  TraceEvent::TraceValue value;
  if (arg.has_bool_value()) {
    value.as_bool = arg.bool_value();
    TraceEvent::AppendValueAsJSON(TRACE_VALUE_TYPE_BOOL, value, out);
    return;
  }

  if (arg.has_uint_value()) {
    value.as_uint = arg.uint_value();
    TraceEvent::AppendValueAsJSON(TRACE_VALUE_TYPE_UINT, value, out);
    return;
  }

  if (arg.has_int_value()) {
    value.as_int = arg.int_value();
    TraceEvent::AppendValueAsJSON(TRACE_VALUE_TYPE_INT, value, out);
    return;
  }

  if (arg.has_double_value()) {
    value.as_double = arg.double_value();
    TraceEvent::AppendValueAsJSON(TRACE_VALUE_TYPE_DOUBLE, value, out);
    return;
  }

  if (arg.has_pointer_value()) {
    value.as_pointer = reinterpret_cast<void*>(arg.pointer_value());
    TraceEvent::AppendValueAsJSON(TRACE_VALUE_TYPE_POINTER, value, out);
    return;
  }

  if (arg.has_string_value()) {
    std::string str = arg.string_value();
    value.as_string = &str[0];
    TraceEvent::AppendValueAsJSON(TRACE_VALUE_TYPE_STRING, value, out);
    return;
  }

  if (arg.has_json_value()) {
    *out += arg.json_value();
    return;
  }

  if (arg.has_traced_value()) {
    AppendProtoDictAsJSON(out, arg.traced_value());
    return;
  }

  NOTREACHED();
}

void OutputJSONFromTraceEventProto(
    const perfetto::protos::ChromeTraceEvent& event,
    const std::unordered_map<int, std::string>& string_table,
    const JSONTraceExporter::ArgumentFilterPredicate& argument_filter_predicate,
    std::string* out) {
  char phase = static_cast<char>(event.phase());
  const char* name =
      event.has_name_index()
          ? GetStringFromStringTable(string_table, event.name_index())
          : event.name().c_str();
  const char* category_group_name =
      event.has_category_group_name_index()
          ? GetStringFromStringTable(string_table,
                                     event.category_group_name_index())
          : event.category_group_name().c_str();

  base::StringAppendF(out,
                      "{\"pid\":%i,\"tid\":%i,\"ts\":%" PRId64
                      ",\"ph\":\"%c\",\"cat\":\"%s\",\"name\":",
                      event.process_id(), event.thread_id(), event.timestamp(),
                      phase, category_group_name);
  base::EscapeJSONString(name, true, out);

  if (event.has_duration()) {
    base::StringAppendF(out, ",\"dur\":%" PRId64, event.duration());
  }

  if (event.has_thread_duration()) {
    base::StringAppendF(out, ",\"tdur\":%" PRId64, event.thread_duration());
  }

  if (event.has_thread_timestamp()) {
    base::StringAppendF(out, ",\"tts\":%" PRId64, event.thread_timestamp());
  }

  // Output async tts marker field if flag is set.
  if (event.flags() & TRACE_EVENT_FLAG_ASYNC_TTS) {
    base::StringAppendF(out, ", \"use_async_tts\":1");
  }

  // If id_ is set, print it out as a hex string so we don't loose any
  // bits (it might be a 64-bit pointer).
  unsigned int id_flags =
      event.flags() & (TRACE_EVENT_FLAG_HAS_ID | TRACE_EVENT_FLAG_HAS_LOCAL_ID |
                       TRACE_EVENT_FLAG_HAS_GLOBAL_ID);
  if (id_flags) {
    if (event.has_scope()) {
      base::StringAppendF(out, ",\"scope\":\"%s\"", event.scope().c_str());
    }

    DCHECK(event.has_id());
    switch (id_flags) {
      case TRACE_EVENT_FLAG_HAS_ID:
        base::StringAppendF(out, ",\"id\":\"0x%" PRIx64 "\"",
                            static_cast<uint64_t>(event.id()));
        break;

      case TRACE_EVENT_FLAG_HAS_LOCAL_ID:
        base::StringAppendF(out, ",\"id2\":{\"local\":\"0x%" PRIx64 "\"}",
                            static_cast<uint64_t>(event.id()));
        break;

      case TRACE_EVENT_FLAG_HAS_GLOBAL_ID:
        base::StringAppendF(out, ",\"id2\":{\"global\":\"0x%" PRIx64 "\"}",
                            static_cast<uint64_t>(event.id()));
        break;

      default:
        NOTREACHED() << "More than one of the ID flags are set";
        break;
    }
  }

  if (event.flags() & TRACE_EVENT_FLAG_BIND_TO_ENCLOSING)
    base::StringAppendF(out, ",\"bp\":\"e\"");

  if (event.has_bind_id()) {
    base::StringAppendF(out, ",\"bind_id\":\"0x%" PRIx64 "\"",
                        static_cast<uint64_t>(event.bind_id()));
  }

  if (event.flags() & TRACE_EVENT_FLAG_FLOW_IN)
    base::StringAppendF(out, ",\"flow_in\":true");
  if (event.flags() & TRACE_EVENT_FLAG_FLOW_OUT)
    base::StringAppendF(out, ",\"flow_out\":true");

  // Instant events also output their scope.
  if (phase == TRACE_EVENT_PHASE_INSTANT) {
    char scope = '?';
    switch (event.flags() & TRACE_EVENT_FLAG_SCOPE_MASK) {
      case TRACE_EVENT_SCOPE_GLOBAL:
        scope = TRACE_EVENT_SCOPE_NAME_GLOBAL;
        break;

      case TRACE_EVENT_SCOPE_PROCESS:
        scope = TRACE_EVENT_SCOPE_NAME_PROCESS;
        break;

      case TRACE_EVENT_SCOPE_THREAD:
        scope = TRACE_EVENT_SCOPE_NAME_THREAD;
        break;
    }
    base::StringAppendF(out, ",\"s\":\"%c\"", scope);
  }

  *out += ",\"args\":";

  JSONTraceExporter::ArgumentNameFilterPredicate argument_name_filter_predicate;
  bool strip_args =
      event.args_size() > 0 && !argument_filter_predicate.is_null() &&
      !argument_filter_predicate.Run(category_group_name, name,
                                     &argument_name_filter_predicate);

  if (strip_args) {
    *out += "\"__stripped__\"";
  } else {
    *out += "{";

    for (int i = 0; i < event.args_size(); ++i) {
      auto& arg = event.args(i);

      if (i > 0) {
        *out += ",";
      }

      *out += "\"";
      std::string arg_name =
          arg.has_name_index()
              ? GetStringFromStringTable(string_table, arg.name_index())
              : arg.name();
      *out += arg_name;
      *out += "\":";

      if (!argument_name_filter_predicate.is_null() &&
          !argument_name_filter_predicate.Run(arg_name.c_str())) {
        *out += "\"__stripped__\"";
        continue;
      }
      OutputJSONFromArgumentValue(arg, out);
    }

    *out += "}";
  }

  *out += "}";
}

std::unique_ptr<base::DictionaryValue> ConvertTraceStatsToDict(
    const perfetto::protos::TraceStats& trace_stats) {
  auto dict = std::make_unique<base::DictionaryValue>();
  dict->SetInteger("producers_connected", trace_stats.producers_connected());
  dict->SetInteger("producers_seen", trace_stats.producers_seen());
  dict->SetInteger("data_sources_registered",
                   trace_stats.data_sources_registered());
  dict->SetInteger("data_sources_seen", trace_stats.data_sources_seen());
  dict->SetInteger("tracing_sessions", trace_stats.tracing_sessions());
  dict->SetInteger("total_buffers", trace_stats.total_buffers());
  dict->SetInteger("chunks_discarded", trace_stats.chunks_discarded());
  dict->SetInteger("patches_discarded", trace_stats.patches_discarded());
  auto buf_list = std::make_unique<base::ListValue>();
  for (const auto& buf_stats : trace_stats.buffer_stats()) {
    base::Value buf_value(base::Value::Type::DICTIONARY);
    base::DictionaryValue* buf_dict;
    buf_value.GetAsDictionary(&buf_dict);
    buf_dict->SetInteger("buffer_size", buf_stats.buffer_size());
    buf_dict->SetInteger("bytes_written", buf_stats.bytes_written());
    buf_dict->SetInteger("bytes_overwritten", buf_stats.bytes_overwritten());
    buf_dict->SetInteger("bytes_read", buf_stats.bytes_read());
    buf_dict->SetInteger("padding_bytes_written",
                         buf_stats.padding_bytes_written());
    buf_dict->SetInteger("padding_bytes_cleared",
                         buf_stats.padding_bytes_cleared());
    buf_dict->SetInteger("chunks_written", buf_stats.chunks_written());
    buf_dict->SetInteger("chunks_rewritten", buf_stats.chunks_rewritten());
    buf_dict->SetInteger("chunks_overwritten", buf_stats.chunks_overwritten());
    buf_dict->SetInteger("chunks_discarded", buf_stats.chunks_discarded());
    buf_dict->SetInteger("chunks_read", buf_stats.chunks_read());
    buf_dict->SetInteger("chunks_committed_out_of_order",
                         buf_stats.chunks_committed_out_of_order());
    buf_dict->SetInteger("write_wrap_count", buf_stats.write_wrap_count());
    buf_dict->SetInteger("patches_succeeded", buf_stats.patches_succeeded());
    buf_dict->SetInteger("patches_failed", buf_stats.patches_failed());
    buf_dict->SetInteger("readaheads_succeeded",
                         buf_stats.readaheads_succeeded());
    buf_dict->SetInteger("readaheads_failed", buf_stats.readaheads_failed());
    buf_dict->SetInteger("abi_violations", buf_stats.abi_violations());
    buf_list->GetList().push_back(std::move(buf_value));
  }
  dict->SetList("buffer_stats", std::move(buf_list));
  return dict;
}

}  // namespace

void AppendProtoDictAsJSON(std::string* out,
                           const perfetto::protos::ChromeTracedValue& dict) {
  out->append("{");

  DCHECK_EQ(dict.dict_keys_size(), dict.dict_values_size());
  for (int i = 0; i < dict.dict_keys_size(); ++i) {
    if (i != 0) {
      out->append(",");
    }

    base::EscapeJSONString(dict.dict_keys(i), true, out);
    out->append(":");

    AppendProtoValueAsJSON(out, dict.dict_values(i));
  }

  out->append("}");
}

JSONTraceExporter::JSONTraceExporter(
    ArgumentFilterPredicate argument_filter_predicate,
    OnTraceEventJSONCallback callback)
    : json_callback_(callback),
      metadata_(std::make_unique<base::DictionaryValue>()),
      argument_filter_predicate_(argument_filter_predicate) {
  DCHECK(json_callback_);
}

JSONTraceExporter::~JSONTraceExporter() = default;

void JSONTraceExporter::OnTraceData(std::vector<perfetto::TracePacket> packets,
                                    bool has_more) {
  DCHECK(json_callback_);
  DCHECK(!packets.empty() || !has_more);

  // Since we write each event before checking the limit, we'll
  // always go slightly over and hence we reserve some extra space
  // to avoid most reallocs.
  const size_t kReserveCapacity = kTraceEventBufferSizeInBytes * 5 / 4;
  std::string out;
  out.reserve(kReserveCapacity);

  if (label_filter_.empty() && !has_output_json_preamble_) {
    out = "{\"traceEvents\":[";
    has_output_json_preamble_ = true;
  }

  for (auto& encoded_packet : packets) {
    perfetto::protos::ChromeTracePacket packet;
    bool decoded = encoded_packet.Decode(&packet);
    DCHECK(decoded);

    if (packet.has_trace_stats()) {
      metadata_->SetDictionary("perfetto_trace_stats",
                               ConvertTraceStatsToDict(packet.trace_stats()));
      continue;
    }

    if (!packet.has_chrome_events()) {
      continue;
    }

    auto& bundle = packet.chrome_events();

    if (label_filter_.empty() || label_filter_ == "traceEvents") {
      std::unordered_map<int, std::string> string_table;
      for (auto& string_table_entry : bundle.string_table()) {
        string_table[string_table_entry.index()] = string_table_entry.value();
      }

      for (auto& event : bundle.trace_events()) {
        if (out.size() > kTraceEventBufferSizeInBytes) {
          json_callback_.Run(out, nullptr, true);
          out.clear();
        }

        if (has_output_first_event_) {
          out += ",\n";
        } else {
          has_output_first_event_ = true;
        }

        OutputJSONFromTraceEventProto(event, string_table,
                                      argument_filter_predicate_, &out);
      }
    }

    for (auto& metadata : bundle.metadata()) {
      if (metadata.has_string_value()) {
        metadata_->SetString(metadata.name(), metadata.string_value());
      } else if (metadata.has_int_value()) {
        metadata_->SetInteger(metadata.name(), metadata.int_value());
      } else if (metadata.has_bool_value()) {
        metadata_->SetBoolean(metadata.name(), metadata.bool_value());
      } else if (metadata.has_json_value()) {
        std::unique_ptr<base::Value> value(
            base::JSONReader::ReadDeprecated(metadata.json_value()));
        metadata_->Set(metadata.name(), std::move(value));
      } else {
        NOTREACHED();
      }
    }

    for (auto& legacy_ftrace_output : bundle.legacy_ftrace_output()) {
      legacy_system_ftrace_output_ += legacy_ftrace_output;
    }

    for (auto& legacy_json_trace : bundle.legacy_json_trace()) {
      // Tracing agents should only add this field when there is some data.
      DCHECK(!legacy_json_trace.data().empty());
      switch (legacy_json_trace.type()) {
        case perfetto::protos::ChromeLegacyJsonTrace::USER_TRACE:
          if (has_output_first_event_) {
            out += ",\n";
          } else {
            has_output_first_event_ = true;
          }
          out += legacy_json_trace.data();
          break;
        case perfetto::protos::ChromeLegacyJsonTrace::SYSTEM_TRACE:
          if (legacy_system_trace_events_.empty()) {
            legacy_system_trace_events_ = "{";
          } else {
            legacy_system_trace_events_ += ",";
          }
          legacy_system_trace_events_ += legacy_json_trace.data();
          break;
        default:
          NOTREACHED();
      }
    }
  }

  if (!has_more) {
    if (label_filter_.empty()) {
      out += "]";
    }

    if ((label_filter_.empty() || label_filter_ == "systemTraceEvents") &&
        (!legacy_system_ftrace_output_.empty() ||
         !legacy_system_trace_events_.empty())) {
      // Should only have system events (e.g. ETW) or system ftrace output.
      DCHECK(legacy_system_ftrace_output_.empty() ||
             legacy_system_trace_events_.empty());
      out += ",\"systemTraceEvents\":";
      if (!legacy_system_ftrace_output_.empty()) {
        std::string escaped;
        base::EscapeJSONString(legacy_system_ftrace_output_,
                               true /* put_in_quotes */, &escaped);
        out += escaped;
      } else {
        out += legacy_system_trace_events_ + "}";
      }
    }

    if (label_filter_.empty()) {
      if (!metadata_->empty()) {
        out += ",\"metadata\":";
        std::string json_value;
        base::JSONWriter::Write(*metadata_, &json_value);
        out += json_value;
      }

      out += "}";
    }
  }

  json_callback_.Run(out, metadata_.get(), has_more);
}

}  // namespace tracing
