/*******************************************************************************
* Copyright 2017-2022 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include <algorithm> // for std::reverse and std::copy
#include <functional> // for std::bind and std::placeholders
#include <list>
#include <string> // for std::string
#include <utility> // for std::pair
#include <vector> // for std::vector

#include <assert.h>

#include "oneapi/dnnl/dnnl.hpp"
#if DNNL_GPU_RUNTIME == DNNL_RUNTIME_OCL
#include "oneapi/dnnl/dnnl_ocl.hpp"
#elif DNNL_GPU_RUNTIME == DNNL_RUNTIME_DPCPP
#include "oneapi/dnnl/dnnl_sycl.hpp"
#endif

#if DNNL_CPU_THREADING_RUNTIME == DNNL_RUNTIME_THREADPOOL
#include "oneapi/dnnl/dnnl_threadpool.h"
#endif

#ifndef DNNL_DISABLE_PRIMITIVE_CACHE
#include "src/common/primitive_cache.hpp"
#endif

#include "cpu/platform.hpp"

#include "tests/test_thread.hpp"

#include "dnnl_common.hpp"
#include "dnnl_memory.hpp"

#if DNNL_GPU_RUNTIME == DNNL_RUNTIME_OCL \
        || DNNL_GPU_RUNTIME == DNNL_RUNTIME_SYCL
extern "C" dnnl_status_t dnnl_impl_gpu_set_profiling(int flag);
extern "C" dnnl_status_t dnnl_impl_gpu_reset_profiling();
extern "C" dnnl_status_t dnnl_impl_gpu_get_profile_info(
        uint64_t &time, double &freq, int mode);
#endif

int check_pd_cache(const_dnnl_primitive_desc_t pd) {
#ifndef DNNL_DISABLE_PRIMITIVE_CACHE
    int capacity = 0;
    DNN_SAFE(dnnl_get_primitive_cache_capacity(&capacity), CRIT);
    if (capacity && !dnnl::impl::is_pd_in_cache(pd)) {
        BENCHDNN_PRINT(0, "error: %s\n",
                "primitive descriptor is expected to be fetched from "
                "the primitive cache");
        return FAIL;
    }
#endif
    return OK;
}

int check_primitive_cache(dnnl_primitive_t p) {
#ifndef DNNL_DISABLE_PRIMITIVE_CACHE
    int capacity = 0;
    DNN_SAFE(dnnl_get_primitive_cache_capacity(&capacity), CRIT);
    if (capacity && !dnnl::impl::is_primitive_in_cache(p)) {
        BENCHDNN_PRINT(0, "error: %s\n",
                "primitive is expected to be fetched from the primitive "
                "cache");
        return FAIL;
    }
#endif
    return OK;
}

size_t set_primitive_cache_capacity_without_clearing(size_t capacity) {
#ifndef DNNL_DISABLE_PRIMITIVE_CACHE
    return dnnl::impl::set_primitive_cache_capacity_without_clearing(capacity);
#endif
    return size_t(0);
}

int get_cache_blob_id(
        std::vector<uint8_t> &cache_blob_id, const_dnnl_primitive_desc_t pd) {
    dnnl_dim_t count;
    const uint8_t *c_id;
    DNN_SAFE(dnnl_primitive_desc_query(
                     pd, dnnl_query_cache_blob_id_size_s64, 0, (void *)&count),
            WARN);
    DNN_SAFE(dnnl_primitive_desc_query(
                     pd, dnnl_query_cache_blob_id, 0, (void **)&c_id),
            WARN);
    cache_blob_id = {c_id, c_id + count};
    return OK;
}

int get_cache_blob(std::vector<uint8_t> &cache_blob, dnnl_primitive_t prim) {
    size_t size = 0;
    DNN_SAFE(dnnl_primitive_get_cache_blob(prim, &size, nullptr), WARN);

    cache_blob.resize(size);
    DNN_SAFE(dnnl_primitive_get_cache_blob(prim, &size, cache_blob.data()),
            WARN);
    return OK;
}

struct lru_cache_t {
    lru_cache_t(size_t capacity) : capacity_(capacity) {}

    const std::vector<uint8_t> &get(const std::vector<uint8_t> &key) {
        auto it = cache_mapper_.find(key);
        if (it == cache_mapper_.end()) { return dummy_; }
        cache_list_.splice(cache_list_.begin(), cache_list_, it->second);
        return cache_list_.front().value_;
    }

    void add(const std::vector<uint8_t> &key,
            const std::vector<uint8_t> &value) {
        assert(!cache_mapper_.count(key));
        if (cache_mapper_.size() >= capacity_) {
            cache_mapper_.erase(cache_list_.back().key_);
            cache_list_.pop_back();
        }
        cache_list_.push_front(entry_t(key, value));
        cache_mapper_.insert({key, cache_list_.begin()});
    }

private:
    lru_cache_t(const lru_cache_t &other) = delete;
    lru_cache_t(lru_cache_t &&other) = delete;
    lru_cache_t &operator=(const lru_cache_t &other) = delete;
    lru_cache_t &operator=(lru_cache_t &&other) = delete;

    struct entry_t {
        entry_t(const std::vector<uint8_t> &key,
                const std::vector<uint8_t> &value)
            : key_(key), value_(value) {}
        std::vector<uint8_t> key_;
        std::vector<uint8_t> value_;
    };

    size_t capacity_;
    std::list<entry_t> cache_list_;
    std::map<std::vector<uint8_t>, std::list<entry_t>::iterator> cache_mapper_;

    const std::vector<uint8_t> dummy_;
};

lru_cache_t &get_test_cache() {
    static lru_cache_t cache(1024);
    return cache;
}

int test_persistent_cache_api(benchdnn_dnnl_wrapper_t<dnnl_primitive_t> &prim,
        const_dnnl_primitive_desc_t pd, res_t *res) {
    if (!is_gpu() || (is_gpu() && DNNL_GPU_RUNTIME != DNNL_RUNTIME_OCL)) {
        return OK;
    }

    // Start testing persistent cache API.
    // 1. Disable primitive cache to make sure that the next primitive will
    // be created from the cache blob and not fetched from the primitive cache.
    const auto old_capacity = set_primitive_cache_capacity_without_clearing(0);
    // 2. Get cache blob ID to use it as a key for the `test_cache`.
    std::vector<uint8_t> cache_blob_id;
    SAFE(get_cache_blob_id(cache_blob_id, pd), WARN);
    // 3. Check if a cache blob for the obtained cache blob ID is present in the
    //    `test_cache`.
    //    a) If the cache blob is found the primitive is created from it.
    //    b) If the cache blob is not found then get it from the previously
    //       created primitive, store it in the cache and create the primitive
    //       from it.
    dnnl_primitive_t p {};
    auto &cache = get_test_cache();
    auto cache_value = cache.get(cache_blob_id);
    if (!cache_value.empty()) {
        const size_t size = cache_value.size();
        const uint8_t *cache_blob = cache_value.data();
        DNN_SAFE(
                dnnl_primitive_create_from_cache_blob(&p, pd, size, cache_blob),
                WARN);
    } else {
        std::vector<uint8_t> cache_blob;
        SAFE(get_cache_blob(cache_blob, prim), WARN);

        // The cross-engine reorder is a special primitive that may contain no
        // kernels therefore the cache blob will always be empty, which is
        // the correct behavior.
        if (cache_blob.empty()) {
            set_primitive_cache_capacity_without_clearing(old_capacity);
            if (res->impl_name.find("cross_engine") != std::string::npos)
                return OK;

            BENCHDNN_PRINT(
                    0, "error: %s\n", "cache blob is not expected to be empty");
            res->state = FAILED;
            return FAIL;
        }

        DNN_SAFE(dnnl_primitive_create_from_cache_blob(
                         &p, pd, cache_blob.size(), cache_blob.data()),
                WARN);
        cache.add(cache_blob_id, cache_blob);
    }
    prim.reset(p);

    // 4. Restore the original primitive cache capacity to make it functional.
    set_primitive_cache_capacity_without_clearing(old_capacity);

    return OK;
}

float round_to_nearest_representable(dnnl_data_type_t dt, float value) {
    switch (dt) {
        case dnnl_f32: break;
        case dnnl_f64: break;
        case dnnl_bf16: value = (float)dnnl::impl::bfloat16_t(value); break;
        case dnnl_f16: value = (float)dnnl::impl::float16_t(value); break;
        case dnnl_s32:
        case dnnl_s8:
        case dnnl_u8: value = maybe_saturate(dt, value); break;
        default: SAFE(FAIL, CRIT);
    }

    return value;
}

// Engine kind used to run oneDNN primitives for testing
dnnl_engine_kind_t engine_tgt_kind = dnnl_cpu;
// Engine index used to run oneDNN primitives for testing
size_t engine_index = 0;
// CPU ISA specific hints : none by default
isa_hints_t hints {isa_hints_t::none};

memory_kind_ext_t memory_kind {default_memory_kind};

void init_isa_settings() {
    if (hints.get() == isa_hints_t::no_hints)
        DNN_SAFE_V(dnnl_set_cpu_isa_hints(dnnl_cpu_isa_no_hints));
    else if (hints.get() == isa_hints_t::prefer_ymm)
        DNN_SAFE_V(dnnl_set_cpu_isa_hints(dnnl_cpu_isa_prefer_ymm));
    else {
        // Do nothing when hints == none
        assert(hints.get() == isa_hints_t::none);
    }
}

args_t &args_t::set(int arg, const dnn_mem_t &mem) {
    args_.emplace_back(arg, &mem);
    return *this;
}

args_t &args_t::set(
        const std::vector<int> &args, const std::vector<dnn_mem_t> &mems) {
    assert(args.size() == mems.size());
    for (size_t i = 0; i < mems.size(); ++i)
        args_.emplace_back(args[i], &mems[i]);
    return *this;
}

const dnn_mem_t &args_t::find(int arg) const {
    static dnn_mem_t empty_stub;
    for (const auto &e : args_) {
        if (e.first == arg) return *(e.second);
    }
    return empty_stub;
}

// Unmap before passing the memory to execute
void execute_unmap_args(
        const args_t &args, std::vector<dnnl_exec_arg_t> &dnnl_args) {
    dnnl_args.resize(args.size());
    for (int i = 0; i < args.size(); ++i) {
        if (args.dnn_mem(i).is_mapped()) args.dnn_mem(i).unmap();

        dnnl_args[i].arg = args.arg(i);
        dnnl_args[i].memory = args.dnn_mem(i).m_;
    }
}

// Map the memory back after execute
void execute_map_args(const args_t &args) {
    for (int i = 0; i < args.size(); ++i)
        if (!args.dnn_mem(i).is_mapped()) args.dnn_mem(i).map();
}

int execute_and_wait(perf_function_t &exec_func, const dnnl_engine_t &engine,
        const args_t &args, res_t *res) {
    stream_t stream(engine);
    std::vector<dnnl_exec_arg_t> dnnl_args;

    execute_unmap_args(args, dnnl_args);

    DNN_SAFE(exec_func(stream, dnnl_args), CRIT);
    DNN_SAFE(dnnl_stream_wait(stream), CRIT);
    if (res) res->state = EXECUTED;

    execute_map_args(args);

    return OK;
}

dnnl_status_t primitive_executor(dnnl_primitive_t prim,
        const dnnl_stream_t &stream,
        const std::vector<dnnl_exec_arg_t> &dnnl_args) {
    return dnnl_primitive_execute(
            prim, stream, (int)dnnl_args.size(), dnnl_args.data());
}

int execute_and_wait(dnnl_primitive_t prim, const args_t &args, res_t *res) {
    perf_function_t exec_func = std::bind(&primitive_executor, prim,
            std::placeholders::_1, std::placeholders::_2);
    auto pd = query_pd(prim);
    auto engine = query_engine(pd);
    return execute_and_wait(exec_func, engine, args, res);
}

void enable_gpu_profiling() {
#if DNNL_GPU_RUNTIME == DNNL_RUNTIME_OCL \
        || DNNL_GPU_RUNTIME == DNNL_RUNTIME_SYCL
    if (!is_bench_mode(PROF)) return;
    DNN_SAFE_V(dnnl_impl_gpu_set_profiling(1));
#endif
}

void disable_gpu_profiling() {
#if DNNL_GPU_RUNTIME == DNNL_RUNTIME_OCL \
        || DNNL_GPU_RUNTIME == DNNL_RUNTIME_SYCL
    if (!is_bench_mode(PROF)) return;
    DNN_SAFE_V(dnnl_impl_gpu_reset_profiling());
    DNN_SAFE_V(dnnl_impl_gpu_set_profiling(0));
#endif
}

void reset_gpu_profiling() {
#if DNNL_GPU_RUNTIME == DNNL_RUNTIME_OCL \
        || DNNL_GPU_RUNTIME == DNNL_RUNTIME_SYCL
    if (!is_bench_mode(PROF)) return;
    DNN_SAFE_V(dnnl_impl_gpu_reset_profiling());
#endif
}

void get_gpu_profiling_info(uint64_t &nsec, double &freq, int mode) {
#if DNNL_GPU_RUNTIME == DNNL_RUNTIME_OCL \
        || DNNL_GPU_RUNTIME == DNNL_RUNTIME_SYCL
    if (!is_bench_mode(PROF)) return;
    DNN_SAFE_V(dnnl_impl_gpu_get_profile_info(nsec, freq, mode));
#endif
}

bool should_stop(const timer::timer_t &t) {
    const bool stop = false
            || (fix_times_per_prb && t.times() >= fix_times_per_prb)
            || (!fix_times_per_prb && t.total_ms() >= max_ms_per_prb
                    && t.times() >= min_times_per_prb);
    return stop;
}

bool should_stop_ctime(const timer::timer_t &ct) {
    // TODO (kgajdamo): establish the conditions
    // for completing the ctime measurement.
    const bool stop = ct.times() >= ctimes_per_prb;
    return stop;
}

inline int measure_perf_individual(timer::timer_t &t, dnnl_stream_t stream,
        perf_function_t &perf_func, std::vector<dnnl_exec_arg_t> &dnnl_args) {
    t.reset();
    while (true) {
        DNN_SAFE(perf_func(stream, dnnl_args), WARN);
        t.stamp();
        if (should_stop(t)) break;
    }
    return OK;
}

inline int measure_perf_aggregate(timer::timer_t &t, dnnl_stream_t stream,
        perf_function_t &perf_func, std::vector<dnnl_exec_arg_t> &dnnl_args) {
    const int max_batch_times = 10000;

    // Warm-up run, this is not measured due to possibility the associated
    // kernel has not been built and skews the results.
    DNN_SAFE(perf_func(stream, dnnl_args), WARN);
    DNN_SAFE(dnnl_stream_wait(stream), WARN);

    int cur_batch_times
            = fix_times_per_prb ? fix_times_per_prb : min_times_per_prb;

    t.reset();
    reset_gpu_profiling();

    bool is_first_loop = true;
    while (true) {
        for (int i = 0; i < cur_batch_times; i++) {
            DNN_SAFE(perf_func(stream, dnnl_args), WARN);
        }
        DNN_SAFE(dnnl_stream_wait(stream), WARN);

        if (is_bench_mode(PROF)) {
            uint64_t nsec = 0;
            double freq = 0;
            get_gpu_profiling_info(nsec, freq, 0);
            reset_gpu_profiling();
            t.stamp_with_frequency(cur_batch_times, nsec / 1e6, freq);
        } else {
            t.stamp(cur_batch_times);
        }

        if (should_stop(t)) break;

        // Adjust cur_batch_times after the first batch run
        if (is_first_loop) {
            double ms_min = t.ms(timer::timer_t::min);
            // Heuristic: try to use ~5 batch runs for the whole benchmark
            int batch_times_heuristic = (ms_min == 0.0)
                    ? INT_MAX
                    : MAX2(1,
                            (int)((max_ms_per_prb - t.total_ms()) / ms_min
                                    / 5));
            cur_batch_times = MIN2(max_batch_times, batch_times_heuristic);
            is_first_loop = false;
        }
    }

    return OK;
}

int measure_perf(const thr_ctx_t &ctx, res_t *res, perf_function_t &perf_func,
        args_t &args) {
    int ret = OK;
    if (is_bench_mode(PERF)) {
        const auto &engine = get_test_engine();
        stream_t stream(engine, ctx.get_interop_obj());
        std::vector<dnnl_exec_arg_t> dnnl_args;
        execute_unmap_args(args, dnnl_args);

        auto &t = res->timer_map.perf_timer();
        // For non-DPCPP CPU: measure individual iterations.
        // For DPCPP CPU and GPU: measure iterations in batches to hide driver
        // overhead. DPCPP CPU follows the model of GPU, thus, handled similar.
        if (is_cpu() && !is_sycl_engine(engine))
            ret = execute_in_thr_ctx(ctx, measure_perf_individual, t, stream,
                    perf_func, dnnl_args);
        else
            ret = execute_in_thr_ctx(ctx, measure_perf_aggregate, t, stream,
                    perf_func, dnnl_args);

        if (ret == OK) execute_map_args(args);
    }
    return ret;
}

int measure_perf(
        const thr_ctx_t &ctx, res_t *res, dnnl_primitive_t prim, args_t &args) {
    perf_function_t perf_func = std::bind(&primitive_executor, prim,
            std::placeholders::_1, std::placeholders::_2);

    return measure_perf(ctx, res, perf_func, args);
}

void maybe_prepare_runtime_scales(dnn_mem_t &scales_m,
        const attr_t::scale_t &scale, int64_t scale_cnt, const float *scales) {
    if (!scale.runtime) return;

    const int64_t count = scale.policy == policy_t::COMMON ? 1 : scale_cnt;

    scales_m = dnn_mem_t(1, &count, dnnl_f32, tag::x, get_test_engine());
    for (int64_t c = 0; c < count; ++c)
        ((float *)scales_m)[c] = scales[c];
}

void maybe_prepare_runtime_zero_points(dnn_mem_t &zero_points_m,
        const attr_t &attr, int arg, int64_t count,
        const int32_t *zero_points) {
    if (!attr.zero_points.runtime(arg)) return;

    const auto e = attr.zero_points.get(arg);
    const int64_t cnt = e.policy == policy_t::COMMON ? 1 : count;

    zero_points_m = dnn_mem_t(1, &cnt, dnnl_s32, tag::x, get_test_engine());
    for (int64_t c = 0; c < cnt; ++c)
        ((int32_t *)zero_points_m)[c] = zero_points[c];
}

std::vector<float> prepare_po_vals(const dnn_mem_t &dst_m, const args_t &args,
        const std::vector<std::pair<int, int>> &v_po_masks,
        const size_t dst_off) {
    std::vector<float> v_vals(v_po_masks.size());

    for (size_t d = 0; d < v_po_masks.size(); ++d) {
        const auto po_offset
                = dst_m.get_scale_idx(dst_off, v_po_masks[d].second);
        const float val = args.find(v_po_masks[d].first).get_elem(po_offset);
        v_vals[d] = val;
    }
    return v_vals;
}

bool check_md_consistency_with_tag(
        const_dnnl_memory_desc_t md, const std::string &tag) {
    auto md_new_tag = dnn_mem_t::init_md(
            query_md_ndims(md), query_md_dims(md), query_md_data_type(md), tag);
    return dnnl_memory_desc_equal(md_new_tag, md);
}

void skip_start(res_t *res) {
    if (benchdnn_stat.tests < test_start) {
        res->state = SKIPPED;
        res->reason = SKIP_START;
        return;
    }
}

void skip_unimplemented_data_type(
        const std::vector<dnnl_data_type_t> &v_dt, dir_t dir, res_t *res) {
    const bool has_f64_support = is_f64_supported();
#if DNNL_CPU_RUNTIME != DNNL_RUNTIME_NONE
    using namespace dnnl::impl::cpu::platform;
    // bf16 is supported on AVX512-CORE+
    const bool has_bf16_support
            = is_gpu() || (is_cpu() && has_data_type_support(dnnl_bf16));
    const bool has_f16_support = (is_gpu() && (dir & FLAG_FWD))
            || (is_cpu() && has_data_type_support(dnnl_f16));

#else
    const bool has_bf16_support = is_gpu();
    // f16 is supported on GPU for inference only.
    const bool has_f16_support = is_gpu() && (dir & FLAG_FWD);
#endif

    for (const auto &i_dt : v_dt) {
        bool need_skip = false;
        switch (i_dt) {
            case dnnl_bf16: need_skip = !has_bf16_support; break;
            case dnnl_f16: need_skip = !has_f16_support; break;
            case dnnl_f64: need_skip = !has_f64_support; break;
            default: break;
        }
        if (need_skip) {
            res->state = SKIPPED, res->reason = DATA_TYPE_NOT_SUPPORTED;
            return;
        }
    }
}

void skip_unimplemented_sum_po(
        const attr_t &attr, res_t *res, dnnl_data_type_t dst_dt) {
    const auto &po = attr.post_ops;
    if (po.is_def()) return;

    const int first_sum_idx = po.find(attr_t::post_ops_t::SUM);
    if (first_sum_idx == -1) return;

    const auto sum_dt = po.entry[first_sum_idx].sum.dt;

    for (int idx = 0; idx < po.len(); ++idx) {
        const auto &e = po.entry[idx];
        if (e.is_sum_kind()) {
            // Sum with zero-point is not supported on GPU
            if (is_gpu() && e.sum.zero_point != 0) {
                res->state = SKIPPED, res->reason = CASE_NOT_SUPPORTED;
                break;
            }
            // Each sum must have same data on CPU
            if (is_cpu() && e.sum.dt != sum_dt) {
                res->state = SKIPPED, res->reason = CASE_NOT_SUPPORTED;
                break;
            }
            // Sum must have data type with the same size like dst on both
            if (dst_dt != dnnl_data_type_undef && sum_dt != dnnl_data_type_undef
                    && dnnl_data_type_size(dst_dt)
                            != dnnl_data_type_size(e.sum.dt)) {
                res->state = SKIPPED, res->reason = CASE_NOT_SUPPORTED;
                return;
            }
        }
    }
}

void skip_unimplemented_arg_scale(const attr_t &attr, res_t *res) {
    for (const auto &arg_s : attr.scales.scales) {
        if (arg_s.second.policy != policy_t::COMMON) {
            res->state = SKIPPED, res->reason = CASE_NOT_SUPPORTED;
            return;
        }
    }
}

void skip_invalid_inplace(res_t *res, dnnl_data_type_t sdt,
        dnnl_data_type_t ddt, const std::string &stag,
        const std::string &dtag) {
    // Note: existing implementation of dnn_mem_t doesn't allow to track the
    // fact that two different objects pointing on the same SYCL memory should
    // not map/unmap both objects.
    // This leads to the restriction that memory descriptors should coincide,
    // thus, a single memory object would be used for in-place validation.
    // General limitation of in-place mode is having same amount of memory on
    // input and output.
    if (sdt != ddt) {
        res->state = SKIPPED, res->reason = INVALID_CASE;
        return;
    }

    if (dtag == tag::any) return;
    if (stag != dtag) {
        res->state = SKIPPED, res->reason = INVALID_CASE;
        return;
    }
}

// Check ensures that attributes don't cause implementation fallback
int check_same_pd(const dnnl_primitive_desc_t &pd_no_attr, res_t *res) {
    const auto pd_no_attr_name = query_impl_info(pd_no_attr);
    if (res->impl_name == pd_no_attr_name) return OK;

    res->state = FAILED;
    BENCHDNN_PRINT(0,
            "ERROR: attributes caused impl fallback from [%s] to [%s]\n",
            pd_no_attr_name.c_str(), res->impl_name.c_str());
    return FAIL;
}

bool is_cpu(const dnnl_engine_t &engine) {
    return query_engine_kind(engine) == dnnl_cpu;
}

bool is_gpu(const dnnl_engine_t &engine) {
    return query_engine_kind(engine) == dnnl_gpu;
}

bool is_sycl_engine(const dnnl_engine_t &engine) {
    if (is_cpu(engine)) return DNNL_CPU_RUNTIME == DNNL_RUNTIME_DPCPP;
    if (is_gpu(engine)) return DNNL_GPU_RUNTIME == DNNL_RUNTIME_DPCPP;
    return false;
}

bool is_opencl_engine(const dnnl_engine_t &engine) {
    if (is_gpu(engine)) return DNNL_GPU_RUNTIME == DNNL_RUNTIME_OCL;
    return false;
}

bool is_nvidia_gpu(const dnnl_engine_t &engine) {
#ifdef DNNL_WITH_SYCL
    if (!is_gpu(engine)) return false;
    constexpr int nvidia_vendor_id = 0x10DE;
    auto eng = dnnl::engine(engine, true);
    auto device = dnnl::sycl_interop::get_device(eng);
    const auto eng_vendor_id
            = device.get_info<::sycl::info::device::vendor_id>();
    return eng_vendor_id == nvidia_vendor_id;
#endif
    return false;
}

bool is_amd_gpu(const dnnl_engine_t &engine) {
#ifdef DNNL_WITH_SYCL
    if (!is_gpu(engine)) return false;
    constexpr int amd_vendor_id = 0x1002;
    auto eng = dnnl::engine(engine, true);
    auto device = dnnl::sycl_interop::get_device(eng);
    const auto eng_vendor_id
            = device.get_info<::sycl::info::device::vendor_id>();
    return eng_vendor_id == amd_vendor_id;
#endif
    return false;
}

bool is_f64_supported(const dnnl_engine_t &engine) {
    if (!is_gpu(engine)) return false;
    if (is_nvidia_gpu(engine) || is_amd_gpu()) return false;
#if DNNL_GPU_RUNTIME == DNNL_RUNTIME_DPCPP
    if (is_sycl_engine(engine)) {
        auto eng = dnnl::engine(engine, true);
        auto dev = dnnl::sycl_interop::get_device(eng);
#ifdef DNNL_SYCL_INTEROP_USE_SYCL121
        return dev.has_extension("cl_khr_fp64");
#else
        return dev.has(::sycl::aspect::fp64);
#endif
    }
#endif
#if DNNL_GPU_RUNTIME == DNNL_RUNTIME_OCL
    if (is_opencl_engine(engine)) {
        auto eng = dnnl::engine(engine, true);
        cl_device_id dev = dnnl::ocl_interop::get_device(eng);
        size_t param_size = 0;
        cl_int err = clGetDeviceInfo(
                dev, CL_DEVICE_EXTENSIONS, 0, nullptr, &param_size);
        if (err != CL_SUCCESS) return false;

        std::string extension_string(param_size, '\0');
        err = clGetDeviceInfo(dev, CL_DEVICE_EXTENSIONS, param_size,
                &extension_string[0], &param_size);
        if (err != CL_SUCCESS) return false;

        return extension_string.find("cl_khr_fp64") != std::string::npos;
    }
#endif
    return false;
}

#if defined(_WIN32) && !defined(__GNUC__)
#include "windows.h"

static size_t get_cpu_ram_size() {
    MEMORYSTATUSEX s {};
    s.dwLength = sizeof(s);
    GlobalMemoryStatusEx(&s);
    return s.ullTotalPhys;
}
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__QNXNTO__)
#include <unistd.h>
#include <sys/sysctl.h>

static size_t get_cpu_ram_size() {
#ifdef __APPLE__
    int query_ram[] = {CTL_HW, HW_MEMSIZE};
#else
    int query_ram[] = {CTL_HW, HW_PHYSMEM};
#endif
    int query_ram_len = sizeof(query_ram) / sizeof(*query_ram);
    size_t totalram = 0;
    size_t length = sizeof(totalram);

    sysctl(query_ram, query_ram_len, &totalram, &length, NULL, 0);
    return totalram;
}
#else
#include <sys/sysinfo.h>

static size_t get_cpu_ram_size() {
    struct sysinfo s {};
    sysinfo(&s);
    return s.totalram;
}
#endif

static size_t get_gpu_ram_size() {
    // XXX: create a tmp engine to query what we need.
    // It will be removed in the future as part of switching back
    // to the global engine.
    engine_t eng_tmp(engine_tgt_kind);
    dnnl::engine eng(eng_tmp, true);
    if (eng.get_kind() != dnnl::engine::kind::gpu) return 0;

#if DNNL_GPU_RUNTIME == DNNL_RUNTIME_OCL
    cl_int status = CL_SUCCESS;
    // Get single device attached to the engine.
    engine_t engine_tgt(engine_tgt_kind);
    cl_device_id ocl_device = dnnl::ocl_interop::get_device(eng);

    cl_ulong ram_size = 0;
    status = clGetDeviceInfo(ocl_device, CL_DEVICE_GLOBAL_MEM_SIZE,
            sizeof(cl_ulong), &ram_size, nullptr);
    if (status == CL_SUCCESS) return (size_t)ram_size;
#elif DNNL_GPU_RUNTIME == DNNL_RUNTIME_DPCPP
    auto sycl_dev = dnnl::sycl_interop::get_device(eng);
    return (size_t)sycl_dev.get_info<::sycl::info::device::global_mem_size>();
#endif
    return 0;
}

struct check_mem_size_args_t {
    check_mem_size_args_t(const_dnnl_primitive_desc_t pd, bool want_input,
            bool add_ref_size = false)
        : pd(pd)
        , want_input(want_input)
        , add_ref_size(add_ref_size)
        , is_scratchpad(false)
        , total_size_device(0)
        , total_size_cpu(0)
        , scratchpad_size(0) {}

    // Input args.
    const_dnnl_primitive_desc_t pd;
    bool want_input;
    bool add_ref_size;
    bool is_scratchpad;

    // Output args.
    size_t total_size_device;
    size_t total_size_cpu;
    size_t scratchpad_size;
};

static int check_total_size(
        const check_mem_size_args_t &check_mem_size_args, res_t *res) {
    static uint64_t cpu_device_capacity = get_cpu_ram_size();
    static uint64_t gpu_device_capacity = get_gpu_ram_size();

    const uint64_t device_max_capacity
            = is_cpu() ? cpu_device_capacity : gpu_device_capacity;
    const uint64_t cpu_max_capacity = cpu_device_capacity;

    // 0.75f is taken randomly and is subject to change in future.
    const double capacity_factor = 0.75;
    const double benchdnn_device_limit = capacity_factor * device_max_capacity;
    const double benchdnn_cpu_limit = capacity_factor * cpu_max_capacity;
    assert(benchdnn_device_limit > 0 && benchdnn_cpu_limit > 0);

    const bool fits_device_ram = is_gpu()
            ? (check_mem_size_args.total_size_device <= benchdnn_device_limit)
            : true;
    if (!fits_device_ram) {
        BENCHDNN_PRINT(
                2, "%s\n", "benchdnn: not enough device RAM for a problem.");
        res->state = SKIPPED;
        res->reason = NOT_ENOUGH_RAM;
    }

    auto GB = [](double bytes) { return bytes / powf(2, 30); };

    if (is_gpu()) {
        BENCHDNN_PRINT((!fits_device_ram ? 2 : 6),
                "Requested: %g GB, benchdnn device limit: %g GB, device RAM "
                "capacity: %g GB\n",
                GB(check_mem_size_args.total_size_device),
                GB(benchdnn_device_limit), GB(gpu_device_capacity));
    }

    size_t total_size_cpu = check_mem_size_args.total_size_cpu;
    if (is_cpu()) total_size_cpu += check_mem_size_args.total_size_device;
    bool fits_cpu_ram = total_size_cpu <= benchdnn_cpu_limit;

    if (!fits_cpu_ram) {
        BENCHDNN_PRINT(
                2, "%s\n", "benchdnn: not enough CPU RAM for a problem.");
        // Try to catch a huge scratchpad size requested by the library.
        // Use following logic:
        //     scratch_size
        // ---------------------- <= 0.75 (pre-defined threshold).
        // io_size + scratch_size
        //
        // 0.75 value supposed to be experimental and might be adjusted.
        static constexpr float scratch_trh = 0.75f;
        if (check_mem_size_args.scratchpad_size
                > scratch_trh * total_size_cpu) {
            BENCHDNN_PRINT(2, "%s `%ld` %s `%ld`.\n",
                    "benchdnn: CPU scratchpad size",
                    (long)check_mem_size_args.scratchpad_size,
                    "exceeded a given threshold",
                    (long)(scratch_trh * total_size_cpu));
            res->state = FAILED;
        } else {
            res->state = SKIPPED;
        }
        res->reason = NOT_ENOUGH_RAM;
    }

    BENCHDNN_PRINT((!fits_cpu_ram ? 2 : 6),
            "Requested: %g GB, benchdnn CPU limit: %g GB, CPU RAM capacity: %g "
            "GB\n",
            GB(total_size_cpu), GB(benchdnn_cpu_limit),
            GB(cpu_device_capacity));

    return res->state == FAILED ? FAIL : OK;
}

static void add_md_size(const_dnnl_memory_desc_t md,
        check_mem_size_args_t &check_mem_size_args) {
    const auto mem_size = dnnl_memory_desc_get_size(md);
    // Runtime mem size is not defined.
    if (mem_size == 0 || mem_size == DNNL_RUNTIME_SIZE_VAL) return;

    check_mem_size_args.total_size_device += mem_size; // Original memory size.
    if (!check_mem_size_args.add_ref_size) return;

    // Reference memories are always tag::abx fp32, hence need re-creating
    // memory descriptor and take its size.
    auto ref_md = dnn_mem_t::init_md(
            query_md_ndims(md), query_md_dims(md), dnnl_f32, tag::abx);
    const auto ref_md_size = dnnl_memory_desc_get_size(ref_md);

    // Correctness pass allocates additional tag::abx f32 memory.
    bool compare_mem_factor = !check_mem_size_args.want_input
            && check_mem_size_args.add_ref_size;

    // All memory is mapped once it is created and unmapped only before
    // primitive execution. Device memory requires additional buffer for mapped
    // memory.
    // XXX: In DPC++ build oneDNN uses USM memory, which shouldn't require an
    // additional buffer, so map factor should be equal to 0 for DPC++.
    // However due to a driver issue oneDNN pretends that shared USM is not
    // accessible on the host, hence map will allocate an extra memory.
    check_mem_size_args.total_size_cpu += !is_cpu() * mem_size; // Map factor.
    if (check_mem_size_args.is_scratchpad) {
        check_mem_size_args.scratchpad_size += mem_size;
    } else {
        check_mem_size_args.total_size_cpu += ref_md_size; // Reference memory.
        // Comparison memory.
        check_mem_size_args.total_size_cpu += compare_mem_factor * ref_md_size;
    }
}

bool is_fwd_prop_kind(dnnl_prop_kind_t prop_kind) {
    return prop_kind == dnnl_forward_training
            || prop_kind == dnnl_forward_inference
            || prop_kind == dnnl_prop_kind_undef;
}

static void get_memory_bytes(check_mem_size_args_t &check_mem_size_args) {
    auto const_pd = check_mem_size_args.pd;
    const int n_idx = check_mem_size_args.want_input
            ? query_n_inputs(const_pd)
            : query_n_outputs(const_pd);
    const auto prop_kind = query_prop_kind(const_pd);
    const bool is_fwd = is_fwd_prop_kind(prop_kind);

#define MD(name) dnnl_query_##name##_md
    std::vector<dnnl_query_t> query_fwd_in_mds {MD(src), MD(weights)};
    std::vector<dnnl_query_t> query_fwd_out_mds {MD(dst), MD(workspace)};

    std::vector<dnnl_query_t> query_bwd_in_mds {
            MD(src), MD(weights), MD(dst), MD(diff_dst), MD(workspace)};
    std::vector<dnnl_query_t> query_bwd_out_mds {
            MD(diff_src), MD(diff_weights)};
#undef MD

    const auto &query_in_mds = is_fwd ? query_fwd_in_mds : query_bwd_in_mds;
    const auto &query_out_mds = is_fwd ? query_fwd_out_mds : query_bwd_out_mds;
    const auto &query_mds
            = check_mem_size_args.want_input ? query_in_mds : query_out_mds;

    for_(const auto query : query_mds)
    for (int idx = 0; idx < n_idx; ++idx) {
        const auto &md = query_md(const_pd, query, idx);
        add_md_size(md, check_mem_size_args);
    }
}

int check_mem_size(const_dnnl_memory_desc_t md, res_t *res) {
    if (!mem_check) return OK;

    check_mem_size_args_t check_mem_size_args(nullptr, false, false);
    check_mem_size_args.total_size_device = dnnl_memory_desc_get_size(md);

    return check_total_size(check_mem_size_args, res);
}

int check_mem_size(const_dnnl_primitive_desc_t const_pd, res_t *res) {
    if (!mem_check) return OK;

    // Get input sizes.
    check_mem_size_args_t check_mem_size_args(const_pd, /* want_input = */ true,
            /* add_ref_size = */ true);
    get_memory_bytes(check_mem_size_args);

    // Get scratchpad size. Treat it as `want_input=true` to avoid comparison
    // factor count. Since scratchpad modes are mutual excluded, it takes sizes
    // of both modes since either of them will report 0 size.
    check_mem_size_args.is_scratchpad = true;
    const auto &scratchpad_md = query_md(const_pd, DNNL_ARG_SCRATCHPAD);
    add_md_size(scratchpad_md, check_mem_size_args);
    check_mem_size_args.is_scratchpad = false;
    check_mem_size_args.total_size_device += query_mem_consumption(const_pd);
    check_mem_size_args.scratchpad_size += query_mem_consumption(const_pd);

    // Get output sizes.
    check_mem_size_args.want_input = false;
    get_memory_bytes(check_mem_size_args);

    return check_total_size(check_mem_size_args, res);
}

