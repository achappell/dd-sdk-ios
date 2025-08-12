/*
 * Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
 * This product includes software developed at Datadog (https://www.datadoghq.com/).
 * Copyright 2019-Present Datadog, Inc.
 */

#include "profile.h"
#include <cstdio>
#include <cstring>

namespace dd::profiler {

profile::profile(uint64_t sampling_interval_ns) 
    : sampling_interval_ns_(sampling_interval_ns) {
    
    // Ensure empty string is always at index 0
    strings_.push_back("");
    string_lookup_[""] = 0;
    
    // Pre-intern common strings for performance
    empty_str_id_ = intern_string("");
    wall_time_str_id_ = intern_string("wall-time");
    nanoseconds_str_id_ = intern_string("nanoseconds");
    end_timestamp_ns_str_id_ = intern_string("end_timestamp_ns");
}

void profile::add_samples(const stack_trace_t* traces, int count) {
    if (!traces || count <= 0) {
        return;
    }
    
    for (int i = 0; i < count; ++i) {
        const auto& trace = traces[i];
        std::vector<uint32_t> location_ids;
        location_ids.reserve(trace.frame_count);
        
        // Build location IDs from stack frames with automatic deduplication
        for (uint32_t j = 0; j < trace.frame_count; ++j) {
            const auto& frame = trace.frames[j];
            uint32_t location_id = add_frame(frame);
            location_ids.push_back(location_id);
        }
        
        // Create labels with timestamp
        std::vector<label_t> labels;
        labels.push_back(label_t{
            .key_id = end_timestamp_ns_str_id_,
            .str_id = 0,
            .num = static_cast<int64_t>(trace.timestamp),
            .num_unit_id = nanoseconds_str_id_
        });
        
        // Create sample key and aggregate
        sample_key_t key{location_ids, labels};
        
        // Add sample with count=1 and wall-time value
        std::vector<int64_t> values = {
            static_cast<int64_t>(trace.sampling_interval_nanos) // wall-time
        };
        
        auto it = samples_.find(key);
        if (it != samples_.end()) {
            // Aggregate with existing sample
            auto& existing = it->second;
            for (size_t k = 0; k < values.size() && k < existing.values.size(); ++k) {
                existing.values[k] += values[k];
            }
            existing.last_timestamp = trace.timestamp;
        } else {
            // Create new sample
            sample_data_t sample_data;
            sample_data.values = values;
            sample_data.last_timestamp = trace.timestamp;
            samples_[std::move(key)] = std::move(sample_data);
        }
    }
}

uint32_t profile::intern_string(const std::string& str) {
    auto it = string_lookup_.find(str);
    if (it != string_lookup_.end()) {
        return it->second;
    }
    
    uint32_t id = static_cast<uint32_t>(strings_.size());
    strings_.push_back(str);
    string_lookup_[str] = id;
    return id;
}

uint32_t profile::intern_function(const function_t& func) {
    auto it = function_lookup_.find(func);
    if (it != function_lookup_.end()) {
        return it->second;
    }
    
    uint32_t id = static_cast<uint32_t>(functions_.size() + 1); // 1-based IDs
    functions_.push_back(func);
    function_lookup_[func] = id;
    return id;
}

uint32_t profile::intern_mapping(const mapping_t& mapping) {
    auto it = mapping_lookup_.find(mapping);
    if (it != mapping_lookup_.end()) {
        return it->second;
    }
    
    uint32_t id = static_cast<uint32_t>(mappings_.size() + 1); // 1-based IDs
    mappings_.push_back(mapping);
    mapping_lookup_[mapping] = id;
    return id;
}

uint32_t profile::intern_location(const location_t& location) {
    auto it = location_lookup_.find(location);
    if (it != location_lookup_.end()) {
        return it->second;
    }
    
    uint32_t id = static_cast<uint32_t>(locations_.size() + 1); // 1-based IDs
    locations_.push_back(location);
    location_lookup_[location] = id;
    return id;
}

uint32_t profile::add_binary_image(const binary_image_t& image) {
    // Check for empty mapping (optimization from libdatadog)
    if (image.load_address == 0 && 
        (image.filename == nullptr || strlen(image.filename) == 0)) {
        return 0; // No mapping
    }
    
    std::string filename = image.filename ? image.filename : "";
    std::string build_id = uuid_string(image.uuid);
    
    mapping_t mapping{
        .memory_start = image.load_address,
        .memory_limit = UINT64_MAX, // Unknown limit
        .file_offset = 0,
        .filename_id = intern_string(filename),
        .build_id = intern_string(build_id)
    };
    
    return intern_mapping(mapping);
}

uint32_t profile::add_frame(const stack_frame_t& frame) {
    uint32_t mapping_id = add_binary_image(frame.image);
    
    // Create function entry with address as name (unsymbolized)
    char addr_str[32];
    snprintf(addr_str, sizeof(addr_str), "0x%llx", frame.instruction_ptr);
    
    function_t function{
        .name_id = intern_string(addr_str),
        .system_name_id = empty_str_id_,
        .filename_id = empty_str_id_
    };
    
    uint32_t function_id = intern_function(function);
    
    location_t location{
        .mapping_id = mapping_id,
        .function_id = function_id,
        .address = frame.instruction_ptr,
        .line = 0
    };
    
    return intern_location(location);
}

std::string profile::uuid_string(const uuid_t uuid) {
    char uuid_str[37];  // 36 chars + null terminator
    snprintf(uuid_str, sizeof(uuid_str),
             "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
             uuid[0], uuid[1], uuid[2], uuid[3],
             uuid[4], uuid[5], uuid[6], uuid[7],
             uuid[8], uuid[9], uuid[10], uuid[11],
             uuid[12], uuid[13], uuid[14], uuid[15]);
    return std::string(uuid_str);
}

} // namespace dd::profiler
