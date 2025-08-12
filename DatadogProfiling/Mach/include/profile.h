/*
 * Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
 * This product includes software developed at Datadog (https://www.datadoghq.com/).
 * Copyright 2019-Present Datadog, Inc.
 */

#ifndef DD_PROFILER_PROFILE_H_
#define DD_PROFILER_PROFILE_H_

#include "mach_profiler.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace dd::profiler {

/**
 * Represents a deduplicated function in the profile.
 */
struct function_t {
    uint32_t name_id;
    uint32_t system_name_id;
    uint32_t filename_id;
    
    bool operator==(const function_t& other) const {
        return name_id == other.name_id && 
               system_name_id == other.system_name_id && 
               filename_id == other.filename_id;
    }
};

/**
 * Represents a deduplicated mapping in the profile.
 */
struct mapping_t {
    uint64_t memory_start;
    uint64_t memory_limit;
    uint64_t file_offset;
    uint32_t filename_id;
    uint32_t build_id;
    
    bool operator==(const mapping_t& other) const {
        return memory_start == other.memory_start &&
               memory_limit == other.memory_limit &&
               file_offset == other.file_offset &&
               filename_id == other.filename_id &&
               build_id == other.build_id;
    }
};

/**
 * Represents a deduplicated location in the profile.
 */
struct location_t {
    uint32_t mapping_id;
    uint32_t function_id;
    uint64_t address;
    int64_t line;
    
    bool operator==(const location_t& other) const {
        return mapping_id == other.mapping_id &&
               function_id == other.function_id &&
               address == other.address &&
               line == other.line;
    }
};

/**
 * Represents a label in the profile.
 */
struct label_t {
    uint32_t key_id;
    uint32_t str_id;
    int64_t num;
    uint32_t num_unit_id;
    
    bool operator==(const label_t& other) const {
        return key_id == other.key_id &&
               str_id == other.str_id &&
               num == other.num &&
               num_unit_id == other.num_unit_id;
    }
};

/**
 * Sample key for aggregation.
 */
struct sample_key_t {
    std::vector<uint32_t> location_ids;
    std::vector<label_t> labels;
    
    bool operator==(const sample_key_t& other) const {
        return location_ids == other.location_ids && labels == other.labels;
    }
};

/**
 * Aggregated sample data.
 */
struct sample_data_t {
    std::vector<int64_t> values;
    uint64_t last_timestamp;
};

} // namespace dd::profiler

// Hash specializations
namespace std {
    template<>
    struct hash<dd::profiler::function_t> {
        size_t operator()(const dd::profiler::function_t& f) const {
            return hash<uint32_t>{}(f.name_id) ^ 
                   (hash<uint32_t>{}(f.system_name_id) << 1) ^
                   (hash<uint32_t>{}(f.filename_id) << 2);
        }
    };
    
    template<>
    struct hash<dd::profiler::mapping_t> {
        size_t operator()(const dd::profiler::mapping_t& m) const {
            return hash<uint64_t>{}(m.memory_start) ^ 
                   (hash<uint64_t>{}(m.memory_limit) << 1) ^
                   (hash<uint64_t>{}(m.file_offset) << 2) ^
                   (hash<uint32_t>{}(m.filename_id) << 3) ^
                   (hash<uint32_t>{}(m.build_id) << 4);
        }
    };
    
    template<>
    struct hash<dd::profiler::location_t> {
        size_t operator()(const dd::profiler::location_t& l) const {
            return hash<uint32_t>{}(l.mapping_id) ^ 
                   (hash<uint32_t>{}(l.function_id) << 1) ^
                   (hash<uint64_t>{}(l.address) << 2) ^
                   (hash<int64_t>{}(l.line) << 3);
        }
    };
    
    template<>
    struct hash<dd::profiler::label_t> {
        size_t operator()(const dd::profiler::label_t& l) const {
            return hash<uint32_t>{}(l.key_id) ^ 
                   (hash<uint32_t>{}(l.str_id) << 1) ^
                   (hash<int64_t>{}(l.num) << 2) ^
                   (hash<uint32_t>{}(l.num_unit_id) << 3);
        }
    };
    
    template<>
    struct hash<dd::profiler::sample_key_t> {
        size_t operator()(const dd::profiler::sample_key_t& key) const {
            size_t h1 = 0;
            for (const auto& id : key.location_ids) {
                h1 ^= hash<uint32_t>{}(id) + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
            }
            
            size_t h2 = 0;
            for (const auto& label : key.labels) {
                h2 ^= hash<dd::profiler::label_t>{}(label) + 0x9e3779b9 + (h2 << 6) + (h2 >> 2);
            }
            
            return h1 ^ (h2 << 1);
        }
    };
}

namespace dd::profiler {

/**
 * Efficient profile aggregator with automatic deduplication and sample aggregation.
 */
class profile {
public:
    explicit profile(uint64_t sampling_interval_ns);
    ~profile() = default;
    
    profile(const profile&) = delete;
    profile& operator=(const profile&) = delete;
    
    /**
     * Add samples from stack traces
     */
    void add_samples(const stack_trace_t* traces, int count);

    std::vector<std::string> strings_;
    std::vector<function_t> functions_;
    std::vector<mapping_t> mappings_;
    std::vector<location_t> locations_;
    std::unordered_map<sample_key_t, sample_data_t> samples_;
    
    /**
     * Profile sampling interval in nanoseconds.
     */ 
    uint64_t sampling_interval_ns_;
    
    // Cached string IDs
    uint32_t empty_str_id_;
    uint32_t wall_time_str_id_;
    uint32_t nanoseconds_str_id_;
    uint32_t end_timestamp_ns_str_id_;

private:
    std::unordered_map<std::string, uint32_t> string_lookup_;
    std::unordered_map<function_t, uint32_t> function_lookup_;
    std::unordered_map<mapping_t, uint32_t> mapping_lookup_;
    std::unordered_map<location_t, uint32_t> location_lookup_;
    
    // Helper methods
    uint32_t intern_string(const std::string& str);
    uint32_t intern_function(const function_t& func);
    uint32_t intern_mapping(const mapping_t& mapping);
    uint32_t intern_location(const location_t& location);
    uint32_t add_binary_image(const binary_image_t& image);
    uint32_t add_frame(const stack_frame_t& frame);
    std::string uuid_string(const uuid_t uuid);
};

} // namespace dd::profiler

#endif // DD_PROFILER_PROFILE_H_
