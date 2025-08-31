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

export namespace Flat_nprpc_base {
  export class ExceptionCommFailure_Direct extends NPRPC.Flat.Flat {
    public get __ex_id() { return this.buffer.dv.getUint32(this.offset+0,true); }
    public set __ex_id(value: number) { this.buffer.dv.setUint32(this.offset+0,value,true); }
    public get what() {
      const offset = this.offset + 4;
      const n = this.buffer.dv.getUint32(offset + 4, true);
      return n > 0 ? u8dec.decode(new DataView(this.buffer.array_buffer, offset + this.buffer.dv.getUint32(offset, true), n)) : ""
    }
    public set what(str: string) {
      const bytes = u8enc.encode(str);
      const offset = NPRPC.Flat._alloc(this.buffer, this.offset + 4, bytes.length, 1, 1);
      new Uint8Array(this.buffer.array_buffer, offset).set(bytes);
    }
  }
}
export class ExceptionTimeout extends NPRPC.Exception {
  constructor() { super("ExceptionTimeout"); }
}

export namespace Flat_nprpc_base {
  export class ExceptionTimeout_Direct extends NPRPC.Flat.Flat {
    public get __ex_id() { return this.buffer.dv.getUint32(this.offset+0,true); }
    public set __ex_id(value: number) { this.buffer.dv.setUint32(this.offset+0,value,true); }
  }
}
export class ExceptionObjectNotExist extends NPRPC.Exception {
  constructor() { super("ExceptionObjectNotExist"); }
}

export namespace Flat_nprpc_base {
  export class ExceptionObjectNotExist_Direct extends NPRPC.Flat.Flat {
    public get __ex_id() { return this.buffer.dv.getUint32(this.offset+0,true); }
    public set __ex_id(value: number) { this.buffer.dv.setUint32(this.offset+0,value,true); }
  }
}
export class ExceptionUnknownFunctionIndex extends NPRPC.Exception {
  constructor() { super("ExceptionUnknownFunctionIndex"); }
}

export namespace Flat_nprpc_base {
  export class ExceptionUnknownFunctionIndex_Direct extends NPRPC.Flat.Flat {
    public get __ex_id() { return this.buffer.dv.getUint32(this.offset+0,true); }
    public set __ex_id(value: number) { this.buffer.dv.setUint32(this.offset+0,value,true); }
  }
}
export class ExceptionUnknownMessageId extends NPRPC.Exception {
  constructor() { super("ExceptionUnknownMessageId"); }
}

export namespace Flat_nprpc_base {
  export class ExceptionUnknownMessageId_Direct extends NPRPC.Flat.Flat {
    public get __ex_id() { return this.buffer.dv.getUint32(this.offset+0,true); }
    public set __ex_id(value: number) { this.buffer.dv.setUint32(this.offset+0,value,true); }
  }
}
export class ExceptionUnsecuredObject extends NPRPC.Exception {
  constructor(  public class_id: string) { super("ExceptionUnsecuredObject"); }
}

export namespace Flat_nprpc_base {
  export class ExceptionUnsecuredObject_Direct extends NPRPC.Flat.Flat {
    public get __ex_id() { return this.buffer.dv.getUint32(this.offset+0,true); }
    public set __ex_id(value: number) { this.buffer.dv.setUint32(this.offset+0,value,true); }
    public get class_id() {
      const offset = this.offset + 4;
      const n = this.buffer.dv.getUint32(offset + 4, true);
      return n > 0 ? u8dec.decode(new DataView(this.buffer.array_buffer, offset + this.buffer.dv.getUint32(offset, true), n)) : ""
    }
    public set class_id(str: string) {
      const bytes = u8enc.encode(str);
      const offset = NPRPC.Flat._alloc(this.buffer, this.offset + 4, bytes.length, 1, 1);
      new Uint8Array(this.buffer.array_buffer, offset).set(bytes);
    }
  }
}
export class ExceptionBadAccess extends NPRPC.Exception {
  constructor() { super("ExceptionBadAccess"); }
}

