// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "swift_builder.hpp"
#include "utils.hpp"
#include <cassert>
#include <iostream>
#include <fstream>

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

SwiftBuilder::SwiftBuilder(Context* ctx, std::filesystem::path out_dir)
    : Builder(ctx)
    , out_dir_(std::move(out_dir))
{
}

std::ostream& operator<<(std::ostream& os, const SwiftBuilder::_ns& ns)
{
  if (!ns.nm->name().empty()) {
    // Swift uses nested enums for namespaces
    os << ns.nm->name();
  }
  return os;
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
    // In Swift, don't prefix with namespace if it's in the same module
    os << swift_type_name(cflat(type)->name);
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
    os << "ObjectPtr<Object>";
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

void SwiftBuilder::emit_struct2(AstStructDecl* s, bool is_exception)
{
  const std::string type_keyword = is_exception ? "struct" : "struct";
  
  out << bl() << "public " << type_keyword << " " << swift_type_name(s->name);
  
  if (is_exception) {
    out << ": NPRPCError";
  } else {
    out << ": Codable, Sendable";
  }
  
  out << " " << bb();
  
  // Emit fields with default values to allow var result: StructName declarations
  for (auto field : s->fields) {
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
    case FieldType::Enum:
      // Use first enum value as default
      out << " = ." << swift_method_name(cenum(actual_type)->items.begin()->first);
      break;
    case FieldType::Optional:
      out << " = nil";
      break;
    default:
      // Structs and other complex types don't get defaults
      break;
    }
    out << "\n";
  }
  
  // Generate memberwise initializer
  if (!s->fields.empty()) {
    // First, generate no-argument init (for unmarshal)
    out << "\n" << bl() << "public init() {}\n";
    
    // Then, generate memberwise initializer
    out << "\n" << bl() << "public init(";
    bool first = true;
    for (auto field : s->fields) {
      if (!first) out << ", ";
      first = false;
      out << field->name << ": ";
      emit_type(field->type, out);
    }
    out << ") " << bb();
    for (auto field : s->fields) {
      out << bl() << "self." << field->name << " = " << field->name << "\n";
    }
    out << eb();
  }
  
  // For exceptions, add the required message property
  if (is_exception) {
    out << "\n" << bl() << "public var message: String { \"" << s->name << "\" }\n";
  }
  
  out << eb() << "\n";
}

void SwiftBuilder::emit_struct(AstStructDecl* s)
{
  emit_struct2(s, false);
  emit_marshal_function(s);
  emit_unmarshal_function(s);
}

void SwiftBuilder::emit_exception(AstStructDecl* s)
{
  emit_struct2(s, true);
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
    
    // Return type: collect out parameters and return value
    std::vector<AstFunctionArgument*> out_params;
    for (auto& arg : fn->args) {
      if (arg->modifier == ArgumentModifier::Out) {
        out_params.push_back(arg);
      }
    }
    
    bool has_return = !fn->is_void();
    if (has_return || !out_params.empty()) {
      out << " throws -> ";
      
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
      out << " throws";
    }
    
    out << "\n";
  }
  
  out << eb() << "\n";
}

