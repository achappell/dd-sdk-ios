/*
 * Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
 * This product includes software developed at Datadog (https://www.datadoghq.com/).
 * Copyright 2019-Present Datadog, Inc.
 */

/**
 * @file profile_packer.cpp
 * @brief Converts internal profile data structures to protobuf format for serialization
 * 
 * This module implements the conversion from Datadog's internal profiling data structures
 * to the standardized pprof protobuf format. The pprof format is a Google-defined format
 * for representing profiling data that can be consumed by various profiling tools.
 * 
 * Key responsibilities:
 * - Convert internal string tables, functions, mappings, locations, and samples to protobuf
 * - Handle memory allocation consistently through a custom allocator
 * - Serialize the final protobuf structure to binary format
 * 
 * The implementation follows the pprof specification and ensures all data is properly
 * deduplicated and referenced by ID rather than duplicated content.
 */

#include "profile_pprof_packer.h"
#include "profile.h"
#include "profile.pb-c.h"
#include <cstdlib>
#include <cstring>

/**
 * @brief Custom allocator functions for protobuf-c memory management
 * 
 * These functions provide a consistent allocation interface that wraps
 * standard malloc/free. Using a custom allocator ensures all protobuf
 * memory can be tracked and cleaned up properly.
 */

/** @brief Allocation function wrapper around malloc */
static void* sys_malloc(void* allocator_data, size_t size) {
    (void)allocator_data;
    return malloc(size);
}

/** @brief Deallocation function wrapper around free */
static void sys_free(void* allocator_data, void* ptr) {
    (void)allocator_data;
    free(ptr);
}

/** @brief Global allocator instance used for all protobuf allocations */
static ProtobufCAllocator profile_allocator = {
    sys_malloc,
    sys_free,
    nullptr  // allocator_data
};

/** @brief Allocate memory using the protobuf allocator */
static inline void* pb_alloc(ProtobufCAllocator* allocator, size_t size) {
    return allocator->alloc(allocator->allocator_data, size);
}

