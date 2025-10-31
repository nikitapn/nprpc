import * as NPRPC from '@/base'

const u8enc = new TextEncoder();
const u8dec = new TextDecoder();

export type oid_t = bigint/*u64*/;
export type poa_idx_t = number/*u16*/;
export type oflags_t = number/*u16*/;
export type uuid_t = Uint8Array;
export type ifs_idx_t = number/*u8*/;
export type fn_idx_t = number/*u8*/;
export class ExceptionCommFailure extends NPRPC.Exception {
  constructor(  public what: string) { super("ExceptionCommFailure"); }
}

export function unmarshal_ExceptionCommFailure(buf: NPRPC.FlatBuffer, offset: number): ExceptionCommFailure {
const result = {} as ExceptionCommFailure;
result.what = NPRPC.unmarshal_string(buf, offset + 0);
return result;
}

export class ExceptionTimeout extends NPRPC.Exception {
  constructor() { super("ExceptionTimeout"); }
}

export function unmarshal_ExceptionTimeout(buf: NPRPC.FlatBuffer, offset: number): ExceptionTimeout {
const result = {} as ExceptionTimeout;
return result;
}

export class ExceptionObjectNotExist extends NPRPC.Exception {
  constructor() { super("ExceptionObjectNotExist"); }
}

export function unmarshal_ExceptionObjectNotExist(buf: NPRPC.FlatBuffer, offset: number): ExceptionObjectNotExist {
const result = {} as ExceptionObjectNotExist;
return result;
}

export class ExceptionUnknownFunctionIndex extends NPRPC.Exception {
  constructor() { super("ExceptionUnknownFunctionIndex"); }
}

export function unmarshal_ExceptionUnknownFunctionIndex(buf: NPRPC.FlatBuffer, offset: number): ExceptionUnknownFunctionIndex {
const result = {} as ExceptionUnknownFunctionIndex;
return result;
}

export class ExceptionUnknownMessageId extends NPRPC.Exception {
  constructor() { super("ExceptionUnknownMessageId"); }
}

export function unmarshal_ExceptionUnknownMessageId(buf: NPRPC.FlatBuffer, offset: number): ExceptionUnknownMessageId {
const result = {} as ExceptionUnknownMessageId;
return result;
}

export class ExceptionUnsecuredObject extends NPRPC.Exception {
  constructor(  public class_id: string) { super("ExceptionUnsecuredObject"); }
}

export function unmarshal_ExceptionUnsecuredObject(buf: NPRPC.FlatBuffer, offset: number): ExceptionUnsecuredObject {
const result = {} as ExceptionUnsecuredObject;
result.class_id = NPRPC.unmarshal_string(buf, offset + 0);
return result;
}

export class ExceptionBadAccess extends NPRPC.Exception {
  constructor() { super("ExceptionBadAccess"); }
}

export function unmarshal_ExceptionBadAccess(buf: NPRPC.FlatBuffer, offset: number): ExceptionBadAccess {
const result = {} as ExceptionBadAccess;
return result;
}

export class ExceptionBadInput extends NPRPC.Exception {
  constructor() { super("ExceptionBadInput"); }
}

export function unmarshal_ExceptionBadInput(buf: NPRPC.FlatBuffer, offset: number): ExceptionBadInput {
const result = {} as ExceptionBadInput;
return result;
}

export enum DebugLevel { //u32
  DebugLevel_Critical,
  DebugLevel_InactiveTimeout,
  DebugLevel_EveryCall,
  DebugLevel_EveryMessageContent,
  DebugLevel_TraceAll
}

export enum EndPointType { //u32
  Tcp,
  TcpTethered,
  WebSocket,
  SecuredWebSocket,
  SharedMemory
}