void SwiftBuilder::emit_client_proxy(AstInterfaceDecl* ifs)
{
  const std::string class_name = swift_type_name(ifs->name);
  
  out << bl() << "// Client proxy for " << class_name << "\n";
  out << bl() << "// Pure Swift implementation with direct marshalling\n";
  out << bl() << "public class " << class_name << ": " << class_name << "Protocol " << bb();
  out << bl() << "private let object: NPRPCObject\n\n";
  
  // Constructor
  out << bl() << "public init(_ object: NPRPCObject) " << bb();
  out << bl() << "self.object = object\n";
  out << eb() << "\n";
  
  // Implement protocol methods with marshalling
  for (auto& fn : ifs->fns) {
    make_arguments_structs(fn);
    
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
    
    out << ") throws";
    
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
    
    // Calculate buffer size
    const auto fixed_size = get_arguments_offset() + (fn->in_s ? fn->in_s->size : 0);
    const auto capacity = fixed_size + (fn->in_s ? (fn->in_s->flat ? 0 : 128) : 0);
    
    out << bl() << "// Prepare buffer\n";
    out << bl() << "let buffer = FlatBuffer()\n";
    out << bl() << "buffer.prepare(" << capacity << ")\n";
    out << bl() << "buffer.commit(" << fixed_size << ")\n";
    out << bl() << "guard let data = buffer.data else { throw NPRPCError.bufferError }\n\n";
    
    // Write message header
    out << bl() << "// Write message header\n";
    out << bl() << "data.storeBytes(of: UInt32(0), toByteOffset: 0, as: UInt32.self)  // size (set later)\n";
    out << bl() << "data.storeBytes(of: UInt32(1), toByteOffset: 4, as: UInt32.self)  // msg_id: FunctionCall\n";
    out << bl() << "data.storeBytes(of: UInt32(0), toByteOffset: 8, as: UInt32.self)  // msg_type: Request\n";
    out << bl() << "data.storeBytes(of: UInt32(0), toByteOffset: 12, as: UInt32.self) // reserved\n\n";
    
    // Write call header
    out << bl() << "// Write call header\n";
    out << bl() << "data.storeBytes(of: object.poaIdx, toByteOffset: " << size_of_header << ", as: UInt16.self)\n";
    out << bl() << "data.storeBytes(of: UInt8(0), toByteOffset: " << (size_of_header + 2) << ", as: UInt8.self)  // interface_idx\n";
    out << bl() << "data.storeBytes(of: UInt8(" << fn->idx << "), toByteOffset: " << (size_of_header + 3) << ", as: UInt8.self)  // function_idx\n";
    out << bl() << "data.storeBytes(of: object.objectId, toByteOffset: " << (size_of_header + 8) << ", as: UInt64.self)\n\n";
    
    // Marshal input arguments
    if (fn->in_s) {
      out << bl() << "// Marshal input arguments\n";
      out << bl() << "let inArgs = (";
      int ix = 0;
      for (auto arg : fn->args) {
        if (arg->modifier == ArgumentModifier::Out) continue;
        if (ix > 0) out << ", ";
        out << "_" << (ix + 1) << ": " << arg->name;
        ++ix;
      }
      out << ")\n";
      out << bl() << "marshal_" << fn->in_s->name << "(buffer: data, offset: " << get_arguments_offset() << ", data: inArgs)\n\n";
    }
    
    // Set message size
    out << bl() << "data.storeBytes(of: UInt32(" << (fixed_size - 4) << "), toByteOffset: 0, as: UInt32.self)\n\n";
    
    // Send/receive
    out << bl() << "// Send and receive\n";
    out << bl() << "try object.session.sendReceive(buffer: buffer, timeout: object.timeout)\n\n";
    
    // Handle reply
    out << bl() << "// Handle reply\n";
    out << bl() << "let stdReply = handleStandardReply(buffer: buffer)\n";
    if (fn->ex) {
      out << bl() << "if stdReply == 1 { throw " << ctx_->current_file() << "_throwException(buffer: buffer) }\n";
    }
    
    if (!fn->out_s) {
      out << bl() << "if stdReply != 0 { throw NPRPCError.unexpectedReply }\n";
    } else {
      out << bl() << "if stdReply != -1 { throw NPRPCError.unexpectedReply }\n\n";
      
      // Unmarshal output arguments
      out << bl() << "guard let responseData = buffer.data else { throw NPRPCError.bufferError }\n";
      
      bool out_needs_endpoint = false;
      for (auto f : fn->out_s->fields) {
        if (contains_object(f->type)) {
          out_needs_endpoint = true;
          break;
        }
      }
      
      if (out_needs_endpoint) {
        out << bl() << "let out = unmarshal_" << fn->out_s->name << "(buffer: responseData, offset: " << size_of_header << ", endpoint: object.endpoint)\n";
      } else {
        out << bl() << "let out = unmarshal_" << fn->out_s->name << "(buffer: responseData, offset: " << size_of_header << ")\n";
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
    
    out << eb() << "\n";
  }
  
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
    
    out << ") throws";
    
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
  
  // Read function index
  out << bl() << "// Read function index\n";
  out << bl() << "let functionIdx = data.load(fromByteOffset: " << (size_of_header + 3) << ", as: UInt8.self)\n\n";
  
  // Switch on function index
  out << bl() << "switch functionIdx " << bb();
  
  for (auto& fn : ifs->fns) {
    out << bl() << "case " << fn->idx << ": // " << fn->name << "\n";
    out << bb();
    
    // Unmarshal input arguments
    if (fn->in_s) {
      out << bl() << "\n// Unmarshal input arguments\n";
      bool in_needs_endpoint = false;
      for (auto f : fn->in_s->fields) {
        if (contains_object(f->type)) {
          in_needs_endpoint = true;
          break;
        }
      }
      
      if (in_needs_endpoint) {
        out << bl() << "let ia = unmarshal_" << fn->in_s->name << "(buffer: data, offset: " << get_arguments_offset() << ", endpoint: remoteEndpoint)\n";
      } else {
        out << bl() << "let ia = unmarshal_" << fn->in_s->name << "(buffer: data, offset: " << get_arguments_offset() << ")\n";
      }
    }
    
    // Prepare output buffer if needed
    if (fn->out_s && !fn->out_s->flat) {
      const auto offset = size_of_header;
      const auto initial_size = offset + fn->out_s->size;
      out << bl() << "\n// Prepare output buffer\n";
      out << bl() << "let obuf = buffer\n";
      out << bl() << "obuf.consume(obuf.size)\n";
      out << bl() << "obuf.prepare(" << (initial_size + 128) << ")\n";
      out << bl() << "obuf.commit(" << initial_size << ")\n";
    }
    
    // Call the implementation
    out << bl() << "\n";
    if (fn->ex) {
      out << bl() << "do " << bb();
    }
    
    // Call user's implementation - returns value(s)
    std::vector<AstFunctionArgument*> out_params;
    for (auto arg : fn->args) {
      if (arg->modifier == ArgumentModifier::Out) {
        out_params.push_back(arg);
      }
    }
    bool has_return = !fn->is_void();
    
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
      out << ") = try " << swift_method_name(fn->name) << "(";
    } else if (has_return || !out_params.empty()) {
      // Single return value
      out << bl() << "let ";
      if (has_return) {
        out << "__ret_val";
      } else {
        out << "_out_" << out_params[0]->name;
      }
      out << " = try " << swift_method_name(fn->name) << "(";
    } else {
      // No return
      out << bl() << "try " << swift_method_name(fn->name) << "(";
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
    
    // Handle exception
    if (fn->ex) {
      const auto offset = size_of_header;
      const auto initial_size = offset + fn->ex->size;
      
      out << eb();
      out << bl() << "catch let e as " << fn->ex->name << " " << bb();
      out << bl() << "let obuf = buffer\n";
      out << bl() << "obuf.consume(obuf.size)\n";
      out << bl() << "obuf.prepare(" << initial_size << ")\n";
      out << bl() << "obuf.commit(" << initial_size << ")\n";
      out << bl() << "guard let exData = obuf.data else { return }\n";
      
      out << bl() << "let ex_data = (";
      out << "__ex_id: " << fn->ex->exception_id;
      for (size_t i = 1; i < fn->ex->fields.size(); ++i) {
        auto mb = fn->ex->fields[i];
        out << ", " << mb->name << ": e." << mb->name;
      }
      out << ")\n";
      
      out << bl() << "marshal_" << fn->ex->name << "(buffer: exData, offset: " << offset << ", data: ex_data)\n";
      out << bl() << "exData.storeBytes(of: UInt32(obuf.size - 4), toByteOffset: 0, as: UInt32.self)\n";
      out << bl() << "exData.storeBytes(of: UInt32(3), toByteOffset: 4, as: UInt32.self)  // MessageId.Exception\n";
      out << bl() << "exData.storeBytes(of: UInt32(1), toByteOffset: 8, as: UInt32.self)  // MessageType.Answer\n";
      out << bl() << "return\n";
      out << eb();
      out << bl() << "catch " << bb();
      out << bl() << "makeSimpleAnswer(buffer: buffer, messageId: 10)  // Error\n";
      out << bl() << "return\n";
      out << eb();
    }
    
    // Marshal output
    if (!fn->out_s) {
      out << bl() << "\n// Send success\n";
      out << bl() << "makeSimpleAnswer(buffer: buffer, messageId: 5)  // Success\n";
    } else {
      if (fn->out_s->flat) {
        const auto offset = size_of_header;
        const auto initial_size = offset + fn->out_s->size;
        out << bl() << "\n// Prepare output buffer\n";
        out << bl() << "let obuf = buffer\n";
        out << bl() << "obuf.consume(obuf.size)\n";
        out << bl() << "obuf.prepare(" << initial_size << ")\n";
        out << bl() << "obuf.commit(" << initial_size << ")\n";
      }
      
      out << bl() << "\n// Marshal output arguments\n";
      out << bl() << "guard let outData = buffer.data else { return }\n";
      out << bl() << "let out_data = (";
      
      int ix = 0;
      if (!fn->is_void()) {
        out << "_1: __ret_val";
        ix = 1;
      }
      for (auto out_arg : out_params) {
        if (ix > 0) out << ", ";
        ++ix;
        out << "_" << ix << ": _out_" << out_arg->name;
      }
      out << ")\n";
      
      out << bl() << "marshal_" << fn->out_s->name << "(buffer: outData, offset: " << size_of_header << ", data: out_data)\n";
      out << bl() << "outData.storeBytes(of: UInt32(buffer.size - 4), toByteOffset: 0, as: UInt32.self)\n";
      out << bl() << "outData.storeBytes(of: UInt32(2), toByteOffset: 4, as: UInt32.self)  // MessageId.BlockResponse\n";
      out << bl() << "outData.storeBytes(of: UInt32(1), toByteOffset: 8, as: UInt32.self)  // MessageType.Answer\n";
    }
    
    out << eb();
  }
  
  // Default case
  out << bl() << "default:\n";
  out << bb();
  out << bl() << "makeSimpleAnswer(buffer: buffer, messageId: 10)  // Error_UnknownFunctionIdx\n";
  out << eb();
  
  out << eb() << " // switch\n";
  out << eb() << " // dispatch\n";
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
    
    out << bl() << "do " << bb();
    
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
    
    out << eb() << " catch " << bb();
    out << bl() << "// TODO: Propagate Swift error to C++ exception\n";
    out << bl() << "fatalError(\"Error in " << fn->name << ": \\(error)\")\n";
    out << eb();
    out << eb() << "\n\n";
  }
}

void SwiftBuilder::emit_interface(AstInterfaceDecl* ifs)
{
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
    
    out << bl() << "buffer.storeBytes(of: " << field_access << ", toByteOffset: offset + " << field_offset << ", as: ";
    emit_fundamental_type(token, out);
    out << ".self)\n";
    break;
  }
  case FieldType::Enum: {
    const int size = get_fundamental_size(cenum(f->type)->token_id);
    const int field_offset = align_offset(size, offset, size);
    out << bl() << "buffer.storeBytes(of: " << field_access << ".rawValue, toByteOffset: offset + " << field_offset << ", as: Int32.self)\n";
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
    out << bl() << "NPRPC.marshal_object_id(buffer: buffer, offset: offset + " << field_offset << ", objectId: " << field_access << ".data)\n";
    break;
  }
  case FieldType::Array: {
    auto wt = cwt(f->type)->real_type();
    auto arr = static_cast<AstArrayDecl*>(f->type);
    auto [v_size, v_align] = get_type_size_align(f->type);
    auto [ut_size, ut_align] = get_type_size_align(wt);
    const int field_offset = align_offset(v_align, offset, v_size);
    
    if (is_fundamental(wt)) {
      out << bl() << "NPRPC.marshal_fundamental_array(buffer: buffer, offset: offset + " << field_offset << ", array: " << field_access << ")\n";
    } else {
      auto flat_struct = cflat(wt);
      std::string type_name = flat_struct ? flat_struct->name : "<unknown>";
      out << bl() << "for i in 0..<" << arr->length << bb();
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
      out << bb();
      out << bl() << "NPRPC.marshal_struct_vector(buffer: buffer, offset: offset + " << field_offset << ", vector: " << field_access << ", elementSize: " << ut_size << ") { buf, off, elem in\n";
      out << bl() << "marshal_" << type_name << "(buffer: buf, offset: off, data: elem)\n";
      out << eb();
    }
    break;
  }
  case FieldType::Optional: {
    auto wt = cwt(f->type)->real_type();
    auto [v_size, v_align] = get_type_size_align(f->type);
    const int field_offset = align_offset(v_align, offset, v_size);
    
    out << bb() << "if let value = " << field_access << " {\n";
    bb();
    if (is_fundamental(wt)) {
      out << bb() << "NPRPC.marshal_optional_fundamental(buffer: buffer, offset: offset + " << field_offset << ", value: value)\n";
    } else {
      auto flat_struct = cflat(wt);
      std::string type_name = flat_struct ? flat_struct->name : "<unknown>";
      out << bb() << "NPRPC.marshal_optional_struct(buffer: buffer, offset: offset + " << field_offset << ", value: value) { buf, off in\n";
      out << bb(false);
      out << bb() << "marshal_" << type_name << "(buffer: buf, offset: off, data: value)\n";
      out << eb();
    }
    eb();
    out << bb() << "} else {\n";
    bb();
    out << bb() << "buffer.storeBytes(of: UInt32(0), toByteOffset: offset + " << field_offset << ", as: UInt32.self)\n";
    eb();
    out << bb() << "}\n";
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

void SwiftBuilder::emit_marshal_function(AstStructDecl* s)
{
  calc_struct_size_align(s);
  
  std::string data_type = s->is_exception() ? (s->name + "_Data") : s->name;
  
  out << "\n" << bl() << "// MARK: - Marshal " << s->name << "\n";
  out << bl() << "public func marshal_" << s->name << "(buffer: UnsafeMutableRawPointer, offset: Int, data: " << data_type << ") ";
  out << bb();

  int current_offset = 0;
  for (auto field : s->fields) {
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
      out << bl() << field_name << " = unmarshal_" << nested->name << "(buffer: buffer, offset: offset + " << field_offset << ", endpoint: endpoint)\n";
    } else {
      out << bl() << field_name << " = unmarshal_" << nested->name << "(buffer: buffer, offset: offset + " << field_offset << ")\n";
    }
    break;
  }
  case FieldType::Object: {
    const int field_offset = align_offset(align_of_object, offset, size_of_object);
    if (has_endpoint) {
      out << bl() << field_name << " = NPRPC.unmarshal_object_proxy(buffer: buffer, offset: offset + " << field_offset << ", endpoint: endpoint)\n";
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
      out << bl() << field_name << " = (0..<" << arr->length << ").map { i in\n";
      out << bb();
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
      out << bb();
      out << bl() << "unmarshal_" << cflat(wt)->name << "(buffer: buf, offset: off)\n";
      out << eb();
    }
    break;
  }
  case FieldType::Optional: {
    auto wt = cwt(f->type)->real_type();
    auto [v_size, v_align] = get_type_size_align(f->type);
    const int field_offset = align_offset(v_align, offset, v_size);
    
    out << bb() << "if buffer.load(fromByteOffset: offset + " << field_offset << ", as: UInt32.self) != 0 {\n";
    bb();
    if (is_fundamental(wt)) {
      out << bb() << field_name << " = NPRPC.unmarshal_optional_fundamental(buffer: buffer, offset: offset + " << field_offset << ")\n";
    } else {
      out << bb() << field_name << " = NPRPC.unmarshal_optional_struct(buffer: buffer, offset: offset + " << field_offset << ") { buf, off in\n";
      out << bb();
      out << bb() << "unmarshal_" << cflat(wt)->name << "(buffer: buf, offset: off)\n";
      out << eb();
    }
    out << eb();
    out << bl() << "} else {\n" << bb(false);
    out << bl() << field_name << " = nil\n";
    out << eb();
    out << bl() << "}\n";
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
  
  out << "\n" << bl() << "// MARK: - Unmarshal " << s->name << "\n";
  out << bl() <<"public func unmarshal_" << s->name << "(buffer: UnsafeRawPointer, offset: Int";
  if (has_objects) {
    out << ", endpoint: NPRPC.EndPoint";
  }
  out << ") -> " << s->name << " ";
  out << bb();
  
  // Create struct with default values (fields have defaults from emit_struct2)
  out << bl() << "var result = " << s->name << "()\n";
  
  int current_offset = 0;
  for (auto field : s->fields) {
    emit_field_unmarshal(field, current_offset, "result", has_objects);
  }
  
  out << bl() << "return result\n";
  out << eb() << "\n";
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
  
  // Write to output file
  auto output_file = out_dir_ / (ctx_->module() + ".swift");
  
  std::ofstream ofs(output_file);
  if (!ofs) {
    throw std::runtime_error("Failed to open output file: " +
                             output_file.string());
  }

  // Swift file header
  ofs << "// Generated by npidl compiler\n";
  ofs << "// DO NOT EDIT - all changes will be lost\n\n";
  // Only import NPRPC for non-base modules (avoid self-import)
  if (ctx_ && ctx_->module() != "nprpc") {
    std::cerr << "Emitting Swift module import for module: " << ctx_->module() << "\n";
    ofs << "import NPRPC\n\n";
  }

  ofs << out.str();
  ofs.close();
  
  std::cout << "Generated: " << output_file << "\n";
}

// Static methods for generating C++ bridge code (called from CppBuilder)
// Helper to emit C++ types for bridge
static void emit_cpp_type(AstTypeDecl* type, std::ostream& os, Context* ctx) {
  switch (type->id) {
  case FieldType::Fundamental:
    switch (cft(type)->token_id) {
    case TokenId::Boolean: os << "bool"; break;
    case TokenId::Int8: os << "int8_t"; break;
    case TokenId::UInt8: os << "uint8_t"; break;
    case TokenId::Int16: os << "int16_t"; break;
    case TokenId::UInt16: os << "uint16_t"; break;
    case TokenId::Int32: os << "int32_t"; break;
    case TokenId::UInt32: os << "uint32_t"; break;
    case TokenId::Int64: os << "int64_t"; break;
    case TokenId::UInt64: os << "uint64_t"; break;
    case TokenId::Float32: os << "float"; break;
    case TokenId::Float64: os << "double"; break;
    default: os << "int32_t"; break;
    }
    break;
  case FieldType::Struct:
    os << cflat(type)->name;
    break;
  case FieldType::Enum:
    os << cenum(type)->name;
    break;
  default:
    os << "int32_t"; // fallback
    break;
  }
}

void SwiftBuilder::emit_cpp_swift_bridge_header(AstInterfaceDecl* ifs, std::ostream& oh, Context* ctx)
{
  oh << "\n#ifdef NPRPC_SWIFT_BRIDGE\n";
  oh << "// Swift servant bridge for " << ifs->name << "\n";
  oh << "class " << ifs->name << "_SwiftBridge : public I" << ifs->name << "_Servant {\n";
  oh << "  void* swift_servant_;\n";
  oh << "public:\n";
  oh << "  " << ifs->name << "_SwiftBridge(void* swift_servant) : swift_servant_(swift_servant) {}\n\n";
  
  // Override virtual methods
  for (auto& fn : ifs->fns) {
    oh << "  void " << fn->name << "(";
    bool first = true;
    for (auto& arg : fn->args) {
      if (!first) oh << ", ";
      first = false;
      emit_cpp_type(arg->type, oh, ctx);
      if (arg->modifier == ArgumentModifier::Out) oh << "&";
      else if (needs_marshalling(arg->type)) oh << " const&";
      oh << " " << arg->name;
    }
    oh << ") override;\n";
  }
  
  oh << "};\n\n";
  
  // Extern C trampoline declarations
  oh << "extern \"C\" {\n";
  for (auto& fn : ifs->fns) {
    oh << "  void " << fn->name << "_swift_trampoline(void* swift_servant";
    for (auto& arg : fn->args) {
      oh << ", ";
      if (needs_marshalling(arg->type)) {
        // Pass buffer pointer for complex types
        oh << "void*";
      } else {
        emit_cpp_type(arg->type, oh, ctx);
        if (arg->modifier == ArgumentModifier::Out) oh << "*";
      }
      oh << " " << arg->name;
    }
    oh << ");\n";
  }
  oh << "}\n";
  oh << "#endif // NPRPC_SWIFT_BRIDGE\n\n";
}

void SwiftBuilder::emit_cpp_swift_bridge_impl(AstInterfaceDecl* ifs, std::ostream& oc, Context* ctx)
{
  oc << "\n#ifdef NPRPC_SWIFT_BRIDGE\n";
  oc << "// Swift bridge implementation for " << ifs->name << "\n";
  
  for (auto& fn : ifs->fns) {
    oc << "void " << ctx->nm_cur()->to_ts_namespace() << "::" << ifs->name << "_SwiftBridge::" << fn->name << "(";
    bool first = true;
    for (auto& arg : fn->args) {
      if (!first) oc << ", ";
      first = false;
      emit_cpp_type(arg->type, oc, ctx);
      if (arg->modifier == ArgumentModifier::Out) oc << "&";
      else if (needs_marshalling(arg->type)) oc << " const&";
      oc << " " << arg->name;
    }
    oc << ") {\n";
    
    // Allocate buffers for complex types
    for (auto& arg : fn->args) {
      if (needs_marshalling(arg->type)) {
        if (arg->type->id == FieldType::Struct) {
          auto s = cflat(arg->type);
          calc_struct_size_align(s);
          oc << "  alignas(" << s->align << ") std::byte __" << arg->name << "_buf[" << s->size << "];\n";
          if (arg->modifier == ArgumentModifier::In) {
            oc << "  marshal_" << s->name << "(__" << arg->name << "_buf, 0, " << arg->name << ");\n";
          }
        }
      }
    }
    
    oc << "  " << fn->name << "_swift_trampoline(swift_servant_";
    for (auto& arg : fn->args) {
      oc << ", ";
      if (needs_marshalling(arg->type)) {
        oc << "__" << arg->name << "_buf";
      } else {
        if (arg->modifier == ArgumentModifier::Out) oc << "&";
        oc << arg->name;
      }
    }
    oc << ");\n";
    
    // Unmarshal out parameters
    for (auto& arg : fn->args) {
      if (arg->modifier == ArgumentModifier::Out && needs_marshalling(arg->type)) {
        if (arg->type->id == FieldType::Struct) {
          auto s = cflat(arg->type);
          oc << "  " << arg->name << " = unmarshal_" << s->name << "(__" << arg->name << "_buf, 0);\n";
        }
      }
    }
    
    oc << "}\n\n";
  }
  
  oc << "#endif // NPRPC_SWIFT_BRIDGE\n\n";
}

// Generate C++ marshal/unmarshal functions for structs (used by Swift bridge)
void SwiftBuilder::emit_cpp_marshal_functions(AstStructDecl* s, std::ostream& oc, Context* ctx)
{
  calc_struct_size_align(s);
  
  oc << "#ifdef NPRPC_SWIFT_BRIDGE\n";
  oc << "// C++ marshal/unmarshal for Swift bridge\n";
  oc << "static void marshal_" << s->name << "(void* buffer, int offset, const " << s->name << "& data) {\n";
  oc << "  auto* ptr = static_cast<std::byte*>(buffer) + offset;\n";
  
  int current_offset = 0;
  for (auto field : s->fields) {
    auto& f = field;
    switch (f->type->id) {
    case FieldType::Fundamental: {
      const auto token = cft(f->type)->token_id;
      const int size = get_fundamental_size(token);
      const int field_offset = align_offset(size, current_offset, size);
      oc << "  *reinterpret_cast<";
      emit_cpp_type(f->type, oc, ctx);
      oc << "*>(ptr + " << field_offset << ") = data." << f->name << ";\n";
      break;
    }
    case FieldType::Enum: {
      const int size = get_fundamental_size(cenum(f->type)->token_id);
      const int field_offset = align_offset(size, current_offset, size);
      oc << "  *reinterpret_cast<int32_t*>(ptr + " << field_offset << ") = static_cast<int32_t>(data." << f->name << ");\n";
      break;
    }
    case FieldType::Struct: {
      auto nested = cflat(f->type);
      const int field_offset = align_offset(nested->align, current_offset, nested->size);
      oc << "  marshal_" << nested->name << "(buffer, offset + " << field_offset << ", data." << f->name << ");\n";
      break;
    }
    default:
      oc << "  // TODO: marshal " << f->name << " (type " << static_cast<int>(f->type->id) << ")\n";
    }
  }
  
  oc << "}\n\n";
  
  // Unmarshal function
  oc << "static " << s->name << " unmarshal_" << s->name << "(const void* buffer, int offset) {\n";
  oc << "  const auto* ptr = static_cast<const std::byte*>(buffer) + offset;\n";
  oc << "  return " << s->name << "{\n";
  
  current_offset = 0;
  bool first = true;
  for (auto field : s->fields) {
    if (!first) oc << ",\n";
    first = false;
    
    auto& f = field;
    oc << "    /*." << f->name << " = */ ";
    
    switch (f->type->id) {
    case FieldType::Fundamental: {
      const auto token = cft(f->type)->token_id;
      const int size = get_fundamental_size(token);
      const int field_offset = align_offset(size, current_offset, size);
      oc << "*reinterpret_cast<const ";
      emit_cpp_type(f->type, oc, ctx);
      oc << "*>(ptr + " << field_offset << ")";
      break;
    }
    case FieldType::Enum: {
      const int size = get_fundamental_size(cenum(f->type)->token_id);
      const int field_offset = align_offset(size, current_offset, size);
      oc << "static_cast<" << cenum(f->type)->name << ">(*reinterpret_cast<const int32_t*>(ptr + " << field_offset << "))";
      break;
    }
    case FieldType::Struct: {
      auto nested = cflat(f->type);
      const int field_offset = align_offset(nested->align, current_offset, nested->size);
      oc << "unmarshal_" << nested->name << "(buffer, offset + " << field_offset << ")";
      break;
    }
    default:
      oc << "{}/*TODO*/";
    }
  }
  
  oc << "\n  };\n";
  oc << "}\n";
  oc << "#endif // NPRPC_SWIFT_BRIDGE\n\n";
}

} // namespace npidl::builders