namespace dd::profiler {

/**
 * @brief Helper function forward declarations
 * 
 * These functions handle the conversion of specific data types from the internal
 * profile representation to protobuf structures. Each function is responsible
 * for allocating and populating the corresponding protobuf message type.
 */

/** @brief Convert string table to protobuf format with proper memory allocation */
void perftools__profiles__profile__add_strings(const std::string* strings, size_t count, Perftools__Profiles__Profile* pprof, ProtobufCAllocator* allocator);

/** @brief Set sample type definitions (e.g., "cpu"/"nanoseconds", "wall"/"nanoseconds") */
void perftools__profiles__profile__set_sample_type(int64_t type, int64_t unit, Perftools__Profiles__Profile* pprof, ProtobufCAllocator* allocator);

/** @brief Set period type and value for sampling interval */
void perftools__profiles__profile__set_period(int64_t type, int64_t unit, int64_t period, Perftools__Profiles__Profile* pprof, ProtobufCAllocator* allocator);

/** @brief Convert function information to protobuf format */
void perftools__profiles__profile__add_functions(const function_t* functions, size_t count, Perftools__Profiles__Profile* pprof, ProtobufCAllocator* allocator);

/** @brief Convert memory mapping information to protobuf format */
void perftools__profiles__profile__add_mappings(const mapping_t* mappings, size_t count, Perftools__Profiles__Profile* pprof, ProtobufCAllocator* allocator);

/** @brief Convert code location information to protobuf format */
void perftools__profiles__profile__add_locations(const location_t* locations, size_t count, Perftools__Profiles__Profile* pprof, ProtobufCAllocator* allocator);

/** @brief Convert aggregated sample data to protobuf format */
void perftools__profiles__profile__add_samples(const std::unordered_map<sample_key_t, sample_data_t>& samples, Perftools__Profiles__Profile* pprof, ProtobufCAllocator* allocator);

/**
 * @brief Pack profile data into pprof protobuf binary format
 * 
 * This is the main entry point for converting profile data to the
 * standardized pprof binary format. The function:
 * 
 * 1. Creates a protobuf Profile message structure
 * 2. Converts all internal data (strings, functions, mappings, etc.) to protobuf format
 * 3. Serializes the protobuf message to binary data
 * 4. Returns ownership of the allocated buffer to the caller
 * 
 * @param prof The profile data to pack
 * @param data Output parameter - pointer to allocated buffer containing serialized data
 * @return Size of the serialized data in bytes, or 0 on failure
 * 
 * @note The caller is responsible for freeing the returned buffer with free()
 * @note All protobuf memory is allocated using the custom allocator and cleaned up automatically
 */
size_t profile_pprof_pack(const profile& prof, uint8_t** data) {
    if (!data) return 0;
    
    // Allocate and initialize the main protobuf profile structure
    auto* pprof = static_cast<Perftools__Profiles__Profile*>(
        pb_alloc(&profile_allocator, sizeof(Perftools__Profiles__Profile))
    );
    perftools__profiles__profile__init(pprof);
    
    // Convert each component of the profile to protobuf format
    perftools__profiles__profile__add_strings(prof.strings_.data(), prof.strings_.size(), pprof, &profile_allocator);
    perftools__profiles__profile__set_sample_type(prof.wall_time_str_id_, prof.nanoseconds_str_id_, pprof, &profile_allocator);
    perftools__profiles__profile__set_period(prof.wall_time_str_id_, prof.nanoseconds_str_id_, prof.sampling_interval_ns_, pprof, &profile_allocator);
    perftools__profiles__profile__add_functions(prof.functions_.data(), prof.functions_.size(), pprof, &profile_allocator);
    perftools__profiles__profile__add_mappings(prof.mappings_.data(), prof.mappings_.size(), pprof, &profile_allocator);
    perftools__profiles__profile__add_locations(prof.locations_.data(), prof.locations_.size(), pprof, &profile_allocator);
    perftools__profiles__profile__add_samples(prof.samples_, pprof, &profile_allocator);
    
    // Calculate required buffer size and serialize to binary format
    size_t packed_size = perftools__profiles__profile__get_packed_size(pprof);
    
    if (packed_size == 0) {
        perftools__profiles__profile__free_unpacked(pprof, &profile_allocator);
        return 0;
    }
    
    uint8_t* buffer = static_cast<uint8_t*>(malloc(packed_size));
    if (!buffer) {
        perftools__profiles__profile__free_unpacked(pprof, &profile_allocator);
        return 0;
    }
    
    size_t actual_size = pprof_pb_message_pack(
        reinterpret_cast<const ProtobufCMessage*>(pprof),
        buffer
    );
    
    // Clean up protobuf structures (buffer is transferred to caller)
    perftools__profiles__profile__free_unpacked(pprof, &profile_allocator);
    
    if (actual_size == 0) {
        free(buffer);
        return 0;
    }
    
    *data = buffer;
    return actual_size;
}

/**
 * @brief Convert string table to protobuf format
 * 
 * Copies all strings from the internal string table to protobuf format.
 * Each string is allocated using the protobuf allocator to ensure
 * consistent memory management.
 */
void perftools__profiles__profile__add_strings(const std::string* strings, size_t count, Perftools__Profiles__Profile* pprof, ProtobufCAllocator* allocator) {
    pprof->n_string_table = count;
    if (count == 0) return;
    
    pprof->string_table = static_cast<char**>(pb_alloc(allocator, count * sizeof(char*)));
    
    for (size_t i = 0; i < count; ++i) {
        size_t str_len = strings[i].length() + 1;  // +1 for null terminator
        pprof->string_table[i] = static_cast<char*>(pb_alloc(allocator, str_len));
        memcpy(pprof->string_table[i], strings[i].c_str(), str_len);
    }
}

void perftools__profiles__profile__set_sample_type(int64_t type, int64_t unit, Perftools__Profiles__Profile* pprof, ProtobufCAllocator* allocator) {
    // Create wall-time sample types
    pprof->n_sample_type = 1;
    pprof->sample_type = static_cast<Perftools__Profiles__ValueType**>(
        pb_alloc(allocator, pprof->n_sample_type * sizeof(Perftools__Profiles__ValueType*))
    );
    
    auto* sample_type_0 = static_cast<Perftools__Profiles__ValueType*>(
        pb_alloc(allocator, sizeof(Perftools__Profiles__ValueType))
    );
    perftools__profiles__value_type__init(sample_type_0);
    sample_type_0->type = type;
    sample_type_0->unit = unit;
    pprof->sample_type[0] = sample_type_0;
}

void perftools__profiles__profile__set_period(int64_t type, int64_t unit, int64_t period, Perftools__Profiles__Profile* pprof, ProtobufCAllocator* allocator) {
    // Create a separate ValueType for period_type
    auto* period_type = static_cast<Perftools__Profiles__ValueType*>(
        pb_alloc(allocator, sizeof(Perftools__Profiles__ValueType))
    );
    perftools__profiles__value_type__init(period_type);
    period_type->type = type;
    period_type->unit = unit;
    pprof->period_type = period_type;
    
    // Set the period value
    pprof->period = period;
}

void perftools__profiles__profile__add_functions(const function_t* functions, size_t count, Perftools__Profiles__Profile* pprof, ProtobufCAllocator* allocator) {
    pprof->n_function = count;
    if (pprof->n_function == 0) return;
    
    pprof->function = static_cast<Perftools__Profiles__Function**>(
        pb_alloc(allocator, pprof->n_function * sizeof(Perftools__Profiles__Function*))
    );
    
    for (size_t i = 0; i < count; ++i) {
        auto* func = static_cast<Perftools__Profiles__Function*>(
            pb_alloc(allocator, sizeof(Perftools__Profiles__Function))
        );
        perftools__profiles__function__init(func);
        func->id = static_cast<uint64_t>(i + 1);
        func->name = functions[i].name_id;
        func->system_name = functions[i].system_name_id;
        func->filename = functions[i].filename_id;
        pprof->function[i] = func;
    }
}

void perftools__profiles__profile__add_mappings(const mapping_t* mappings, size_t count, Perftools__Profiles__Profile* pprof, ProtobufCAllocator* allocator) {
    pprof->n_mapping = count;
    if (pprof->n_mapping == 0) return;
    
    pprof->mapping = static_cast<Perftools__Profiles__Mapping**>(
        pb_alloc(allocator, pprof->n_mapping * sizeof(Perftools__Profiles__Mapping*))
    );
    
    for (size_t i = 0; i < count; ++i) {
        auto* mapping = static_cast<Perftools__Profiles__Mapping*>(
            pb_alloc(allocator, sizeof(Perftools__Profiles__Mapping))
        );
        perftools__profiles__mapping__init(mapping);
        mapping->id = static_cast<uint64_t>(i + 1);
        mapping->memory_start = mappings[i].memory_start;
        mapping->memory_limit = mappings[i].memory_limit;
        mapping->file_offset = mappings[i].file_offset;
        mapping->filename = mappings[i].filename_id;
        mapping->build_id = mappings[i].build_id;
        pprof->mapping[i] = mapping;
    }
}

void perftools__profiles__profile__add_locations(const location_t* locations, size_t count, Perftools__Profiles__Profile* pprof, ProtobufCAllocator* allocator) {
    pprof->n_location = count;
    if (pprof->n_location == 0) return;
    
    pprof->location = static_cast<Perftools__Profiles__Location**>(
        pb_alloc(allocator, pprof->n_location * sizeof(Perftools__Profiles__Location*))
    );
    
    for (size_t i = 0; i < count; ++i) {
        auto* location = static_cast<Perftools__Profiles__Location*>(
            pb_alloc(allocator, sizeof(Perftools__Profiles__Location))
        );
        perftools__profiles__location__init(location);
        location->id = static_cast<uint64_t>(i + 1);
        location->mapping_id = locations[i].mapping_id;
        location->address = locations[i].address;
        
        // Create line entry
        auto* line = static_cast<Perftools__Profiles__Line*>(
            pb_alloc(allocator, sizeof(Perftools__Profiles__Line))
        );
        perftools__profiles__line__init(line);
        line->function_id = locations[i].function_id;
        line->line = locations[i].line;
        
        location->n_line = 1;
        location->line = static_cast<Perftools__Profiles__Line**>(
            pb_alloc(allocator, sizeof(Perftools__Profiles__Line*))
        );
        location->line[0] = line;
        
        pprof->location[i] = location;
    }
}

void perftools__profiles__profile__add_samples(const std::unordered_map<sample_key_t, sample_data_t>& samples, Perftools__Profiles__Profile* pprof, ProtobufCAllocator* allocator) {
    pprof->n_sample = samples.size();
    if (pprof->n_sample == 0) return;
    
    pprof->sample = static_cast<Perftools__Profiles__Sample**>(
        pb_alloc(allocator, pprof->n_sample * sizeof(Perftools__Profiles__Sample*))
    );
    
    size_t sample_idx = 0;
    for (const auto& [key, sample_data] : samples) {
        auto* sample = static_cast<Perftools__Profiles__Sample*>(
            pb_alloc(allocator, sizeof(Perftools__Profiles__Sample))
        );
        perftools__profiles__sample__init(sample);
        
        // Set location IDs
        sample->n_location_id = key.location_ids.size();
        sample->location_id = static_cast<uint64_t*>(
            pb_alloc(allocator, key.location_ids.size() * sizeof(uint64_t))
        );
        for (size_t i = 0; i < key.location_ids.size(); ++i) {
            sample->location_id[i] = key.location_ids[i];
        }
        
        // Set values
        sample->n_value = sample_data.values.size();
        sample->value = static_cast<int64_t*>(
            pb_alloc(allocator, sample_data.values.size() * sizeof(int64_t))
        );
        for (size_t i = 0; i < sample_data.values.size(); ++i) {
            sample->value[i] = sample_data.values[i];
        }
        
        // Set labels
        sample->n_label = key.labels.size();
        if (sample->n_label > 0) {
            sample->label = static_cast<Perftools__Profiles__Label**>(
                pb_alloc(allocator, sample->n_label * sizeof(Perftools__Profiles__Label*))
            );
            for (size_t i = 0; i < key.labels.size(); ++i) {
                auto* label = static_cast<Perftools__Profiles__Label*>(
                    pb_alloc(allocator, sizeof(Perftools__Profiles__Label))
                );
                perftools__profiles__label__init(label);
                label->key = key.labels[i].key_id;
                label->str = key.labels[i].str_id;
                label->num = key.labels[i].num;
                label->num_unit = key.labels[i].num_unit_id;
                sample->label[i] = label;
            }
        }
        
        pprof->sample[sample_idx++] = sample;
    }
}

} // namespace dd::profiler