export namespace detail { 
export interface ObjectIdLocal {
  poa_idx: poa_idx_t;
  object_id: oid_t;
}

export function marshal_ObjectIdLocal(buf: NPRPC.FlatBuffer, offset: number, data: ObjectIdLocal): void {
buf.dv.setUint16(offset + 0, data.poa_idx, true);
buf.dv.setBigUint64(offset + 8, data.object_id, true);
}

export function unmarshal_ObjectIdLocal(buf: NPRPC.FlatBuffer, offset: number): ObjectIdLocal {
const result = {} as ObjectIdLocal;
result.poa_idx = buf.dv.getUint16(offset + 0, true);
result.object_id = buf.dv.getBigUint64(offset + 8, true);
return result;
}

export enum ObjectFlag { //u32
  Persistent = 1,
  Tethered = 2
}

export interface ObjectId {
  object_id: oid_t;
  poa_idx: poa_idx_t;
  flags: oflags_t;
  origin: uuid_t;
  class_id: string;
  urls: string;
}

export function marshal_ObjectId(buf: NPRPC.FlatBuffer, offset: number, data: ObjectId): void {
buf.dv.setBigUint64(offset + 0, data.object_id, true);
buf.dv.setUint16(offset + 8, data.poa_idx, true);
buf.dv.setUint16(offset + 10, data.flags, true);
const __arr = new Uint8Array(buf.array_buffer, offset + 12, 16);
__arr.set(data.origin);
NPRPC.marshal_string(buf, offset + 28, data.class_id);
NPRPC.marshal_string(buf, offset + 36, data.urls);
}

export function unmarshal_ObjectId(buf: NPRPC.FlatBuffer, offset: number): ObjectId {
const result = {} as ObjectId;
result.object_id = buf.dv.getBigUint64(offset + 0, true);
result.poa_idx = buf.dv.getUint16(offset + 8, true);
result.flags = buf.dv.getUint16(offset + 10, true);
result.origin = new Uint8Array(buf.array_buffer, offset + 12, 16);
result.class_id = NPRPC.unmarshal_string(buf, offset + 28);
result.urls = NPRPC.unmarshal_string(buf, offset + 36);
return result;
}

} // namespace detail

export namespace impl { 
  export enum MessageId { //u32
    FunctionCall = 0,
    BlockResponse,
    AddReference,
    ReleaseObject,
    Success,
    Exception,
    Error_PoaNotExist,
    Error_ObjectNotExist,
    Error_CommFailure,
    Error_UnknownFunctionIdx,
    Error_UnknownMessageId,
    Error_BadAccess,
    Error_BadInput
  }

  export enum MessageType { //u32
    Request = 0,
    Answer
  }

export interface Header {
  size: number/*u32*/;
  msg_id: MessageId;
  msg_type: MessageType;
  request_id: number/*u32*/;
}

export function marshal_Header(buf: NPRPC.FlatBuffer, offset: number, data: Header): void {
buf.dv.setUint32(offset + 0, data.size, true);
buf.dv.setInt32(offset + 4, data.msg_id, true);
buf.dv.setInt32(offset + 8, data.msg_type, true);
buf.dv.setUint32(offset + 12, data.request_id, true);
}

export function unmarshal_Header(buf: NPRPC.FlatBuffer, offset: number): Header {
const result = {} as Header;
result.size = buf.dv.getUint32(offset + 0, true);
result.msg_id = buf.dv.getInt32(offset + 4, true);
result.msg_type = buf.dv.getInt32(offset + 8, true);
result.request_id = buf.dv.getUint32(offset + 12, true);
return result;
}

export interface CallHeader {
  poa_idx: poa_idx_t;
  interface_idx: ifs_idx_t;
  function_idx: fn_idx_t;
  object_id: oid_t;
}

export function marshal_CallHeader(buf: NPRPC.FlatBuffer, offset: number, data: CallHeader): void {
buf.dv.setUint16(offset + 0, data.poa_idx, true);
buf.dv.setUint8(offset + 2, data.interface_idx);
buf.dv.setUint8(offset + 3, data.function_idx);
buf.dv.setBigUint64(offset + 8, data.object_id, true);
}

export function unmarshal_CallHeader(buf: NPRPC.FlatBuffer, offset: number): CallHeader {
const result = {} as CallHeader;
result.poa_idx = buf.dv.getUint16(offset + 0, true);
result.interface_idx = buf.dv.getUint8(offset + 2);
result.function_idx = buf.dv.getUint8(offset + 3);
result.object_id = buf.dv.getBigUint64(offset + 8, true);
return result;
}

} // namespace impl


function nprpc_base_throw_exception(buf: NPRPC.FlatBuffer): void { 
  switch( buf.read_exception_number() ) {
    case 0:
    {
      let ex_obj = unmarshal_ExceptionCommFailure(buf, 16);
      throw new ExceptionCommFailure(ex_obj.what);
    }
    case 1:
    {
      throw new ExceptionTimeout();
    }
    case 2:
    {
      throw new ExceptionObjectNotExist();
    }
    case 3:
    {
      throw new ExceptionUnknownFunctionIndex();
    }
    case 4:
    {
      throw new ExceptionUnknownMessageId();
    }
    case 5:
    {
      let ex_obj = unmarshal_ExceptionUnsecuredObject(buf, 16);
      throw new ExceptionUnsecuredObject(ex_obj.class_id);
    }
    case 6:
    {
      throw new ExceptionBadAccess();
    }
    case 7:
    {
      throw new ExceptionBadInput();
    }
    default:
      throw "unknown rpc exception";
  }
}
