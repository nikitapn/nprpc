// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "utils.hpp"
#include <boost/container/small_vector.hpp>
#include <cassert>
#include <stack>
namespace npidl {

int get_fundamental_size(TokenId token_id)
{
  switch (token_id) {
  case TokenId::Boolean:
  case TokenId::Int8:
  case TokenId::UInt8:
    return 1;
  case TokenId::Int16:
  case TokenId::UInt16:
    return 2;
  case TokenId::Int32:
  case TokenId::UInt32:
  case TokenId::Float32:
    return 4;
  case TokenId::Int64:
  case TokenId::UInt64:
  case TokenId::Float64:
    return 8;
  default:
    assert(false);
    return 0;
  }
}

int align_offset(int align_of,
                 int& last_field_ended,
                 int size,
                 int elements_size)
{
  int unalign = last_field_ended % align_of;
  int offset = last_field_ended + (unalign ? align_of - unalign : 0);
  last_field_ended = offset + (size * elements_size);
  return offset;
}

void calc_struct_size_align(AstStructDecl* s,
                            AstTypeDecl* t,
                            int& offset,
                            int elements_size)
{
  switch (t->id) {
  case FieldType::Fundamental: {
    int size = get_fundamental_size(cft(t)->token_id);
    if (s->align < size)
      s->align = size;
    align_offset(size, offset, size, elements_size);
    break;
  }
  case FieldType::Struct: {
    auto member_s = cflat(t);
    assert(member_s->align != -1 && member_s->size != -1);
    if (s->align < member_s->align)
      s->align = member_s->align;
    align_offset(member_s->align, offset, member_s->size, elements_size);
    break;
  }
  case FieldType::Array:
    calc_struct_size_align(s, car(t)->type, offset, car(t)->length);
    break;
  case FieldType::Alias:
    calc_struct_size_align(s, calias(t)->type, offset, 1);
    break;
  case FieldType::Enum: {
    int size = get_fundamental_size(cenum(t)->token_id);
    if (s->align < size)
      s->align = size;
    align_offset(size, offset, size, elements_size);
    break;
  }
  case FieldType::Vector:
  case FieldType::String:
    if (s->align < 4)
      s->align = 4;
    align_offset(4, offset, 8, elements_size);
    break;
  case FieldType::Optional:
    if (s->align < 4)
      s->align = 4;
    align_offset(4, offset, 4, elements_size);
    break;
  case FieldType::Object: // nprpc::detail::flat::ObjectId
    if (s->align < static_cast<int>(align_of_object))
      s->align = align_of_object;
    align_offset(align_of_object, offset, size_of_object, elements_size);
    break;
  default:
    assert(false);
    break;
  }
}

void calc_struct_size_align(AstStructDecl* s)
{
  s->align = 1;
  int offset = 0;

  for (auto f : s->fields) {
    calc_struct_size_align(s, f->type, offset, 1);
  }

  s->size = align_offset(s->align, offset, 0);

  // std::cout << s->name << ": s. " << s->size << " a. " << s->align <<"\n";
}

std::tuple<int, int> get_type_size_align(AstTypeDecl* type)
{
  switch (type->id) {
  case FieldType::Fundamental: {
    int size = get_fundamental_size(cft(type)->token_id);
    return {size, size};
  }
  case FieldType::Struct:
    return {cflat(type)->size, cflat(type)->align};
  case FieldType::Array: {
    auto const [size, align] = get_type_size_align(car(type)->type);
    return {size * car(type)->length, align};
  }
  case FieldType::Vector:
  case FieldType::String:
    return {8, 4};
  case FieldType::Optional:
    return {4, 4};
  case FieldType::Enum: {
    const auto size = get_fundamental_size(cenum(type)->token_id);
    return {size, size};
  }
  case FieldType::Alias:
    return get_type_size_align(calias(type)->get_real_type());

  case FieldType::Object:
    return {size_of_object, align_of_object};

  default:
    assert(false);
    break;
  }

  std::cerr << "Error: get_type_size_align called with unsupported type id: " << int(type->id) << "\n";
  std::abort();

  return {};
}

void get_type_id(const AstTypeDecl* type,
                 struct_id_t& id,
                 std::string* field_name)
{
  switch (type->id) {
  case FieldType::Fundamental:
    switch (cft(type)->token_id) {
    case TokenId::Boolean:
      id += "Fb";
      break;
    case TokenId::Int8:
      id += "Fi8";
      break;
    case TokenId::UInt8:
      id += "Fu8";
      break;
    case TokenId::Int16:
      id += "Fi16";
      break;
    case TokenId::UInt16:
      id += "Fu16";
      break;
    case TokenId::Int32:
      id += "Fi32";
      break;
    case TokenId::UInt32:
      id += "Fu32";
      break;
    case TokenId::Int64:
      id += "Fi64";
      break;
    case TokenId::UInt64:
      id += "Fu64";
      break;
    case TokenId::Float32:
      id += "Ff32";
      break;
    case TokenId::Float64:
      id += "Ff64";
      break;
    default:
      assert(false);
      break;
    }
    break;
  case FieldType::Struct:
    id += static_cast<const AstStructDecl*>(type)->name;
    if (field_name)
      id += *field_name;
    break;
  case FieldType::Array:
    id += 'A';
    get_type_id(static_cast<const AstWrapType*>(type)->type, id, nullptr);
    break;
  case FieldType::Vector:
    id += 'V';
    get_type_id(static_cast<const AstWrapType*>(type)->type, id, nullptr);
    break;
  case FieldType::String:
    id += 'S';
    break;
  case FieldType::Optional:
    id += '?';
    get_type_id(static_cast<const AstWrapType*>(type)->type, id, nullptr);
    break;
  case FieldType::Object:
    id += 'O';
    break;
  case FieldType::Enum:
    id += 'E' + cenum(type)->name;
    break;
  case FieldType::Alias:
    id += 'L' + calias(type)->name;
    break;
  default:
    assert(false);
    break;
  }
}

const struct_id_t& AstStructDecl::get_function_struct_id()
{
  if (unique_id.length() > 0)
    return unique_id;

  int param_n = 0;
  for (auto f : fields) {
    unique_id += std::to_string(++param_n);
    get_type_id(f->type, unique_id, &f->name);
  }

  return unique_id;
}

bool is_flat(AstTypeDecl* type)
{
  switch (type->id) {
  case FieldType::Fundamental:
  case FieldType::Enum:
    return true;
  case FieldType::Struct:
    return cflat(type)->flat;
  case FieldType::Alias:
    return is_flat(calias(type)->type);
  case FieldType::Array:
    return is_flat(car(type)->type);
  case FieldType::Vector:
  case FieldType::String:
  case FieldType::Optional:
  case FieldType::Object:
  case FieldType::Interface:
    return false;
  default:
    assert(false);
    return false;
  }
}

bool is_fundamental(AstTypeDecl* type)
{
  switch (type->id) {
  case FieldType::Fundamental:
  case FieldType::Enum:
    return true;
  case FieldType::Alias:
    return is_fundamental(calias(type)->type);
  default:
    return false;
  }
}

bool contains_object(AstTypeDecl* type)
{
  switch (type->id) {
  case FieldType::Object:
    return true;
  case FieldType::Struct: {
    auto s = cflat(type);
    for (auto f : s->fields) {
      if (contains_object(f->type))
        return true;
    }
    return false;
  }
  case FieldType::Array:
  case FieldType::Vector:
    return contains_object(cwt(type)->type);
  case FieldType::Optional:
    return contains_object(cwt(type)->real_type());
  case FieldType::Alias:
    return contains_object(calias(type)->type);
  default:
    return false;
  }
}

void dfs_interface(std::function<void(AstInterfaceDecl*)> fn,
                   AstInterfaceDecl* start)
{
  using T = std::pair<size_t, std::vector<AstInterfaceDecl*>*>;
  std::stack<T, boost::container::small_vector<T, 8>> stack;
  stack.push({0, &start->plist});
  fn(start);
  do {
    auto& top = stack.top();
    if (top.first < top.second->size()) {
      auto next_ifs = (*top.second)[top.first++];
      fn(next_ifs);
      stack.push({0, &next_ifs->plist});
    } else {
      stack.pop();
    }
  } while (!stack.empty());
};

} // namespace npidl