export namespace Flat_nprpc_base {
  export class ExceptionBadAccess_Direct extends NPRPC.Flat.Flat {
    public get __ex_id() { return this.buffer.dv.getUint32(this.offset+0,true); }
    public set __ex_id(value: number) { this.buffer.dv.setUint32(this.offset+0,value,true); }
  }
}
export class ExceptionBadInput extends NPRPC.Exception {
  constructor() { super("ExceptionBadInput"); }
}

export namespace Flat_nprpc_base {
  export class ExceptionBadInput_Direct extends NPRPC.Flat.Flat {
    public get __ex_id() { return this.buffer.dv.getUint32(this.offset+0,true); }
    public set __ex_id(value: number) { this.buffer.dv.setUint32(this.offset+0,value,true); }
  }
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

export namespace Flat_nprpc_base {
  export class ObjectIdLocal_Direct extends NPRPC.Flat.Flat {
    public get poa_idx() { return this.buffer.dv.getUint16(this.offset+0,true); }
    public set poa_idx(value: number) { this.buffer.dv.setUint16(this.offset+0,value,true); }
    public get object_id() { return this.buffer.dv.getBigUint64(this.offset+8,true); }
    public set object_id(value: bigint) { this.buffer.dv.setBigUint64(this.offset+8,value,true); }
  }
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

export namespace Flat_nprpc_base {
  export class ObjectId_Direct extends NPRPC.Flat.Flat {
    public get object_id() { return this.buffer.dv.getBigUint64(this.offset+0,true); }
    public set object_id(value: bigint) { this.buffer.dv.setBigUint64(this.offset+0,value,true); }
    public get poa_idx() { return this.buffer.dv.getUint16(this.offset+8,true); }
    public set poa_idx(value: number) { this.buffer.dv.setUint16(this.offset+8,value,true); }
    public get flags() { return this.buffer.dv.getUint16(this.offset+10,true); }
    public set flags(value: number) { this.buffer.dv.setUint16(this.offset+10,value,true); }
    public origin_d() { return new NPRPC.Flat.Array_Direct1_u8(this.buffer, this.offset + 12, 16); }
    public get class_id() {
      const offset = this.offset + 28;
      const n = this.buffer.dv.getUint32(offset + 4, true);
      return n > 0 ? u8dec.decode(new DataView(this.buffer.array_buffer, offset + this.buffer.dv.getUint32(offset, true), n)) : ""
    }
    public set class_id(str: string) {
      const bytes = u8enc.encode(str);
      const offset = NPRPC.Flat._alloc(this.buffer, this.offset + 28, bytes.length, 1, 1);
      new Uint8Array(this.buffer.array_buffer, offset).set(bytes);
    }
    public get urls() {
      const offset = this.offset + 36;
      const n = this.buffer.dv.getUint32(offset + 4, true);
      return n > 0 ? u8dec.decode(new DataView(this.buffer.array_buffer, offset + this.buffer.dv.getUint32(offset, true), n)) : ""
    }
    public set urls(str: string) {
      const bytes = u8enc.encode(str);
      const offset = NPRPC.Flat._alloc(this.buffer, this.offset + 36, bytes.length, 1, 1);
      new Uint8Array(this.buffer.array_buffer, offset).set(bytes);
    }
  }
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

export namespace Flat_nprpc_base {
  export class Header_Direct extends NPRPC.Flat.Flat {
    public get size() { return this.buffer.dv.getUint32(this.offset+0,true); }
    public set size(value: number) { this.buffer.dv.setUint32(this.offset+0,value,true); }
    public get msg_id() { return this.buffer.dv.getUint32(this.offset+4,true); }
    public set msg_id(value: MessageId) { this.buffer.dv.setUint32(this.offset+4,value,true); }
    public get msg_type() { return this.buffer.dv.getUint32(this.offset+8,true); }
    public set msg_type(value: MessageType) { this.buffer.dv.setUint32(this.offset+8,value,true); }
    public get request_id() { return this.buffer.dv.getUint32(this.offset+12,true); }
    public set request_id(value: number) { this.buffer.dv.setUint32(this.offset+12,value,true); }
  }
}
export interface CallHeader {
  poa_idx: poa_idx_t;
  interface_idx: ifs_idx_t;
  function_idx: fn_idx_t;
  object_id: oid_t;
}

export namespace Flat_nprpc_base {
  export class CallHeader_Direct extends NPRPC.Flat.Flat {
    public get poa_idx() { return this.buffer.dv.getUint16(this.offset+0,true); }
    public set poa_idx(value: number) { this.buffer.dv.setUint16(this.offset+0,value,true); }
    public get interface_idx() { return this.buffer.dv.getUint8(this.offset+2); }
    public set interface_idx(value: number) { this.buffer.dv.setUint8(this.offset+2,value); }
    public get function_idx() { return this.buffer.dv.getUint8(this.offset+3); }
    public set function_idx(value: number) { this.buffer.dv.setUint8(this.offset+3,value); }
    public get object_id() { return this.buffer.dv.getBigUint64(this.offset+8,true); }
    public set object_id(value: bigint) { this.buffer.dv.setBigUint64(this.offset+8,value,true); }
  }
}
} // namespace impl


function nprpc_base_throw_exception(buf: NPRPC.FlatBuffer): void { 
  switch( buf.read_exception_number() ) {
    case 0:
    {
      let ex_flat = new Flat_nprpc_base.ExceptionCommFailure_Direct(buf, 16);
      let ex_1: string = '';
      ex_1 = ex_flat.what;
      throw new ExceptionCommFailure(ex_1);
    }
    case 1:
    {
      let ex_flat = new Flat_nprpc_base.ExceptionTimeout_Direct(buf, 16);
      throw new ExceptionTimeout();
    }
    case 2:
    {
      let ex_flat = new Flat_nprpc_base.ExceptionObjectNotExist_Direct(buf, 16);
      throw new ExceptionObjectNotExist();
    }
    case 3:
    {
      let ex_flat = new Flat_nprpc_base.ExceptionUnknownFunctionIndex_Direct(buf, 16);
      throw new ExceptionUnknownFunctionIndex();
    }
    case 4:
    {
      let ex_flat = new Flat_nprpc_base.ExceptionUnknownMessageId_Direct(buf, 16);
      throw new ExceptionUnknownMessageId();
    }
    case 5:
    {
      let ex_flat = new Flat_nprpc_base.ExceptionUnsecuredObject_Direct(buf, 16);
      let ex_1: string = '';
      ex_1 = ex_flat.class_id;
      throw new ExceptionUnsecuredObject(ex_1);
    }
    case 6:
    {
      let ex_flat = new Flat_nprpc_base.ExceptionBadAccess_Direct(buf, 16);
      throw new ExceptionBadAccess();
    }
    case 7:
    {
      let ex_flat = new Flat_nprpc_base.ExceptionBadInput_Direct(buf, 16);
      throw new ExceptionBadInput();
    }
    default:
      throw "unknown rpc exception";
  }
}
export namespace detail.helpers {
  export const assign_from_flat_ObjectId = (src: Flat_nprpc_base.ObjectId_Direct): ObjectId => {
    let dest: ObjectId = {} as ObjectId;
    dest = {} as ObjectId;
    dest.object_id = src.object_id;
    dest.poa_idx = src.poa_idx;
    dest.flags = src.flags;
    {
      dest.origin = src.origin_d().typed_array
    }
    dest.class_id = src.class_id;
    dest.urls = src.urls;
    return dest
  }
  export const assign_from_ts_ObjectId = (dest: Flat_nprpc_base.ObjectId_Direct, src: ObjectId) => {
    dest.object_id = src.object_id;
    dest.poa_idx = src.poa_idx;
    dest.flags = src.flags;
    dest.origin_d().copy_from_typed_array(src.origin); 
    dest.class_id = src.class_id;
    dest.urls = src.urls;
  }
  } // namespace detail.helpers
