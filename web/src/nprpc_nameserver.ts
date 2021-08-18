import * as NPRPC from './nprpc'

export class Nameserver extends NPRPC.ObjectProxy {
  public static get servant_t(): new() => _INameserver_Servant {
    return _INameserver_Servant;
  }


  public async Bind(obj: /*in*/NPRPC.detail.ObjectId, name: /*in*/string): Promise<void> {
    let interface_idx = (arguments.length == 2 ? 0 : arguments[arguments.length - 1]);
    let buf = NPRPC.FlatBuffer.create();
    buf.prepare(200);
    buf.commit(72);
    buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
    buf.write_msg_type(NPRPC.impl.MessageType.Request);
    let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
    __ch.object_id = this.data.object_id;
    __ch.poa_idx = this.data.poa_idx;
    __ch.interface_idx = interface_idx;
    __ch.function_idx = 0;
  let _ = new Flat_nprpc_nameserver.nprpc_nameserver_M1_Direct(buf,32);
  _._1.object_id = obj.object_id;
  _._1.ip4 = obj.ip4;
  _._1.port = obj.port;
  _._1.websocket_port = obj.websocket_port;
  _._1.poa_idx = obj.poa_idx;
  _._1.flags = obj.flags;
  _._1.class_id = obj.class_id;
  _._2 = name;
    buf.write_len(buf.size - 4);
    await NPRPC.rpc.call(
      {ip4: this.data.ip4, port: this.data.websocket_port}, buf, this.timeout
    );
    let std_reply = NPRPC.handle_standart_reply(buf);
    if (std_reply != 0) {
      console.log("received an unusual reply for function with no output arguments");
    }
}

  public async Resolve(name: /*in*/string, obj: /*out*/NPRPC.ref<NPRPC.ObjectProxy>): Promise<boolean/*boolean*/> {
    let interface_idx = (arguments.length == 2 ? 0 : arguments[arguments.length - 1]);
    let buf = NPRPC.FlatBuffer.create();
    buf.prepare(168);
    buf.commit(40);
    buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
    buf.write_msg_type(NPRPC.impl.MessageType.Request);
    let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
    __ch.object_id = this.data.object_id;
    __ch.poa_idx = this.data.poa_idx;
    __ch.interface_idx = interface_idx;
    __ch.function_idx = 1;
  let _ = new Flat_nprpc_nameserver.nprpc_nameserver_M2_Direct(buf,32);
  _._1 = name;
    buf.write_len(buf.size - 4);
    await NPRPC.rpc.call(
      {ip4: this.data.ip4, port: this.data.websocket_port}, buf, this.timeout
    );
    let std_reply = NPRPC.handle_standart_reply(buf);
    if (std_reply != -1) {
      console.log("received an unusual reply for function with output arguments");
      throw new NPRPC.Exception("Unknown Error");
    }
  let out = new Flat_nprpc_nameserver.nprpc_nameserver_M3_Direct(buf, 16);
  obj.value = NPRPC.create_object_from_flat(out._2, this.data.ip4);
  let __ret_value: boolean/*boolean*/;
  __ret_value = out._1;
  return __ret_value;
}

};

export interface INameserver_Servant
{
  Bind(obj: NPRPC.ObjectProxy, name: string): void;
  Resolve(name: string, obj: NPRPC.detail.Flat_nprpc_base.ObjectId_Direct): boolean/*boolean*/;
}