int get_memory_footprint(const_dnnl_primitive_desc_t const_pd, res_t *res) {
    check_mem_size_args_t check_mem_in_size_args(
            const_pd, /* want_input = */ true);
    get_memory_bytes(check_mem_in_size_args); // Get input bytes.
    check_mem_size_args_t check_mem_out_size_args(
            const_pd, /* want_input = */ false);
    get_memory_bytes(check_mem_out_size_args); // Get output bytes.

    // Update read bytes with dst bytes in case of sum post-op.
    auto const_attr_po = query_post_ops(const_pd);
    auto po_len = dnnl_post_ops_len(const_attr_po);
    for (int idx = 0; idx < po_len; ++idx) {
        const auto kind = dnnl_post_ops_get_kind(const_attr_po, idx);
        if (kind == dnnl_sum) {
            const auto &dst_md = query_md(const_pd, DNNL_ARG_DST);
            add_md_size(dst_md, check_mem_in_size_args);
        }
    }

    res->ibytes = check_mem_in_size_args.total_size_device;
    res->obytes = check_mem_out_size_args.total_size_device;

    return OK;
}

memory_kind_ext_t str2memory_kind(const char *str) {
#define CASE(param) \
    if (!strcasecmp(#param, str)) return memory_kind_ext_t::param

    CASE(usm);
    CASE(buffer);
    CASE(usm_device);
    CASE(usm_shared);

#undef CASE

    assert(!"not expected");
    return memory_kind_ext_t::usm;
}

static void maybe_print_cpu_engine_error_message() {
#if DNNL_CPU_RUNTIME == DNNL_RUNTIME_SYCL
    fprintf(stderr,
            "ERROR: can't create CPU engine. Possible reasons for this error:\n"
            "- Incorrect SYCL_DEVICE_FILTER. The filter must be either unset "
            "or include 'opencl:cpu' devices.\n"
            "- Missing TBB library which is required for OpenCL CPU runtime. "
            "Check that TBB library is available in the system.\n"
            "- Missing OpenCL CPU runtime or other issues with OpenCL CPU "
            "runtime. Check that output from `sycl-ls` or `clinfo -l` commands "
            "include any CPU devices.\n");
#endif
}

engine_t::engine_t(dnnl_engine_kind_t engine_kind) : is_owner_(true) {
    enable_gpu_profiling();
    size_t idx = engine_kind == dnnl_cpu ? 0 : engine_index;
    dnnl_status_t status = dnnl_engine_create(&engine_, engine_kind, idx);
    if (engine_kind == dnnl_cpu && status != dnnl_success)
        maybe_print_cpu_engine_error_message();
    DNN_SAFE_V(status);
}

engine_t::engine_t(dnnl_engine_t engine) : engine_(engine), is_owner_(false) {}

engine_t::engine_t(const engine_t &other) {
    is_owner_ = other.is_owner_;

    if (!is_owner_) {
        engine_ = other.engine_;
        return;
    }

    dnnl_engine_kind_t engine_kind;
    DNN_SAFE_V(dnnl_engine_get_kind(other.engine_, &engine_kind));

    if (engine_kind == dnnl_cpu) {
#if DNNL_CPU_RUNTIME == DNNL_RUNTIME_SYCL
        void *dev;
        void *ctx;
        DNN_SAFE_V(dnnl_sycl_interop_engine_get_device(other.engine_, &dev));
        DNN_SAFE_V(dnnl_sycl_interop_engine_get_context(other.engine_, &ctx));
        DNN_SAFE_V(dnnl_sycl_interop_engine_create(&engine_, dev, ctx));
#else
        DNN_SAFE_V(dnnl_engine_create(&engine_, dnnl_cpu, 0));
#endif
    } else if (engine_kind == dnnl_gpu) {
#if DNNL_GPU_RUNTIME == DNNL_RUNTIME_OCL
        cl_device_id dev;
        cl_context ctx;
        DNN_SAFE_V(dnnl_ocl_interop_get_device(other.engine_, &dev));
        DNN_SAFE_V(dnnl_ocl_interop_engine_get_context(other.engine_, &ctx));
        DNN_SAFE_V(dnnl_ocl_interop_engine_create(&engine_, dev, ctx));
#elif DNNL_GPU_RUNTIME == DNNL_RUNTIME_SYCL
        void *dev;
        void *ctx;
        DNN_SAFE_V(dnnl_sycl_interop_engine_get_device(other.engine_, &dev));
        DNN_SAFE_V(dnnl_sycl_interop_engine_get_context(other.engine_, &ctx));
        DNN_SAFE_V(dnnl_sycl_interop_engine_create(&engine_, dev, ctx));
#endif
    } else {
        assert(!"unsupported engine kind");
    }
}

engine_t::~engine_t() {
    if (is_owner_) DNN_SAFE_V(dnnl_engine_destroy(engine_));
}

stream_t::stream_t(dnnl_engine_t engine, void *interop_obj) {
#if DNNL_CPU_THREADING_RUNTIME == DNNL_RUNTIME_THREADPOOL
    if (is_cpu(engine)) {
        auto tp = static_cast<dnnl::threadpool_interop::threadpool_iface *>(
                interop_obj);
        if (tp == nullptr) tp = dnnl::testing::get_threadpool();
        SAFE_V(dnnl_threadpool_interop_stream_create(&stream_, engine, tp));
        return;
    }
#endif
    DNN_SAFE_V(dnnl_stream_create(&stream_, engine, dnnl_stream_default_flags));
}

stream_t::~stream_t() {
    DNN_SAFE_V(dnnl_stream_destroy(stream_));
}

float reorder_rescale_factor() {
    float factor = 1.f;
#if DNNL_CPU_RUNTIME != DNNL_RUNTIME_NONE
    if (is_cpu(get_test_engine()))
        factor = dnnl::impl::cpu::platform::s8s8_weights_scale_factor();
#endif
    return factor;
}

dims_t md2dims(const dnnl_memory_desc_t &md) {
    auto ndims = query_md_ndims(md);
    dims_t dims(ndims, 0);
    for (int d = 0; d < ndims; ++d)
        dims[d] = query_md_dims(md)[d];
    return dims;
}

dnnl_data_type_t deduce_cfg_data_type(
        dnnl_data_type_t in_dt, const attr_t &attr, data_kind_t dk) {
    dnnl_data_type_t dt_ = in_dt;

    if ((dk == SRC || dk == WEI) && dt_ == dnnl_f32) {
        // Update data type based on fpmath-mode attribute
        switch (attr.fpmath_mode) {
            case dnnl_fpmath_mode_strict: break;
            case dnnl_fpmath_mode_bf16: dt_ = dnnl_bf16; break;
            case dnnl_fpmath_mode_tf32: dt_ = dnnl_bf16; break;
            default: assert(!"unsupported_fpmath_mode"); SAFE_V(CRIT);
        }
    } else if (dk == DST) {
        // Sum post-op defines the type of filling destination.
        const int sum_idx = attr.post_ops.find(attr_t::post_ops_t::SUM);
        if (sum_idx >= 0) {
            auto sum_dt = attr.post_ops.entry[sum_idx].sum.dt;
            if (sum_dt != dnnl_data_type_undef) dt_ = sum_dt;
        }
    }

    return dt_;
}
