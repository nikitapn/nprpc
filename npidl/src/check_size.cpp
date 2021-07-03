
//#define BROKEN

#ifndef BROKEN
# include "../../include/nprpc/nprpc_base.hpp"
# include "utils.hpp"

constexpr size_t check_size_of_object = sizeof(nprpc::detail::flat::ObjectId);
constexpr size_t check_align_of_object = alignof(nprpc::detail::flat::ObjectId);

static_assert(check_size_of_object == size_of_object);
static_assert(check_align_of_object == align_of_object);


constexpr size_t check_object_id_size_static = offsetof(nprpc::detail::flat::ObjectId, class_id);
static_assert(check_object_id_size_static == size_of_object_without_class_id);

constexpr size_t check_size_of_call_header = sizeof(nprpc::detail::flat::CallHeader);
constexpr size_t check_align_of_call_header = alignof(nprpc::detail::flat::CallHeader);

static_assert(check_size_of_call_header == size_of_call_header);
static_assert(check_align_of_call_header == align_of_call_header);

#endif
