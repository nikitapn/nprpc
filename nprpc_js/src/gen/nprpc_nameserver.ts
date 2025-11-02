import * as NPRPC from '@/index_internal'

const u8enc = new TextEncoder();
const u8dec = new TextDecoder();

export class Nameserver extends NPRPC.ObjectProxy {
  public static get servant_t(): new() => _INameserver_Servant {
    return _INameserver_Servant;
  }


  public async Bind(obj: /*in*/NPRPC.ObjectId, name: /*in*/string): Promise<void> {
    let interface_idx = (arguments.length == 2 ? 0 : arguments[arguments.length - 1]);
    const buf = NPRPC.FlatBuffer.create();
    buf.prepare(216);
    buf.commit(88);
    buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
    buf.write_msg_type(NPRPC.impl.MessageType.Request);
    // Write CallHeader directly
    buf.dv.setUint16(16 + 0, this.data.poa_idx, true);
    buf.dv.setUint8(16 + 2, interface_idx);
    buf.dv.setUint8(16 + 3, 0);
    buf.dv.setBigUint64(16 + 8, this.data.object_id, true);
    marshal_nprpc_nameserver_M1(buf, 32, {_1: obj, _2: name});
    buf.write_len(buf.size - 4);
    await NPRPC.rpc.call(this.endpoint, buf, this.timeout);
    let std_reply = NPRPC.handle_standart_reply(buf);
    if (std_reply != 0) {
      console.log("received an unusual reply for function with no output arguments");
    }
  }
  public async Resolve(name: /*in*/string, obj: /*out*/NPRPC.ref<NPRPC.ObjectProxy>): Promise<boolean/*boolean*/> {
    let interface_idx = (arguments.length == 2 ? 0 : arguments[arguments.length - 1]);
    const buf = NPRPC.FlatBuffer.create();
    buf.prepare(168);
    buf.commit(40);
    buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
    buf.write_msg_type(NPRPC.impl.MessageType.Request);
    // Write CallHeader directly
    buf.dv.setUint16(16 + 0, this.data.poa_idx, true);
    buf.dv.setUint8(16 + 2, interface_idx);
    buf.dv.setUint8(16 + 3, 1);
    buf.dv.setBigUint64(16 + 8, this.data.object_id, true);
    marshal_nprpc_nameserver_M2(buf, 32, {_1: name});
    buf.write_len(buf.size - 4);
    await NPRPC.rpc.call(this.endpoint, buf, this.timeout);
    let std_reply = NPRPC.handle_standart_reply(buf);
    if (std_reply != -1) {
      console.log("received an unusual reply for function with output arguments");
      throw new NPRPC.Exception("Unknown Error");
    }
    const out = unmarshal_nprpc_nameserver_M3(buf, 16);
    obj.value = NPRPC.create_object_from_oid(out._2, this.endpoint);
    return out._1;
  }