export class _INameserver_Servant extends NPRPC.ObjectServant {
  public static _get_class(): string { return "nprpc_nameserver/nprpc.Nameserver"; }
  public readonly get_class = () => { return _INameserver_Servant._get_class(); }
  public readonly dispatch = (buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean) => {
    _INameserver_Servant._dispatch(this, buf, remote_endpoint, from_parent);
  }
  static _dispatch(obj: _INameserver_Servant, buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean): void {
  let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
  switch(__ch.function_idx) {
    case 0: {
      let ia = new Flat_nprpc_nameserver.nprpc_nameserver_M1_Direct(buf, 32);
      (obj as any).Bind(NPRPC.create_object_from_flat(ia._1, remote_endpoint.ip4), ia._2);
      NPRPC.make_simple_answer(buf, NPRPC.impl.MessageId.Success);
      break;
    }
    case 1: {
      let ia = new Flat_nprpc_nameserver.nprpc_nameserver_M2_Direct(buf, 32);
      let obuf = buf;
      obuf.consume(obuf.size);
      obuf.prepare(184);
      obuf.commit(56);
      let oa = new Flat_nprpc_nameserver.nprpc_nameserver_M3_Direct(obuf,16);
let __ret_val: boolean/*boolean*/;
      __ret_val = (obj as any).Resolve(ia._1, oa._2);
  oa._1 = __ret_val;
      obuf.write_len(obuf.size - 4);
      obuf.write_msg_id(NPRPC.impl.MessageId.BlockResponse);
      obuf.write_msg_type(NPRPC.impl.MessageType.Answer);
      break;
    }
    default:
      NPRPC.make_simple_answer(buf, NPRPC.impl.MessageId.Error_UnknownFunctionIdx);
  }
  }
}

export interface nprpc_nameserver_M1 {
  _1: NPRPC.detail.ObjectId;
  _2: string;
}

export namespace Flat_nprpc_nameserver {
export class nprpc_nameserver_M1_Direct extends NPRPC.Flat.Flat {
  public get _1() { return new NPRPC.detail.Flat_nprpc_base.ObjectId_Direct(this.buffer, this.offset + 0); }
  public get _2() {
    let enc = new TextDecoder("utf-8");
    let v_begin = this.offset + 32;
    let data_offset = v_begin + this.buffer.dv.getUint32(v_begin, true);
    let bn = this.buffer.array_buffer.slice(data_offset, data_offset + this.buffer.dv.getUint32(v_begin + 4, true));
    return enc.decode(bn);
  }
  public set _2(str: string) {
    let enc = new TextEncoder();
    let bytes = enc.encode(str);
    let len = bytes.length;
    let offset = NPRPC.Flat._alloc(this.buffer, this.offset + 32, len, 1, 1);
    new Uint8Array(this.buffer.array_buffer, offset).set(bytes);
  }
}
} // namespace Flat 
export interface nprpc_nameserver_M2 {
  _1: string;
}

export namespace Flat_nprpc_nameserver {
export class nprpc_nameserver_M2_Direct extends NPRPC.Flat.Flat {
  public get _1() {
    let enc = new TextDecoder("utf-8");
    let v_begin = this.offset + 0;
    let data_offset = v_begin + this.buffer.dv.getUint32(v_begin, true);
    let bn = this.buffer.array_buffer.slice(data_offset, data_offset + this.buffer.dv.getUint32(v_begin + 4, true));
    return enc.decode(bn);
  }
  public set _1(str: string) {
    let enc = new TextEncoder();
    let bytes = enc.encode(str);
    let len = bytes.length;
    let offset = NPRPC.Flat._alloc(this.buffer, this.offset + 0, len, 1, 1);
    new Uint8Array(this.buffer.array_buffer, offset).set(bytes);
  }
}
} // namespace Flat 
export interface nprpc_nameserver_M3 {
  _1: boolean/*boolean*/;
  _2: NPRPC.detail.ObjectId;
}

export namespace Flat_nprpc_nameserver {
export class nprpc_nameserver_M3_Direct extends NPRPC.Flat.Flat {
  public get _1() { return (this.buffer.dv.getUint8(this.offset+0) === 0x01); }
  public set _1(value: boolean) { this.buffer.dv.setUint8(this.offset+0, value === true ? 0x01 : 0x00); }
  public get _2() { return new NPRPC.detail.Flat_nprpc_base.ObjectId_Direct(this.buffer, this.offset + 8); }
}
} // namespace Flat 
