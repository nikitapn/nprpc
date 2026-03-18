// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "ts_builder.hpp"
#include "utils.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <string_view>

namespace npidl::builders {

static std::string_view fundamental_to_ts(TokenId id);
static std::string_view fundamental_kind_literal(TokenId id);

using std::placeholders::_1;
using std::placeholders::_2;

static const int token_mod_addr = std::ios_base::xalloc();

template <int _Mod> struct token_os_mod {
  static constexpr int _mod = _Mod;
};

template <int _Write> struct read_write_field : token_os_mod<_Write> {
  inline static int offset_addr = std::ios_base::xalloc();
  int offset;
  explicit read_write_field(int o)
      : offset(o)
  {
  }
};

template <int _Mod>
static std::ostream& operator<<(std::ostream& os,
                                const token_os_mod<_Mod>& /* field */)
{
  os.iword(token_mod_addr) = token_os_mod<_Mod>::_mod;
  return os;
}

template <int _Mod>
static std::ostream& operator<<(std::ostream& os,
                                const read_write_field<_Mod>& field)
{
  os.iword(token_mod_addr) = read_write_field<_Mod>::_mod;
  os.iword(read_write_field<_Mod>::offset_addr) = field.offset;
  return os;
}

using read_field = read_write_field<0>;
using write_field = read_write_field<1>;
using _token_type = token_os_mod<2>;
constexpr auto toktype = _token_type{};

static std::ostream& operator<<(std::ostream& os, const TokenId& token_id)
{
  const auto token_mod = os.iword(token_mod_addr);
  if (token_mod == 0) {
    const auto offset = os.iword(read_field::offset_addr);
    switch (token_id) {
    case TokenId::Boolean:
      os << "(this.buffer.dv.getUint8" << "(this.offset+" << offset
         << ") === 0x01)";
      break;
    case TokenId::Int8:
      os << "this.buffer.dv.getInt8" << "(this.offset+" << offset << ")";
      break;
    case TokenId::UInt8:
      os << "this.buffer.dv.getUint8" << "(this.offset+" << offset << ")";
      break;
    case TokenId::Int16:
      os << "this.buffer.dv.getInt16" << "(this.offset+" << offset << ",true)";
      break;
    case TokenId::UInt16:
      os << "this.buffer.dv.getUint16" << "(this.offset+" << offset << ",true)";
      break;
    case TokenId::Int32:
      os << "this.buffer.dv.getInt32" << "(this.offset+" << offset << ",true)";
      break;
    case TokenId::UInt32:
      os << "this.buffer.dv.getUint32" << "(this.offset+" << offset << ",true)";
      break;
    case TokenId::Int64:
      os << "this.buffer.dv.getBigInt64" << "(this.offset+" << offset
         << ",true)";
      break;
    case TokenId::UInt64:
      os << "this.buffer.dv.getBigUint64" << "(this.offset+" << offset
         << ",true)";
      break;
    case TokenId::Float32:
      os << "this.buffer.dv.getFloat32" << "(this.offset+" << offset
         << ",true)";
      break;
    case TokenId::Float64:
      os << "this.buffer.dv.getFloat64" << "(this.offset+" << offset
         << ",true)";
      break;
    default:
      assert(false);
    }
  } else if (token_mod == 1) {
    const auto offset = os.iword(read_field::offset_addr);
    switch (token_id) {
    case TokenId::Boolean:
      os << "this.buffer.dv.setUint8" << "(this.offset+" << offset
         << ", value === true ? 0x01 : 0x00)";
      break;
    case TokenId::Int8:
      os << "this.buffer.dv.setInt8" << "(this.offset+" << offset << ",value)";
      break;
    case TokenId::UInt8:
      os << "this.buffer.dv.setUint8" << "(this.offset+" << offset << ",value)";
      break;
    case TokenId::Int16:
      os << "this.buffer.dv.setInt16" << "(this.offset+" << offset
         << ",value,true)";
      break;
    case TokenId::UInt16:
      os << "this.buffer.dv.setUint16" << "(this.offset+" << offset
         << ",value,true)";
      break;
    case TokenId::Int32:
      os << "this.buffer.dv.setInt32" << "(this.offset+" << offset
         << ",value,true)";
      break;
    case TokenId::UInt32:
      os << "this.buffer.dv.setUint32" << "(this.offset+" << offset
         << ",value,true)";
      break;
    case TokenId::Int64:
      os << "this.buffer.dv.setBigInt64" << "(this.offset+" << offset
         << ",value,true)";
      break;
    case TokenId::UInt64:
      os << "this.buffer.dv.setBigUint64" << "(this.offset+" << offset
         << ",value,true)";
      break;
    case TokenId::Float32:
      os << "this.buffer.dv.setFloat32" << "(this.offset+" << offset
         << ",value,true)";
      break;
    case TokenId::Float64:
      os << "this.buffer.dv.setFloat64" << "(this.offset+" << offset
         << ",value,true)";
      break;
    default:
      assert(false);
    }
  } else if (token_mod == 2) {
    // os << fundamental_to_ts(token_id);

    switch (token_id) {
    case TokenId::Boolean:
      os << "boolean";
      break;
    case TokenId::Int8:
      os << "i8";
      break;
    case TokenId::UInt8:
      os << "u8";
      break;
    case TokenId::Int16:
      os << "i16";
      break;
    case TokenId::UInt16:
      os << "u16";
      break;
    case TokenId::Int32:
      os << "i32";
      break;
    case TokenId::UInt32:
      os << "u32";
      break;
    case TokenId::Int64:
      os << "i64";
      break;
    case TokenId::UInt64:
      os << "u64";
      break;
    case TokenId::Float32:
      os << "f32";
      break;
    case TokenId::Float64:
      os << "f64";
      break;
    default:
      assert(false);
    }

  } else {
    assert(false);
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const TSBuilder::_ns& ns)
{
  int level = Namespace::substract(ns.builder.ctx_->nm_cur(), ns.nm);
  const auto path = ns.nm->construct_path(".", level);
  if (path.size() == 0 || (path.size() == 1 && path[0] == '.')) {
    return os;
  }

  return os << path << '.';
}

static std::string_view fundamental_to_ts(TokenId id)
{
  using namespace std::string_view_literals;
  switch (id) {
  case TokenId::Boolean:
    return "boolean"sv;
  case TokenId::Int8:
  case TokenId::UInt8:
  case TokenId::Int16:
  case TokenId::UInt16:
  case TokenId::Int32:
  case TokenId::UInt32:
  case TokenId::Float32:
  case TokenId::Float64:
    return "number"sv;
  case TokenId::Int64:
  case TokenId::UInt64:
    return "bigint"sv;
  default:
    throw std::runtime_error("Unknown fundamental type");
  }
}

static std::string_view fundamental_kind_literal(TokenId id)
{
  using namespace std::string_view_literals;
  switch (id) {
  case TokenId::Boolean:
    return "'bool'"sv;
  case TokenId::Int8:
    return "'i8'"sv;
  case TokenId::UInt8:
    return "'u8'"sv;
  case TokenId::Int16:
    return "'i16'"sv;
  case TokenId::UInt16:
    return "'u16'"sv;
  case TokenId::Int32:
    return "'i32'"sv;
  case TokenId::UInt32:
    return "'u32'"sv;
  case TokenId::Int64:
    return "'i64'"sv;
  case TokenId::UInt64:
    return "'u64'"sv;
  case TokenId::Float32:
    return "'f32'"sv;
  case TokenId::Float64:
    return "'f64'"sv;
  default:
    throw std::runtime_error("Unknown fundamental type");
  }
}

static std::string make_unique_variable_name(const std::string& base) {
  static int counter = 0;
  return base + std::to_string(counter++);
}

static std::string_view get_typed_array_name(TokenId id)
{
  switch (id) {
  case TokenId::Boolean:
    // TODO: need implement packing and unpacking bits into bytes
    // in C++ builder and here
    assert(false && "Typed array for boolean is not implemented");
    return "Uint8Array";
  case TokenId::Int8:
    return "Int8Array";
  case TokenId::UInt8:
    return "Uint8Array";
  case TokenId::Int16:
    return "Int16Array";
  case TokenId::UInt16:
    return "Uint16Array";
  case TokenId::Int32:
    return "Int32Array";
  case TokenId::UInt32:
    return "Uint32Array";
  case TokenId::Int64:
    return "BigInt64Array";
  case TokenId::UInt64:
    return "BigUint64Array";
  case TokenId::Float32:
    return "Float32Array";
  case TokenId::Float64:
    return "Float64Array";
  default:
    assert(false);
    return "/*unknown typed array*/";
  }
}

void TSBuilder::emit_type(AstTypeDecl* type, std::ostream& os)
{
  switch (type->id) {
  case FieldType::Fundamental:
    os << toktype << fundamental_to_ts(cft(type)->token_id) << "/*"
       << cft(type)->token_id << "*/";
    break;
  case FieldType::Struct:
    os << ns(cflat(type)->nm) << cflat(type)->name;
    break;
  case FieldType::Vector:
  case FieldType::Array: {
    auto ut = cwt(type)->type;
    if (ut->id == FieldType::Fundamental) {
      os << get_typed_array_name(cft(ut)->token_id);
    } else {
      os << "Array<" << emit_type(ut) << ">";
    }
    break;
  }
  case FieldType::String:
    os << "string";
    break;
  case FieldType::Void:
    os << "void";
    break;
  case FieldType::Object:
    os << "NPRPC.ObjectProxy";
    break;
  case FieldType::Alias:
    os << ns(calias(type)->nm) << calias(type)->name;
    break;
  case FieldType::Enum:
    os << ns(cenum(type)->nm) << cenum(type)->name;
    break;
  case FieldType::Optional:
    emit_type(cwt(type)->type, os);
    break;
  default:
    assert(false);
  }
}

void TSBuilder::emit_variable(AstTypeDecl* type,
                              std::string name,
                              std::ostream& os)
{
  switch (type->id) {
  case FieldType::Fundamental:
    os << bl() << "let " << name << ": " << toktype
       << fundamental_to_ts(cft(type)->token_id) << "/*" << cft(type)->token_id
       << "*/;\n";
    break;
  case FieldType::Struct:
    os << bl() << "let " << name << ": " << cflat(type)->name << " = {} as "
       << cflat(type)->name << ";\n";
    break;
  case FieldType::Vector:
  case FieldType::Array:
    os << bl() << "let " << name << ": Array<" << emit_type(cwt(type)->type)
       << "> = [];\n";
    break;
  case FieldType::String:
    os << bl() << "let " << name << ": string = '';\n";
    break;
  case FieldType::Void:
    os << bl() << "let " << name << ": void;\n";
    break;
  case FieldType::Object:
    os << bl() << "let " << name
       << ": NPRPC.ObjectId = new NPRPC.ObjectId();\n";
    break;
  case FieldType::Alias:
    emit_variable(calias(type)->get_real_type(), name, os);
    break;
  case FieldType::Enum:
    os << bl() << "let " << name << ": " << ns(cenum(type)->nm)
       << cenum(type)->name << ";\n";
    break;
  case FieldType::Optional:
    emit_variable(cwt(type)->type, name, os);
    break;
  default:
    assert(false);
  }
}

void TSBuilder::emit_parameter_type_for_proxy_call_r(AstTypeDecl* type,
                                                     std::ostream& os,
                                                     bool input)
{
  switch (type->id) {
  case FieldType::Fundamental:
    os << fundamental_to_ts(cft(type)->token_id);
    break;
  case FieldType::Struct:
    os << ns(ctx_->nm_cur()) << cflat(type)->name;
    break;
  case FieldType::Array:
  case FieldType::Vector: {
    auto ut = cwt(type)->type;
    if (ut->id == FieldType::Fundamental) {
      os << get_typed_array_name(cft(ut)->token_id);
    } else {
      os << "Array<";
      emit_parameter_type_for_proxy_call_r(ut, os, input);
      os << ">";
    }
    if (type->id == FieldType::Array)
      os << "/*" << car(type)->length << "*/";
    break;
  }
  case FieldType::String:
    os << "string";
    break;
  case FieldType::Optional:
    emit_parameter_type_for_proxy_call_r(copt(type)->type, os, input);
    break;
  case FieldType::Enum:
    os << ns(cenum(type)->nm) << cenum(type)->name;
    break;
  case FieldType::Alias:
    os << ns(calias(type)->nm) << calias(type)->name;
    break;
  case FieldType::Void:
    os << "void";
    break;
  case FieldType::Object:
    // Input: ObjectId (raw oid from poa.activate_object)
    // Output: ObjectProxy (with endpoint selection)
    os << (input ? "NPRPC.ObjectId" : "NPRPC.ObjectProxy");
    break;
  default:
    assert(false);
  }
}

void TSBuilder::emit_parameter_type_for_proxy_call(AstFunctionArgument* arg,
                                                   std::ostream& os)
{
  const bool input = (arg->modifier == ArgumentModifier::In);
  os << (input ? "/*in*/" : "/*out*/");
  const bool as_reference = arg->modifier == ArgumentModifier::Out;
  if (as_reference)
    os << "NPRPC.ref<";
  emit_parameter_type_for_proxy_call_r(arg->type, os, input);
  if (as_reference)
    os << '>';
}

void TSBuilder::emit_parameter_type_for_servant(AstFunctionArgument* arg,
                                                std::ostream& os)
{
  const bool input = (arg->modifier == ArgumentModifier::In);
  os << (input ? "/*in*/" : "/*out*/");
  const bool as_reference = arg->modifier == ArgumentModifier::Out;
  if (as_reference)
    os << "NPRPC.ref<";
  // For servant interface with objects:
  // Input: ObjectProxy (server needs to call methods on received object)
  // Output: ObjectId (server provides raw data)
  if (arg->type->id == FieldType::Object) {
    os << (input ? "NPRPC.ObjectProxy" : "NPRPC.ObjectId");
  } else {
    emit_parameter_type_for_proxy_call_r(arg->type, os, input);
  }
  if (as_reference)
    os << '>';
}

void TSBuilder::emit_stream_value_type(AstTypeDecl* type, std::ostream& os)
{
  auto* real_type = type->id == FieldType::Alias ? calias(type)->get_real_type() : type;
  if (real_type->id == FieldType::Fundamental &&
      cft(real_type)->token_id == TokenId::UInt8) {
    os << "Uint8Array";
  } else {
    emit_type(type, os);
  }
}

void TSBuilder::emit_stream_reader_type(AstStreamDecl* stream,
                                        std::ostream& os)
{
  os << "NPRPC.StreamReader<";
  emit_stream_value_type(stream->stream_out_type(), os);
  os << '>';
}

void TSBuilder::emit_stream_writer_type(AstTypeDecl* type, std::ostream& os)
{
  os << "NPRPC.StreamWriter<";
  emit_stream_value_type(type, os);
  os << '>';
}

void TSBuilder::emit_stream_proxy_return_type(AstFunctionDecl* fn,
                                              std::ostream& os)
{
  auto* stream = fn->stream_decl;
  assert(stream != nullptr);

  switch (fn->stream_kind) {
  case StreamKind::Server:
    emit_stream_reader_type(stream, os);
    break;
  case StreamKind::Client:
    emit_stream_writer_type(stream->stream_in_type(), os);
    break;
  case StreamKind::Bidi:
    os << "NPRPC.BidiStream<";
    emit_stream_value_type(stream->stream_in_type(), os);
    os << ", ";
    emit_stream_value_type(stream->stream_out_type(), os);
    os << '>';
    break;
  default:
    assert(false);
  }
}

void TSBuilder::emit_stream_serializer(AstTypeDecl* type, std::ostream& os)
{
  switch (type->id) {
  case FieldType::Fundamental: {
    auto token = cft(type)->token_id;
    switch (token) {
    case TokenId::Boolean:
      os << "((value: boolean) => Uint8Array.of(value ? 1 : 0))";
      break;
    case TokenId::Int8:
      os << "((value: number) => new Uint8Array(Int8Array.of(value).buffer))";
      break;
    case TokenId::UInt8:
      os << "((value: Uint8Array) => value)";
      break;
    case TokenId::Int16:
      os << "((value: number) => { const buffer = new ArrayBuffer(2); const view = new DataView(buffer); view.setInt16(0, value, true); return new Uint8Array(buffer); })";
      break;
    case TokenId::UInt16:
      os << "((value: number) => { const buffer = new ArrayBuffer(2); const view = new DataView(buffer); view.setUint16(0, value, true); return new Uint8Array(buffer); })";
      break;
    case TokenId::Int32:
      os << "((value: number) => { const buffer = new ArrayBuffer(4); const view = new DataView(buffer); view.setInt32(0, value, true); return new Uint8Array(buffer); })";
      break;
    case TokenId::UInt32:
      os << "((value: number) => { const buffer = new ArrayBuffer(4); const view = new DataView(buffer); view.setUint32(0, value, true); return new Uint8Array(buffer); })";
      break;
    case TokenId::Int64:
      os << "((value: bigint) => { const buffer = new ArrayBuffer(8); const view = new DataView(buffer); view.setBigInt64(0, value, true); return new Uint8Array(buffer); })";
      break;
    case TokenId::UInt64:
      os << "((value: bigint) => { const buffer = new ArrayBuffer(8); const view = new DataView(buffer); view.setBigUint64(0, value, true); return new Uint8Array(buffer); })";
      break;
    case TokenId::Float32:
      os << "((value: number) => { const buffer = new ArrayBuffer(4); const view = new DataView(buffer); view.setFloat32(0, value, true); return new Uint8Array(buffer); })";
      break;
    case TokenId::Float64:
      os << "((value: number) => { const buffer = new ArrayBuffer(8); const view = new DataView(buffer); view.setFloat64(0, value, true); return new Uint8Array(buffer); })";
      break;
    default:
      assert(false);
    }
    break;
  }
  case FieldType::Vector: {
    auto* wt = cwt(type)->real_type();
    auto [elem_size, elem_align] = get_type_size_align(wt);
    if (is_fundamental(wt)) {
      os << "((value: ";
      emit_stream_value_type(type, os);
      os << ") => { const buf = NPRPC.FlatBuffer.create(8 + value.byteLength); buf.commit(8); NPRPC.marshal_typed_array(buf, 0, value, "
         << elem_size << ", " << elem_align
         << "); return new Uint8Array(buf.array_buffer, 0, buf.size); })";
    } else if (wt->id == FieldType::Struct) {
      auto* s = cflat(wt);
      os << "((value: ";
      emit_stream_value_type(type, os);
      os << ") => { const buf = NPRPC.FlatBuffer.create(8 + value.length * " << s->size
         << "); buf.commit(8); NPRPC.marshal_struct_array(buf, 0, value, marshal_" << s->name
         << ", " << s->size << ", " << s->align
         << "); return new Uint8Array(buf.array_buffer, 0, buf.size); })";
    } else {
      assert(false);
    }
    break;
  }
  case FieldType::Array: {
    auto* wt = cwt(type)->real_type();
    auto* arr = car(type);
    auto [elem_size, elem_align] = get_type_size_align(wt);
    if (is_fundamental(wt)) {
      os << "((value: ";
      emit_stream_value_type(type, os);
      os << ") => { if (value.length !== " << arr->length << ") throw new NPRPC.Exception('Invalid fixed array length'); return new Uint8Array(value.buffer.slice(value.byteOffset, value.byteOffset + value.byteLength)); })";
    } else if (wt->id == FieldType::Struct) {
      auto* s = cflat(wt);
      os << "((value: ";
      emit_stream_value_type(type, os);
      os << ") => { if (value.length !== " << arr->length << ") throw new NPRPC.Exception('Invalid fixed array length'); const buf = NPRPC.FlatBuffer.create(" << (arr->length * s->size + 128) << "); buf.commit(" << (arr->length * s->size) << "); for (let i = 0; i < value.length; ++i) marshal_" << s->name << "(buf, i * " << s->size << ", value[i]); return new Uint8Array(buf.array_buffer, 0, buf.size); })";
    } else {
      assert(false);
    }
    (void)elem_size;
    (void)elem_align;
    break;
  }
  case FieldType::Enum:
    emit_stream_serializer(cenum(type), os);
    break;
  case FieldType::Alias:
    emit_stream_serializer(calias(type)->get_real_type(), os);
    break;
  case FieldType::String:
    os << "((value: string) => { const buf = NPRPC.FlatBuffer.create(128); buf.commit(8); NPRPC.marshal_string(buf, 0, value); return new Uint8Array(buf.array_buffer, 0, buf.size); })";
    break;
  case FieldType::Struct: {
    auto* s = cflat(type);
    const auto initial_capacity = s->size + 128;
    os << "((value: ";
    emit_type(type, os);
    os << ") => { const buf = NPRPC.FlatBuffer.create(" << initial_capacity
       << "); buf.commit(" << s->size << "); marshal_" << s->name
       << "(buf, 0, value); return new Uint8Array(buf.array_buffer, 0, buf.size); })";
    break;
  }
  default:
    assert(false);
  }
}

void TSBuilder::emit_stream_deserializer(AstTypeDecl* type, std::ostream& os)
{
  switch (type->id) {
  case FieldType::Fundamental: {
    auto token = cft(type)->token_id;
    switch (token) {
    case TokenId::Boolean:
      os << "((data: Uint8Array) => data[0] !== 0)";
      break;
    case TokenId::Int8:
      os << "((data: Uint8Array) => new DataView(data.buffer, data.byteOffset, data.byteLength).getInt8(0))";
      break;
    case TokenId::UInt8:
      os << "NPRPC.bytes_deserializer";
      break;
    case TokenId::Int16:
      os << "((data: Uint8Array) => new DataView(data.buffer, data.byteOffset, data.byteLength).getInt16(0, true))";
      break;
    case TokenId::UInt16:
      os << "((data: Uint8Array) => new DataView(data.buffer, data.byteOffset, data.byteLength).getUint16(0, true))";
      break;
    case TokenId::Int32:
      os << "((data: Uint8Array) => new DataView(data.buffer, data.byteOffset, data.byteLength).getInt32(0, true))";
      break;
    case TokenId::UInt32:
      os << "((data: Uint8Array) => new DataView(data.buffer, data.byteOffset, data.byteLength).getUint32(0, true))";
      break;
    case TokenId::Int64:
      os << "((data: Uint8Array) => new DataView(data.buffer, data.byteOffset, data.byteLength).getBigInt64(0, true))";
      break;
    case TokenId::UInt64:
      os << "((data: Uint8Array) => new DataView(data.buffer, data.byteOffset, data.byteLength).getBigUint64(0, true))";
      break;
    case TokenId::Float32:
      os << "((data: Uint8Array) => new DataView(data.buffer, data.byteOffset, data.byteLength).getFloat32(0, true))";
      break;
    case TokenId::Float64:
      os << "((data: Uint8Array) => new DataView(data.buffer, data.byteOffset, data.byteLength).getFloat64(0, true))";
      break;
    default:
      assert(false);
    }
    break;
  }
  case FieldType::Vector: {
    auto* wt = cwt(type)->real_type();
    auto [elem_size, elem_align] = get_type_size_align(wt);
    if (is_fundamental(wt)) {
      os << "((data: Uint8Array) => NPRPC.unmarshal_typed_array(NPRPC.FlatBuffer.from_array_buffer(data.slice().buffer), 0, "
         << get_typed_array_name(cft(wt)->token_id) << ") as ";
      emit_stream_value_type(type, os);
      os << ")";
    } else if (wt->id == FieldType::Struct) {
      auto* s = cflat(wt);
      os << "((data: Uint8Array) => NPRPC.unmarshal_struct_array(NPRPC.FlatBuffer.from_array_buffer(data.slice().buffer), 0, unmarshal_"
         << s->name << ", " << s->size << ") as Array<";
      emit_type(wt, os);
      os << ">)";
    } else {
      assert(false);
    }
    (void)elem_align;
    break;
  }
  case FieldType::Array: {
    auto* wt = cwt(type)->real_type();
    auto* arr = car(type);
    auto [elem_size, elem_align] = get_type_size_align(wt);
    if (is_fundamental(wt)) {
      os << "((data: Uint8Array) => { const value = new " << get_typed_array_name(cft(wt)->token_id)
         << "(data.slice().buffer); if (value.length !== " << arr->length
         << ") throw new NPRPC.Exception('Invalid fixed array length'); return value; })";
    } else if (wt->id == FieldType::Struct) {
      auto* s = cflat(wt);
      os << "((data: Uint8Array) => { const buf = NPRPC.FlatBuffer.from_array_buffer(data.slice().buffer); const result = new Array<";
      emit_type(wt, os);
      os << ">(" << arr->length << "); for (let i = 0; i < " << arr->length << "; ++i) result[i] = unmarshal_"
         << s->name << "(buf, i * " << s->size << "); return result; })";
    } else {
      assert(false);
    }
    (void)elem_size;
    (void)elem_align;
    break;
  }
  case FieldType::Enum:
    emit_stream_deserializer(cenum(type), os);
    break;
  case FieldType::Alias:
    emit_stream_deserializer(calias(type)->get_real_type(), os);
    break;
  case FieldType::String:
    os << "((data: Uint8Array) => NPRPC.unmarshal_string(NPRPC.FlatBuffer.from_array_buffer(data.slice().buffer), 0))";
    break;
  case FieldType::Struct: {
    auto* s = cflat(type);
    os << "((data: Uint8Array) => unmarshal_" << s->name
       << "(NPRPC.FlatBuffer.from_array_buffer(data.slice().buffer), 0))";
    break;
  }
  default:
    assert(false);
  }
}

void TSBuilder::assign_from_ts_type(AstTypeDecl* type,
                                    std::string op1,
                                    std::string op2,
                                    bool from_iterator)
{
  switch (type->id) {
  case FieldType::Fundamental:
  case FieldType::String:
  case FieldType::Enum:
    out << bl() << op1 << " = " << op2 << ";\n";
    break;
  case FieldType::Struct: {
    auto s = cflat(type);
    for (auto field : s->fields) {
      assign_from_ts_type(field->type,
                          op1 + (from_iterator ? "." : ".") + field->name,
                          op2 + '.' + field->name);
    }
    break;
  }
  case FieldType::Vector:
    out << bl() << op1 << '(' << op2 << ".length);\n";
    [[fallthrough]];
  case FieldType::Array: {
    auto ut = cwt(type)->type;
    auto real_type = cwt(type)->real_type();
    if (is_fundamental(real_type)) {
      // auto [size, align] = get_type_size_align(wt);
      out << bl() << op1 << "_d()."
          << (ut->id == FieldType::Fundamental ? "copy_from_typed_array("
                                               : "copy_from_ts_array(")
          << op2 << "); \n";
    } else {
      out << bb() << bl() << "let vv = " << op1 << "_d(), index = 0;\n"
          << bl() << "for (let e of vv)\n"
          << bb();
      assign_from_ts_type(real_type, "e", op2 + "[index]", true);
      out << bl() << "++index;\n" << eb() << eb();
    }
    break;
  }
  case FieldType::Optional: {
    auto wt = cwt(type)->real_type();
    if (is_fundamental(wt)) {
      // auto [size, align] = get_type_size_align(wt);
      out << bb() << bl() << "let opt = " << op1 << ";\n"
          << bl() << "if (" << op2 << ") {\n"
          << bb(false) << bl() << "opt.alloc();\n"
          << bl() << "opt.value = " << op2 << "!\n"
          << eb(false) << bl() << "} else {\n"
          << bb(false) << bl() << "opt.set_nullopt();\n"
          << eb() << eb();
    } else {
      out << bb() << bl() << "let opt = " << op1 << ";\n"
          << bl() << "if (" << op2 << ") {\n"
          << bb(false) << bl() << "let opt = " << op1 << ";\n"
          << bl() << "opt.alloc();\n"; // <<
      // bl() << "let value = opt.value;\n";
      // Checked for nullopt above, but TS doesn't know that
      // unless we use "!" here, or create a temporary variable
      assign_from_ts_type(wt, "opt.value", op2 + "!", true);
      out << eb(false) << bl() << "} else {\n"
          << bb(false) << bl() << "opt.set_nullopt();\n"
          << eb() << eb();
    }
    break;
  }
  case FieldType::Alias:
    assign_from_ts_type(calias(type)->type, op1, op2, from_iterator);
    break;
  case FieldType::Object:
    out << bl() << "NPRPC.oid_assign_from_ts(" << op1 << ", " << op2 << ");\n";
    break;
  default:
    assert(false);
    break;
  }
}

void TSBuilder::assign_from_flat_type(AstTypeDecl* type,
                                      std::string op1,
                                      std::string op2,
                                      bool from_iterator,
                                      bool top_object,
                                      bool direct)
{
  static int _idx = 0;
  switch (type->id) {
  case FieldType::Fundamental:
  case FieldType::String:
  case FieldType::Enum:
    out << bl() << op1 << " = " << op2 << ";\n";
    break;
  case FieldType::Struct: {
    auto s = cflat(type);
    out << bl() << op1 << " = {} as " << s->name << ";\n";
    for (auto field : s->fields)
      assign_from_flat_type(field->type,
                            op1 + (from_iterator ? "." : ".") + field->name,
                            op2 + '.' + field->name, false, false);
    break;
  }
  case FieldType::Array:
  case FieldType::Vector: {
    auto ut = cwt(type)->type;
    auto real_type = cwt(type)->real_type();
    if (top_object && direct) {
      out << bl() << op1 << " = " << op2 << "_d();\n";
      break;
    }
    auto idxs = "index_" + std::to_string(_idx++);
    if (is_fundamental(real_type)) {
      // assert(!top_object);
      out << bb() << bl() << op1 << " = " << op2 << "_d()"
          << (ut->id == FieldType::Fundamental ? ".typed_array\n" : ".array;\n")
          << eb();
    } else {
      out << bb() << bl() << "let vv = " << op2 << "_d(), " << idxs
          << " = 0;\n";
      if (top_object)
        out << bl() << op1 << ".length = vv.elements_size;\n";
      else
        out << bl() << "(" << op1
            << " as Array<any>) = new Array<any>(vv.elements_size)\n";
      out << bl() << "for (let e of vv) {\n" << bb(false);
      assign_from_flat_type(real_type, op1 + '[' + idxs + ']', "e", true,
                            false);
      out << bl() << "++" << idxs << ";\n" << eb() << eb();
    }
    break;
  }
  case FieldType::Optional: {
    auto wt = cwt(type)->real_type();
    if (is_fundamental(wt)) {
      // auto [size, align] = get_type_size_align(wt);
      out << bb() << bl() << "if (" << op2 << ".has_value) {\n"
          << bb(false) << bl() << op1 << " = " << op2 << ".value\n"
          << eb(false) << bl() << "} else {\n"
          << bb(false) << bl() << op1 << " = undefined\n"
          << eb() << eb();
    } else {
      out << bb() << bl() << "let opt = " << op2 << ";\n"
          << bl() << "if (opt.has_value) {\n"
          << bb(false); // <<
      // bl() << "let value = opt.value;\n";
      assign_from_flat_type(wt, op1 + '!', "opt.value", false, false);
      out << eb(false) << bl() << "} else {\n"
          << bb(false) << bl() << op1 << " = undefined\n"
          << eb() << eb();
    }
    break;
  }
  case FieldType::Alias:
    assign_from_flat_type(calias(type)->get_real_type(), op1, op2,
                          from_iterator, top_object);
    break;
  case FieldType::Object:
    if (true || top_object) {
      // expecting out passed by reference
      out << bl() << op1 << " = NPRPC.create_object_from_flat(" << op2
          << ", this.endpoint);\n";
    } else {
      out << bl() << op1 << " = NPRPC.oid_create_from_flat(" << op2 << ");\n";
    }
    break;
  default:
    assert(false);
    break;
  }
}

void TSBuilder::emit_struct2(AstStructDecl* s, bool is_exception)
{
  // native typescript
  if (!is_exception) {
    out << "export interface " << s->name << " {\n";
    for (auto const f : s->fields) {
      out << "  " << f->name << (f->is_optional() ? "?: " : ": ");
      // For function argument structs with object types, use ObjectId
      // instead of ObjectProxy
      if (f->function_argument && f->type->id == FieldType::Object) {
        out << "NPRPC.ObjectId";
      } else {
        out << emit_type(f->type);
      }
      out << ";\n";
    }
  } else {
    // For exceptions, generate both the exception class and a marshalling
    // interface The interface includes __ex_id for marshalling purposes
    out << "export interface " << s->name << "_Data {\n";
    for (auto const f : s->fields) {
      out << "  " << f->name << (f->is_optional() ? "?: " : ": ");
      out << emit_type(f->type);
      out << ";\n";
    }
    out << "}\n\n";

    out << bl() << "export class " << s->name << " extends NPRPC.Exception {\n"
        << bb(false) << bl() << "constructor(";
    for (size_t ix = 1; ix < s->fields.size(); ++ix) {
      auto f = s->fields[ix];
      out << bl() << "public " << f->name << (f->is_optional() ? "?: " : ": ")
          << emit_type(f->type);
      if (ix + 1 < s->fields.size())
        out << ", ";
    }
    out << ") { super(\"" << s->name << "\"); }\n";
  }

  out << eb() << "\n";
}

void TSBuilder::emit_constant(const std::string& name, AstNumber* number)
{
  out << bl() << "export const " << name << " = ";
  std::visit(overloads
      {
          [&](int64_t x) { out << x; },
          [&](float x) { out << x; },
          [&](double x) { out << x; },
          [&](bool x) { out << std::ios::boolalpha << x << std::ios::dec; },
      },
      number->value);
  out << ";\n";
}

void TSBuilder::emit_struct(AstStructDecl* s)
{
  emit_struct2(s, false);
  emit_marshal_function(s);
  out << '\n';
  emit_unmarshal_function(s);
  out << '\n';
}

void TSBuilder::emit_exception(AstStructDecl* s)
{
  assert(s->is_exception());
  emit_struct2(s, true);
  // Generate both marshal and unmarshal for exceptions
  // Marshal is needed on server side when throwing exceptions
  // Unmarshal is needed on client side when catching exceptions
  emit_marshal_function(s);
  emit_unmarshal_function(s);
  out << '\n';
}

void TSBuilder::finalize()
{
  auto filename = ctx_->get_file_path().filename();
  filename.replace_extension(".ts");
  std::ofstream ofs(out_dir_ / filename, std::ios::binary);

  if (ctx_->is_nprpc_base()) {
    ofs << "import * as NPRPC from '@/base'\n\n";
  } else if (ctx_->is_nprpc_nameserver()) {
    ofs << "import * as NPRPC from '@/index_internal'\n\n";
  } else {
    ofs << "import * as NPRPC from 'nprpc'\n\n";
  }

  ofs << "const u8enc = new TextEncoder();\n"
         "const u8dec = new TextDecoder();\n\n";

  // throw_exception function body
  auto& exs = ctx_->exceptions;
  if (!exs.empty()) {
    out << '\n'
        << bl() << "function " << ctx_->current_file()
        << "_throw_exception(buf: NPRPC.FlatBuffer): void { \n"
        << bb(false) << bl() << "switch( buf.read_exception_number() ) {\n"
        << bb(false);

    always_full_namespace(true);
    for (auto ex : exs) {
      out << bl() << "case " << ex->exception_id << ":\n" << bb();
      // Use unmarshal function instead of _Direct class
      if (ex->fields.size() > 1) {
        // Skip header (16 bytes) + __ex_id field (4 bytes)
        out << bl() << "let ex_obj = unmarshal_" << ex->name << "(buf, "
            << size_of_header << " + 4);\n";
        out << bl() << "throw new " << ns(ex->nm) << ex->name << "(";
        for (size_t i = 1; i < ex->fields.size(); ++i) {
          out << "ex_obj." << ex->fields[i]->name;
          if (i + 1 < ex->fields.size())
            out << ", ";
        }
        out << ");\n";
      } else {
        // Exception has no fields beyond id
        out << bl() << "throw new " << ns(ex->nm) << ex->name << "();\n";
      }
      out << eb(); // case
    }
    always_full_namespace(false);
    out << bl() << "default:\n"
        << bb(false) << bl() << "throw \"unknown rpc exception\";\n"
        << eb(false) << // default
        eb() <<         // switch
        eb()            // function
        ;
  }

  emit_arguments_structs([this](AstStructDecl* s) {
    emit_struct2(s, false);
    emit_marshal_function(s);
    out << '\n';
    emit_unmarshal_function(s);
    out << '\n';
  });

  ofs << out.str();
}

void TSBuilder::emit_using(AstAliasDecl* u)
{
  out << bl() << "export type " << u->name << " = " << emit_type(u->type)
      << ";\n";
}

void TSBuilder::emit_enum(AstEnumDecl* e)
{
  out << bl() << "export enum " << e->name << " { //" << toktype << e->token_id
      << '\n'
      << bb(false);
  std::int64_t ix = 0;
  for (size_t i = 0; i < e->items.size(); ++i) {
    out << bl() << e->items[i].first;
    auto const n = e->items[i].second;
    if (n.second || ix != n.first) { // explicit
      out << " = " << n.first;
      ix = n.first.decimal() + 1;
    } else {
      ++ix;
    }
    if (i != e->items.size() - 1)
      out << ",\n";
  }
  out << '\n' << eb() << '\n';
}

void TSBuilder::emit_namespace_begin()
{
  if (ctx_->nm_cur()->parent() && ctx_->nm_cur()->parent()->name().empty())
    return;
  out << bl() << "export namespace " << ctx_->nm_cur()->name() << " { \n"
      << bb(false);
}

void TSBuilder::emit_namespace_end()
{
  if (ctx_->nm_cur()->parent() && ctx_->nm_cur()->parent()->name().empty())
    return;
  out << bl() << "} // namespace " << ctx_->nm_cur()->name() << "\n\n"
      << eb(false);
}

void TSBuilder::emit_interface(AstInterfaceDecl* ifs)
{
  auto const flat_nm = "Flat_" + ctx_->current_file();
  const auto servant_iname = 'I' + ifs->name + "_Servant";

  auto emit_function_arguments =
      [](bool ts, AstFunctionDecl* fn, std::ostream& os,
         std::function<void(AstFunctionArgument*, std::ostream & os)> emitter,
         bool skip_stream = false) {
        os << '(';
        size_t ix = 0;
        for (auto arg : fn->args) {
          if (skip_stream && arg->type->id == FieldType::Stream)
            continue;
          if (ix > 0)
            os << ", ";
          os << arg->name;
          if (!ts)
            os << ": ";
          else
            os << (arg->is_optional() && arg->modifier != ArgumentModifier::Out
                       ? "?: "
                       : ": ");
          emitter(arg, os);
          ++ix;
        }
        os << ')';
      };

  // Proxy definition =======================================================
  out << bl() << "export class " << ifs->name << ' ';

  // if (ifs->plist.size()) {
  // out << " extends " << ifs->plist[0]->name << "\n";
  // for (size_t i = 1; i < ifs->plist.size(); ++i) {
  //   out << " extends " << ifs->plist[i]->name << "\n";
  // }
  // out << "{\n";
  //} else {
  //}

  out << "extends NPRPC.ObjectProxy {\n"
      << bb(false) << bl() << "public static get servant_t(): new() => _"
      << servant_iname << " {\n"
      << bb(false) << bl() << "return _" << servant_iname << ";\n"
      << eb() << '\n';
  ;

  // parent's functions
  std::map<AstInterfaceDecl*, int> ifs_idxs;
  auto count_all = [&ifs_idxs](AstInterfaceDecl* ifs_inherited, int& n) {
    ifs_idxs.emplace(ifs_inherited, n);
  };

  int n = 1;
  for (auto parent : ifs->plist) {
    dfs_interface(std::bind(count_all, _1, std::ref(n)), parent);
  }

  for (auto& inherited_ifs : ifs_idxs) {
    if (inherited_ifs.first->fns.size()) {
      out << bl() << "// " << inherited_ifs.first->name << '\n';
    }
    for (auto& fn : inherited_ifs.first->fns) {
      out << bl() << "public async " << fn->name;
      emit_function_arguments(
          false, fn, out,
          std::bind(&TSBuilder::emit_parameter_type_for_proxy_call, this, _1,
                    _2),
          fn->is_stream && fn->stream_kind != StreamKind::Server);
      out << ": Promise<";
      if (fn->is_stream) {
        emit_stream_proxy_return_type(fn, out);
      } else {
        out << emit_type(fn->ret_value);
      }
      out << "> {\n"
          << bb(false) << bl() << (!fn->is_void() ? "return " : "")
          << inherited_ifs.first->name << ".prototype." << fn->name
          << ".bind(this,";
      for (auto arg : fn->args) {
        if (fn->is_stream && arg->type->id == FieldType::Stream)
          continue;
        out << arg->name << ',';
      }
      out << inherited_ifs.second << ")();\n" << eb();
    }
  }
  // proxy object functions definitions
  out << '\n';
  for (auto& fn : ifs->fns) {
    if (fn->is_stream) {
      int visible_arg_count = 0;
      for (auto arg : fn->args) {
        if (fn->stream_kind != StreamKind::Server && arg->type->id == FieldType::Stream)
          continue;
        ++visible_arg_count;
      }

      out << bl() << "public async " << fn->name;
      emit_function_arguments(
          true, fn, out,
          std::bind(&TSBuilder::emit_parameter_type_for_proxy_call, this, _1,
                    _2),
          fn->stream_kind != StreamKind::Server);
      out << ": Promise<";
      emit_stream_proxy_return_type(fn, out);
      out << "> {\n"
          << bb(false) << bl()
          << "const interface_idx = (arguments.length == " << visible_arg_count
          << " ? 0 : arguments[arguments.length - 1]);\n"
          << bl() << "const conn = NPRPC.rpc.get_connection(this.endpoint);\n"
          << bl() << "const stream_id = conn.stream_manager.generate_stream_id();\n";

      const auto fixed_size =
          get_stream_init_arguments_offset() + (fn->in_s ? fn->in_s->size : 0);
      const auto capacity =
          fixed_size + (fn->in_s ? (fn->in_s->flat ? 0 : 128) : 0);

      out << bl() << "const buf = NPRPC.FlatBuffer.create();\n"
          << bl() << "buf.prepare(" << capacity << ");\n"
          << bl() << "buf.commit(" << fixed_size << ");\n"
          << bl() << "buf.write_msg_id(NPRPC.impl.MessageId.StreamInitialization);\n"
          << bl() << "buf.write_msg_type(NPRPC.impl.MessageType.Request);\n"
          << bl() << "NPRPC.impl.marshal_StreamInit(buf, " << size_of_header << ", {\n"
          << bb(false) << bl() << "stream_id,\n"
          << bl() << "poa_idx: this.data.poa_idx,\n"
          << bl() << "interface_idx,\n"
          << bl() << "object_id: this.data.object_id,\n"
          << bl() << "func_idx: " << fn->idx << ",\n"
          << bl() << "stream_kind: NPRPC.impl.StreamKind.";

      switch (fn->stream_kind) {
      case StreamKind::Server:
        out << "Server";
        break;
      case StreamKind::Client:
        out << "Client";
        break;
      case StreamKind::Bidi:
        out << "Bidi";
        break;
      default:
        assert(false);
      }

      out << "\n" << eb(false) << bl() << "});\n";

      if (fn->in_s) {
        out << bl() << "marshal_" << fn->in_s->name << "(buf, "
            << get_stream_init_arguments_offset() << ", {";

        int ix = 0;
        for (auto in : fn->args) {
          if (in->modifier == ArgumentModifier::Out || in->type->id == FieldType::Stream)
            continue;
          if (ix > 0)
            out << ", ";
          out << "_" << (ix + 1) << ": " << in->name;
          ++ix;
        }
        out << "});\n";
      }

      out << bl() << "buf.write_len(buf.size - 4);\n";

      out << bl() << "(globalThis as any).__nprpc_debug?.stream_start({direction:'client',"
          << "class_id:_" << servant_iname << "._get_class(),"
          << "poa_idx:this.data.poa_idx,object_id:String(this.data.object_id),interface_idx,"
          << "func_idx:" << fn->idx << ",method_name:'" << fn->name << "',"
          << "endpoint:{hostname:this.endpoint.hostname,port:this.endpoint.port,"
             "transport:NPRPC.EndPoint.to_string(this.endpoint.type).replace('://','') as any},"
          << "stream_id:String(stream_id),stream_kind:'";

      switch (fn->stream_kind) {
      case StreamKind::Server:
        out << "server";
        break;
      case StreamKind::Client:
        out << "client";
        break;
      case StreamKind::Bidi:
        out << "bidi";
        break;
      default:
        assert(false);
      }

      out << "',request_args:{";
      {
        bool _dbg_first = true;
        for (auto _dbg_a : fn->args) {
          if (_dbg_a->modifier == ArgumentModifier::Out || _dbg_a->type->id == FieldType::Stream) continue;
          if (!_dbg_first) out << ",";
          out << _dbg_a->name << ":" << _dbg_a->name;
          _dbg_first = false;
        }
      }
      out << "},request_bytes:buf.size});\n";

      switch (fn->stream_kind) {
      case StreamKind::Server:
        out << bl() << "return await NPRPC.rpc.open_server_stream(this.endpoint, buf, stream_id, this.timeout, ";
        emit_stream_deserializer(fn->stream_decl->stream_out_type(), out);
        out << ");\n";
        break;
      case StreamKind::Client:
        out << bl() << "return await NPRPC.rpc.open_client_stream(this.endpoint, buf, stream_id, this.timeout, ";
        emit_stream_serializer(fn->stream_decl->stream_in_type(), out);
        out << ");\n";
        break;
      case StreamKind::Bidi:
        out << bl() << "return await NPRPC.rpc.open_bidi_stream(this.endpoint, buf, stream_id, this.timeout, ";
        emit_stream_serializer(fn->stream_decl->stream_in_type(), out);
        out << ", ";
        emit_stream_deserializer(fn->stream_decl->stream_out_type(), out);
        out << ");\n";
        break;
      default:
        assert(false);
      }

      out << eb();
      continue;
    }

    out << bl() << "public async " << fn->name;
    emit_function_arguments(
        true, fn, out,
        std::bind(&TSBuilder::emit_parameter_type_for_proxy_call, this, _1,
                  _2));
    out << ": Promise<" << emit_type(fn->ret_value) << "> {\n"
        << bb(false) << bl()
        << "let interface_idx = (arguments.length == " << fn->args.size()
        << " ? 0 : arguments[arguments.length - 1]);\n";

    const auto fixed_size =
        get_arguments_offset() + (fn->in_s ? fn->in_s->size : 0);
    const auto capacity =
        fixed_size + (fn->in_s ? (fn->in_s->flat ? 0 : 128) : 0);
    out << bl() << "const buf = NPRPC.FlatBuffer.create();\n"
        << bl() << "buf.prepare(" << capacity << ");\n"
        << bl() << "buf.commit(" << fixed_size << ");\n"
        << bl() << "buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);\n"
        << bl() << "buf.write_msg_type(NPRPC.impl.MessageType.Request);\n"
        << bl() << "// Write CallHeader directly\n"
        << bl() << "buf.dv.setUint16(" << size_of_header
        << " + 0, this.data.poa_idx, true);\n"
        << bl() << "buf.dv.setUint8(" << size_of_header
        << " + 2, interface_idx);\n"
        << bl() << "buf.dv.setUint8(" << size_of_header << " + 3, " << fn->idx
        << ");\n"
        << bl() << "buf.dv.setBigUint64(" << size_of_header
        << " + 8, this.data.object_id, true);\n";

    if (fn->in_s) {
      // Use new marshal function instead of _Direct wrapper
      out << bl() << "marshal_" << fn->in_s->name << "(buf, "
          << get_arguments_offset() << ", {";

      int ix = 0;
      for (auto in : fn->args) {
        if (in->modifier == ArgumentModifier::Out)
          continue;
        if (ix > 0)
          out << ", ";
        out << "_" << (ix + 1) << ": ";
        // Input parameters are now ObjectId directly (no .data access
        // needed)
        out << in->name;
        ++ix;
      }
      out << "});\n";
    }

    out << bl() << "buf.write_len(buf.size - 4);\n";

    // Debug hook: record call start
    out << bl() << "const __dbg_t0 = Date.now();\n";
    out << bl() << "const __dbg_id = (globalThis as any).__nprpc_debug?.call_start({direction:'client',"
        << "class_id:_" << servant_iname << "._get_class(),"
        << "poa_idx:this.data.poa_idx,object_id:String(this.data.object_id),interface_idx,"
        << "func_idx:" << fn->idx << ",method_name:'" << fn->name << "',"
        << "endpoint:{hostname:this.endpoint.hostname,port:this.endpoint.port,"
           "transport:NPRPC.EndPoint.to_string(this.endpoint.type).replace('://','') as any},"
        << "request_args:{";  
    {
      bool _dbg_first = true;
      for (auto _dbg_a : fn->args) {
        if (_dbg_a->modifier == ArgumentModifier::Out) continue;
        if (!_dbg_first) out << ",";
        out << _dbg_a->name << ":" << _dbg_a->name;
        _dbg_first = false;
      }
    }
    out << "},request_bytes:buf.size});\n";

    out << bl() << "await NPRPC.rpc.call(this.endpoint, buf, this.timeout);\n"
        << bl() << "let std_reply = NPRPC.handle_standart_reply(buf);\n";

    if (fn->ex) {
      out << bl() << "if (std_reply == 1)" << bb() << bl()
          << ctx_->current_file() << "_throw_exception(buf);\n"
          << eb();
      ;
    }

    if (!fn->out_s) {
      out << bl() << "if (std_reply != 0) {\n"
          << bb(false) << bl()
          << "console.log(\"received an unusual reply for function with "
             "no "
             "output arguments\");\n"
          << eb();
    } else {
      out << bl() << "if (std_reply != -1) {\n"
          << bb(false) << bl()
          << "console.log(\"received an unusual reply for function with "
             "output "
             "arguments\");\n"
          << bl() << "throw new NPRPC.Exception(\"Unknown Error\");\n"
          << eb();

      // Use new unmarshal function instead of _Direct wrapper
      // Check if output struct needs remote_endpoint (has nested user
      // structs with objects)
      bool out_needs_endpoint = false;
      for (auto f : fn->out_s->fields) {
        if (f->type->id == FieldType::Struct) {
          auto nested = cflat(f->type);
          if (nested->fields.empty() || !nested->fields[0]->function_argument) {
            if (contains_object(f->type)) {
              out_needs_endpoint = true;
              break;
            }
          }
        }
      }
      if (out_needs_endpoint) {
        out << bl() << "const out = unmarshal_" << fn->out_s->name << "(buf, "
            << size_of_header << ", this.endpoint);\n";
      } else {
        out << bl() << "const out = unmarshal_" << fn->out_s->name << "(buf, "
            << size_of_header << ");\n";
      }

      int ix = fn->is_void() ? 0 : 1;

      for (auto out_arg : fn->args) {
        if (out_arg->modifier == ArgumentModifier::In)
          continue;
        ++ix;
        // For Object types, convert ObjectId to ObjectProxy
        if (out_arg->type->id == FieldType::Object) {
          out << bl() << out_arg->name
              << ".value = NPRPC.create_object_from_oid(out._" << ix
              << ", this.endpoint);\n";
        } else {
          out << bl() << out_arg->name << ".value = out._" << ix << ";\n";
        }
      }

      if (!fn->is_void()) {
        // Debug hook: call_end with response payload before returning
        out << bl() << "(globalThis as any).__nprpc_debug?.call_end(__dbg_id,"
            << "{status:'success',duration_ms:Date.now()-__dbg_t0,response_bytes:buf.size,response_args:out});\n";
        out << bl() << "return out._1;\n";
      }
    }

    // Debug hook: call_end for void-return path (no out_s, or out_s with void return)
    if (fn->is_void()) {
      out << bl() << "(globalThis as any).__nprpc_debug?.call_end(__dbg_id,"
          << "{status:'success',duration_ms:Date.now()-__dbg_t0,response_bytes:buf.size});\n";
    }
    out << eb(); // function
  }

  // HTTP Transport Support ================================================
  // Generate nested 'http' object with methods that return values directly
  out << '\n' << bl() << "// HTTP Transport (alternative to WebSocket)\n";
  out << bl() << "public readonly http = {\n";
  out << bb(false);

  bool first_http_method = true;
  for (auto& fn : ifs->fns) {
    if (fn->is_stream)
      continue; // skip stream functions for now

    if (!first_http_method)
      out << ",\n";
    first_http_method = false;

    out << bl() << fn->name << ": async (";

    // Input parameters only (no 'out' refs)
    bool first_param = true;
    for (auto arg : fn->args) {
      if (arg->modifier == ArgumentModifier::Out)
        continue;
      if (!first_param)
        out << ", ";
      first_param = false;
      out << arg->name << (arg->is_optional() ? "?: " : ": ");
      emit_parameter_type_for_proxy_call(arg, out);
    }

    out << "): Promise<";

    // Return type: combine return value + out parameters into object/tuple
    bool has_return = !fn->is_void();
    int out_param_count = 0;
    for (auto arg : fn->args) {
      if (arg->modifier == ArgumentModifier::Out)
        out_param_count++;
    }

    if (has_return && out_param_count == 0) {
      // Simple return value
      out << emit_type(fn->ret_value);
    } else if (!has_return && out_param_count == 1) {
      // Single out parameter - return it directly (no ref wrapper for
      // HTTP)
      for (auto arg : fn->args) {
        if (arg->modifier == ArgumentModifier::Out) {
          out << emit_type(arg->type);
          break;
        }
      }
    } else if (has_return || out_param_count > 0) {
      // Multiple returns - wrap in object
      out << "{ ";
      if (has_return) {
        out << "result: " << emit_type(fn->ret_value);
        if (out_param_count > 0)
          out << ", ";
      }
      int out_ix = 0;
      for (auto arg : fn->args) {
        if (arg->modifier == ArgumentModifier::Out) {
          if (out_ix > 0)
            out << ", ";
          out << arg->name << ": ";
          out << emit_type(arg->type);
          out_ix++;
        }
      }
      out << " }";
    } else {
      // void with no out params
      out << "void";
    }

    out << "> => {\n";
    out << bb(false);

    // Build the request (same as WebSocket version)
    const auto fixed_size =
        get_arguments_offset() + (fn->in_s ? fn->in_s->size : 0);
    const auto capacity =
        fixed_size + (fn->in_s ? (fn->in_s->flat ? 0 : 128) : 0);
    out << bl() << "const buf = NPRPC.FlatBuffer.create();\n"
        << bl() << "buf.prepare(" << capacity << ");\n"
        << bl() << "buf.commit(" << fixed_size << ");\n"
        << bl() << "buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);\n"
        << bl() << "buf.write_msg_type(NPRPC.impl.MessageType.Request);\n"
        << bl() << "buf.dv.setUint16(" << size_of_header
        << " + 0, this.data.poa_idx, true);\n"
        << bl() << "buf.dv.setUint8(" << size_of_header << " + 2, 0);\n"
        << // interface_idx = 0
        bl() << "buf.dv.setUint8(" << size_of_header << " + 3, " << fn->idx
        << ");\n"
        << bl() << "buf.dv.setBigUint64(" << size_of_header
        << " + 8, this.data.object_id, true);\n";

    if (fn->in_s) {
      out << bl() << "marshal_" << fn->in_s->name << "(buf, "
          << get_arguments_offset() << ", {";
      int ix = 0;
      for (auto in : fn->args) {
        if (in->modifier == ArgumentModifier::Out)
          continue;
        if (ix > 0)
          out << ", ";
        out << "_" << (ix + 1) << ": " << in->name;
        ++ix;
      }
      out << "});\n";
    }

    out << bl() << "buf.write_len(buf.size - 4);\n\n";

    // Debug hook: call_start before fetch
    out << bl() << "const __dbg_t0 = Date.now();\n";
    out << bl() << "const __dbg_id = (globalThis as any).__nprpc_debug?.call_start({direction:'client',"
        << "class_id:_" << servant_iname << "._get_class(),"
        << "poa_idx:this.data.poa_idx,object_id:String(this.data.object_id),"
        << "interface_idx:0,func_idx:" << fn->idx << ",method_name:'" << fn->name << "',"
        << "endpoint:{hostname:this.endpoint.hostname,port:this.endpoint.port,"
           "transport:'http'},"
        << "request_args:{";
    {
      bool _dbg_first = true;
      for (auto _dbg_a : fn->args) {
        if (_dbg_a->modifier == ArgumentModifier::Out) continue;
        if (!_dbg_first) out << ",";
        out << _dbg_a->name << ":" << _dbg_a->name;
        _dbg_first = false;
      }
    }
    out << "},request_bytes:buf.size});\n\n";

    // HTTP fetch instead of WebSocket
    out << bl()
        << "const url = `http${this.endpoint.is_ssl() ? 's' : "
           "''}://${this.endpoint.hostname}:${this.endpoint.port}/rpc`;\n"
        << bl() << "const response = await fetch(url, {\n"
        << bb(false) << bl() << "method: 'POST',\n"
        << bl() << "headers: { 'Content-Type': 'application/octet-stream' },\n"
        << bl() << "credentials: 'include',\n"
        << bl() << "body: buf.array_buffer\n"
        << eb() << ");\n\n"
        << bl()
        << "if (!response.ok) throw new NPRPC.Exception(`HTTP error: "
           "${response.status}`);\n"
        << bl() << "const response_data = await response.arrayBuffer();\n"
        << bl() << "buf.set_buffer(response_data);\n\n"
        << bl() << "let std_reply = NPRPC.handle_standart_reply(buf);\n";

    if (fn->ex) {
      out << bl() << "if (std_reply == 1) " << ctx_->current_file()
          << "_throw_exception(buf);\n";
    }

    if (!fn->out_s) {
      // No output
      out << bl()
          << "if (std_reply != 0) throw new NPRPC.Exception(\"Unexpected "
             "reply\");\n";
      out << bl() << "(globalThis as any).__nprpc_debug?.call_end(__dbg_id,"
          << "{status:'success',duration_ms:Date.now()-__dbg_t0,response_bytes:buf.size});\n";
    } else {
      out << bl()
          << "if (std_reply != -1) throw new "
             "NPRPC.Exception(\"Unexpected "
             "reply\");\n";

      bool out_needs_endpoint = false;
      for (auto f : fn->out_s->fields) {
        if (f->type->id == FieldType::Struct) {
          auto nested = cflat(f->type);
          if (nested->fields.empty() || !nested->fields[0]->function_argument) {
            if (contains_object(f->type)) {
              out_needs_endpoint = true;
              break;
            }
          }
        }
      }

      if (out_needs_endpoint) {
        out << bl() << "const out = unmarshal_" << fn->out_s->name << "(buf, "
            << size_of_header << ", this.endpoint);\n";
      } else {
        out << bl() << "const out = unmarshal_" << fn->out_s->name << "(buf, "
            << size_of_header << ");\n";
      }

      // Build return value
      bool has_ret = !fn->is_void();
      int out_count = 0;
      for (auto arg : fn->args) {
        if (arg->modifier == ArgumentModifier::Out)
          out_count++;
      }

      const auto dbg_end_http = "(globalThis as any).__nprpc_debug?.call_end(__dbg_id,"
                            "{status:'success',duration_ms:Date.now()-__dbg_t0,response_bytes:buf.size";
      if (has_ret && out_count == 0) {
        out << bl() << dbg_end_http << ",response_args:out});\n";
        out << bl() << "return out._1;\n";
      } else if (!has_ret && out_count == 1) {
        // Single out param - always at _1 in output struct
        for (auto arg : fn->args) {
          if (arg->modifier == ArgumentModifier::Out) {
            out << bl() << dbg_end_http << ",response_args:out});\n";
            if (arg->type->id == FieldType::Object) {
              out << bl()
                  << "return "
                     "NPRPC.create_object_from_oid(out._1, "
                     "this.endpoint);\n";
            } else {
              out << bl() << "return out._1;\n";
            }
            break;
          }
        }
      } else {
        // Multiple returns - build object
        out << bl() << dbg_end_http << ",response_args:out});\n";
        out << bl() << "return {";
        bool first_field = true;
        if (has_ret) {
          out << " result: out._1";
          first_field = false;
        }
        int ix = has_ret ? 2 : 1;
        for (auto arg : fn->args) {
          if (arg->modifier == ArgumentModifier::Out) {
            if (!first_field)
              out << ", ";
            first_field = false;
            out << " " << arg->name << ": ";
            if (arg->type->id == FieldType::Object) {
              out << "NPRPC.create_object_from_oid(out._" << ix
                  << ", this.endpoint)";
            } else {
              out << "out._" << ix;
            }
            ix++;
          }
        }
        out << " };\n";
      }
    }

    out << eb(false);   // Decrement depth
    out << bl() << "}"; // Close arrow function (no newline - comma comes after)
  }

  // Close http object literal
  out << eb(
      false); // Decrement depth for http object (from bb(false) on line 909)
  out << "\n" << bl() << "};\n";

  out << eb(); // Proxy class ends

  // Servant definition
  out << bl() << "export interface " << servant_iname << '\n';
  if (ifs->plist.size()) {
    out << " extends I" << ifs->plist[0]->name << "_Servant\n";
    for (size_t i = 1; i < ifs->plist.size(); ++i) {
      out << ", I" << ifs->plist[i]->name << "_Servant\n";
    }
  }
  out << bb();
  for (auto fn : ifs->fns) {
    out << bl() << fn->name;
    if (fn->is_stream) {
      out << '(';
      size_t ix = 0;
      for (auto arg : fn->args) {
        if (ix > 0)
          out << ", ";
        out << arg->name << (arg->is_optional() ? "?: " : ": ");
        if (arg->type->id == FieldType::Stream) {
          if (fn->stream_kind == StreamKind::Client) {
            out << "NPRPC.StreamReader<";
            emit_stream_value_type(fn->stream_decl->stream_in_type(), out);
            out << ">";
          } else {
            out << "NPRPC.BidiStream<";
            emit_stream_value_type(fn->stream_decl->stream_out_type(), out);
            out << ", ";
            emit_stream_value_type(fn->stream_decl->stream_in_type(), out);
            out << ">";
          }
        } else {
          emit_parameter_type_for_servant(arg, out);
        }
        ++ix;
      }
      if (fn->stream_kind == StreamKind::Bidi) {
        if (ix > 0)
          out << ", ";
        out << "stream: NPRPC.BidiStream<";
        emit_stream_value_type(fn->stream_decl->stream_out_type(), out);
        out << ", ";
        emit_stream_value_type(fn->stream_decl->stream_in_type(), out);
        out << ">";
      }
      out << ")";

      switch (fn->stream_kind) {
      case StreamKind::Server:
        out << ": AsyncIterable<";
        emit_stream_value_type(fn->stream_decl->stream_out_type(), out);
        out << "> | Iterable<";
        emit_stream_value_type(fn->stream_decl->stream_out_type(), out);
        out << "> | Promise<AsyncIterable<";
        emit_stream_value_type(fn->stream_decl->stream_out_type(), out);
        out << "> | Iterable<";
        emit_stream_value_type(fn->stream_decl->stream_out_type(), out);
        out << ">>;\n";
        break;
      case StreamKind::Client:
      case StreamKind::Bidi:
        out << ": void | Promise<void>;\n";
        break;
      default:
        assert(false);
      }
      continue;
    }

    emit_function_arguments(
        false, fn, out,
        std::bind(&TSBuilder::emit_parameter_type_for_servant, this, _1, _2));
    out << ": " << emit_type(fn->ret_value) << ";\n";
  }

  out << eb(); // interface ends

  out << bl() << "export class _" << servant_iname
      << " extends NPRPC.ObjectServant {\n"
      << bb(false) << bl() << "public static _get_class(): string { return \""
      << ctx_->current_file() << '/' << ctx_->nm_cur()->full_idl_namespace()
      << '.' /*ns(ctx_->nm_cur()) */ << ifs->name << "\"; }\n"
      << bl() << "public readonly get_class = () => { return _" << servant_iname
      << "._get_class(); }\n"
      << bl()
      << "public readonly dispatch = (buf: NPRPC.FlatBuffer, "
         "remote_endpoint: "
         "NPRPC.EndPoint, from_parent: boolean) => {\n"
      << bb(false) << bl() << "_" << servant_iname
      << "._dispatch(this, buf, remote_endpoint, from_parent);\n"
      << eb() << bl()
      << "public readonly dispatch_stream = (buf: NPRPC.FlatBuffer, "
        "remote_endpoint: NPRPC.EndPoint, from_parent: boolean) => {\n"
      << bb(false) << bl() << "_" << servant_iname
      << "._dispatch_stream(this, buf, remote_endpoint, from_parent);\n"
      << eb() << bl() << "static _dispatch(obj: _" << servant_iname
      << ", buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, "
         "from_parent: boolean): void {\n"
      << bb(false);

  // Servant dispatch ====================================================
  out << bl() << "// Read CallHeader directly\n"
      << bl() << "const function_idx = buf.dv.getUint8(" << size_of_header
      << " + 3);\n";

  if (ifs->plist.empty()) {
    // ok
  } else {
    out << bl() << "if (from_parent == false) {\n"
        << bb(false) << bl() << "const interface_idx = buf.dv.getUint8("
        << size_of_header << " + 2);\n"
        << bl() << "switch(interface_idx) {\n"
        << bb(false) << bl() << "case 0:\n"
        << bb(false) << bl() << "break;\n"
        << eb(false);
    int ix = 1;
    auto select_interface = [&ix, this, ifs](AstInterfaceDecl* i) {
      if (i == ifs)
        return;
      out << bl() << "case " << ix << ":\n"
          << bb(false) << bl() << "_I" << i->name
          << "_Servant._dispatch(obj, buf, remote_endpoint, true);\n"
          << bl() << "return;\n"
          << eb(false);
      ++ix;
    };

    dfs_interface(select_interface, ifs);

    out << bl() << "default:\n"
        << bb(false) << bl() << "throw \"unknown interface\";\n"
        << eb(false) << eb() << // switch
        eb()                    // if from_parent == false
        ;
  }

  out << bl() << "switch(function_idx) {\n" << bb(false);

  for (auto fn : ifs->fns) {
    if (fn->is_stream)
      continue; // skip stream functions for now

    out << bl() << "case " << fn->idx << ": {\n" << bb(false);
    int out_ix = fn->is_void() ? 0 : 1;
    if (fn->out_s) {
      for (auto arg : fn->args) {
        if (arg->modifier == ArgumentModifier::Out) {
          // For output parameters, use ObjectId instead of
          // ObjectProxy for object types
          out << bl() << "let _out_" << ++out_ix << ": ";
          if (arg->type->id == FieldType::Object) {
            out << "NPRPC.ObjectId";
          } else {
            out << emit_type(arg->type);
          }
          out << ";\n";
        }
      }
    }

    if (fn->in_s) {
      // Check if input struct needs remote_endpoint (has nested user
      // structs with objects)
      bool in_needs_endpoint = false;
      for (auto f : fn->in_s->fields) {
        if (f->type->id == FieldType::Struct) {
          auto nested = cflat(f->type);
          if (nested->fields.empty() || !nested->fields[0]->function_argument) {
            if (contains_object(f->type)) {
              in_needs_endpoint = true;
              break;
            }
          }
        }
      }
      if (in_needs_endpoint) {
        out << bl() << "const ia = unmarshal_" << fn->in_s->name << "(buf, "
            << get_arguments_offset() << ", remote_endpoint);\n";
      } else {
        out << bl() << "const ia = unmarshal_" << fn->in_s->name << "(buf, "
            << get_arguments_offset() << ");\n";
      }
    }

    if (fn->out_s && !fn->out_s->flat) {
      const auto offset = size_of_header;
      const auto initial_size = offset + fn->out_s->size;
      out << bl() << "const obuf = buf;\n"
          << bl() << "obuf.consume(obuf.size);\n"
          << bl() << "obuf.prepare(" << initial_size + 128 << ");\n"
          << bl() << "obuf.commit(" << initial_size << ");\n";
    }

    if (!fn->is_void())
      emit_variable(fn->ret_value, "__ret_val", out);

    // Debug hook: declared before try so __dbg_t0/__dbg_id are visible in catch
    out << bl() << "const __dbg_t0 = Date.now();\n";
    out << bl() << "const __dbg_id = (globalThis as any).__nprpc_debug?.call_start({direction:'server',"
        << "class_id:_" << servant_iname << "._get_class(),"
        << "poa_idx:obj.poa.index,object_id:String(obj.oid),"
        << "interface_idx:0,func_idx:" << fn->idx << ",method_name:'" << fn->name << "',"
        << "endpoint:{hostname:remote_endpoint.hostname,port:remote_endpoint.port,"
           "transport:NPRPC.EndPoint.to_string(remote_endpoint.type).replace('://','') as any},"
        << "request_args:";
    if (fn->in_s) {
      out << "ia";
    } else {
      out << "{}";
    }
    out << "});\n";

    if (fn->ex)
      out << bl() << "try {\n" << bb(false);

    out << bl() << (fn->is_void() ? "" : "__ret_val = ") << "(obj as any)."
        << fn->name << "(";

    size_t in_ix = 0, idx = 0;
    out_ix = fn->is_void() ? 0 : 1;
    for (auto arg : fn->args) {
      if (arg->modifier == ArgumentModifier::Out) {
        assert(fn->out_s);
        // For output arguments, we'll create refs to pass
        out << bl() << "_out_" << ++out_ix;
      } else {
        // For input arguments, convert ObjectId to ObjectProxy if
        // needed
        ++in_ix;
        if (arg->type->id == FieldType::Object) {
          out << "NPRPC.create_object_from_oid(ia._" << in_ix
              << ", remote_endpoint)";
        } else {
          out << "ia._" << in_ix;
        }
      }
      if (++idx != fn->args.size())
        out << ", ";
    }
    out << ");\n";

    /*
    out_ix = fn->is_void() ? 0 : 1;
    for (auto arg : fn->args) {
      if (arg->modifier == ArgumentModifier::Out) {
        ++out_ix;
        if (arg->type->id == FieldType::Object) {
          oc <<
            "{\n"
            "  auto obj = impl::g_rpc->get_object(" << "oa._" << out_ix <<
    "().poa_idx(), " << "oa._" << out_ix << "().object_id());\n" "  if (obj)
    if (auto real_obj = (*obj).get(); real_obj)
    ref_list.add_ref(real_obj);\n"
            "}\n"
            ;
        }
      }
    }
    */

    if (fn->is_throwing()) {
      auto declared_exceptions = fn->exceptions;
      if (declared_exceptions.empty() && fn->ex)
        declared_exceptions.push_back(fn->ex);

      const auto offset = size_of_header;

      out << eb() << bl() << "catch(e) {\n" << bb(false);
      for (auto* ex : declared_exceptions) {
        const auto initial_size = offset + ex->size;
        always_full_namespace(true);
        out << bl() << "if (e instanceof " << emit_type(ex) << ") {\n";
        always_full_namespace(false);
        out << bb(false)
            << bl() << "const obuf = buf;\n"
            << bl() << "obuf.consume(obuf.size);\n"
            << bl() << "obuf.prepare(" << initial_size << ");\n"
            << bl() << "obuf.commit(" << initial_size << ");\n"
            << bl() << "const ex_data = {__ex_id: " << ex->exception_id;
        for (size_t i = 1; i < ex->fields.size(); ++i) {
          auto mb = ex->fields[i];
          out << ", " << mb->name << ": e." << mb->name;
        }
        out << "};\n";
        always_full_namespace(true);
        out << bl() << ns(ex->nm) << "marshal_" << ex->name << "(obuf, "
            << offset << ", ex_data);\n";
        always_full_namespace(false);
        out << bl() << "obuf.write_len(obuf.size - 4);\n"
            << bl() << "obuf.write_msg_id(NPRPC.impl.MessageId.Exception);\n"
            << bl() << "obuf.write_msg_type(NPRPC.impl.MessageType.Answer);\n"
            << bl() << "(globalThis as any).__nprpc_debug?.call_end(__dbg_id,{status:'error',duration_ms:Date.now()-__dbg_t0,error:String(e)});\n"
            << bl() << "return;\n"
            << eb();
      }
      out << bl() << "throw e;\n"
          << eb();
    }
    // Debug hook: call_end for success path
    out << bl() << "(globalThis as any).__nprpc_debug?.call_end(__dbg_id,{status:'success',duration_ms:Date.now()-__dbg_t0});\n";

    if (!fn->out_s) {
      out << bl()
          << "NPRPC.make_simple_answer(buf, "
             "NPRPC.impl.MessageId.Success);\n";
    } else {
      if (fn->out_s->flat) { // it means that we are writing output data
                             // in the input buffer
        const auto offset = size_of_header;
        const auto initial_size = offset + fn->out_s->size;
        out << bl() << "const obuf = buf;\n"
            << bl() << "obuf.consume(obuf.size);\n"
            << bl() << "obuf.prepare(" << initial_size << ");\n"
            << bl() << "obuf.commit(" << initial_size << ");\n";
      }

      // Build output object
      out << bl() << "const out_data = {";
      int ix = 0;
      if (!fn->is_void()) {
        out << "_1: __ret_val";
        ix = 1;
      }
      for (auto out_arg : fn->args) {
        if (out_arg->modifier == ArgumentModifier::In)
          continue;
        if (ix > 0)
          out << ", ";
        ++ix;
        out << "_" << ix << ": ";
        // Output variables are now declared as ObjectId directly, no
        // .data needed
        out << "_out_" << ix;
      }
      out << "};\n";

      out << bl() << "marshal_" << fn->out_s->name << "(obuf, "
          << size_of_header << ", out_data);\n"
          << bl() << "obuf.write_len(obuf.size - 4);\n"
          << bl() << "obuf.write_msg_id(NPRPC.impl.MessageId.BlockResponse);\n"
          << bl() << "obuf.write_msg_type(NPRPC.impl.MessageType.Answer);\n";
    }

    out << bl() << "break;\n" << eb(); // case ends
    ;
  }

  out << bl() << "default:\n"
      << bb(false) << bl()
      << "NPRPC.make_simple_answer(buf, "
         "NPRPC.impl.MessageId.Error_UnknownFunctionIdx);\n"
      << eb(false) << // default ends
      eb() <<         // switch block ends
      eb() <<         // dispatch ends
      bl() << "static _dispatch_stream(obj: _" << servant_iname
      << ", buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, "
         "from_parent: boolean): void {\n"
      << bb(false)
      << bl() << "const init = NPRPC.impl.unmarshal_StreamInit(buf, "
      << size_of_header << ");\n"
      << bl() << "const function_idx = init.func_idx;\n";

  if (!ifs->plist.empty()) {
    out << bl() << "if (from_parent == false) {\n"
        << bb(false) << bl() << "switch(init.interface_idx) {\n"
        << bb(false) << bl() << "case 0:\n"
        << bb(false) << bl() << "break;\n"
        << eb(false);

    int ix = 1;
    auto select_stream_interface = [&ix, this, ifs](AstInterfaceDecl* i) {
      if (i == ifs)
        return;
      out << bl() << "case " << ix << ":\n"
          << bb(false) << bl() << "_I" << i->name
          << "_Servant._dispatch_stream(obj, buf, remote_endpoint, true);\n"
          << bl() << "return;\n"
          << eb(false);
      ++ix;
    };

    dfs_interface(select_stream_interface, ifs);

    out << bl() << "default:\n"
        << bb(false) << bl() << "throw \"unknown interface\";\n"
        << eb(false) << eb() << eb();
  }

  out << bl() << "const conn = NPRPC.rpc.get_connection(remote_endpoint);\n"
      << bl() << "switch(function_idx) {\n" << bb(false);

  for (auto fn : ifs->fns) {
    if (!fn->is_stream)
      continue;

    out << bl() << "case " << fn->idx << ": {\n" << bb(false);

    if (fn->in_s) {
      bool in_needs_endpoint = false;
      for (auto f : fn->in_s->fields) {
        if (f->type->id == FieldType::Struct) {
          auto nested = cflat(f->type);
          if (nested->fields.empty() || !nested->fields[0]->function_argument) {
            if (contains_object(f->type)) {
              in_needs_endpoint = true;
              break;
            }
          }
        }
      }
      if (in_needs_endpoint) {
        out << bl() << "const ia = unmarshal_" << fn->in_s->name << "(buf, "
            << get_stream_init_arguments_offset() << ", remote_endpoint);\n";
      } else {
        out << bl() << "const ia = unmarshal_" << fn->in_s->name << "(buf, "
            << get_stream_init_arguments_offset() << ");\n";
      }
    }

    out << bl() << "(globalThis as any).__nprpc_debug?.stream_start({direction:'server',"
        << "class_id:_" << servant_iname << "._get_class(),"
        << "poa_idx:obj.poa.index,object_id:String(obj.oid),"
        << "interface_idx:init.interface_idx,func_idx:" << fn->idx << ",method_name:'" << fn->name << "',"
        << "endpoint:{hostname:remote_endpoint.hostname,port:remote_endpoint.port,"
           "transport:NPRPC.EndPoint.to_string(remote_endpoint.type).replace('://','') as any},"
        << "stream_id:String(init.stream_id),stream_kind:'";

    switch (fn->stream_kind) {
    case StreamKind::Server:
      out << "server";
      break;
    case StreamKind::Client:
      out << "client";
      break;
    case StreamKind::Bidi:
      out << "bidi";
      break;
    default:
      assert(false);
    }

    out << "',request_args:";
    if (fn->in_s) {
      out << "ia";
    } else {
      out << "{}";
    }
    out << ",request_bytes:buf.size});\n";

    switch (fn->stream_kind) {
    case StreamKind::Server:
      out << bl() << "const writer = conn.stream_manager.create_writer(init.stream_id, ";
      emit_stream_serializer(fn->stream_decl->stream_out_type(), out);
      out << ");\n"
          << bl() << "NPRPC.make_simple_answer(buf, NPRPC.impl.MessageId.Success);\n"
          << bl() << "void (async () => {\n"
          << bb(false)
          << bl() << "try {\n"
          << bb(false)
          << bl() << "const source = await (obj as any)." << fn->name << "(";
      {
        size_t in_ix = 0;
        bool first_arg = true;
        for (auto arg : fn->args) {
          if (arg->modifier == ArgumentModifier::Out || arg->type->id == FieldType::Stream)
            continue;
          if (!first_arg)
            out << ", ";
          ++in_ix;
          if (arg->type->id == FieldType::Object) {
            out << "NPRPC.create_object_from_oid(ia._" << in_ix << ", remote_endpoint)";
          } else {
            out << "ia._" << in_ix;
          }
          first_arg = false;
        }
        if (fn->stream_kind == StreamKind::Bidi) {
          if (!first_arg)
            out << ", ";
          out << "stream";
        }
      }
      out << ");\n"
          << bl() << "for await (const chunk of source as any) {\n"
          << bb(false) << bl() << "writer.write(chunk);\n" << eb()
          << bl() << "writer.close();\n"
          << eb(false)
          << bl() << "} catch (e) {\n"
          << bb(false)
          << bl() << "writer.abort();\n"
          << bl() << "console.error('Stream handler failed', e);\n"
          << eb()
          << bl() << "})();\n"
          << bl() << "return;\n";
      break;
    case StreamKind::Client:
      out << bl() << "const reader = conn.stream_manager.create_reader(init.stream_id, ";
      emit_stream_deserializer(fn->stream_decl->stream_in_type(), out);
      out << ");\n"
          << bl() << "NPRPC.make_simple_answer(buf, NPRPC.impl.MessageId.Success);\n"
          << bl() << "void (async () => {\n"
          << bb(false)
          << bl() << "try {\n"
          << bb(false)
          << bl() << "await (obj as any)." << fn->name << "(";
      {
        size_t in_ix = 0;
        bool first_arg = true;
        for (auto arg : fn->args) {
          if (!first_arg)
            out << ", ";
          if (arg->type->id == FieldType::Stream) {
            out << "reader";
          } else {
            ++in_ix;
            if (arg->type->id == FieldType::Object) {
              out << "NPRPC.create_object_from_oid(ia._" << in_ix << ", remote_endpoint)";
            } else {
              out << "ia._" << in_ix;
            }
          }
          first_arg = false;
        }
      }
      out << ");\n"
          << eb(false)
          << bl() << "} catch (e) {\n"
          << bb(false)
          << bl() << "reader.cancel();\n"
          << bl() << "console.error('Stream handler failed', e);\n"
          << eb()
          << bl() << "})();\n"
          << bl() << "return;\n";
      break;
    case StreamKind::Bidi:
      out << bl() << "const stream = conn.stream_manager.create_bidi_stream(init.stream_id, ";
      emit_stream_serializer(fn->stream_decl->stream_out_type(), out);
      out << ", ";
      emit_stream_deserializer(fn->stream_decl->stream_in_type(), out);
      out << ");\n"
          << bl() << "NPRPC.make_simple_answer(buf, NPRPC.impl.MessageId.Success);\n"
          << bl() << "void (async () => {\n"
          << bb(false)
          << bl() << "try {\n"
          << bb(false)
          << bl() << "await (obj as any)." << fn->name << "(";
      {
        size_t in_ix = 0;
        bool first_arg = true;
        for (auto arg : fn->args) {
          if (!first_arg)
            out << ", ";
          {
            ++in_ix;
            if (arg->type->id == FieldType::Object) {
              out << "NPRPC.create_object_from_oid(ia._" << in_ix << ", remote_endpoint)";
            } else {
              out << "ia._" << in_ix;
            }
          }
          first_arg = false;
        }
        if (!first_arg)
          out << ", ";
        out << "stream";
      }
      out << ");\n"
          << eb(false)
          << bl() << "} catch (e) {\n"
          << bb(false)
          << bl() << "stream.writer.abort();\n"
          << bl() << "stream.reader.cancel();\n"
          << bl() << "console.error('Stream handler failed', e);\n"
          << eb()
          << bl() << "})();\n"
          << bl() << "return;\n";
      break;
    default:
      assert(false);
    }

    out << eb();
  }

  out << bl() << "default:\n"
      << bb(false) << bl()
      << "NPRPC.make_simple_answer(buf, NPRPC.impl.MessageId.Error_UnknownFunctionIdx);\n"
      << eb(false) << eb() << eb() << eb() << '\n';
}

TSBuilder::_ns TSBuilder::ns(Namespace* nm) const { return {*this, nm}; }

void TSBuilder::emit_field_marshal(AstFieldDecl* f,
                                   int& offset,
                                   const std::string& data_name,
                                   bool is_generated_arg_struct)
{
  const std::string field_access = data_name + "." + f->name;

  switch (f->type->id) {
  case FieldType::Fundamental: {
    const auto token = cft(f->type)->token_id;
    const int size = get_fundamental_size(token);
    const int field_offset = align_offset(size, offset, size);

    switch (token) {
    case TokenId::Boolean:
      out << bl() << "buf.dv.setUint8(offset + " << field_offset << ", "
          << field_access << " ? 1 : 0);\n";
      break;
    case TokenId::Int8:
      out << bl() << "buf.dv.setInt8(offset + " << field_offset << ", "
          << field_access << ");\n";
      break;
    case TokenId::UInt8:
      out << bl() << "buf.dv.setUint8(offset + " << field_offset << ", "
          << field_access << ");\n";
      break;
    case TokenId::Int16:
      out << bl() << "buf.dv.setInt16(offset + " << field_offset << ", "
          << field_access << ", true);\n";
      break;
    case TokenId::UInt16:
      out << bl() << "buf.dv.setUint16(offset + " << field_offset << ", "
          << field_access << ", true);\n";
      break;
    case TokenId::Int32:
      out << bl() << "buf.dv.setInt32(offset + " << field_offset << ", "
          << field_access << ", true);\n";
      break;
    case TokenId::UInt32:
      out << bl() << "buf.dv.setUint32(offset + " << field_offset << ", "
          << field_access << ", true);\n";
      break;
    case TokenId::Int64:
      out << bl() << "buf.dv.setBigInt64(offset + " << field_offset << ", "
          << field_access << ", true);\n";
      break;
    case TokenId::UInt64:
      out << bl() << "buf.dv.setBigUint64(offset + " << field_offset << ", "
          << field_access << ", true);\n";
      break;
    case TokenId::Float32:
      out << bl() << "buf.dv.setFloat32(offset + " << field_offset << ", "
          << field_access << ", true);\n";
      break;
    case TokenId::Float64:
      out << bl() << "buf.dv.setFloat64(offset + " << field_offset << ", "
          << field_access << ", true);\n";
      break;
    default:
      assert(false);
    }
    break;
  }
  case FieldType::Enum: {
    const auto token = cenum(f->type)->token_id;
    const int size = get_fundamental_size(token);
    const int field_offset = align_offset(size, offset, size);

    switch (token) {
    case TokenId::Int8:
      out << bl() << "buf.dv.setInt8(offset + " << field_offset << ", "
          << field_access << ");\n";
      break;
    case TokenId::UInt8:
      out << bl() << "buf.dv.setUint8(offset + " << field_offset << ", "
          << field_access << ");\n";
      break;
    case TokenId::Int16:
      out << bl() << "buf.dv.setInt16(offset + " << field_offset << ", "
          << field_access << ", true);\n";
      break;
    case TokenId::UInt16:
      out << bl() << "buf.dv.setUint16(offset + " << field_offset << ", "
          << field_access << ", true);\n";
      break;
    case TokenId::Int32:
      out << bl() << "buf.dv.setInt32(offset + " << field_offset << ", "
          << field_access << ", true);\n";
      break;
    case TokenId::UInt32:
      out << bl() << "buf.dv.setUint32(offset + " << field_offset << ", "
          << field_access << ", true);\n";
      break;
    case TokenId::Int64:
      out << bl() << "buf.dv.setBigInt64(offset + " << field_offset << ", "
          << field_access << ", true);\n";
      break;
    case TokenId::UInt64:
      out << bl() << "buf.dv.setBigUint64(offset + " << field_offset << ", "
          << field_access << ", true);\n";
      break;
    default:
      assert(false);
    }
    break;
  }
  case FieldType::String: {
    const int field_offset = align_offset(4, offset, 8);
    out << bl() << "NPRPC.marshal_string(buf, offset + " << field_offset << ", "
        << field_access << ");\n";
    break;
  }
  case FieldType::Struct: {
    auto s = cflat(f->type);
    const int field_offset = align_offset(s->align, offset, s->size);
    out << bl() << "marshal_" << s->name << "(buf, offset + " << field_offset
        << ", " << field_access << ");\n";
    break;
  }
  case FieldType::Object: {
    const int field_offset =
        align_offset(align_of_object, offset, size_of_object);
    // For user-defined structs, the field is ObjectProxy and we need to
    // extract .data For generated M structs, the field is already ObjectId
    if (is_generated_arg_struct) {
      // Generated struct: field is ObjectId
      out << bl() << "NPRPC.detail.marshal_ObjectId(buf, offset + "
          << field_offset << ", " << field_access << ");\n";
    } else {
      // User-defined struct: field is ObjectProxy, extract .data
      out << bl() << "NPRPC.detail.marshal_ObjectId(buf, offset + "
          << field_offset << ", " << field_access << ".data);\n";
    }
    break;
  }
  case FieldType::Array: {
    // Fixed-size array - data is inline at the offset
    auto wt = cwt(f->type)->real_type();
    auto arr = static_cast<AstArrayDecl*>(f->type);
    auto [v_size, v_align] = get_type_size_align(f->type);
    auto [ut_size, ut_align] = get_type_size_align(wt);
    const int field_offset = align_offset(v_align, offset, v_size);

    if (is_fundamental(wt)) {
      // For fixed-size arrays of fundamentals, copy data directly to
      // buffer
      auto typed_array_name = get_typed_array_name(cft(wt)->token_id);
      auto __arr = make_unique_variable_name("__arr");
      out << bl() << "const " << __arr << " = new " << typed_array_name
          << "(buf.array_buffer, offset + " << field_offset << ", "
          << arr->length << ");\n";
      out << bl() << __arr << ".set(" << field_access << ");\n";
    } else if (wt->id == FieldType::String) {
      // For fixed-size arrays of strings, marshal each element in place
      out << bl() << "for (let i = 0; i < " << arr->length << "; ++i) {\n"
          << bb(false);
      out << bl() << "NPRPC.marshal_string(buf, offset + " << field_offset
          << " + i * 8, " << field_access << "[i]);\n";
      out << eb();

    }else if (wt->id == FieldType::Struct) {
      // For fixed-size arrays of structs, marshal each element in place
      out << bl() << "for (let i = 0; i < " << arr->length << "; ++i) {\n"
          << bb(false);
      out << bl() << "marshal_" << cflat(wt)->name << "(buf, offset + "
          << field_offset << " + i * " << ut_size << ", " << field_access
          << "[i]);\n";
      out << eb();
    } else {
      throw std::runtime_error("Unsupported vector element type for marshalling");
    }
    break;
  }
  case FieldType::Vector: {
    // Dynamic vector - data is indirect (ptr + length)
    auto wt = cwt(f->type)->real_type();
    auto [v_size, v_align] = get_type_size_align(f->type);
    auto [ut_size, ut_align] = get_type_size_align(wt);
    const int field_offset = align_offset(v_align, offset, v_size);

    if (is_fundamental(wt)) {
      out << bl() << "NPRPC.marshal_typed_array(buf, offset + " << field_offset
          << ", " << field_access << ", " << ut_size << ", " << ut_align
          << ");\n";
    } else if (wt->id == FieldType::String) {
      out << bl() << "NPRPC.marshal_string_array(buf, offset + " << field_offset
          << ", " << field_access << ");\n";
    } else if (wt->id == FieldType::Struct) {
      out << bl() << "NPRPC.marshal_struct_array(buf, offset + " << field_offset
          << ", " << field_access << ", marshal_" << cflat(wt)->name << ", "
          << ut_size << ", " << ut_align << ");\n";
    } else {
      throw std::runtime_error("Unsupported vector element type for marshalling");
    }
    break;
  }
  case FieldType::Optional: {
    // All optionals have the same layout: 4-byte relative offset (0 = no
    // value)
    auto declared_optional_type = copt(f->type)->type;
    auto wt = cwt(f->type)->real_type();
    auto [v_size, v_align] = get_type_size_align(f->type);
    const int field_offset = align_offset(v_align, offset, v_size);

    out << bl() << "if (" << field_access << " !== undefined) {\n" << bb(false);

    if (is_fundamental(wt)) {
      out << bl() << "NPRPC.marshal_optional_fundamental(buf, offset + "
          << field_offset << ", " << field_access << ", "
          << fundamental_kind_literal(cft(wt)->token_id) << ");\n";
    } else if (wt->id == FieldType::Struct) {
      auto [wt_size, wt_align] = get_type_size_align(wt);
      out << bl() << "NPRPC.marshal_optional_struct(buf, offset + "
          << field_offset << ", " << field_access << ", marshal_"
          << cflat(wt)->name << ", " << wt_size << ", " << wt_align << ");\n";
    } else if (wt->id == FieldType::String) {
      // Optional string uses marshal_optional_struct with marshal_string
      out << bl() << "NPRPC.marshal_optional_struct(buf, offset + "
          << field_offset << ", " << field_access
          << ", NPRPC.marshal_string, 8, 4);\n";
    } else if (wt->id == FieldType::Vector || wt->id == FieldType::Array) {
      // Optional vector/array also uses the optional_struct pattern
      auto real_elem_type = cwt(wt)->real_type();
      auto [ut_size, ut_align] = get_type_size_align(real_elem_type);
      if (is_fundamental(real_elem_type)) {
        const bool alias_to_vector = declared_optional_type->id == FieldType::Alias &&
                                     calias(declared_optional_type)->get_real_type()->id == FieldType::Vector;
        const bool keep_typed_array = alias_to_vector &&
                                      cft(real_elem_type)->token_id == TokenId::UInt8;
        if (alias_to_vector && !keep_typed_array) {
          auto typed_array_name = get_typed_array_name(cft(real_elem_type)->token_id);
          auto temp_var = "temp_" + f->name;
          out << bl() << "const " << temp_var << " = new " << typed_array_name
              << "(" << field_access << ");\n";
          out << bl() << "NPRPC.marshal_optional_struct(buf, offset + "
              << field_offset << ", " << temp_var
              << ", (b, o, v) => NPRPC.marshal_typed_array(b, o, v, " << ut_size
              << ", " << ut_align << "), 8, 4);\n";
        } else {
          out << bl() << "NPRPC.marshal_optional_struct(buf, offset + "
              << field_offset << ", " << field_access
              << ", (b, o, v) => NPRPC.marshal_typed_array(b, o, v, " << ut_size
              << ", " << ut_align << "), 8, 4);\n";
        }
      } else if (real_elem_type->id == FieldType::Struct) {
        out << bl() << "NPRPC.marshal_optional_struct(buf, offset + "
            << field_offset << ", " << field_access
            << ", (b, o, v) => NPRPC.marshal_struct_array(b, o, v, "
               "marshal_"
            << cflat(real_elem_type)->name << ", " << ut_size << ", "
            << ut_align << "), 8, 4);\n";
      } else {
        assert(false && "Unsupported vector element type in optional "
                        "for marshalling");
      }
    } else {
      assert(false && "Unsupported optional element type for marshalling");
    }

    out << eb(false) << bl() << "} else {\n"
        << bb(false) << bl() << "buf.dv.setUint32(offset + " << field_offset
        << ", 0, true); // nullopt\n"
        << eb();
    break;
  }
  case FieldType::Alias: {
    auto real_type = calias(f->type)->get_real_type();
    // Check if this is an alias to a VECTOR (not array) of fundamentals
    // Arrays stay as TypedArrays, but vector aliases use JavaScript arrays
    // for semantic meaning
    if (real_type->id == FieldType::Vector) {
      auto elem_type = cwt(real_type)->real_type();
      if (is_fundamental(elem_type)) {
        // Convert Array<T> to TypedArray for marshalling
        auto [v_size, v_align] = get_type_size_align(real_type);
        auto [ut_size, ut_align] = get_type_size_align(elem_type);
        const int field_offset = align_offset(v_align, offset, v_size);
        auto typed_array_name = get_typed_array_name(cft(elem_type)->token_id);
        auto temp_var = "temp_" + f->name;
        out << bl() << "const " << temp_var << " = new " << typed_array_name
            << "(" << field_access << ");\n";
        // Marshal the typed array directly (don't recurse, to avoid
        // appending field name again)
        out << bl() << "NPRPC.marshal_typed_array(buf, offset + "
            << field_offset << ", " << temp_var << ", " << ut_size << ", "
            << ut_align << ");\n";
        return;
      }
    }
    // For other aliases (including array aliases), just resolve and marshal
    auto temp_field = *f;
    temp_field.type = real_type;
    emit_field_marshal(&temp_field, offset, data_name);
    return;
  }
  default:
    assert(false);
  }
}

void TSBuilder::emit_field_unmarshal(AstFieldDecl* f,
                                     int& offset,
                                     const std::string& result_name,
                                     bool has_endpoint)
{
  const std::string field_name = result_name + "." + f->name;

  switch (f->type->id) {
  case FieldType::Fundamental: {
    const auto token = cft(f->type)->token_id;
    const int size = get_fundamental_size(token);
    const int field_offset = align_offset(size, offset, size);

    switch (token) {
    case TokenId::Boolean:
      out << bl() << field_name << " = buf.dv.getUint8(offset + "
          << field_offset << ") !== 0;\n";
      break;
    case TokenId::Int8:
      out << bl() << field_name << " = buf.dv.getInt8(offset + " << field_offset
          << ");\n";
      break;
    case TokenId::UInt8:
      out << bl() << field_name << " = buf.dv.getUint8(offset + "
          << field_offset << ");\n";
      break;
    case TokenId::Int16:
      out << bl() << field_name << " = buf.dv.getInt16(offset + "
          << field_offset << ", true);\n";
      break;
    case TokenId::UInt16:
      out << bl() << field_name << " = buf.dv.getUint16(offset + "
          << field_offset << ", true);\n";
      break;
    case TokenId::Int32:
      out << bl() << field_name << " = buf.dv.getInt32(offset + "
          << field_offset << ", true);\n";
      break;
    case TokenId::UInt32:
      out << bl() << field_name << " = buf.dv.getUint32(offset + "
          << field_offset << ", true);\n";
      break;
    case TokenId::Int64:
      out << bl() << field_name << " = buf.dv.getBigInt64(offset + "
          << field_offset << ", true);\n";
      break;
    case TokenId::UInt64:
      out << bl() << field_name << " = buf.dv.getBigUint64(offset + "
          << field_offset << ", true);\n";
      break;
    case TokenId::Float32:
      out << bl() << field_name << " = buf.dv.getFloat32(offset + "
          << field_offset << ", true);\n";
      break;
    case TokenId::Float64:
      out << bl() << field_name << " = buf.dv.getFloat64(offset + "
          << field_offset << ", true);\n";
      break;
    default:
      assert(false);
    }
    break;
  }
  case FieldType::Enum: {
    const auto token = cenum(f->type)->token_id;
    const int size = get_fundamental_size(token);
    const int field_offset = align_offset(size, offset, size);

    switch (token) {
    case TokenId::Int8:
      out << bl() << field_name << " = buf.dv.getInt8(offset + " << field_offset
          << ");\n";
      break;
    case TokenId::UInt8:
      out << bl() << field_name << " = buf.dv.getUint8(offset + " << field_offset
          << ");\n";
      break;
    case TokenId::Int16:
      out << bl() << field_name << " = buf.dv.getInt16(offset + " << field_offset
          << ", true);\n";
      break;
    case TokenId::UInt16:
      out << bl() << field_name << " = buf.dv.getUint16(offset + " << field_offset
          << ", true);\n";
      break;
    case TokenId::Int32:
      out << bl() << field_name << " = buf.dv.getInt32(offset + " << field_offset
          << ", true);\n";
      break;
    case TokenId::UInt32:
      out << bl() << field_name << " = buf.dv.getUint32(offset + " << field_offset
          << ", true);\n";
      break;
    case TokenId::Int64:
      out << bl() << field_name << " = buf.dv.getBigInt64(offset + " << field_offset
          << ", true);\n";
      break;
    case TokenId::UInt64:
      out << bl() << field_name << " = buf.dv.getBigUint64(offset + " << field_offset
          << ", true);\n";
      break;
    default:
      assert(false);
    }
    break;
  }
  case FieldType::String: {
    const int field_offset = align_offset(4, offset, 8);
    out << bl() << field_name << " = NPRPC.unmarshal_string(buf, offset + "
        << field_offset << ");\n";
    break;
  }
  case FieldType::Struct: {
    auto s = cflat(f->type);
    const int field_offset = align_offset(s->align, offset, s->size);
    // Pass remote_endpoint if the nested struct contains objects
    if (has_endpoint && contains_object(f->type)) {
      out << bl() << field_name << " = unmarshal_" << s->name
          << "(buf, offset + " << field_offset << ", remote_endpoint);\n";
    } else {
      out << bl() << field_name << " = unmarshal_" << s->name
          << "(buf, offset + " << field_offset << ");\n";
    }
    break;
  }
  case FieldType::Object: {
    const int field_offset =
        align_offset(align_of_object, offset, size_of_object);
    if (has_endpoint) {
      // Convert ObjectId to ObjectProxy using remote_endpoint
      out << bl() << field_name << " = NPRPC.create_object_from_oid("
          << "NPRPC.detail.unmarshal_ObjectId(buf, offset + " << field_offset
          << "), remote_endpoint);\n";
    } else {
      // Just unmarshal as ObjectId
      out << bl() << field_name
          << " = NPRPC.detail.unmarshal_ObjectId(buf, offset + " << field_offset
          << ");\n";
    }
    break;
  }
  case FieldType::Array: {
    // Fixed-size array - data is inline at the offset
    auto wt = cwt(f->type)->real_type();
    auto arr = static_cast<AstArrayDecl*>(f->type);
    auto [v_size, v_align] = get_type_size_align(f->type);
    auto [ut_size, ut_align] = get_type_size_align(wt);
    const int field_offset = align_offset(v_align, offset, v_size);

    if (is_fundamental(wt)) {
      // For fixed-size arrays of fundamentals, create view directly on
      // buffer
      auto typed_array_name = get_typed_array_name(cft(wt)->token_id);
      out << bl() << field_name << " = new " << typed_array_name
          << "(buf.array_buffer, offset + " << field_offset << ", "
          << arr->length << ");\n";
    } else if (wt->id == FieldType::String) {
      // For fixed-size arrays of strings, unmarshal each element
      out << bl() << field_name << " = new Array(" << arr->length << ");\n";
      out << bl() << "for (let i = 0; i < " << arr->length << "; ++i) {\n"
          << bb(false);
      out << bl() << field_name << "[i] = NPRPC.unmarshal_string(buf, offset + "
          << field_offset << " + i * 8);\n";
      out << eb();
    } else if (wt->id == FieldType::Struct) {
      // For fixed-size arrays of structs, unmarshal each element
      out << bl() << field_name << " = new Array(" << arr->length << ");\n";
      out << bl() << "for (let i = 0; i < " << arr->length << "; ++i) {\n"
          << bb(false);
      if (has_endpoint && contains_object(wt)) {
        out << bl() << field_name << "[i] = unmarshal_" << cflat(wt)->name
            << "(buf, offset + " << field_offset << " + i * " << ut_size
            << ", remote_endpoint);\n";
      } else {
        out << bl() << field_name << "[i] = unmarshal_" << cflat(wt)->name
            << "(buf, offset + " << field_offset << " + i * " << ut_size
            << ");\n";
      }
      out << eb();
    } else {
      // TODO: Support arrays of other types (e.g., arrays of arrays, etc.)
      throw std::runtime_error("Unsupported array element type for unmarshalling");
    }
    break;
  }
  case FieldType::Vector: {
    // Dynamic vector - data is indirect (ptr + length)
    auto wt = cwt(f->type)->real_type();
    auto [v_size, v_align] = get_type_size_align(f->type);
    auto [ut_size, ut_align] = get_type_size_align(wt);
    const int field_offset = align_offset(v_align, offset, v_size);

    if (is_fundamental(wt)) {
      // Cast to specific typed array type (e.g., Uint8Array for uuid_t)
      out << bl() << field_name
          << " = NPRPC.unmarshal_typed_array(buf, offset + " << field_offset
          << ", " << get_typed_array_name(cft(wt)->token_id) << ") as "
          << get_typed_array_name(cft(wt)->token_id) << ";\n";
    } else if (wt->id == FieldType::String) {
      out << bl() << field_name
          << " = NPRPC.unmarshal_string_array(buf, offset + " << field_offset
          << ");\n";
    } else if (wt->id == FieldType::Struct) {
      out << bl() << field_name
          << " = NPRPC.unmarshal_struct_array(buf, offset + " << field_offset
          << ", unmarshal_" << cflat(wt)->name << ", " << ut_size << ");\n";
     } else {
      // TODO: Support vectors of other types (e.g., vectors of vectors, etc.)
      throw std::runtime_error("Unsupported vector element type for unmarshalling");
    }
    break;
  }
  case FieldType::Optional: {
    // All optionals have the same layout: 4-byte relative offset (0 = no
    // value)
    auto declared_optional_type = copt(f->type)->type;
    auto wt = cwt(f->type)->real_type();
    auto [v_size, v_align] = get_type_size_align(f->type);
    const int field_offset = align_offset(v_align, offset, v_size);

    // Check if the relative offset is non-zero
    out << bl() << "if (buf.dv.getUint32(offset + " << field_offset
        << ", true) !== 0) {\n"
        << bb(false);

    if (is_fundamental(wt)) {
      out << bl() << field_name
          << " = NPRPC.unmarshal_optional_fundamental(buf, offset + "
          << field_offset << ", "
          << fundamental_kind_literal(cft(wt)->token_id) << ");\n";
    } else if (wt->id == FieldType::Struct) {
      auto [wt_size, wt_align] = get_type_size_align(wt);
      out << bl() << field_name
          << " = NPRPC.unmarshal_optional_struct(buf, offset + " << field_offset
          << ", unmarshal_" << cflat(wt)->name << ", " << wt_align << ");\n";
    } else if (wt->id == FieldType::String) {
      // Optional string uses unmarshal_optional_struct with
      // unmarshal_string
      out << bl() << field_name
          << " = NPRPC.unmarshal_optional_struct(buf, offset + " << field_offset
          << ", NPRPC.unmarshal_string, 4);\n";
    } else if (wt->id == FieldType::Vector || wt->id == FieldType::Array) {
      // Optional vector/array
      auto real_elem_type = cwt(wt)->real_type();
      auto [ut_size, ut_align] = get_type_size_align(real_elem_type);
      if (is_fundamental(real_elem_type)) {
        auto typed_array_name =
            get_typed_array_name(cft(real_elem_type)->token_id);
        const bool alias_to_vector = declared_optional_type->id == FieldType::Alias &&
                                     calias(declared_optional_type)->get_real_type()->id == FieldType::Vector;
        const bool keep_typed_array = alias_to_vector &&
                                      cft(real_elem_type)->token_id == TokenId::UInt8;
        if (alias_to_vector && !keep_typed_array) {
          auto temp_var = "temp_" + f->name;
          out << bl() << "const " << temp_var
              << " = NPRPC.unmarshal_optional_struct(buf, offset + "
              << field_offset << ", (b, o) => NPRPC.unmarshal_typed_array(b, o, "
              << typed_array_name << ") as " << typed_array_name << ", 4) as "
              << typed_array_name << ";\n";
          out << bl() << field_name << " = Array.from(" << temp_var << ");\n";
        } else {
          out << bl() << field_name
              << " = NPRPC.unmarshal_optional_struct(buf, offset + "
              << field_offset << ", (b, o) => NPRPC.unmarshal_typed_array(b, o, "
              << typed_array_name << ") as " << typed_array_name << ", 4) as "
              << typed_array_name << ";\n";
        }
      } else if (real_elem_type->id == FieldType::Struct) {
        out << bl() << field_name
            << " = NPRPC.unmarshal_optional_struct(buf, offset + "
            << field_offset
            << ", (b, o) => NPRPC.unmarshal_struct_array(b, o, "
               "unmarshal_"
            << cflat(real_elem_type)->name << ", " << ut_size << "), 4);\n";
      } else {
        assert(false && "Unsupported vector element type in optional "
                        "for unmarshalling");
      }
    } else {
      assert(false && "Unsupported optional element type for unmarshalling");
    }

    out << eb(false) << bl() << "} else {\n"
        << bb(false) << bl() << field_name << " = undefined;\n"
        << eb();
    break;
  }
  case FieldType::Alias: {
    auto real_type = calias(f->type)->get_real_type();
    // Check if this is an alias to a VECTOR (not array) of fundamentals
    // Arrays stay as TypedArrays, but vector aliases use JavaScript arrays
    // for semantic meaning
    if (real_type->id == FieldType::Vector) {
      auto elem_type = cwt(real_type)->real_type();
      if (is_fundamental(elem_type)) {
        // Unmarshal vector aliases into their declared TS representation.
        auto [v_size, v_align] = get_type_size_align(real_type);
        auto [ut_size, ut_align] = get_type_size_align(elem_type);
        const int field_offset = align_offset(v_align, offset, v_size);
        auto typed_array_name = get_typed_array_name(cft(elem_type)->token_id);
        if (cft(elem_type)->token_id == TokenId::UInt8) {
          out << bl() << result_name << "." << f->name
              << " = NPRPC.unmarshal_typed_array(buf, offset + " << field_offset
              << ", " << typed_array_name << ") as " << typed_array_name << ";\n";
        } else {
          auto temp_var = "temp_" + f->name;
          out << bl() << "const " << temp_var
              << " = NPRPC.unmarshal_typed_array(buf, offset + " << field_offset
              << ", " << typed_array_name << ") as " << typed_array_name << ";\n";
          out << bl() << result_name << "." << f->name << " = Array.from("
              << temp_var << ");\n";
        }
        (void)ut_align;
        return;
      }
    }
    // For other aliases (including array aliases), just resolve and
    // unmarshal
    auto temp_field = *f;
    temp_field.type = real_type;
    emit_field_unmarshal(&temp_field, offset, result_name, has_endpoint);
    return;
  }
  default:
    assert(false);
  }
}

void TSBuilder::emit_marshal_function(AstStructDecl* s)
{
  // For exceptions, use the _Data interface that includes __ex_id
  std::string data_type = s->is_exception() ? (s->name + "_Data") : s->name;

  // Check if this is a generated argument struct (has function_argument
  // fields)
  bool is_generated_arg_struct = false;
  if (!s->fields.empty() && s->fields[0]->function_argument) {
    is_generated_arg_struct = true;
  }

  out << bl() << "export function marshal_" << s->name
      << "(buf: NPRPC.FlatBuffer, offset: number, data: " << data_type
      << "): void {\n";
  bb();

  int current_offset = 0;
  for (auto field : s->fields) {
    emit_field_marshal(field, current_offset, "data", is_generated_arg_struct);
  }

  out << eb();
}

void TSBuilder::emit_unmarshal_function(AstStructDecl* s)
{
  // Check if this struct needs remote_endpoint parameter
  // Cases that need it:
  // 1. User-defined structs with direct object fields (convert ObjectId to
  // ObjectProxy)
  // 2. Generated argument structs with nested user-defined structs that have
  // objects
  //    (need to pass remote_endpoint through to nested unmarshal calls)
  bool has_objects = false;
  bool is_generated_arg_struct = false;

  // Check if this is a generated argument struct (has function_argument
  // fields)
  if (!s->fields.empty() && s->fields[0]->function_argument) {
    is_generated_arg_struct = true;
  }

  // Check if we need remote_endpoint
  if (is_generated_arg_struct) {
    // For generated structs, only need remote_endpoint if we have nested
    // user-defined structs with objects
    for (auto f : s->fields) {
      if (f->type->id == FieldType::Struct) {
        auto nested_struct = cflat(f->type);
        // Check if nested struct is user-defined (not generated) and
        // contains objects
        if (nested_struct->fields.empty() ||
            !nested_struct->fields[0]->function_argument) {
          // It's a user-defined struct, check if it contains objects
          if (contains_object(f->type)) {
            has_objects = true;
            break;
          }
        }
      }
    }
  } else {
    // For user-defined structs, need remote_endpoint if we have direct
    // object fields
    for (auto f : s->fields) {
      if (contains_object(f->type)) {
        has_objects = true;
        break;
      }
    }
  }

  // Add remote_endpoint parameter if struct contains objects (and is not a
  // generated arg struct)
  if (has_objects) {
    out << bl() << "export function unmarshal_" << s->name
        << "(buf: NPRPC.FlatBuffer, offset: number, remote_endpoint: "
           "NPRPC.EndPoint): "
        << s->name << " {\n";
  } else {
    out << bl() << "export function unmarshal_" << s->name
        << "(buf: NPRPC.FlatBuffer, offset: number): " << s->name << " {\n";
  }
  bb();

  out << bl() << "const result = {} as " << s->name << ";\n";

  int current_offset = 0;
  // For exceptions, skip field 0 (__ex_id) as it's implicit and not part of
  // the class
  size_t start_index = s->is_exception() ? 1 : 0;
  for (size_t i = start_index; i < s->fields.size(); ++i) {
    emit_field_unmarshal(s->fields[i], current_offset, "result", has_objects);
  }

  out << bl() << "return result;\n" << eb();
}

TSBuilder::TSBuilder(Context* ctx, std::filesystem::path out_dir)
    : Builder(ctx)
    , out_dir_(out_dir)
{
}

} // namespace npidl::builders