/*******************************************************************************
* Copyright 2020-2022 Intel Corporation
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

/// @file
/// C API

#ifndef ONEAPI_DNNL_DNNL_GRAPH_H
#define ONEAPI_DNNL_DNNL_GRAPH_H

#include "oneapi/dnnl/dnnl_graph_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @addtogroup dnnl_graph_api
/// @{

/// @addtogroup dnnl_graph_api_allocator
/// @{

/// Creates a host allocator with the given allocation and deallocation
/// call-back function pointers.
///
/// @param allocator Output allocator.
/// @param host_malloc A pointer to malloc function for host.
/// @param host_free A pointer to free function for host.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_allocator_create(
        dnnl_graph_allocator_t *allocator,
        dnnl_graph_host_allocate_f host_malloc,
        dnnl_graph_host_deallocate_f host_free);

/// Destroys an allocator.
///
/// @param allocator The allocator to be destroyed.

/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_allocator_destroy(
        dnnl_graph_allocator_t allocator);

/// @} dnnl_graph_api_allocator

/// @addtogroup dnnl_graph_api_logical_tensor
/// @{

/// Initializes a logical tensor with id, data type, number of dimensions,
/// layout type, and property. The logical tensor's dims are unknown with this
/// interface.
///
/// @param logical_tensor Output logical tensor.
/// @param tid The unique id of the output logical tensor.
/// @param dtype Elements data type.
/// @param ndims Number of dimensions.
/// @param ltype Layout type of the underlying tensor buffer.
/// @param ptype Tensor property type.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_logical_tensor_init(
        dnnl_graph_logical_tensor_t *logical_tensor, size_t tid,
        dnnl_graph_data_type_t dtype, int32_t ndims,
        dnnl_graph_layout_type_t ltype, dnnl_graph_tensor_property_t ptype);

/// Initializes a logical tensor with basic information and dims. The logical
/// tensor's dimensions and layout will be initialized according to the input
/// arguments.
///
/// @note
///     If dims contains all valid values and layout type is
///     #dnnl_graph_layout_type_strided. The strides field in
///     #dnnl_graph_logical_tensor_t will be calculated in a row major and
///     contiguous way. Otherwise, Accessing the strides field is an undefined
///     behavior.
///
///     Eg. dims (2, 3, 4, 5) will get strides (60, 20, 5, 1)
///
/// @param logical_tensor Output logical tensor.
/// @param tid The unique id of output logical tensor.
/// @param dtype Elements data type.
/// @param ndims Number of dimensions.
/// @param dims Array of dimensions.
/// @param ltype Layout type of the underlying tensor memory.
/// @param ptype Tensor property type.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_logical_tensor_init_with_dims(
        dnnl_graph_logical_tensor_t *logical_tensor, size_t tid,
        dnnl_graph_data_type_t dtype, int32_t ndims,
        const dnnl_graph_dims_t dims, dnnl_graph_layout_type_t ltype,
        dnnl_graph_tensor_property_t ptype);

/// Initializes a logical tensor with dimensions and strides provided by user.
///
/// @note
///     Once strides are explicitly provided through the API, the `layout_type`
///     in #dnnl_graph_logical_tensor_t can only be
///     #dnnl_graph_layout_type_strided or #dnnl_graph_layout_type_any.
///
/// @param logical_tensor Output logical tensor.
/// @param tid The unique id of output logical tensor.
/// @param dtype Elements data type.
/// @param ndims Number of dimensions.
/// @param dims Array of dimensions.
/// @param strides Array of strides.
/// @param ptype Tensor property type.
/// @returns #dnnl_graph_success on success or a status describing the error
/// otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_logical_tensor_init_with_strides(
        dnnl_graph_logical_tensor_t *logical_tensor, size_t tid,
        dnnl_graph_data_type_t dtype, int32_t ndims,
        const dnnl_graph_dims_t dims, const dnnl_graph_dims_t strides,
        dnnl_graph_tensor_property_t ptype);

/// Returns the memory size described by the logical tensor. If it's a strided
/// layout, the size will be calculated by `dims` and `strides`. If it's an
/// opaque layout, the size will be decided by `layout_id`.
///
/// @param logical_tensor Logical tensor.
/// @param size Output memory size in bytes.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_logical_tensor_get_mem_size(
        const dnnl_graph_logical_tensor_t *logical_tensor, size_t *size);

/// Compares if two logical tensors have the same layout.
///
/// @param lt1 The handle of first logical tensor.
/// @param lt2 The handle of second logical tensor.
/// @param is_same 1 if these two logical tensors have the same layout and 0 if
///     these two logical tensors have different layouts.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_logical_tensor_has_same_layout(
        const dnnl_graph_logical_tensor_t *lt1,
        const dnnl_graph_logical_tensor_t *lt2, uint8_t *is_same);

/// @} dnnl_graph_api_logical_tensor

/// @addtogroup dnnl_graph_api_tensor
/// @{

/// Creates a tensor with logical tensor, engine, and data handle.
///
/// @param tensor Output tensor.
/// @param logical_tensor Description for this tensor.
/// @param engine Engine to use.
/// @param handle Handle of the memory buffer to use as an underlying storage.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_tensor_create(
        dnnl_graph_tensor_t *tensor,
        const dnnl_graph_logical_tensor_t *logical_tensor,
        const_dnnl_graph_engine_t engine, void *handle);

/// Destroys a tensor.
///
/// @param tensor The tensor to be destroyed.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_tensor_destroy(
        dnnl_graph_tensor_t tensor);

/// Gets the data handle of a tensor. If `type` doesn't match tensor's data
/// type, nullptr will be returned.
///
/// @param tensor The input tensor.
/// @param type Expected data type of the tensor.
/// @param handle Pointer to the data of input tensor.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_tensor_get_if_type(
        const_dnnl_graph_tensor_t tensor, dnnl_graph_data_type_t type,
        void **handle);

/// Set data handle for a tensor.
///
/// @param tensor The input tensor.
/// @param handle New data handle for tensor.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_tensor_set_data_handle(
        dnnl_graph_tensor_t tensor, void *handle);

/// Returns the engine of a tensor object.
///
/// @param tensor The input tensor.
/// @param engine Output engine on which the tensor is located.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_tensor_get_engine(
        const_dnnl_graph_tensor_t tensor, dnnl_graph_engine_t *engine);

/// @} dnnl_graph_api_tensor

/// @addtogroup dnnl_graph_api_op
/// @{

/// Initializes an op with unique id, kind, and name.
///
/// @param op Output op
/// @param id The unique id of the output op.
/// @param kind The op kind.
/// @param verbose_name The string added as the op name.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_op_create(dnnl_graph_op_t *op,
        uint64_t id, dnnl_graph_op_kind_t kind, const char *const verbose_name);

/// Destroys an op.
///
/// @param op The op to be destroyed.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_op_destroy(dnnl_graph_op_t op);

/// Adds input logical tensor to the op.
///
/// @param op Input op.
/// @param input The input logical tensor to be added.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_op_add_input(
        dnnl_graph_op_t op, const dnnl_graph_logical_tensor_t *input);

/// Adds output logical tensor to the op.
///
/// @param op Input op.
/// @param output The output logical tensor to be added.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_op_add_output(
        dnnl_graph_op_t op, const dnnl_graph_logical_tensor_t *output);

/// Sets floating point attribute to an op.
///
/// @param op Input op.
/// @param name The attribute's name.
/// @param value The attribute's value.
/// @param value_len The length of the value. 0 means a single floating point
///     value while 1 means a vector of floating point values with size = 1.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_op_set_attr_f32(
        dnnl_graph_op_t op, dnnl_graph_op_attr_t name, const float *value,
        size_t value_len);

/// Sets boolean attribute to an op.
///
/// @param op Input op.
/// @param name The attribute's name.
/// @param value The attribute's value.
/// @param value_len The length of the value. 0 means a single boolean value.
///     Currently, the library doesn't support a vector of boolean values.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_op_set_attr_bool(
        dnnl_graph_op_t op, dnnl_graph_op_attr_t name, const uint8_t *value,
        size_t value_len);

/// Sets integer attribute to an op.
///
/// @param op Input op.
/// @param name The attribute's name.
/// @param value The attribute's value.
/// @param value_len The length of the value. 0 means a single integer value
///     while 1 means a vector of integer values with size = 1.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_op_set_attr_s64(
        dnnl_graph_op_t op, dnnl_graph_op_attr_t name, const int64_t *value,
        size_t value_len);

/// Sets string attribute to an op.
///
/// @param op Input op.
/// @param name The attribute's name.
/// @param value The attribute's value.
/// @param value_len The length of the string value.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_op_set_attr_str(
        dnnl_graph_op_t op, dnnl_graph_op_attr_t name, const char *value,
        size_t value_len);

/// Returns the unique id of an op.
///
/// @param op Input op.
/// @param id Output the unique id.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_op_get_id(
        const_dnnl_graph_op_t op, size_t *id);

/// Returns the kind of an op.
///
/// @param op Input op.
/// @param kind Output op kind.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_op_get_kind(
        const_dnnl_graph_op_t op, dnnl_graph_op_kind_t *kind);

/// @} dnnl_graph_api_op

/// @addtogroup dnnl_graph_api_partition
/// @{

/// Creates a new empty partition.
///
/// @param partition The handle of the output partition.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_partition_create(
        dnnl_graph_partition_t *partition);

/// Creates a new partition with a given operator and engine kind. The API is
/// used to create a partition from an operation directly without creating the
/// graph and calling `get_partitions()`. The output partition contains only one
/// operation.
///
/// @param partition The handle of output partition.
/// @param op The operation used to create partition.
/// @param ekind The engine kind used to create partition.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_partition_create_with_op(
        dnnl_graph_partition_t *partition, const_dnnl_graph_op_t op,
        dnnl_graph_engine_kind_t ekind);

/// Destroys a partition.
///
/// @param partition The partition to be destroyed.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_partition_destroy(
        dnnl_graph_partition_t partition);

/// Returns the number of operations in a partition.
///
/// @param partition The target partition.
/// @param num Output the number of operations.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_partition_get_op_num(
        const_dnnl_graph_partition_t partition, size_t *num);

/// Returns the list of op IDs of the partition.
///
/// @param partition The target partition.
/// @param num The number of ops.
/// @param ids Output the op IDs.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_partition_get_ops(
        dnnl_graph_partition_t partition, size_t num, size_t *ids);

/// Returns the ID of a partition.
///
/// @param partition The target partition.
/// @param id Output the ID of the partition.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_partition_get_id(
        const_dnnl_graph_partition_t partition, size_t *id);

/// Compiles a partition with given input and output logical tensors. The output
/// logical tensors can contain unknown dimensions. For this case, the
/// compilation will deduce the output shapes according to input shapes. The
/// output logical tensors can also have layout type `any`. The compilation will
/// choose the optimal layout for output tensors. The optimal layout will be
/// represented as an opaque layout ID saved in the output logical tensor.
///
/// @param partition The target partition.
/// @param compiled_partition Output compiled partition.
/// @param in_num The number of input logical tensors.
/// @param inputs A list of input logical tensors.
/// @param out_num The number of output logical tensors.
/// @param outputs A list of output logical tensors.
/// @param engine The target engine of the compilation.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_partition_compile(
        dnnl_graph_partition_t partition,
        dnnl_graph_compiled_partition_t compiled_partition, size_t in_num,
        const dnnl_graph_logical_tensor_t **inputs, size_t out_num,
        const dnnl_graph_logical_tensor_t **outputs,
        const_dnnl_graph_engine_t engine);

/// Returns the number of input logical tensors of a partition.
///
/// @param partition The target partition.
/// @param num Output the number of input logical tensors.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_partition_get_in_ports_num(
        const_dnnl_graph_partition_t partition, size_t *num);

/// Returns a list of input logical tensors from a partition.
///
/// @param partition The target partition.
/// @param num The number of input logical tensors.
/// @param inputs The list of input logical tensors.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_partition_get_in_ports(
        const_dnnl_graph_partition_t partition, size_t num,
        dnnl_graph_logical_tensor_t *inputs);

/// Returns the number of output logical tensors of a partition.
///
/// @param partition The target partition.
/// @param num Output the number of output logical tensors.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_partition_get_out_ports_num(
        const_dnnl_graph_partition_t partition, size_t *num);

/// Returns a list of output logical tensors from a partition.
///
/// @param partition The target partition.
/// @param num The number of output logical tensors.
/// @param outputs The list of output logical tensors.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_partition_get_out_ports(
        const_dnnl_graph_partition_t partition, size_t num,
        dnnl_graph_logical_tensor_t *outputs);

/// Returns the supporting status of a partition. Some operations may not be
/// supported by the library under certain circumstances. During partitioning
/// stage, unsupported partitions will be returned to users with each containing
/// an unsupported operation. Users should check the supporting status of a
/// partition before transforming the computation graph or compiling the
/// partition.
///
/// @param partition The target partition.
/// @param is_supported Output flag to indicate the supporting status. 0 means
///     unsupported while 1 means supported.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_partition_is_supported(
        const_dnnl_graph_partition_t partition, uint8_t *is_supported);

/// Returns the engine kind of a partition.
///
/// @param partition The target partition.
/// @param kind The output engine kind.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_partition_get_engine_kind(
        const_dnnl_graph_partition_t partition, dnnl_graph_engine_kind_t *kind);

/// Returns the kind of the partition. It defines the basic structure of the
/// subgraph contained in a partition.
///
/// @param partition The target partition.
/// @param kind The output partition kind.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_partition_get_kind(
        const_dnnl_graph_partition_t partition,
        dnnl_graph_partition_kind_t *kind);

/// @} dnnl_graph_api_partition

/// @addtogroup dnnl_graph_api_compiled_partition
/// @{

/// Creates a new compiled partition handle.
///
/// @param compiled_partition The handle of output compiled partition.
/// @param partition The handle of input partition.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_compiled_partition_create(
        dnnl_graph_compiled_partition_t *compiled_partition,
        dnnl_graph_partition_t partition);

/// Executes a compiled partition.
///
/// @param compiled_partition The handle of target compiled partition.
/// @param stream The stream used for execution.
/// @param num_inputs The number of input tensors.
/// @param inputs A list of input tensors.
/// @param num_outputs The number of output tensors.
/// @param outputs A non-empty list of output tensors.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_compiled_partition_execute(
        const_dnnl_graph_compiled_partition_t compiled_partition,
        const_dnnl_graph_stream_t stream, size_t num_inputs,
        const_dnnl_graph_tensor_t *inputs, size_t num_outputs,
        const_dnnl_graph_tensor_t *outputs);

/// Destroys a compiled partition.
///
/// @param compiled_partition The compiled partition to be destroyed.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_compiled_partition_destroy(
        dnnl_graph_compiled_partition_t compiled_partition);

/// Queries an input or output logical tensor according to tensor ID. If the
/// tensor ID doesn't belong to any input or output of the compiled partition,
/// an error status #dnnl_graph_invalid_arguments will be returned by the API.
///
/// @param compiled_partition The handle of target compiled_partition.
/// @param tid The unique id of required tensor.
/// @param lt The output logical tensor.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API
dnnl_graph_compiled_partition_query_logical_tensor(
        const_dnnl_graph_compiled_partition_t compiled_partition, size_t tid,
        dnnl_graph_logical_tensor_t *lt);

/// Returns the hint of in-place pairs from a compiled partition. It indicates
/// that an input and an output of the partition can share the same memory
/// buffer for computation. In-place computation helps to reduce the memory
/// footprint and improves cache locality. But since the library may not have a
/// global view of user's application, it's possible that the tensor with
/// `input_id` is used at other places in user's computation graph. In this
/// case, the user should take the in-place pair as a hint and pass a different
/// memory buffer for output tensor to avoid overwriting the input memory buffer
/// which will probably cause unexpected incorrect results.
///
/// @param compiled_partition The handle of target compiled_partition.
/// @param num The number of in-place pairs.
/// @param inplace_pairs The handle of in-place pairs.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API
dnnl_graph_compiled_partition_get_inplace_ports(
        const_dnnl_graph_compiled_partition_t compiled_partition, size_t *num,
        const dnnl_graph_inplace_pair_t **inplace_pairs);

/// @} dnnl_graph_api_compiled_partition

/// @addtogroup dnnl_graph_api_engine
/// @{

/// Creates an engine with specified engine kind and device index.
///
/// @param engine The handle of output engine.
/// @param kind The kind of engine.
/// @param index The device associated to created engine.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_engine_create(
        dnnl_graph_engine_t *engine, dnnl_graph_engine_kind_t kind,
        size_t index);

/// Creates an engine with specified engine kind, device index, and allocator.
///
/// @param engine The handle of output engine.
/// @param kind The kind of engine.
/// @param index The device associated to the engine.
/// @param alloc The allocator associated to the engine.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_engine_create_with_allocator(
        dnnl_graph_engine_t *engine, dnnl_graph_engine_kind_t kind,
        size_t index, const_dnnl_graph_allocator_t alloc);

/// Destroys an engine.
///
/// @param engine The target engine to be destroyed.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_engine_destroy(
        dnnl_graph_engine_t engine);

/// Returns the kind of an engine.
///
/// @param engine The target engine.
/// @param kind The output engine kind.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_engine_get_kind(
        const_dnnl_graph_engine_t engine, dnnl_graph_engine_kind_t *kind);

/// @} dnnl_graph_api_engine

/// @addtogroup dnnl_graph_api_graph
/// @{

/// Creates a new empty graph. A graph is associated to a specific engine kind.
/// The partitions returned from the graph will inherit the engine kind of the
/// graph.
///
/// @param graph The handle of output graph.
/// @param engine_kind The target engine kind.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_graph_create(
        dnnl_graph_graph_t *graph, dnnl_graph_engine_kind_t engine_kind);

/// Creates a new empty graph with an engine kind and a floating-point math
/// mode. All partitions returned from the graph will inherit the engine kind
/// and floating-point math mode.
///
/// @param graph The handle of output graph.
/// @param engine_kind The kind for engine.
/// @param mode The floating-point math mode.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_graph_create_with_fpmath_mode(
        dnnl_graph_graph_t *graph, dnnl_graph_engine_kind_t engine_kind,
        dnnl_graph_fpmath_mode_t mode);

/// Destroys a graph.
///
/// @param graph The graph to be destroyed.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_graph_destroy(
        dnnl_graph_graph_t graph);

/// Adds an operation into a graph. The API will return failure if the operator
/// has already been added to the graph or the operation cannot pass the schema
/// check in the library (eg. input and output numbers and data types, the
/// attributes of the operation, etc.).
///
/// @param graph The target graph.
/// @param op The operation to be added.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_add_op(
        dnnl_graph_graph_t graph, dnnl_graph_op_t op);

/// Filters a graph. Partitions will be claimed internally according to the
/// capability of the library, the engine kind, and the policy.
///
/// @param graph The target graph.
/// @param policy The partition policy.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_graph_filter(
        dnnl_graph_graph_t graph, dnnl_graph_partition_policy_t policy);

/// Returns the number of partitions of a graph. The API should be called after
/// a partition is already filtered. Otherwise, the output number is zero.
///
/// @param graph The graph.
/// @param num Output the number of partitions.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_graph_get_partition_num(
        const_dnnl_graph_graph_t graph, size_t *num);

/// Returns the partitions from a filtered graph.
///
/// @param graph The target graph.
/// @param num The number of partitions.
/// @param partition Output the partitions.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_graph_get_partitions(
        dnnl_graph_graph_t graph, size_t num,
        dnnl_graph_partition_t *partition);

/// @} dnnl_graph_api_graph

/// @addtogroup dnnl_graph_api_stream
/// @{

/// Creates a stream for the specified engine.
///
/// @param stream The handle of output stream.
/// @param engine Engine to create the stream on.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_stream_create(
        dnnl_graph_stream_t *stream, const_dnnl_graph_engine_t engine);

/// Waits for all compiled partitions executing in the stream to finish.
///
/// @param stream The target stream.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_stream_wait(
        dnnl_graph_stream_t stream);

/// Destroys a stream.
///
/// @param stream The target stream to be destroyed.
/// @returns #dnnl_graph_success on success or a status describing the error
///     otherwise.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_stream_destroy(
        dnnl_graph_stream_t stream);

/// @} dnnl_graph_api_stream

/// @addtogroup dnnl_graph_api_compiled_partition_cache
/// @{

/// Returns the number of compiled partitions that can be held in the compiled
/// partition cache at the same time.
///
/// @param capacity Compiled partition cache capacity to query. Concurrently
///     accessing @p capacity is safe.
/// @returns #dnnl_graph_invalid_arguments if the @p capacity value is invalid,
///     and #dnnl_graph_success on success.
dnnl_graph_status_t DNNL_GRAPH_API
dnnl_graph_get_compiled_partition_cache_capacity(int *capacity);

/// Sets a number of compiled partitions that can be held in the compiled
/// partition cache at the same time.
///

/// @param capacity Compiled partition cache capacity to set. The default cache
///     capacity is 1024. If a new @p capacity is less than a number of compiled
///     partition that the compiled partition cache already has, then the excess
///     entries will be evicted. Setting the @p capacity to 0 clears the
///     compiled partition cache and disables it. Concurrently modifying
///     @p capacity is safe.
/// @returns #dnnl_graph_invalid_arguments if the @p capacity value is invalid,
///     and #dnnl_graph_success on success.
dnnl_graph_status_t DNNL_GRAPH_API
dnnl_graph_set_compiled_partition_cache_capacity(int capacity);

/// @} dnnl_graph_api_compiled_partition_cache

/// @addtogroup dnnl_graph_api_constant_tensor_cache
/// @{

/// Controls the enabling or disabling of constant tensor cache. This API must
/// be called once before compilation to take effect.
///
/// @param flag Set to positive value to enable the cache and set to 0 to
///     disable the cache. Negative values are invalid.
/// @returns #dnnl_graph_invalid_arguments if the @p flag value is invalid, and
///     #dnnl_graph_success on success.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_set_constant_tensor_cache(
        int flag);

/// Returns the enabling or disabling status of constant tensor cache.
///
/// @param flag The constant tensor cache enabling status to query.
/// @returns #dnnl_graph_invalid_arguments if the @p flag value is nullptr, and
///     #dnnl_graph_success on success.
dnnl_graph_status_t DNNL_GRAPH_API dnnl_graph_get_constant_tensor_cache(
        int *flag);

/// @} dnnl_graph_api_constant_tensor_cache

/// @addtogroup dnnl_graph_api_service Service
/// @{

/// Returns library version information.
///
/// @returns Pointer to a constant structure containing
///     - major: major version number,
///     - minor: minor version number,
///     - patch: patch release number,
///     - hash: git commit hash.
const dnnl_graph_version_t DNNL_GRAPH_API *dnnl_graph_version(void);

/// @} dnnl_graph_api_service

/// @} dnnl_graph_api

#ifdef __cplusplus
}
#endif
#endif
