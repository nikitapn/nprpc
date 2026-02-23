// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "swift_builder.hpp"
#include "utils.hpp"
#include <cassert>
#include <iostream>
#include <fstream>
#include <set>

namespace npidl::builders {

namespace {
bool is_integral(TokenId id)
{
  return id == TokenId::Int8 || id == TokenId::UInt8 ||
         id == TokenId::Int16 || id == TokenId::UInt16 ||
         id == TokenId::Int32 || id == TokenId::UInt32 ||
         id == TokenId::Int64 || id == TokenId::UInt64;
}

bool is_floating_point(TokenId id)
{
  return id == TokenId::Float32 || id == TokenId::Float64;
}

// Check if type needs marshalling (complex type)
bool needs_marshalling(AstTypeDecl* type) {
  return type->id == FieldType::Struct || 
         type->id == FieldType::String ||
         type->id == FieldType::Vector ||
         type->id == FieldType::Array;
}
} // anonymous namespace

static std::string make_unique_variable_name(const std::string& base) {
  static int counter = 0;
  return base + std::to_string(counter++);
}

SwiftBuilder::SwiftBuilder(Context* ctx, std::filesystem::path out_dir)
    : Builder(ctx)
    , out_dir_(std::move(out_dir))
{
}

std::ostream& operator<<(std::ostream& os, const SwiftBuilder::_ns& ns)
{
  int level = Namespace::substract(ns.builder.ctx_->nm_cur(), ns.nm);
  const auto path = ns.nm->construct_path(".", level);
  if (path.size() == 0 || (path.size() == 1 && path[0] == '.')) {
    return os;
  }

  return os << path << '.';
}

SwiftBuilder::_ns SwiftBuilder::ns(Namespace* nm) const
{
  return {*this, nm};
}

std::string SwiftBuilder::swift_type_name(const std::string& name) const
{
  // Convert C++ naming to Swift (PascalCase)
  // Already assumes PascalCase from IDL
  return name;
}

std::string SwiftBuilder::swift_method_name(const std::string& name) const
{
  // Convert to camelCase for Swift methods
  if (name.empty())
    return name;
  std::string result = name;
  result[0] = std::tolower(result[0]);
  return result;
}

void SwiftBuilder::emit_fundamental_type(TokenId id, std::ostream& os)
{
  switch (id) {
  case TokenId::Boolean:
    os << "Bool";
    break;
  case TokenId::Int8:
    os << "Int8";
    break;
  case TokenId::UInt8:
    os << "UInt8";
    break;
  case TokenId::Int16:
    os << "Int16";
    break;
  case TokenId::UInt16:
    os << "UInt16";
    break;
  case TokenId::Int32:
    os << "Int32";
    break;
  case TokenId::UInt32:
    os << "UInt32";
    break;
  case TokenId::Int64:
    os << "Int64";
    break;
  case TokenId::UInt64:
    os << "UInt64";
    break;
  case TokenId::Float32:
    os << "Float";
    break;
  case TokenId::Float64:
    os << "Double";
    break;
  default:
    assert(false && "Unknown fundamental type");
  }
}

void SwiftBuilder::emit_type(AstTypeDecl* type, std::ostream& os)
{
  switch (type->id) {
  case FieldType::Fundamental:
    emit_fundamental_type(cft(type)->token_id, os);
    break;
  case FieldType::Struct:
    os << ns(cflat(type)->nm) << swift_type_name(cflat(type)->name);
    break;
  case FieldType::Vector:
  case FieldType::Array:
    os << "[";
    emit_type(cwt(type)->type, os);
    os << "]";
    break;
  case FieldType::String:
    os << "String";
    break;
  case FieldType::Void:
    // Swift uses Void or ()
    os << "Void";
    break;
  case FieldType::Object:
    os << "NPRPCObject";
    break;
  case FieldType::Alias:
    // In Swift, don't prefix with namespace if it's in the same module
    os << swift_type_name(calias(type)->name);
    break;
  case FieldType::Enum:
    // In Swift, don't prefix with namespace if it's in the same module
    os << swift_type_name(cenum(type)->name);
    break;
  case FieldType::Optional:
    emit_type(cwt(type)->type, os);
    os << "?";
    break;
  case FieldType::Stream:
    // For streams, wrap the inner type in AsyncThrowingStream
    os << "AsyncThrowingStream<";
    emit_type(cwt(type)->type, os);
    os << ", Error>";
    break;
  default:
    assert(false && "Unknown type");
  }
}

void SwiftBuilder::emit_constant(const std::string& name, AstNumber* number)
{
  out << bl() << "public let " << name << ": ";

  if (number->is_decimal()) {
    auto val = number->decimal();
    if (val < 0) {
      out << "Int = " << val;
    } else {
      out << "UInt = " << val;
    }
  } else {
    out << "Double = " << *number;
  }

  out << "\n\n";
}

void SwiftBuilder::emit_field(AstFieldDecl* f, std::ostream& os)
{
  os << bl() << "public var " << f->name << ": ";
  emit_type(f->type, os);
  os << "\n";
}

void SwiftBuilder::emit_struct2(AstStructDecl* s, Target target)
{
  const std::string type_keyword = target == Target::FunctionArgument ? "fileprivate struct" : "public struct";

  out << bl() << type_keyword << " " << swift_type_name(s->name);

  if (target == Target::Exception) {
    out << ": NPRPCError";
  } else {
    out << ": Codable, Sendable";
  }

  out << " " << bb();

  // Emit fields with default values to allow var result: StructName declarations
  for (auto field : s->fields) {
    if (target == Target::Exception && field->name == "__ex_id") {
      // Skip __ex_id for exceptions, it's only used for marshalling and should not be part of the public API
      continue;
    }
    out << bl() << "public var " << field->name << ": ";
    emit_type(field->type, out);

    // Add default value based on type to allow partial initialization in unmarshal
    auto actual_type = field->type;
    if (actual_type->id == FieldType::Alias) {
      actual_type = calias(actual_type)->get_real_type();
    }

    switch (actual_type->id) {
    case FieldType::Fundamental: {
      auto token = cft(actual_type)->token_id;
      out << " = ";
      if (token == TokenId::Boolean) {
        out << "false";
      } else if (is_floating_point(token)) {
        out << "0.0";
      } else {
        out << "0";  // All integer types
      }
      break;
    }
    case FieldType::String:
      out << " = \"\"";
      break;
    case FieldType::Vector:
    case FieldType::Array:
      out << " = []";
      break;
    case FieldType::Object:
      out << " = NPRPCObject()";
      break;
    case FieldType::Enum:
      // Use first enum value as default
      out << " = ." << swift_method_name(cenum(actual_type)->items.begin()->first);
      break;
    case FieldType::Optional:
      out << " = nil";
      break;
    case FieldType::Struct:
      // Initialize nested struct with its default initializer
      out << " = " << swift_type_name(cflat(actual_type)->name) << "()";
      break;
    default:
      // Other complex types don't get defaults
      break;
    }
    out << "\n";
  }

  // Generate memberwise initializer
  if (!s->fields.empty()) {
    // First, generate no-argument init (for unmarshal)
    out << "\n" << bl() << "public init() {}\n";

    if (s->is_exception() && s->fields.size() == 1) {
      // For exceptions with only __ex_id, we can skip generating the memberwise initializer
      // since it doesn't make sense to initialize an exception with just an ID
    } else {
      // For other structs and exceptions with more fields, generate the memberwise initializer
      // Then, generate memberwise initializer
      out << "\n" << bl() << "public init(";
      bool first = true;
      for (auto field : s->fields) {
        if (target == Target::Exception && field->name == "__ex_id")
          continue;
        if (!first) out << ", ";
        first = false;
        out << field->name << ": ";
        emit_type(field->type, out);
      }
      out << ") " << bb();
      for (auto field : s->fields) {
        if (target == Target::Exception && field->name == "__ex_id")
          continue;
        out << bl() << "self." << field->name << " = " << field->name << "\n";
      }
      out << eb();
    }
  }

  // For exceptions, add the required message property only if not already defined
  if (target == Target::Exception) {
    bool has_message_field = false;
    for (auto field : s->fields) {
      if (field->name == "message") {
        has_message_field = true;
        break;
      }
    }
    if (!has_message_field) {
      out << "\n" << bl() << "public var message: String { \"" << s->name << "\" }\n";
    }
  }

  out << eb() << "\n";
}

void SwiftBuilder::emit_struct(AstStructDecl* s)
{
  emit_struct2(s, Target::Regular);
  emit_marshal_function(s);
  emit_unmarshal_function(s);
}

void SwiftBuilder::emit_exception(AstStructDecl* s)
{
  emit_struct2(s, Target::Exception);
  emit_marshal_function(s);
  emit_unmarshal_function(s);
}

void SwiftBuilder::emit_enum(AstEnumDecl* e)
{
  out << bl() << "public enum " << swift_type_name(e->name) << ": Int32, Codable, Sendable " << bb();

  // Emit cases
  for (auto& item : e->items) {
    out << bl() << "case " << swift_method_name(item.first);
    if (item.second.second) { // has explicit value
      out << " = " << item.second.first;
    }
    out << "\n";
  }

  out << eb() << "\n";
}

void SwiftBuilder::emit_using(AstAliasDecl* u)
{
  out << bl() << "public typealias " << swift_type_name(u->name) << " = ";
  emit_type(u->type, out);
  out << "\n";
}

void SwiftBuilder::emit_protocol(AstInterfaceDecl* ifs)
{
  // Generate Swift protocol for the interface
  out << bl() << "public protocol " << swift_type_name(ifs->name) << "Protocol"
      << " " << bb();

  // Emit method signatures
  for (auto& fn : ifs->fns) {
    out << bl() << "func " << swift_method_name(fn->name) << "(";

    // Only emit 'in' parameters
    bool first = true;
    for (auto& arg : fn->args) {
      if (arg->modifier == ArgumentModifier::In) {
        if (!first)
          out << ", ";
        first = false;
        out << arg->name << ": ";
        emit_type(arg->type, out);
      }
    }

    out << ")";

    // For streaming methods, servant returns AsyncStream<T> (producer)
    if (fn->is_stream) {
      out << " -> AsyncStream<";
      emit_type(static_cast<AstStreamDecl*>(fn->ret_value)->type, out);
      out << ">";
      out << "\n";
      continue;
    }

    // Return type: collect out parameters and return value
    std::vector<AstFunctionArgument*> out_params;
    for (auto& arg : fn->args) {
      if (arg->modifier == ArgumentModifier::Out) {
        out_params.push_back(arg);
      }
    }

    bool has_return = !fn->is_void();
    // Note: IDL async methods are not marked async in protocol - async is a client-side concern
    // Only add throws if the function has raises(...)
    const char* throws_kw = fn->is_throwing() ? " throws" : "";
    if (has_return || !out_params.empty()) {
      out << throws_kw << " -> ";

      // If multiple return values, use tuple
      if ((has_return ? 1 : 0) + out_params.size() > 1) {
        out << "(";
        bool first_ret = true;
        if (has_return) {
          emit_type(fn->ret_value, out);
          first_ret = false;
        }
        for (auto& out_param : out_params) {
          if (!first_ret) out << ", ";
          first_ret = false;
          emit_type(out_param->type, out);
        }
        out << ")";
      } else {
        // Single return value
        if (has_return) {
          emit_type(fn->ret_value, out);
        } else {
          emit_type(out_params[0]->type, out);
        }
      }
    } else {
      out << throws_kw;
    }

    out << "\n";
  }

  out << eb() << "\n";
}

void SwiftBuilder::emit_client_proxy(AstInterfaceDecl* ifs)
{
  const std::string class_name = swift_type_name(ifs->name);
  const std::string class_id = std::string(ctx_->current_file()) + '/' + ctx_->nm_cur()->full_idl_namespace() + '.' + ifs->name;

  // Check if client proxy can conform to protocol
  // A method causes non-conformance if:
  // - It's async (protocol is sync, proxy is async)
  // - It's streaming (protocol returns AsyncStream, proxy returns AsyncThrowingStream)
  // - It's non-async without raises() (protocol has no throws, proxy has throws)
  bool can_conform = true;
  for (auto& fn : ifs->fns) {
    if (fn->is_async || fn->is_stream || !fn->is_throwing()) {
      can_conform = false;
      break;
    }
  }

  out << bl() << "// Client proxy for " << class_name << "\n";
  out << bl() << "// Pure Swift implementation with direct marshalling\n";
  if (can_conform) {
    out << bl() << "final public class " << class_name << ": NPRPCObject, " << class_name << "Protocol, @unchecked Sendable " << bb();
  } else {
    out << bl() << "final public class " << class_name << ": NPRPCObject, @unchecked Sendable " << bb();
  }

  // Static getClass to return the class ID
  out << bl() << "public override class var classId: String {\n" << bb(false);
  out << bl() << "\"" << class_id << "\"\n";
  out << eb() << "\n";

  // Constructor (required, so no override keyword)
  out << bl() << "public required init(handle: UnsafeMutableRawPointer) " << bb();
  out << bl() << "super.init(handle: handle)\n";
  out << eb() << "\n";

  // Required Codable initializer (inherited from NPRPCObject)
  out << bl() << "public required init(from decoder: Decoder) throws " << bb();
  out << bl() << "try super.init(from: decoder)\n";
  out << eb() << "\n";

  // Implement protocol methods with marshalling
  for (auto& fn : ifs->fns) {
    // Handle streaming methods separately
    if (fn->is_stream) {
      emit_client_stream_method(ifs, fn);
      continue;
    }

    out << bl() << "public func " << swift_method_name(fn->name) << "(";

    // Emit only 'in' parameters (to match protocol)
    bool first = true;
    for (auto arg : fn->args) {
      if (arg->modifier == ArgumentModifier::In) {
        if (!first) out << ", ";
        first = false;
        out << arg->name << ": ";
        emit_type(arg->type, out);
      }
    }

    // Client proxy method signatures:
    // - Async with outputs or raises(): `func foo() async throws` (needs response, can fail)
    // - Async without outputs/raises(): `func foo() async` (fire-and-forget, errors ignored)
    // - Non-async: `func foo() throws` (always throws - communication can fail)
    bool async_has_outputs = fn->out_s != nullptr;
    if (fn->is_async && (fn->is_throwing() || async_has_outputs)) {
      out << ") async throws";
    } else if (fn->is_async) {
      out << ") async";
    } else {
      // Non-async methods always throw (communication errors)
      out << ") throws";
    }

    // Return type - match protocol (out params become return values)
    std::vector<AstFunctionArgument*> out_params;
    for (auto arg : fn->args) {
      if (arg->modifier == ArgumentModifier::Out) {
        out_params.push_back(arg);
      }
    }

    bool has_return = !fn->is_void();
    if (has_return || !out_params.empty()) {
      out << " -> ";

      if ((has_return ? 1 : 0) + out_params.size() > 1) {
        out << "(";
        bool first_ret = true;
        if (has_return) {
          emit_type(fn->ret_value, out);
          first_ret = false;
        }
        for (auto& out_param : out_params) {
          if (!first_ret) out << ", ";
          first_ret = false;
          emit_type(out_param->type, out);
        }
        out << ")";
      } else {
        if (has_return) {
          emit_type(fn->ret_value, out);
        } else {
          emit_type(out_params[0]->type, out);
        }
      }
    }

    out << " " << bb();

    // For async methods without throws and without outputs, we use return instead of throw for errors
    // Async with outputs can throw because sendAsyncReceive throws
    const bool can_throw = fn->is_throwing() || !fn->is_async || async_has_outputs;
    const char* error_stmt = can_throw ? "throw" : "return";

    // Calculate buffer size
    const auto fixed_size = get_arguments_offset() + (fn->in_s ? fn->in_s->size : 0);
    const auto capacity = fixed_size + (fn->in_s ? (fn->in_s->flat ? 0 : 128) : 0);

    out << bl() << "// Prepare buffer\n";
    out << bl() << "let buffer = FlatBuffer()\n";
    out << bl() << "buffer.prepare(" << capacity << ")\n";
    out << bl() << "buffer.commit(" << fixed_size << ")\n";
    out << bl() << "guard let data = buffer.data else { " << error_stmt << " "; 
    if (can_throw) {
      out << "BufferError(message: \"Failed to get buffer data\")";
    }
    out << " }\n\n";

    // Write message header
    out << bl() << "// Write message header\n";
    out << bl() << "data.storeBytes(of: UInt32(0), toByteOffset: 0, as: UInt32.self)  // size (set later)\n";
    out << bl() << "data.storeBytes(of: UInt32(0), toByteOffset: 4, as: UInt32.self)  // msg_id: FunctionCall (MessageId enum value 0)\n";
    out << bl() << "data.storeBytes(of: UInt32(0), toByteOffset: 8, as: UInt32.self)  // msg_type: Request\n";
    out << bl() << "data.storeBytes(of: UInt32(0), toByteOffset: 12, as: UInt32.self) // reserved\n\n";

    // Write call header
    out << bl() << "// Write call header\n";
    out << bl() << "data.storeBytes(of: poaIdx, toByteOffset: " << size_of_header << ", as: UInt16.self)\n";
    out << bl() << "data.storeBytes(of: UInt8(0), toByteOffset: " << (size_of_header + 2) << ", as: UInt8.self)  // interface_idx\n";
    out << bl() << "data.storeBytes(of: UInt8(" << fn->idx << "), toByteOffset: " << (size_of_header + 3) << ", as: UInt8.self)  // function_idx\n";
    out << bl() << "data.storeBytes(of: objectId, toByteOffset: " << (size_of_header + 8) << ", as: UInt64.self)\n\n";

    // Marshal input arguments
    if (fn->in_s) {
      out << bl() << "// Marshal input arguments\n";

      // Always create struct instance
      out << bl() << "var inArgs = " << ns(fn->in_s->nm) << fn->in_s->name << "()\n";

      int ix = 0;
      for (auto arg : fn->args) {
        if (arg->modifier == ArgumentModifier::Out) continue;
        ++ix;
        out << bl() << "inArgs._" << ix << " = " << arg->name << "\n";
      }

      out << bl() << ns(fn->in_s->nm) << "marshal_" << fn->in_s->name << "(buffer: buffer, offset: " << get_arguments_offset() << ", data: inArgs)\n\n";
    }

    // Set message size (must be done after marshalling as optionals/vectors may extend buffer)
    out << bl() << "guard let finalData = buffer.data else { " << error_stmt << " ";
    if (can_throw) {
      out << "BufferError(message: \"Failed to get buffer data\")";
    }
    out << " }\n";
    out << bl() << "finalData.storeBytes(of: UInt32(buffer.size - 4), toByteOffset: 0, as: UInt32.self)\n\n";

    // Send request
    if (fn->is_async) {
      // Check if async method has output parameters (requires waiting for response)
      bool has_outputs = fn->out_s != nullptr;
      
      if (has_outputs) {
        // Async method with output parameters - use sendAsyncReceive
        out << bl() << "// Send async and receive response\n";
        out << bl() << "let responseBuffer = try await sendAsyncReceive(buffer: buffer, timeout: timeout)\n\n";
        
        // Handle reply
        out << bl() << "// Handle reply\n";
        out << bl() << "let stdReply = try handleStandardReply(buffer: responseBuffer)\n";
        if (fn->ex) {
          out << bl() << "if stdReply == 1 { throw " << ctx_->current_file() << "_throwException(buffer: responseBuffer) }\n";
        }
        out << bl() << "if stdReply != -1 { throw UnexpectedReplyError(message: \"Unexpected reply\") }\n\n";

        // Unmarshal output arguments
        out << bl() << "guard let responseData = responseBuffer.data else { throw BufferError(message: \"Failed to get response data\") }\n";

        bool out_needs_endpoint = false;
        for (auto f : fn->out_s->fields) {
          if (contains_object(f->type)) {
            out_needs_endpoint = true;
            break;
          }
        }

        if (out_needs_endpoint) {
          out << bl() << "let out = try " << ns(fn->out_s->nm) << "unmarshal_" << fn->out_s->name << "(buffer: responseData, offset: " << size_of_header << ", endpoint: endpoint)\n";
        } else {
          out << bl() << "let out = " << ns(fn->out_s->nm) << "unmarshal_" << fn->out_s->name << "(buffer: responseData, offset: " << size_of_header << ")\n";
        }

        // Return output values
        if ((has_return ? 1 : 0) + out_params.size() > 1) {
          // Multiple return values - use tuple
          out << bl() << "return (";
          bool first_ret = true;
          int ix = 0;
          if (has_return) {
            out << "out._1";
            first_ret = false;
            ix = 1;
          }
          for (auto out_arg : fn->args) {
            if (out_arg->modifier == ArgumentModifier::In) continue;
            ++ix;
            if (!first_ret) out << ", ";
            first_ret = false;
            out << "out._" << ix;
          }
          out << ")\n";
        } else if (has_return) {
          out << bl() << "return out._1\n";
        } else {
          // Single out parameter
          out << bl() << "return out._1\n";
        }
      } else {
        // Async method without outputs - fire-and-forget
        out << bl() << "// Send async (no reply expected)\n";
        if (fn->is_throwing()) {
          out << bl() << "try await sendAsync(buffer: buffer, timeout: timeout)\n";
        } else {
          // Fire-and-forget: wrap in do/catch since sendAsync can throw
          out << bl() << "do {\n";
          out << bl() << "  try await sendAsync(buffer: buffer, timeout: timeout)\n";
          out << bl() << "} catch {\n";
          out << bl() << "  // Fire-and-forget: ignore communication errors\n";
          out << bl() << "}\n";
        }
      }
    } else {
      // Synchronous method - send and wait for reply
      out << bl() << "// Send and receive\n";
      out << bl() << "try sendReceive(buffer: buffer, timeout: timeout)\n\n";

      // Handle reply
      out << bl() << "// Handle reply\n";
      out << bl() << "let stdReply = try handleStandardReply(buffer: buffer)\n";
      if (fn->ex) {
        out << bl() << "if stdReply == 1 { throw " << ctx_->current_file() << "_throwException(buffer: buffer) }\n";
      }

      if (!fn->out_s) {
        out << bl() << "if stdReply != 0 { throw UnexpectedReplyError(message: \"Unexpected reply\") }\n";
      } else {
        out << bl() << "if stdReply != -1 { throw UnexpectedReplyError(message: \"Unexpected reply\") }\n\n";

        // Unmarshal output arguments
        out << bl() << "guard let responseData = buffer.data else { throw BufferError(message: \"Failed to get response data\") }\n";

        bool out_needs_endpoint = false;
        for (auto f : fn->out_s->fields) {
          if (contains_object(f->type)) {
            out_needs_endpoint = true;
            break;
          }
        }

        if (out_needs_endpoint) {
          out << bl() << "let out = try " << ns(fn->out_s->nm) << "unmarshal_" << fn->out_s->name << "(buffer: responseData, offset: " << size_of_header << ", endpoint: endpoint)\n";
        } else {
          out << bl() << "let out = " << ns(fn->out_s->nm) << "unmarshal_" << fn->out_s->name << "(buffer: responseData, offset: " << size_of_header << ")\n";
        }

        // Return output values
        if ((has_return ? 1 : 0) + out_params.size() > 1) {
          // Multiple return values - use tuple
          out << bl() << "return (";
          bool first_ret = true;
          int ix = 0;
          if (has_return) {
            out << "out._1";
            first_ret = false;
            ix = 1;
          }
          for (auto out_arg : fn->args) {
            if (out_arg->modifier == ArgumentModifier::In) continue;
            ++ix;
            if (!first_ret) out << ", ";
            first_ret = false;
            out << "out._" << ix;
          }
          out << ")\n";
        } else if (has_return) {
          out << bl() << "return out._1\n";
        } else {
          // Single out parameter
          out << bl() << "return out._1\n";
        }
      }
    } // end if (fn->is_async) else

    out << eb() << "\n";
  }

  out << eb() << "\n";
}

void SwiftBuilder::emit_client_stream_method(AstInterfaceDecl* ifs, AstFunctionDecl* fn)
{
  auto stream_type = static_cast<AstStreamDecl*>(fn->ret_value)->type;
  
  out << bl() << "public func " << swift_method_name(fn->name) << "(";

  // Emit only 'in' parameters
  bool first = true;
  for (auto arg : fn->args) {
    if (arg->modifier == ArgumentModifier::In) {
      if (!first) out << ", ";
      first = false;
      out << arg->name << ": ";
      emit_type(arg->type, out);
    }
  }

  // Streaming methods return AsyncThrowingStream on client side
  out << ") throws -> AsyncThrowingStream<";
  emit_type(stream_type, out);
  out << ", Error> " << bb();

  // Generate unique stream ID
  out << bl() << "let streamId = nprpc_generate_stream_id()\n\n";

  // Calculate buffer sizes for StreamInit
  const auto args_offset = get_stream_init_arguments_offset();
  const auto fixed_size = args_offset + (fn->in_s ? fn->in_s->size : 0);
  const auto capacity = fixed_size + (fn->in_s ? (fn->in_s->flat ? 0 : 128) : 0);

  out << bl() << "// Prepare StreamInit buffer\n";
  out << bl() << "let buffer = FlatBuffer()\n";
  out << bl() << "buffer.prepare(" << capacity << ")\n";
  out << bl() << "buffer.commit(" << fixed_size << ")\n";
  out << bl() << "guard let data = buffer.data else { throw BufferError(message: \"Failed to get buffer data\") }\n\n";

  // Write message header with StreamInitialization message ID
  out << bl() << "// Write StreamInit message header\n";
  out << bl() << "data.storeBytes(of: UInt32(0), toByteOffset: 0, as: UInt32.self)  // size (set later)\n";
  out << bl() << "data.storeBytes(of: impl.MessageId.streamInitialization.rawValue, toByteOffset: 4, as: Int32.self)\n";
  out << bl() << "data.storeBytes(of: impl.MessageType.request.rawValue, toByteOffset: 8, as: Int32.self)\n";
  out << bl() << "data.storeBytes(of: UInt32(0), toByteOffset: 12, as: UInt32.self)  // reserved\n\n";

  // Write StreamInit fields (matching C++ struct layout with alignment)
  out << bl() << "// Write StreamInit fields\n";
  out << bl() << "data.storeBytes(of: streamId, toByteOffset: " << size_of_header << ", as: UInt64.self)  // offset 0\n";
  out << bl() << "data.storeBytes(of: poaIdx, toByteOffset: " << (size_of_header + 8) << ", as: UInt16.self)  // offset 8\n";
  out << bl() << "data.storeBytes(of: UInt8(0), toByteOffset: " << (size_of_header + 10) << ", as: UInt8.self)  // interface_idx at offset 10\n";
  out << bl() << "data.storeBytes(of: objectId, toByteOffset: " << (size_of_header + 16) << ", as: UInt64.self)  // offset 16 (after 5-byte pad)\n";
  out << bl() << "data.storeBytes(of: UInt8(" << fn->idx << "), toByteOffset: " << (size_of_header + 24) << ", as: UInt8.self)  // func_idx at offset 24\n\n";

  // Marshal input arguments if any
  if (fn->in_s) {
    out << bl() << "// Marshal input arguments\n";
    out << bl() << "var inArgs = " << ns(fn->in_s->nm) << fn->in_s->name << "()\n";

    int ix = 0;
    for (auto arg : fn->args) {
      if (arg->modifier == ArgumentModifier::Out) continue;
      ++ix;
      out << bl() << "inArgs._" << ix << " = " << arg->name << "\n";
    }

    out << bl() << ns(fn->in_s->nm) << "marshal_" << fn->in_s->name << "(buffer: buffer, offset: " << args_offset << ", data: inArgs)\n\n";
  }

  // Set message size
  out << bl() << "guard let finalData = buffer.data else { throw BufferError(message: \"Failed to get buffer data\") }\n";
  out << bl() << "finalData.storeBytes(of: UInt32(buffer.size - 4), toByteOffset: 0, as: UInt32.self)\n\n";

  // Create AsyncThrowingStream with continuation
  out << bl() << "// Create AsyncThrowingStream for receiving chunks\n";
  out << bl() << "return AsyncThrowingStream { continuation in\n" << bb(false);
  out << bl() << "  // Box the continuation for C callback\n";
  out << bl() << "  final class ContinuationBox {\n";
  out << bl() << "    var continuation: AsyncThrowingStream<";
  emit_type(stream_type, out);
  out << ", Error>.Continuation\n";
  out << bl() << "    init(_ c: AsyncThrowingStream<";
  emit_type(stream_type, out);
  out << ", Error>.Continuation) { continuation = c }\n";
  out << bl() << "  }\n";
  out << bl() << "  let box = Unmanaged.passRetained(ContinuationBox(continuation)).toOpaque()\n\n";

  // Define C callbacks
  out << bl() << "  let onChunk: @convention(c) (UnsafeMutableRawPointer?, UnsafeRawPointer?, UInt32) -> Void = { ctx, dataPtr, size in\n";
  out << bl() << "    guard let ctx = ctx, let dataPtr = dataPtr, size > 0 else { return }\n";
  out << bl() << "    let box = Unmanaged<ContinuationBox>.fromOpaque(ctx).takeUnretainedValue()\n";
  // For now, assume fundamental types - later we'd need type-specific deserialization
  out << bl() << "    let value = dataPtr.load(as: ";
  emit_type(stream_type, out);
  out << ".self)\n";
  out << bl() << "    box.continuation.yield(value)\n";
  out << bl() << "  }\n\n";

  out << bl() << "  let onComplete: @convention(c) (UnsafeMutableRawPointer?) -> Void = { ctx in\n";
  out << bl() << "    guard let ctx = ctx else { return }\n";
  out << bl() << "    let box = Unmanaged<ContinuationBox>.fromOpaque(ctx).takeRetainedValue()\n";
  out << bl() << "    box.continuation.finish()\n";
  out << bl() << "  }\n\n";

  out << bl() << "  let onError: @convention(c) (UnsafeMutableRawPointer?, UInt32) -> Void = { ctx, errorCode in\n";
  out << bl() << "    guard let ctx = ctx else { return }\n";
  out << bl() << "    let box = Unmanaged<ContinuationBox>.fromOpaque(ctx).takeRetainedValue()\n";
  out << bl() << "    box.continuation.finish(throwing: RuntimeError(message: \"Stream error: \\(errorCode)\"))\n";
  out << bl() << "  }\n\n";

  // Register stream reader with C++ runtime
  out << bl() << "  nprpc_stream_register_reader(self.handle, streamId, box, onChunk, onComplete, onError)\n\n";

  // Send StreamInit message
  out << bl() << "  // Send StreamInit message\n";
  out << bl() << "  let result = nprpc_stream_send_init(self.handle, buffer.handle, self.timeout)\n";
  out << bl() << "  if result != 0 {\n";
  out << bl() << "    let box = Unmanaged<ContinuationBox>.fromOpaque(box).takeRetainedValue()\n";
  out << bl() << "    box.continuation.finish(throwing: RuntimeError(message: \"Failed to send StreamInit\"))\n";
  out << bl() << "  }\n";
  out << eb() << "\n";

  out << eb() << "\n";
}

void SwiftBuilder::emit_servant_base(AstInterfaceDecl* ifs)
{
  const std::string class_name = swift_type_name(ifs->name);
  const std::string servant_name = class_name + "Servant";

  out << bl() << "// Servant base for " << class_name << "\n";
  out << bl() << "open class " << servant_name << ": NPRPCServant, " << class_name << "Protocol " << bb();

  // Constructor
  out << bl() << "public override init() { super.init() }\n\n";

  // getClass() override - return the fully qualified interface class name
  const std::string class_id = std::string(ctx_->current_file()) + '/' + ctx_->nm_cur()->full_idl_namespace() + '.' + ifs->name;
  out << bl() << "public override func getClass() -> String " << bb();
  out << bl() << "return \"" << class_id << "\"\n";
  out << eb() << "\n";

  // Protocol method stubs (abstract methods to be implemented by subclass)
  for (auto& fn : ifs->fns) {
    out << bl() << "open func " << swift_method_name(fn->name) << "(";

    // Emit only 'in' parameters (to match protocol)
    bool first = true;
    for (auto arg : fn->args) {
      if (arg->modifier == ArgumentModifier::In) {
        if (!first) out << ", ";
        first = false;
        out << arg->name << ": ";
        emit_type(arg->type, out);
      }
    }

    // For streaming methods, return AsyncStream<T>
    if (fn->is_stream) {
      out << ") -> AsyncStream<";
      emit_type(static_cast<AstStreamDecl*>(fn->ret_value)->type, out);
      out << "> " << bb();
      out << bl() << "fatalError(\"Subclass must implement " << swift_method_name(fn->name) << "\")\n";
      out << eb() << "\n";
      continue;
    }

    // Servant methods only have throws if they have raises(...) in IDL
    out << (fn->is_throwing() ? ") throws" : ")");

    // Return type - match protocol
    std::vector<AstFunctionArgument*> out_params;
    for (auto arg : fn->args) {
      if (arg->modifier == ArgumentModifier::Out) {
        out_params.push_back(arg);
      }
    }

    bool has_return = !fn->is_void();
    if (has_return || !out_params.empty()) {
      out << " -> ";

      if ((has_return ? 1 : 0) + out_params.size() > 1) {
        out << "(";
        bool first_ret = true;
        if (has_return) {
          emit_type(fn->ret_value, out);
          first_ret = false;
        }
        for (auto& out_param : out_params) {
          if (!first_ret) out << ", ";
          first_ret = false;
          emit_type(out_param->type, out);
        }
        out << ")";
      } else {
        if (has_return) {
          emit_type(fn->ret_value, out);
        } else {
          emit_type(out_params[0]->type, out);
        }
      }
    }

    out << " " << bb();
    out << bl() << "fatalError(\"Subclass must implement " << swift_method_name(fn->name) << "\")\n";
    out << eb() << "\n";
  }

  // Dispatch method
  out << bl() << "// Dispatch incoming RPC calls\n";
  out << bl() << "public override func dispatch(buffer: FlatBuffer, remoteEndpoint: NPRPCEndpoint) " << bb();

  out << bl() << "guard let data = buffer.data else { return }\n\n";

  // Check if this interface has any streaming methods
  bool has_streaming = false;
  for (auto& fn : ifs->fns) {
    if (fn->is_stream) {
      has_streaming = true;
      break;
    }
  }

  if (has_streaming) {
    // Need to check message type first
    out << bl() << "// Check message type to route streaming vs regular calls\n";
    out << bl() << "let msgId = data.load(fromByteOffset: MemoryLayout<NPRPC.impl.Header>.offset(of: \\NPRPC.impl.Header.msg_id)!, as: Int32.self)\n\n";
    out << bl() << "if msgId == impl.MessageId.streamInitialization.rawValue " << bb();

    // Streaming dispatch path
    out << bl() << "let streamFuncIdx = data.load(fromByteOffset: (" <<
      size_of_header << " + MemoryLayout<NPRPC.impl.StreamInit>.offset(of: \\NPRPC.impl.StreamInit.func_idx)!), as: UInt8.self)\n";
    out << bl() << "switch streamFuncIdx " << bb();

    for (auto& fn : ifs->fns) {
      if (!fn->is_stream) continue;
      out << bl() << "case " << fn->idx << ": // " << fn->name << "\n" << bb(false);
      emit_servant_stream_dispatch(fn);
      out << eb(false);
    }

    out << bl() << "default:\n";
    out << bl() << "  makeSimpleAnswer(buffer: buffer, messageId: impl.MessageId.error_UnknownFunctionIdx)\n";
    out << eb(false) << bl() << "} // switch streamFuncIdx\n";
    out << bl() << "return\n";
    out << eb() << "\n";
  }

  // Regular function call dispatch
  out << bl() << "// Read function index from CallHeader\n";
  out << bl() << "let functionIdx = data.load(fromByteOffset: (" <<
    size_of_header << " + MemoryLayout<NPRPC.impl.CallHeader>.offset(of: \\NPRPC.impl.CallHeader.function_idx)!), as: UInt8.self)\n\n";

  // Switch on function index
  out << bl() << "switch functionIdx " << bb();

  for (auto& fn : ifs->fns) {
    // Skip streaming methods - they're handled above in StreamInit path
    if (fn->is_stream) {
      continue;
    }

    out << bl() << "case " << fn->idx << ": // " << fn->name << "\n" << bb(false);

    // Safety check for untrusted interfaces before unmarshalling
    if (!ifs->trusted && fn->in_s) {
      out << bl() << "// Validate input buffer for untrusted interface\n";
      out << bl() << "guard " << ns(fn->in_s->nm) << "check_" << fn->in_s->get_function_struct_id() << "(buffer: data, bufferSize: buffer.size, offset: " << get_arguments_offset() << ") else " << bb();
      out << bl() << "makeSimpleAnswer(buffer: buffer, messageId: impl.MessageId.error_BadInput)\n";
      out << bl() << "return\n";
      out << eb() << "\n";
    }

    // Unmarshal input arguments
    if (fn->in_s) {
      out << bl() << "// Unmarshal input arguments\n";
      bool in_needs_endpoint = false;
      for (auto f : fn->in_s->fields) {
        if (contains_object(f->type)) {
          in_needs_endpoint = true;
          break;
        }
      }

      if (in_needs_endpoint) {
        // Unmarshalling objects can throw - wrap in do-catch
        out << bl() << "let ia: " << ns(fn->in_s->nm) << fn->in_s->name << "\n";
        out << bl() << "do {\n";
        out << bl() << "  ia = try " << ns(fn->in_s->nm) << "unmarshal_" << fn->in_s->name << "(buffer: data, offset: " << get_arguments_offset() << ", endpoint: remoteEndpoint)\n";
        out << bl() << "} catch {\n";
        out << bl() << "  makeSimpleAnswer(buffer: buffer, messageId: impl.MessageId.error_BadInput)\n";
        out << bl() << "  return\n";
        out << bl() << "}\n";
      } else {
        out << bl() << "let ia = " << ns(fn->in_s->nm) << "unmarshal_" << fn->in_s->name << "(buffer: data, offset: " << get_arguments_offset() << ")\n";
      }
    }

    // Prepare output buffer if needed
    if (fn->out_s && !fn->out_s->flat) {
      const auto offset = size_of_header;
      const auto initial_size = offset + fn->out_s->size;
      out << bl() << "// Prepare output buffer\n";
      out << bl() << "let obuf = buffer\n";
      out << bl() << "obuf.consume(obuf.size)\n";
      out << bl() << "obuf.prepare(" << (initial_size + 128) << ")\n";
      out << bl() << "obuf.commit(" << initial_size << ")\n";
    }

    // Call the implementation
    out << bl() << "\n";
    if (fn->ex) {
      out << bl() << "do {\n" << bb(false);
    }

    // Call user's implementation - returns value(s)
    std::vector<AstFunctionArgument*> out_params;
    for (auto arg : fn->args) {
      if (arg->modifier == ArgumentModifier::Out) {
        out_params.push_back(arg);
      }
    }
    bool has_return = !fn->is_void();
    const std::string try_prefix = fn->is_throwing() ? "try " : "";

    if ((has_return ? 1 : 0) + out_params.size() > 1) {
      // Tuple destructuring
      out << bl() << "let (";
      bool first_ret = true;
      if (has_return) {
        out << "__ret_val";
        first_ret = false;
      }
      for (auto out_arg : out_params) {
        if (!first_ret) out << ", ";
        first_ret = false;
        out << "_out_" << out_arg->name;
      }
      out << ") = " << try_prefix << swift_method_name(fn->name) << "(";
    } else if (has_return || !out_params.empty()) {
      // Single return value
      out << bl() << "let ";
      if (has_return) {
        out << "__ret_val";
      } else {
        out << "_out_" << out_params[0]->name;
      }
      out << " = " << try_prefix << swift_method_name(fn->name) << "(";
    } else {
      // No return
      out << bl() << try_prefix << swift_method_name(fn->name) << "(";
    }

    size_t in_ix = 0, idx = 0;
    for (auto arg : fn->args) {
      if (arg->modifier == ArgumentModifier::In) {
        if (idx > 0) out << ", ";
        out << arg->name << ": ia._" << ++in_ix;
        ++idx;
      }
    }
    out << ")\n";

    // Marshal output
    if (!fn->out_s) {
      out << bl() << "// Send success\n";
      out << bl() << "makeSimpleAnswer(buffer: buffer, messageId: impl.MessageId.success)\n";
    } else {
      if (fn->out_s->flat) {
        const auto offset = size_of_header;
        const auto initial_size = offset + fn->out_s->size;
        out << bl() << "// Prepare output buffer\n";
        out << bl() << "let obuf = buffer\n";
        out << bl() << "obuf.consume(obuf.size)\n";
        out << bl() << "obuf.prepare(" << initial_size << ")\n";
        out << bl() << "obuf.commit(" << initial_size << ")\n";
      }

      out << bl() << "// Marshal output arguments\n";
      out << bl() << "var out_data = " << fn->out_s->name << "()\n";

      int ix = 0;
      if (!fn->is_void()) {
        out << bl() << "out_data._1 = __ret_val\n";
        ix = 1;
      }
      for (auto out_arg : out_params) {
        ++ix;
        out << bl() << "out_data._" << ix << " = _out_" << out_arg->name << "\n";
      }
      out << "\n";

      out << bl() << ns(fn->out_s->nm) << "marshal_" << fn->out_s->name << "(buffer: buffer, offset: " << size_of_header << ", data: out_data)\n";
      out << bl() << "guard let outData = buffer.data else { return }\n";
      out << bl() << "outData.storeBytes(of: UInt32(buffer.size - 4), toByteOffset: 0, as: UInt32.self)\n";
      out << bl() << "outData.storeBytes(of: impl.MessageId.blockResponse.rawValue, toByteOffset: 4, as: Int32.self)\n";
      out << bl() << "outData.storeBytes(of: impl.MessageType.answer.rawValue, toByteOffset: 8, as: Int32.self)\n";
    }

    // Handle exception
    if (fn->ex) {
      const auto offset = size_of_header;
      const auto initial_size = offset + fn->ex->size;

      out << eb();
      out << bl() << "catch let e as " << fn->ex->name << " {\n" << bb(false);
      out << bl() << "let obuf = buffer\n";
      out << bl() << "obuf.consume(obuf.size)\n";
      out << bl() << "obuf.prepare(" << initial_size << ")\n";
      out << bl() << "obuf.commit(" << initial_size << ")\n";
      out << bl() << "guard let exData = obuf.data else { return }\n";
      out << bl() << "marshal_" << fn->ex->name << "(buffer: obuf, offset: " << offset << ", data: e)\n";
      out << bl() << "exData.storeBytes(of: UInt32(obuf.size - 4), toByteOffset: 0, as: UInt32.self)\n";
      out << bl() << "exData.storeBytes(of: impl.MessageId.exception.rawValue, toByteOffset: 4, as: Int32.self)\n";
      out << bl() << "exData.storeBytes(of: impl.MessageType.answer.rawValue, toByteOffset: 8, as: Int32.self)\n";
      out << eb();
      out << bl() << "catch {\n" << bb(false);
      out << bl() << "makeSimpleAnswer(buffer: buffer, messageId: impl.MessageId.error_Unknown)\n";
      out << eb();
    }

    out << eb(false);
  }

  // Default case
  out << bl() << "default:\n";
  out << bl() << "  makeSimpleAnswer(buffer: buffer, messageId: impl.MessageId.error_UnknownFunctionIdx)\n";

  out << eb(false) << bl() << "} // switch\n";
  out << eb(false) << bl() << "} // dispatch\n";
  out << eb() << "\n";
}

void SwiftBuilder::emit_servant_stream_dispatch(AstFunctionDecl* fn)
{
  auto stream_type = static_cast<AstStreamDecl*>(fn->ret_value)->type;

  // For streaming dispatch, we need to:
  // 1. Read stream_id from the StreamInit message (different from CallHeader layout)
  // 2. Call the servant method to get AsyncStream
  // 3. Iterate over the stream and send chunks  
  // 4. Send completion when done

  out << bl() << "// Streaming method dispatch\n";
  out << bl() << "guard let data = buffer.data else { return }\n";
  out << bl() << "let streamId = data.load(fromByteOffset: (" <<
    size_of_header << " + MemoryLayout<NPRPC.impl.StreamInit>.offset(of: \\NPRPC.impl.StreamInit.stream_id)!), as: UInt64.self)\n\n";

  // Unmarshal input arguments if any
  // Input args start after StreamInit
  constexpr auto args_offset = get_stream_init_arguments_offset();
  if (fn->in_s) {
    out << bl() << "// Unmarshal input arguments\n";
    out << bl() << "let ia = " << ns(fn->in_s->nm) << "unmarshal_" << fn->in_s->name << "(buffer: data, offset: " << args_offset << ")\n\n";
  }

  // Get stream_manager from session context (stream_manager is heap-allocated and stays alive)
  out << bl() << "// Get stream_manager for streaming (heap-allocated, survives after dispatch returns)\n";
  out << bl() << "guard let sessionCtx = self.sessionContext,\n";
  out << bl() << "      let streamManager = nprpc_get_stream_manager(sessionCtx) else {\n";
  out << bl() << "  makeSimpleAnswer(buffer: buffer, messageId: impl.MessageId.error_BadInput)\n";
  out << bl() << "  return\n";
  out << bl() << "}\n";
  // Convert to UInt for Swift 6 Sendable compliance
  out << bl() << "let streamManagerInt = UInt(bitPattern: streamManager)\n\n";

  // Call servant method
  out << bl() << "// Get stream from servant implementation\n";
  out << bl() << "let stream = " << swift_method_name(fn->name) << "(";
  bool first = true;
  int ix = 0;
  for (auto arg : fn->args) {
    if (arg->modifier == ArgumentModifier::Out) continue;
    ++ix;
    if (!first) out << ", ";
    first = false;
    out << arg->name << ": ia._" << ix;
  }
  out << ")\n\n";

  // Iterate and send chunks using Task for async iteration
  out << bl() << "// Pump stream and send chunks in background\n";
  out << bl() << "Task {\n" << bb(false);
  out << bl() << "  let streamManagerPtr = UnsafeMutableRawPointer(bitPattern: streamManagerInt)!\n";
  out << bl() << "  var sequence: UInt64 = 0\n";
  out << bl() << "  for await value in stream {\n";

  // Serialize the value and send as chunk
  // For now, handle fundamental types directly
  out << bl() << "    // Send chunk\n";
  out << bl() << "    withUnsafeBytes(of: value) { valueBytes in\n";
  out << bl() << "      nprpc_stream_manager_send_chunk(streamManagerPtr, streamId, valueBytes.baseAddress, UInt32(valueBytes.count), sequence)\n";
  out << bl() << "    }\n";
  out << bl() << "    sequence += 1\n";
  out << bl() << "  }\n";
  out << bl() << "  // Send completion\n";
  out << bl() << "  nprpc_stream_manager_send_complete(streamManagerPtr, streamId, sequence)\n";
  out << eb() << "\n";
}

// Generate Swift trampolines that C++ bridge will call
void SwiftBuilder::emit_swift_trampolines(AstInterfaceDecl* ifs)
{
  const std::string class_name = swift_type_name(ifs->name);

  out << "\n// MARK: - C Trampolines for Swift Servant\n";
  out << "// These are called from C++ bridge (" << class_name << "_SwiftBridge)\n\n";

  for (auto& fn : ifs->fns) {
    // Generate @_cdecl trampoline
    out << "@_cdecl(\"" << fn->name << "_swift_trampoline\")\n";
    out << "func " << fn->name << "_swift_trampoline(\n";
    out << "  _ servant: UnsafeMutableRawPointer";

    // Parameters - use buffer pointers for complex types
    for (auto& arg : fn->args) {
      out << ",\n  _ " << arg->name << ": ";
      if (needs_marshalling(arg->type)) {
        // Complex types pass buffer pointers
        if (arg->modifier == ArgumentModifier::Out) {
          out << "UnsafeMutableRawPointer";
        } else {
          out << "UnsafeRawPointer";
        }
      } else {
        // Fundamental types pass directly
        if (arg->modifier == ArgumentModifier::Out) {
          out << "UnsafeMutablePointer<";
          emit_type(arg->type, out);
          out << ">";
        } else {
          emit_type(arg->type, out);
        }
      }
    }

    out << "\n) ";

    // Return type only if there's a return value (out params handled via pointers)
    std::vector<AstFunctionArgument*> out_params;
    for (auto& arg : fn->args) {
      if (arg->modifier == ArgumentModifier::Out) {
        out_params.push_back(arg);
      }
    }

    bool has_return = !fn->is_void();

    out << bb();
    out << bl() << "let swiftServant = Unmanaged<" << class_name << "Servant>.fromOpaque(servant).takeUnretainedValue()\n";

    // Unmarshal complex input parameters
    for (auto& arg : fn->args) {
      if (arg->modifier == ArgumentModifier::In && needs_marshalling(arg->type)) {
        if (arg->type->id == FieldType::Struct) {
          auto s = cflat(arg->type);
          out << bl() << "let " << arg->name << "Unmarshaled = unmarshal_" << s->name << "(buffer: " << arg->name << ", offset: 0)\n";
        }
      }
    }

    out << bl() << "do {\n" << bb(false);

    // Call Swift method
    out << bl();
    if (has_return || !out_params.empty()) {
      if ((has_return ? 1 : 0) + out_params.size() > 1) {
        out << "let (";
        bool first = true;
        if (has_return) {
          out << "returnValue";
          first = false;
        }
        for (auto& out_param : out_params) {
          if (!first) out << ", ";
          first = false;
          out << out_param->name << "Value";
        }
        out << ") = ";
      } else if (has_return) {
        out << "let returnValue = ";
      } else {
        out << "let " << out_params[0]->name << "Value = ";
      }
    }

    out << "try swiftServant." << swift_method_name(fn->name) << "(";

    // Only pass in parameters (use unmarshaled versions for complex types)
    bool first = true;
    for (auto& arg : fn->args) {
      if (arg->modifier == ArgumentModifier::In) {
        if (!first) out << ", ";
        first = false;
        out << arg->name << ": ";
        if (needs_marshalling(arg->type)) {
          out << arg->name << "Unmarshaled";
        } else {
          out << arg->name;
        }
      }
    }
    out << ")\n";

    // Marshal/set out parameters
    for (auto& out_param : out_params) {
      if (needs_marshalling(out_param->type)) {
        if (out_param->type->id == FieldType::Struct) {
          auto s = cflat(out_param->type);
          out << bl() << "marshal_" << s->name << "(buffer: " << out_param->name << ", offset: 0, data: " << out_param->name << "Value)\n";
        }
      } else {
        out << bl() << out_param->name << ".pointee = " << out_param->name << "Value\n";
      }
    }

    out << eb() << " catch {\n" << bb(false);
    out << bl() << "// TODO: Propagate Swift error to C++ exception\n";
    out << bl() << "fatalError(\"Error in " << fn->name << ": \\(error)\")\n";
    out << eb();
    out << eb() << "\n\n";
  }
}

void SwiftBuilder::emit_interface(AstInterfaceDecl* ifs)
{
  // Emit protocol and client/servant classes under the current namespace
  emit_arguments_structs([this](AstStructDecl* s) {
    auto [ _, inserted ] = emitted_marshal_structs_.insert(s);
    if (inserted) {
      emit_struct2(s, Target::FunctionArgument);
      emit_marshal_function(s);
      emit_unmarshal_function(s);
    }
  });
  emit_protocol(ifs);
  emit_client_proxy(ifs);
  emit_servant_base(ifs);
  // No longer need C trampolines - pure Swift implementation
}

void SwiftBuilder::emit_field_marshal(AstFieldDecl* f, int& offset, const std::string& data_name)
{
  const std::string field_access = data_name + "." + f->name;

  switch (f->type->id) {
  case FieldType::Fundamental: {
    const auto token = cft(f->type)->token_id;
    const int size = get_fundamental_size(token);
    const int field_offset = align_offset(size, offset, size);

    out << bl() << "buf.storeBytes(of: " << field_access << ", toByteOffset: offset + " << field_offset << ", as: ";
    emit_fundamental_type(token, out);
    out << ".self)\n";
    break;
  }
  case FieldType::Enum: {
    const int size = get_fundamental_size(cenum(f->type)->token_id);
    const int field_offset = align_offset(size, offset, size);
    out << bl() << "buf.storeBytes(of: " << field_access << ".rawValue, toByteOffset: offset + " << field_offset << ", as: Int32.self)\n";
    break;
  }
  case FieldType::String: {
    const int field_offset = align_offset(4, offset, 8);  // ptr + length
    out << bl() << "NPRPC.marshal_string(buffer: buffer, offset: offset + " << field_offset << ", string: " << field_access << ")\n";
    break;
  }
  case FieldType::Struct: {
    auto nested = cflat(f->type);
    const int field_offset = align_offset(nested->align, offset, nested->size);
    out << bl() << "marshal_" << nested->name << "(buffer: buffer, offset: offset + " << field_offset << ", data: " << field_access << ")\n";
    break;
  }
  case FieldType::Object: {
    const int field_offset = align_offset(align_of_object, offset, size_of_object);
    out << bl() << "detail.marshal_ObjectId(buffer: buffer, offset: offset + " << field_offset << ", data: " << field_access << ".data)\n";
    break;
  }
  case FieldType::Array: {
    auto wt = cwt(f->type)->real_type();
    auto arr = static_cast<AstArrayDecl*>(f->type);
    auto [v_size, v_align] = get_type_size_align(f->type);
    auto [ut_size, ut_align] = get_type_size_align(wt);
    const int field_offset = align_offset(v_align, offset, v_size);

    // Check array size and warn if mismatch, then copy only what fits
    out << bl() << "if " << field_access << ".count != " << arr->length << " {\n" << bb(false);
    out << bl() << "print(\"Warning: Array field '" << f->name << "' has invalid size: expected " << arr->length << ", got \\(" << field_access << ".count). Copying only \\(min(" << field_access << ".count, " << arr->length << ")) elements.\")\n";
    out << eb();
    const std::string count_name = make_unique_variable_name("actualCount");
    out << bl() << "let " << count_name << " = min(" << field_access << ".count, " << arr->length << ")\n";

    if (is_fundamental(wt)) {
      out << bl() << "NPRPC.marshal_fundamental_array(buffer: buffer, offset: offset + " << field_offset << ", array: " << field_access << ", count: " << count_name << ")\n";
    } else {
      auto flat_struct = cflat(wt);
      std::string type_name = flat_struct ? flat_struct->name : "<unknown>";
      out << bl() << "for i in 0..<" << count_name << " " << bb();
      out << bl() << "marshal_" << type_name << "(buffer: buffer, offset: offset + " << field_offset << " + i * " << ut_size << ", data: " << field_access << "[i])\n";
      out << eb();
    }
    break;
  }
  case FieldType::Vector: {
    auto wt = cwt(f->type)->real_type();
    auto [v_size, v_align] = get_type_size_align(f->type);
    auto [ut_size, ut_align] = get_type_size_align(wt);
    const int field_offset = align_offset(v_align, offset, v_size);

    if (is_fundamental(wt)) {
      out << bl() << "NPRPC.marshal_fundamental_vector(buffer: buffer, offset: offset + " << field_offset << ", vector: " << field_access << ")\n";
    } else {
      auto flat_struct = cflat(wt);
      std::string type_name = flat_struct ? flat_struct->name : "<unknown>";
      out << bl() << "NPRPC.marshal_struct_vector(buffer: buffer, offset: offset + " << field_offset << ", vector: " << field_access << ", elementSize: " << ut_size << ", elementAlignment: " << ut_align << ") { buf, off, elem in\n";
      out << bl() << "  marshal_" << type_name << "(buffer: buf, offset: off, data: elem)\n";
      out << bl() << "}\n";
    }
    break;
  }
  case FieldType::Optional: {
    auto wt = cwt(f->type)->real_type();
    auto [v_size, v_align] = get_type_size_align(f->type);
    const int field_offset = align_offset(v_align, offset, v_size);

    out << bl() << "if let value = " << field_access << " {\n" << bb(false);

    if (is_fundamental(wt)) {
      out << bl() << "NPRPC.marshal_optional_fundamental(buffer: buffer, offset: offset + " << field_offset << ", value: value)\n";
    } else if (wt->id == FieldType::Struct) {
      auto flat_struct = cflat(wt);
      out << bl() << "NPRPC.marshal_optional_struct(buffer: buffer, offset: offset + " << field_offset << ", value: value) { buf, off in\n";
      out << bb(false);
      out << bl() << "marshal_" << flat_struct->name << "(buffer: buf, offset: off, data: value)\n";
      out << eb();
    } else if (wt->id == FieldType::String) {
      out << bl() << "NPRPC.marshal_optional_struct(buffer: buffer, offset: offset + " << field_offset << ", value: value) { buf, off in\n";
      out << bb(false);
      out << bl() << "NPRPC.marshal_string(buffer: buf, offset: off, string: value)\n";
      out << eb();
    } else if (wt->id == FieldType::Enum) {
      out << bl() << "NPRPC.marshal_optional_fundamental(buffer: buffer, offset: offset + " << field_offset << ", value: value.rawValue)\n";
    } else if (wt->id == FieldType::Vector || wt->id == FieldType::Array) {
      const std::string container_type = (wt->id == FieldType::Vector) ? "vector" : "array";
      auto real_elem_type = cwt(wt)->real_type();
      auto [ut_size, ut_align] = get_type_size_align(real_elem_type);
      if (is_fundamental(real_elem_type)) {
        out << bl() << "NPRPC.marshal_optional_struct(buffer: buffer, offset: offset + " << field_offset << ", value: value) { buf, off in\n";
        out << bb(false);
        out << bl() << "NPRPC.marshal_fundamental_" << container_type << "(buffer: buf, offset: off, " << container_type << ": value)\n";
        out << eb();
      } else if (real_elem_type->id == FieldType::Struct) {
        out << bl() << "NPRPC.marshal_optional_struct(buffer: buffer, offset: offset + " << field_offset << ", value: value) { buf, off in\n";
        out << bb(false);
        out << bl() << "NPRPC.marshal_struct_" << container_type << "(buffer: buf, offset: off, " << container_type << ": value, elementSize: " << ut_size << ", elementAlignment: " << ut_align << ") { eb, eo, elem in\n";
        out << bl() << "marshal_" << cflat(real_elem_type)->name << "(buffer: eb, offset: eo, data: elem)\n";
        out << eb();
      } else {
        out << bl() << "// TODO: Optional vector/array of complex type\n";
      }
    } else {
      out << bl() << "// TODO: Optional of type " << static_cast<int>(wt->id) << "\n";
    }
    out << eb(false) << bl() << "} else {\n" << bb(false);
    out << bl() << "buf.storeBytes(of: UInt32(0), toByteOffset: offset + " << field_offset << ", as: UInt32.self)\n";
    out << eb();
    break;
  }
  case FieldType::Alias: {
    auto real_type = calias(f->type)->get_real_type();
    auto temp_field = *f;
    temp_field.type = real_type;
    emit_field_marshal(&temp_field, offset, data_name);
    return;
  }
  default:
    out << bb() << "// TODO: marshal field " << f->name << " (type " << static_cast<int>(f->type->id) << ")\n";
  }
}

// Helper to check if a field type requires direct buffer access (buf variable)
bool needs_buf_for_marshal(AstTypeDecl* type) {
  switch (type->id) {
  case FieldType::Fundamental:
  case FieldType::Enum:
    return true; // Direct storeBytes
  case FieldType::Optional:
    return true; // Writes 0 for nil case
  case FieldType::Alias:
    return needs_buf_for_marshal(calias(type)->get_real_type());
  default:
    return false; // Delegates to other functions
  }
}

void SwiftBuilder::emit_marshal_function(AstStructDecl* s)
{
  // In Swift, exception structs already include __ex_id, so we use the struct name directly
  std::string data_type = s->name;

  assert(s->nm);
  const bool in_namespace = !s->nm->is_root(*ctx_);

  // Check if this is an internal marshalling struct (e.g., foo_M1, foo_M2)
  // These should be fileprivate, not public
  const char* visibility = s->internal ? "fileprivate " : "public ";

  out << "\n" << bl() << "// MARK: - Marshal " << s->name << "\n";
  out << bl() << visibility << (in_namespace ? "static " : "") << "func marshal_" << s->name << "(buffer: FlatBuffer, offset: Int, data: " << data_type << ") ";
  out << bb();

  // Only emit buf variable if any field needs direct byte access
  bool needs_buf = false;
  for (auto field : s->fields) {
    if (needs_buf_for_marshal(field->type)) {
      needs_buf = true;
      break;
    }
  }

  if (needs_buf) {
    out << bl() << "guard let buf = buffer.data else { return }\n";
  }

  int current_offset = 0;
  for (auto field : s->fields) {
    if (s->is_exception() && field->name == "__ex_id") {
      // Special handling for exception ID field - write the exception ID instead of marshaling from data
      out << bl() << "buf.storeBytes(of: UInt32(" << s->exception_id << "), toByteOffset: offset + " << current_offset << ", as: UInt32.self)\n";
      current_offset += 4; // __ex_id is always a UInt32
      continue;
    }
    emit_field_marshal(field, current_offset, "data");
  }

  out << eb();
}

void SwiftBuilder::emit_field_unmarshal(AstFieldDecl* f, int& offset, const std::string& result_name, bool has_endpoint)
{
  const std::string field_name = result_name + "." + f->name;

  switch (f->type->id) {
  case FieldType::Fundamental: {
    const auto token = cft(f->type)->token_id;
    const int size = get_fundamental_size(token);
    const int field_offset = align_offset(size, offset, size);

    out << bl() << field_name << " = buffer.load(fromByteOffset: offset + " << field_offset << ", as: ";
    emit_fundamental_type(token, out);
    out << ".self)\n";
    break;
  }
  case FieldType::Enum: {
    const int size = get_fundamental_size(cenum(f->type)->token_id);
    const int field_offset = align_offset(size, offset, size);
    out << bl() << field_name << " = " << cenum(f->type)->name << "(rawValue: buffer.load(fromByteOffset: offset + " << field_offset << ", as: Int32.self))!\n";
    break;
  }
  case FieldType::String: {
    const int field_offset = align_offset(4, offset, 8);
    out << bl() << field_name << " = NPRPC.unmarshal_string(buffer: buffer, offset: offset + " << field_offset << ")\n";
    break;
  }
  case FieldType::Struct: {
    auto nested = cflat(f->type);
    const int field_offset = align_offset(nested->align, offset, nested->size);
    if (has_endpoint && contains_object(f->type)) {
      out << bl() << field_name << " = try unmarshal_" << nested->name << "(buffer: buffer, offset: offset + " << field_offset << ", endpoint: endpoint)\n";
    } else {
      out << bl() << field_name << " = unmarshal_" << nested->name << "(buffer: buffer, offset: offset + " << field_offset << ")\n";
    }
    break;
  }
  case FieldType::Object: {
    const int field_offset = align_offset(align_of_object, offset, size_of_object);
    if (has_endpoint) {
      out << bl() << field_name << " = try NPRPC.unmarshal_object_proxy(buffer: buffer, offset: offset + " << field_offset << ", endpoint: endpoint)\n";
    } else {
      out << bl() << field_name << " = NPRPC.unmarshal_object_id(buffer: buffer, offset: offset + " << field_offset << ")\n";
    }
    break;
  }
  case FieldType::Array: {
    auto wt = cwt(f->type)->real_type();
    auto arr = static_cast<AstArrayDecl*>(f->type);
    auto [v_size, v_align] = get_type_size_align(f->type);
    auto [ut_size, ut_align] = get_type_size_align(wt);
    const int field_offset = align_offset(v_align, offset, v_size);

    if (is_fundamental(wt)) {
      out << bl() << field_name << " = NPRPC.unmarshal_fundamental_array(buffer: buffer, offset: offset + " << field_offset << ", count: " << arr->length << ")\n";
    } else {
      out << bl() << field_name << " = (0..<" << arr->length << ").map { i in\n" <<  bb(false);
      out << bl() << "unmarshal_" << cflat(wt)->name << "(buffer: buffer, offset: offset + " << field_offset << " + i * " << ut_size << ")\n";
      out << eb();
    }
    break;
  }
  case FieldType::Vector: {
    auto wt = cwt(f->type)->real_type();
    auto [v_size, v_align] = get_type_size_align(f->type);
    auto [ut_size, ut_align] = get_type_size_align(wt);
    const int field_offset = align_offset(v_align, offset, v_size);

    if (is_fundamental(wt)) {
      out << bl() << field_name << " = NPRPC.unmarshal_fundamental_vector(buffer: buffer, offset: offset + " << field_offset << ")\n";
    } else {
      out << bl() << field_name << " = NPRPC.unmarshal_struct_vector(buffer: buffer, offset: offset + " << field_offset << ", elementSize: " << ut_size << ") { buf, off in\n";
      out << bl() << "  unmarshal_" << cflat(wt)->name << "(buffer: buf, offset: off)\n";
      out << bl() << "}\n";
    }
    break;
  }
  case FieldType::Optional: {
    auto wt = cwt(f->type)->real_type();
    auto [v_size, v_align] = get_type_size_align(f->type);
    const int field_offset = align_offset(v_align, offset, v_size);

    out << bl() << "if buffer.load(fromByteOffset: offset + " << field_offset << ", as: UInt32.self) != 0 {\n" << bb(false);
    if (is_fundamental(wt)) {
      out << bl() << field_name << " = NPRPC.unmarshal_optional_fundamental(buffer: buffer, offset: offset + " << field_offset << ")\n";
    } else if (wt->id == FieldType::Struct) {
      out << bl() << field_name << " = NPRPC.unmarshal_optional_struct(buffer: buffer, offset: offset + " << field_offset << ") { buf, off in\n" << bb(false);
      out << bl() << "return unmarshal_" << cflat(wt)->name << "(buffer: buf, offset: off)\n";
      out << eb();
    } else if (wt->id == FieldType::String) {
      out << bl() << field_name << " = NPRPC.unmarshal_optional_struct(buffer: buffer, offset: offset + " << field_offset << ") { buf, off in\n" << bb(false);
      out << bl() << "NPRPC.unmarshal_string(buffer: buf, offset: off)\n";
      out << eb();
    } else if (wt->id == FieldType::Enum) {
      out << bl() << field_name << " = " << cenum(wt)->name << "(rawValue: buffer.load(fromByteOffset: offset + " << field_offset << " + 4, as: Int32.self))\n";
    } else if (wt->id == FieldType::Vector || wt->id == FieldType::Array) {
      auto real_elem_type = cwt(wt)->real_type();
      auto [ut_size, ut_align] = get_type_size_align(real_elem_type);
      if (is_fundamental(real_elem_type)) {
        out << bl() << field_name << " = NPRPC.unmarshal_optional_struct(buffer: buffer, offset: offset + " << field_offset << ") { buf, off in\n" << bb(false);
        out << bl() << "NPRPC.unmarshal_fundamental_vector(buffer: buf, offset: off)\n";
        out << eb();
      } else if (real_elem_type->id == FieldType::Struct) {
        out << bl() << field_name << " = NPRPC.unmarshal_optional_struct(buffer: buffer, offset: offset + " << field_offset << ") { buf, off in\n" << bb(false);
        out << bl() << "NPRPC.unmarshal_struct_vector(buffer: buf, offset: off, elementSize: " << ut_size << ") { eb, eo in\n";
        out << bl() << "unmarshal_" << cflat(real_elem_type)->name << "(buffer: eb, offset: eo)\n";
        out << eb();
      } else {
        out << bl() << "// TODO: Optional vector/array of complex type\n";
      }
    } else {
      out << bl() << "// TODO: Optional of type " << static_cast<int>(wt->id) << "\n";
    }
    out << eb(false);
    out << bl() << "} else {\n" << bb(false);
    out << bl() << field_name << " = nil\n";
    out << eb();
    break;
  }
  case FieldType::Alias: {
    auto real_type = calias(f->type)->get_real_type();
    auto temp_field = *f;
    temp_field.type = real_type;
    emit_field_unmarshal(&temp_field, offset, result_name, has_endpoint);
    return;
  }
  default:
    out << bb() << "// TODO: unmarshal field " << f->name << " (type " << static_cast<int>(f->type->id) << ")\n";
  }
}

void SwiftBuilder::emit_unmarshal_function(AstStructDecl* s)
{
  // Check if any field contains objects
  bool has_objects = false;
  for (auto field : s->fields) {
    if (contains_object(field->type)) {
      has_objects = true;
      break;
    }
  }

  assert(s->nm);
  const bool in_namespace = !s->nm->is_root(*ctx_);
  const char* visibility = s->internal ? "fileprivate " : "public ";

  out << "\n" << bl() << "// MARK: - Unmarshal " << s->name << "\n";
  out << bl() << visibility << (in_namespace ? "static " : "") << "func unmarshal_" << s->name << "(buffer: UnsafeRawPointer, offset: Int";
  if (has_objects) {
    out << ", endpoint: NPRPCEndpoint";
  }
  out << ")";
  if (has_objects) {
    out << " throws";
  }
  out << " -> " << s->name << " ";
  out << bb();

  // Create struct with default values (fields have defaults from emit_struct2)
  if (s->is_exception() && s->fields.size() == 1) {
    out << bl() << "return " << s->name << "()\n";
    out << eb() << "\n";
    return;
  }

  out << bl() << "var result = " << s->name << "()\n";
  int current_offset = s->is_exception() ? 4 : 0; // Start after __ex_id for exceptions
  for (auto field : s->fields) {
    if (s->is_exception() && field->name == "__ex_id") {
      // Skip __ex_id field for exceptions - it's handled separately
      continue;
    }
    emit_field_unmarshal(field, current_offset, "result", has_objects);
  }

  out << bl() << "return result\n";
  out << eb() << "\n";
}

// Emit safety checks for a type recursively
void SwiftBuilder::emit_safety_checks_r(AstTypeDecl* type, const std::string& op, int offset, bool top_type)
{
  switch (type->id) {
  case FieldType::Struct: {
    auto s = cflat(type);

    if (top_type) {
      out << bl() << "guard NPRPC.check_struct_bounds(bufferSize: bufferSize, offset: " << op << ", structSize: " << s->size << ") else { return false }\n";
    }

    if (s->flat)
      break;

    // Check non-flat fields
    int field_offset = 0;
    for (auto field : s->fields) {
      auto ftr = field->type;
      if (ftr->id == FieldType::Alias)
        ftr = calias(ftr)->get_real_type();

      auto [f_size, f_align] = get_type_size_align(ftr);
      int aligned_offset = align_offset(f_align, field_offset, f_size);
      if (ftr->id == FieldType::Vector || ftr->id == FieldType::Array ||
          ftr->id == FieldType::String || ftr->id == FieldType::Optional ||
          ftr->id == FieldType::Struct || ftr->id == FieldType::Object)
      {
        std::string field_op = op + " + " + std::to_string(aligned_offset);
        emit_safety_checks_r(ftr, field_op, aligned_offset, ftr->id != FieldType::Struct);
      }
    }
    break;
  }

  case FieldType::String:
    out << bl() << "guard NPRPC.check_string_bounds(buffer: buffer, bufferSize: bufferSize, offset: " << op << ") else { return false }\n";
    break;

  case FieldType::Vector: {
    auto wt = cwt(type)->type;
    auto [elem_size, elem_align] = get_type_size_align(wt);

    out << bl() << "guard NPRPC.check_vector_bounds(buffer: buffer, bufferSize: bufferSize, offset: " << op << ", elementSize: " << elem_size << ") else { return false }\n";

    if (is_flat(wt))
      break;

    // For non-flat element types, need to check each element  
    out << bl() << "do {\n" << bb(false);
    out << bl() << "let relOffset = Int(buffer.load(fromByteOffset: " << op << ", as: UInt32.self))\n";
    out << bl() << "let count = Int(buffer.load(fromByteOffset: " << op << " + 4, as: UInt32.self))\n";
    out << bl() << "let dataOffset = " << op << " + relOffset\n";
    out << bl() << "for i in 0..<count {\n" << bb(false);
    out << bl() << "let elemOffset = dataOffset + i * " << elem_size << "\n";
    emit_safety_checks_r(wt, "elemOffset", 0, true);
    out << eb(false) << bl() << "}\n";
    out << eb(false) << bl() << "}\n";
    break;
  }

  case FieldType::Optional: {
    auto wt = cwt(type)->type;
    auto [elem_size, elem_align] = get_type_size_align(wt);

    out << bl() << "guard NPRPC.check_optional_bounds(buffer: buffer, bufferSize: bufferSize, offset: " << op << ", valueSize: " << elem_size << ") else { return false }\n";

    if (is_flat(wt))
      break;

    // If present, check the value
    out << bl() << "do {\n" << bb(false);
    out << bl() << "let relOffset = Int(buffer.load(fromByteOffset: " << op << ", as: UInt32.self))\n";
    out << bl() << "if relOffset != 0 {\n" << bb(false);
    out << bl() << "let valueOffset = " << op << " + relOffset\n";
    emit_safety_checks_r(wt, "valueOffset", 0, true);
    out << eb(false) << bl() << "}\n";
    out << eb(false) << bl() << "}\n";
    break;
  }

  case FieldType::Array: {
    auto arr = static_cast<AstArrayDecl*>(type);
    auto wt = cwt(type)->real_type();
    
    if (is_flat(wt))
      break;

    // Check each element of fixed-size array
    auto [elem_size, elem_align] = get_type_size_align(wt);
    out << bl() << "for i in 0..<" << arr->length << " {\n" << bb(false);
    out << bl() << "let elemOffset = " << op << " + i * " << elem_size << "\n";
    emit_safety_checks_r(wt, "elemOffset", 0, true);
    out << eb(false) << bl() << "}\n";
    break;
  }

  case FieldType::Alias: {
    auto real_type = calias(type)->get_real_type();
    emit_safety_checks_r(real_type, op, offset, top_type);
    break;
  }

  case FieldType::Object:
    emit_safety_checks_r(ctx_->builtin_types_info_.object_id_struct, op, offset, false);
    break;

  default:
    break;
  }
}

// Emit check functions for all untrusted interfaces
void SwiftBuilder::emit_safety_checks()
{
  std::set<struct_id_t> emitted;

  for (auto ifs : ctx_->interfaces) {
    if (ifs->trusted)
      continue;

    for (auto fn : ifs->fns) {
      auto s = fn->in_s;
      if (!s)
        continue;

      auto name = s->get_function_struct_id();
      if (emitted.find(name) != emitted.end())
        continue;
      emitted.emplace(name);

      out << "\n" << bl() << "// Safety check for " << s->name << "\n";
      out << bl() << "fileprivate func check_" << name << "(buffer: UnsafeRawPointer, bufferSize: Int, offset: Int) -> Bool " << bb();

      emit_safety_checks_r(s, "offset", 0, true);

      out << bl() << "return true\n";
      out << eb() << "\n";
    }
  }
}

void SwiftBuilder::emit_namespace_begin()
{
  auto ns = ctx_->nm_cur();
  if (!ns->name().empty()) {
    out << bl() << "public enum " << ns->name() << " " << bb();
  }
}

void SwiftBuilder::emit_namespace_end()
{
  auto ns = ctx_->nm_cur();
  if (!ns->name().empty()) {
    out << eb() << "\n";
  }
}

void SwiftBuilder::finalize()
{
  if (out.str().empty())
    return;

  // Emit safety check functions for untrusted interfaces at file scope
  emit_safety_checks();

  // Write to output file - use IDL filename (like C++) not module name
  auto output_file = out_dir_ / (ctx_->current_file() + ".swift");

  std::ofstream ofs(output_file);
  if (!ofs) {
    throw std::runtime_error("Failed to open output file: " +
                             output_file.string());
  }

  // Swift file header
  ofs << "// Generated by npidl compiler\n";
  ofs << "// DO NOT EDIT - all changes will be lost\n\n";
  // Only import NPRPC for files not in the NPRPC module itself
  // nprpc_base and nprpc_nameserver are part of NPRPC module
  const auto& filename = ctx_->current_file();
  if (filename != "nprpc_base" && filename != "nprpc_nameserver")
    ofs << "import NPRPC\n\n";

  ofs << out.str();

  // Emit throwException function at file scope (outside any namespace enum)
  auto& exs = ctx_->exceptions;
  if (!exs.empty()) {
    ofs << "\n// MARK: - Exception Handling\n";
    ofs << "fileprivate func " << ctx_->current_file()
        << "_throwException(buffer: FlatBuffer) -> any Error {\n";
    ofs << "  guard let data = buffer.constData else {\n";
    ofs << "    return RuntimeError(message: \"Failed to read exception data\")\n";
    ofs << "  }\n";
    ofs << "  let exId = data.load(fromByteOffset: " << size_of_header
        << ", as: UInt32.self)\n";
    ofs << "  switch exId {\n";

    for (auto ex : exs) {
      ofs << "  case " << ex->exception_id << ":\n";
      ofs << "    return " << ns(ex->nm) << "unmarshal_" << ex->name
          << "(buffer: data, offset: " << size_of_header << ")\n";
    }

    ofs << "  default:\n";
    ofs << "    return RuntimeError(message: \"Unknown exception id: \\(exId)\")\n";
    ofs << "  }\n";
    ofs << "}\n";
  }
}

} // namespace npidl::builders