  // HTTP Transport (alternative to WebSocket)
  public readonly http = {
    Bind: async (obj: /*in*/NPRPC.ObjectId, name: /*in*/string): Promise<void> => {
      const buf = NPRPC.FlatBuffer.create();
      buf.prepare(216);
      buf.commit(88);
      buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
      buf.write_msg_type(NPRPC.impl.MessageType.Request);
      buf.dv.setUint16(16 + 0, this.data.poa_idx, true);
      buf.dv.setUint8(16 + 2, 0);
      buf.dv.setUint8(16 + 3, 0);
      buf.dv.setBigUint64(16 + 8, this.data.object_id, true);
      marshal_nprpc_nameserver_M1(buf, 32, {_1: obj, _2: name});
      buf.write_len(buf.size - 4);

      const response = await fetch('/rpc', {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        body: buf.array_buffer
      }
);

      if (!response.ok) throw new NPRPC.Exception(`HTTP error: ${response.status}`);
      const response_data = await response.arrayBuffer();
      buf.set_buffer(response_data);

      let std_reply = NPRPC.handle_standart_reply(buf);
      if (std_reply != 0) throw new NPRPC.Exception("Unexpected reply");
    },
    Resolve: async (name: /*in*/string): Promise<{ result: boolean/*boolean*/, obj: NPRPC.ObjectProxy }> => {
      const buf = NPRPC.FlatBuffer.create();
      buf.prepare(168);
      buf.commit(40);
      buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
      buf.write_msg_type(NPRPC.impl.MessageType.Request);
      buf.dv.setUint16(16 + 0, this.data.poa_idx, true);
      buf.dv.setUint8(16 + 2, 0);
      buf.dv.setUint8(16 + 3, 1);
      buf.dv.setBigUint64(16 + 8, this.data.object_id, true);
      marshal_nprpc_nameserver_M2(buf, 32, {_1: name});
      buf.write_len(buf.size - 4);

      const response = await fetch('/rpc', {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        body: buf.array_buffer
      }
);

      if (!response.ok) throw new NPRPC.Exception(`HTTP error: ${response.status}`);
      const response_data = await response.arrayBuffer();
      buf.set_buffer(response_data);

      let std_reply = NPRPC.handle_standart_reply(buf);
      if (std_reply != -1) throw new NPRPC.Exception("Unexpected reply");
      const out = unmarshal_nprpc_nameserver_M3(buf, 16);
      return { result: out._1,  obj: NPRPC.create_object_from_oid(out._2, this.endpoint) };
    }
  };
}
export interface INameserver_Servant
{
  Bind(obj: /*in*/NPRPC.ObjectProxy, name: /*in*/string): void;
  Resolve(name: /*in*/string, obj: /*out*/NPRPC.ref<NPRPC.ObjectId>): boolean/*boolean*/;
}
export class _INameserver_Servant extends NPRPC.ObjectServant {
  public static _get_class(): string { return "nprpc_nameserver/nprpc.Nameserver"; }
  public readonly get_class = () => { return _INameserver_Servant._get_class(); }
  public readonly dispatch = (buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean) => {
    _INameserver_Servant._dispatch(this, buf, remote_endpoint, from_parent);
  }
  static _dispatch(obj: _INameserver_Servant, buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean): void {
    // Read CallHeader directly
    const function_idx = buf.dv.getUint8(16 + 3);
    switch(function_idx) {
      case 0: {
        const ia = unmarshal_nprpc_nameserver_M1(buf, 32);
        (obj as any).Bind(NPRPC.create_object_from_oid(ia._1, remote_endpoint), ia._2);
        NPRPC.make_simple_answer(buf, NPRPC.impl.MessageId.Success);
        break;
      }
      case 1: {
        let _out_2: NPRPC.ObjectId;
        const ia = unmarshal_nprpc_nameserver_M2(buf, 32);
        const obuf = buf;
        obuf.consume(obuf.size);
        obuf.prepare(200);
        obuf.commit(72);
        let __ret_val: boolean/*boolean*/;
        __ret_val = (obj as any).Resolve(ia._1,         _out_2);
        const out_data = {_1: __ret_val, _2: _out_2};
        marshal_nprpc_nameserver_M3(obuf, 16, out_data);
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
  _1: NPRPC.ObjectId;
  _2: string;
}

export function marshal_nprpc_nameserver_M1(buf: NPRPC.FlatBuffer, offset: number, data: nprpc_nameserver_M1): void {
NPRPC.detail.marshal_ObjectId(buf, offset + 0, data._1);
NPRPC.marshal_string(buf, offset + 48, data._2);
}

export function unmarshal_nprpc_nameserver_M1(buf: NPRPC.FlatBuffer, offset: number): nprpc_nameserver_M1 {
const result = {} as nprpc_nameserver_M1;
result._1 = NPRPC.detail.unmarshal_ObjectId(buf, offset + 0);
result._2 = NPRPC.unmarshal_string(buf, offset + 48);
return result;
}

export interface nprpc_nameserver_M2 {
  _1: string;
}

export function marshal_nprpc_nameserver_M2(buf: NPRPC.FlatBuffer, offset: number, data: nprpc_nameserver_M2): void {
NPRPC.marshal_string(buf, offset + 0, data._1);
}

export function unmarshal_nprpc_nameserver_M2(buf: NPRPC.FlatBuffer, offset: number): nprpc_nameserver_M2 {
const result = {} as nprpc_nameserver_M2;
result._1 = NPRPC.unmarshal_string(buf, offset + 0);
return result;
}

export interface nprpc_nameserver_M3 {
  _1: boolean/*boolean*/;
  _2: NPRPC.ObjectId;
}

export function marshal_nprpc_nameserver_M3(buf: NPRPC.FlatBuffer, offset: number, data: nprpc_nameserver_M3): void {
buf.dv.setUint8(offset + 0, data._1 ? 1 : 0);
NPRPC.detail.marshal_ObjectId(buf, offset + 8, data._2);
}

export function unmarshal_nprpc_nameserver_M3(buf: NPRPC.FlatBuffer, offset: number): nprpc_nameserver_M3 {
const result = {} as nprpc_nameserver_M3;
result._1 = buf.dv.getUint8(offset + 0) !== 0;
result._2 = NPRPC.detail.unmarshal_ObjectId(buf, offset + 8);
return result;
}

