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
buf.heap.HEAPU16[(offset + 0) >> 1] = data.poa_idx;
buf.heap.HEAPU64[(offset + 8) >> 3] = data.object_id;
}

export function unmarshal_ObjectIdLocal(buf: NPRPC.FlatBuffer, offset: number): ObjectIdLocal {
const result = {} as ObjectIdLocal;
result.poa_idx = buf.heap.HEAPU16[(offset + 0) >> 1];
result.object_id = buf.heap.HEAPU64[(offset + 8) >> 3];
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
buf.heap.HEAPU64[(offset + 0) >> 3] = data.object_id;
buf.heap.HEAPU16[(offset + 8) >> 1] = data.poa_idx;
buf.heap.HEAPU16[(offset + 10) >> 1] = data.flags;
NPRPC.marshal_typed_array(buf, offset + 12, data.origin, 1, 1);
NPRPC.marshal_string(buf, offset + 28, data.class_id);
NPRPC.marshal_string(buf, offset + 36, data.urls);
}

export function unmarshal_ObjectId(buf: NPRPC.FlatBuffer, offset: number): ObjectId {
const result = {} as ObjectId;
result.object_id = buf.heap.HEAPU64[(offset + 0) >> 3];
result.poa_idx = buf.heap.HEAPU16[(offset + 8) >> 1];
result.flags = buf.heap.HEAPU16[(offset + 10) >> 1];
result.origin = NPRPC.unmarshal_typed_array(buf, offset + 12, 1) as Uint8Array;
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
buf.heap.HEAPU32[(offset + 0) >> 2] = data.size;
buf.heap.HEAP32[(offset + 4) >> 2] = data.msg_id;
buf.heap.HEAP32[(offset + 8) >> 2] = data.msg_type;
buf.heap.HEAPU32[(offset + 12) >> 2] = data.request_id;
}

export function unmarshal_Header(buf: NPRPC.FlatBuffer, offset: number): Header {
const result = {} as Header;
result.size = buf.heap.HEAPU32[(offset + 0) >> 2];
result.msg_id = buf.heap.HEAP32[(offset + 4) >> 2];
result.msg_type = buf.heap.HEAP32[(offset + 8) >> 2];
result.request_id = buf.heap.HEAPU32[(offset + 12) >> 2];
return result;
}

export interface CallHeader {
  poa_idx: poa_idx_t;
  interface_idx: ifs_idx_t;
  function_idx: fn_idx_t;
  object_id: oid_t;
}

export function marshal_CallHeader(buf: NPRPC.FlatBuffer, offset: number, data: CallHeader): void {
buf.heap.HEAPU16[(offset + 0) >> 1] = data.poa_idx;
buf.heap.HEAPU8[offset + 2] = data.interface_idx;
buf.heap.HEAPU8[offset + 3] = data.function_idx;
buf.heap.HEAPU64[(offset + 8) >> 3] = data.object_id;
}

export function unmarshal_CallHeader(buf: NPRPC.FlatBuffer, offset: number): CallHeader {
const result = {} as CallHeader;
result.poa_idx = buf.heap.HEAPU16[(offset + 0) >> 1];
result.interface_idx = buf.heap.HEAPU8[offset + 2];
result.function_idx = buf.heap.HEAPU8[offset + 3];
result.object_id = buf.heap.HEAPU64[(offset + 8) >> 3];
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
