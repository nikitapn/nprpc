import * as IdlUtils from '../include/npidl/npidl'

export class ObjectIdLocal_Direct {
  private buffer: IdlUtils.NetBuffer;
  private offset: number;

  constructor(buffer: IdlUtils.NetBuffer, offset: number) {
    this.buffer = buffer;
    this.offset = offset;
  }
  public get poa_idx() { return this.buffer.dv.getUint16(this.offset+0,true); }
  public set poa_idx(value: number) { this.buffer.dv.setUint16(this.offset+0,value,true); }
  public get object_id() { return this.buffer.dv.getUint32(this.offset+4,true); }
  public set object_id(value: number) { this.buffer.dv.setUint32(this.offset+4,value,true); }
}

export class ObjectId_Direct {
  private buffer: IdlUtils.NetBuffer;
  private offset: number;

  constructor(buffer: IdlUtils.NetBuffer, offset: number) {
    this.buffer = buffer;
    this.offset = offset;
  }
  public get ip4() { return this.buffer.dv.getUint32(this.offset+0,true); }
  public set ip4(value: number) { this.buffer.dv.setUint32(this.offset+0,value,true); }
  public get port() { return this.buffer.dv.getUint16(this.offset+4,true); }
  public set port(value: number) { this.buffer.dv.setUint16(this.offset+4,value,true); }
  public get poa_idx() { return this.buffer.dv.getUint16(this.offset+6,true); }
  public set poa_idx(value: number) { this.buffer.dv.setUint16(this.offset+6,value,true); }
  public get object_id() { return this.buffer.dv.getUint32(this.offset+8,true); }
  public set object_id(value: number) { this.buffer.dv.setUint32(this.offset+8,value,true); }
  public get_class_id() {
    let enc = new TextDecoder("utf-8");
    let v_begin = this.offset + 12;
    let data_offset = v_begin + this.buffer.dv.getUint32(v_begin, true);
    let bn = this.buffer.array_buffer.slice(data_offset, data_offset + this.buffer.dv.getUint32(v_begin + 4, true));
    return enc.decode(bn);
  }
  public set_class_id(str: string) {
    let enc = new TextEncoder();
    let bytes = enc.encode(str);
    let len = bytes.length;
    let offset = IdlUtils._alloc(this.buffer, this.offset + 12, len + 4, 1, 4);
    new Uint8Array(this.buffer.array_buffer, offset).set(bytes);
  }
}

export class CallHeader_Direct {
  private buffer: IdlUtils.NetBuffer;
  private offset: number;

  constructor(buffer: IdlUtils.NetBuffer, offset: number) {
    this.buffer = buffer;
    this.offset = offset;
  }
  public get object_id() { return this.buffer.dv.getUint32(this.offset+0,true); }
  public set object_id(value: number) { this.buffer.dv.setUint32(this.offset+0,value,true); }
  public get poa_idx() { return this.buffer.dv.getUint16(this.offset+4,true); }
  public set poa_idx(value: number) { this.buffer.dv.setUint16(this.offset+4,value,true); }
  public get function_idx() { return this.buffer.dv.getUint16(this.offset+6,true); }
  public set function_idx(value: number) { this.buffer.dv.setUint16(this.offset+6,value,true); }
}

