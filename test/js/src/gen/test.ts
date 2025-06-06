import * as NPRPC from 'nprpc'

const u8enc = new TextEncoder();
const u8dec = new TextDecoder();

export interface AAA {
  a: number/*u32*/;
  b: string;
  c: string;
}

export namespace Flat_test {
export class AAA_Direct extends NPRPC.Flat.Flat {
  public get a() { return this.buffer.dv.getUint32(this.offset+0,true); }
  public set a(value: number) { this.buffer.dv.setUint32(this.offset+0,value,true); }
  public get b() {
    const offset = this.offset + 4;
    const n = this.buffer.dv.getUint32(offset + 4, true);
    return n > 0 ? u8dec.decode(new DataView(this.buffer.array_buffer, offset + this.buffer.dv.getUint32(offset, true), n)) : ""
  }
  public set b(str: string) {
    const bytes = u8enc.encode(str);
    const offset = NPRPC.Flat._alloc(this.buffer, this.offset + 4, bytes.length, 1, 1);
    new Uint8Array(this.buffer.array_buffer, offset).set(bytes);
  }
  public get c() {
    const offset = this.offset + 8;
    const n = this.buffer.dv.getUint32(offset + 4, true);
    return n > 0 ? u8dec.decode(new DataView(this.buffer.array_buffer, offset + this.buffer.dv.getUint32(offset, true), n)) : ""
  }
  public set c(str: string) {
    const bytes = u8enc.encode(str);
    const offset = NPRPC.Flat._alloc(this.buffer, this.offset + 8, bytes.length, 1, 1);
    new Uint8Array(this.buffer.array_buffer, offset).set(bytes);
  }
}
} // namespace Flat 
export class TestBasic extends NPRPC.ObjectProxy {
  public static get servant_t(): new() => _ITestBasic_Servant {
    return _ITestBasic_Servant;
  }


  public async ReturnBoolean(): Promise<boolean/*boolean*/> {
    let interface_idx = (arguments.length == 0 ? 0 : arguments[arguments.length - 1]);
    let buf = NPRPC.FlatBuffer.create();
    buf.prepare(32);
    buf.commit(32);
    buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
    buf.write_msg_type(NPRPC.impl.MessageType.Request);
    let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
    __ch.object_id = this.data.object_id;
    __ch.poa_idx = this.data.poa_idx;
    __ch.interface_idx = interface_idx;
    __ch.function_idx = 0;
    buf.write_len(buf.size - 4);
    await NPRPC.rpc.call(
      this.endpoint, buf, this.timeout
    );
    let std_reply = NPRPC.handle_standart_reply(buf);
    if (std_reply != -1) {
      console.log("received an unusual reply for function with output arguments");
      throw new NPRPC.Exception("Unknown Error");
    }
  let out = new Flat_test.test_M1_Direct(buf, 16);
  let __ret_value: boolean/*boolean*/;
  __ret_value = out._1;
  return __ret_value;
}

  public async In(a: /*in*/number, b: /*in*/boolean, c: /*in*/Array<number>): Promise<boolean/*boolean*/> {
    let interface_idx = (arguments.length == 3 ? 0 : arguments[arguments.length - 1]);
    let buf = NPRPC.FlatBuffer.create();
    buf.prepare(176);
    buf.commit(48);
    buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
    buf.write_msg_type(NPRPC.impl.MessageType.Request);
    let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
    __ch.object_id = this.data.object_id;
    __ch.poa_idx = this.data.poa_idx;
    __ch.interface_idx = interface_idx;
    __ch.function_idx = 1;
  let _ = new Flat_test.test_M2_Direct(buf,32);
  _._1 = a;
  _._2 = b;
  _._3(c.length);
  _._3_d().copy_from_ts_array(c); 
    buf.write_len(buf.size - 4);
    await NPRPC.rpc.call(
      this.endpoint, buf, this.timeout
    );
    let std_reply = NPRPC.handle_standart_reply(buf);
    if (std_reply != -1) {
      console.log("received an unusual reply for function with output arguments");
      throw new NPRPC.Exception("Unknown Error");
    }
  let out = new Flat_test.test_M1_Direct(buf, 16);
  let __ret_value: boolean/*boolean*/;
  __ret_value = out._1;
  return __ret_value;
}

  public async Out(a: /*out*/NPRPC.ref<number>, b: /*out*/NPRPC.ref<boolean>, c: /*out*/Array<number>): Promise<void> {
    let interface_idx = (arguments.length == 3 ? 0 : arguments[arguments.length - 1]);
    let buf = NPRPC.FlatBuffer.create();
    buf.prepare(32);
    buf.commit(32);
    buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
    buf.write_msg_type(NPRPC.impl.MessageType.Request);
    let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
    __ch.object_id = this.data.object_id;
    __ch.poa_idx = this.data.poa_idx;
    __ch.interface_idx = interface_idx;
    __ch.function_idx = 2;
    buf.write_len(buf.size - 4);
    await NPRPC.rpc.call(
      this.endpoint, buf, this.timeout
    );
    let std_reply = NPRPC.handle_standart_reply(buf);
    if (std_reply != -1) {
      console.log("received an unusual reply for function with output arguments");
      throw new NPRPC.Exception("Unknown Error");
    }
  let out = new Flat_test.test_M2_Direct(buf, 16);
  a.value = out._1;
  b.value = out._2;
  {
  let vv = out._3_d(), index_0 = 0;
  c.length = vv.elements_size;
  for (let e of vv) {
  c[index_0] = e;
    ++index_0;
  }
  }
}

};

export interface ITestBasic_Servant
{
  ReturnBoolean(): boolean/*boolean*/;
  In(a: number, b: boolean, c: typeof NPRPC.Flat.Vector_Direct1_u8): boolean/*boolean*/;
  Out(a: number, b: boolean, c: typeof NPRPC.Flat.Vector_Direct1_u8): void;
}

export class _ITestBasic_Servant extends NPRPC.ObjectServant {
  public static _get_class(): string { return "test/TestBasic"; }
  public readonly get_class = () => { return _ITestBasic_Servant._get_class(); }
  public readonly dispatch = (buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean) => {
    _ITestBasic_Servant._dispatch(this, buf, remote_endpoint, from_parent);
  }
  static _dispatch(obj: _ITestBasic_Servant, buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean): void {
  let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
  switch(__ch.function_idx) {
    case 0: {
let __ret_val: boolean/*boolean*/;
      __ret_val = (obj as any).ReturnBoolean();
      let obuf = buf;
      obuf.consume(obuf.size);
      obuf.prepare(17);
      obuf.commit(17);
      let oa = new Flat_test.test_M1_Direct(obuf,16);
  oa._1 = __ret_val;
      obuf.write_len(obuf.size - 4);
      obuf.write_msg_id(NPRPC.impl.MessageId.BlockResponse);
      obuf.write_msg_type(NPRPC.impl.MessageType.Answer);
      break;
    }
    case 1: {
      let ia = new Flat_test.test_M2_Direct(buf, 32);
let __ret_val: boolean/*boolean*/;
      __ret_val = (obj as any).In(ia._1, ia._2, ia._3_d());
      let obuf = buf;
      obuf.consume(obuf.size);
      obuf.prepare(17);
      obuf.commit(17);
      let oa = new Flat_test.test_M1_Direct(obuf,16);
  oa._1 = __ret_val;
      obuf.write_len(obuf.size - 4);
      obuf.write_msg_id(NPRPC.impl.MessageId.BlockResponse);
      obuf.write_msg_type(NPRPC.impl.MessageType.Answer);
      break;
    }
    case 2: {
      let obuf = buf;
      obuf.consume(obuf.size);
      obuf.prepare(160);
      obuf.commit(32);
      let oa = new Flat_test.test_M2_Direct(obuf,16);
      (obj as any).Out(oa._1, oa._2, oa._3_d);
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

export class TestOptional extends NPRPC.ObjectProxy {
  public static get servant_t(): new() => _ITestOptional_Servant {
    return _ITestOptional_Servant;
  }


  public async InEmpty(a?: /*in*/number): Promise<boolean/*boolean*/> {
    let interface_idx = (arguments.length == 1 ? 0 : arguments[arguments.length - 1]);
    let buf = NPRPC.FlatBuffer.create();
    buf.prepare(164);
    buf.commit(36);
    buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
    buf.write_msg_type(NPRPC.impl.MessageType.Request);
    let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
    __ch.object_id = this.data.object_id;
    __ch.poa_idx = this.data.poa_idx;
    __ch.interface_idx = interface_idx;
    __ch.function_idx = 0;
  let _ = new Flat_test.test_M3_Direct(buf,32);
  {
    let opt = _._1();
    if (a) {
      opt.alloc();
      opt.value = a
    } else {
      opt.set_nullopt();
    }
  }
    buf.write_len(buf.size - 4);
    await NPRPC.rpc.call(
      this.endpoint, buf, this.timeout
    );
    let std_reply = NPRPC.handle_standart_reply(buf);
    if (std_reply != -1) {
      console.log("received an unusual reply for function with output arguments");
      throw new NPRPC.Exception("Unknown Error");
    }
  let out = new Flat_test.test_M1_Direct(buf, 16);
  let __ret_value: boolean/*boolean*/;
  __ret_value = out._1;
  return __ret_value;
}

  public async In(a?: /*in*/number, b?: /*in*/AAA): Promise<boolean/*boolean*/> {
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
  let _ = new Flat_test.test_M4_Direct(buf,32);
  {
    let opt = _._1();
    if (a) {
      opt.alloc();
      opt.value = a
    } else {
      opt.set_nullopt();
    }
  }
  {
    let opt = _._2();
    if (b) {
      let opt = _._2();
      opt.alloc();
      let value = opt.value;
        value.a = b.a;
  value.b = b.b;
  value.c = b.c;
    } else {
      opt.set_nullopt();
    }
  }
    buf.write_len(buf.size - 4);
    await NPRPC.rpc.call(
      this.endpoint, buf, this.timeout
    );
    let std_reply = NPRPC.handle_standart_reply(buf);
    if (std_reply != -1) {
      console.log("received an unusual reply for function with output arguments");
      throw new NPRPC.Exception("Unknown Error");
    }
  let out = new Flat_test.test_M1_Direct(buf, 16);
  let __ret_value: boolean/*boolean*/;
  __ret_value = out._1;
  return __ret_value;
}

  public async OutEmpty(a?: /*out*/number): Promise<void> {
    let interface_idx = (arguments.length == 1 ? 0 : arguments[arguments.length - 1]);
    let buf = NPRPC.FlatBuffer.create();
    buf.prepare(32);
    buf.commit(32);
    buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
    buf.write_msg_type(NPRPC.impl.MessageType.Request);
    let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
    __ch.object_id = this.data.object_id;
    __ch.poa_idx = this.data.poa_idx;
    __ch.interface_idx = interface_idx;
    __ch.function_idx = 2;
    buf.write_len(buf.size - 4);
    await NPRPC.rpc.call(
      this.endpoint, buf, this.timeout
    );
    let std_reply = NPRPC.handle_standart_reply(buf);
    if (std_reply != -1) {
      console.log("received an unusual reply for function with output arguments");
      throw new NPRPC.Exception("Unknown Error");
    }
  let out = new Flat_test.test_M3_Direct(buf, 16);
  {
    if (out._1.has_value) {
      a = out._1.value
    } else {
      a = null
    }
  }
}

  public async Out(a?: /*out*/number): Promise<void> {
    let interface_idx = (arguments.length == 1 ? 0 : arguments[arguments.length - 1]);
    let buf = NPRPC.FlatBuffer.create();
    buf.prepare(32);
    buf.commit(32);
    buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
    buf.write_msg_type(NPRPC.impl.MessageType.Request);
    let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
    __ch.object_id = this.data.object_id;
    __ch.poa_idx = this.data.poa_idx;
    __ch.interface_idx = interface_idx;
    __ch.function_idx = 3;
    buf.write_len(buf.size - 4);
    await NPRPC.rpc.call(
      this.endpoint, buf, this.timeout
    );
    let std_reply = NPRPC.handle_standart_reply(buf);
    if (std_reply != -1) {
      console.log("received an unusual reply for function with output arguments");
      throw new NPRPC.Exception("Unknown Error");
    }
  let out = new Flat_test.test_M3_Direct(buf, 16);
  {
    if (out._1().has_value) {
      a = out._1().value
    } else {
      a = null
    }
  }
}

};

export interface ITestOptional_Servant
{
  InEmpty(a: typeof NPRPC.Flat.Optional_Direct1_u32): boolean/*boolean*/;
  In(a: typeof NPRPC.Flat.Optional_Direct1_u32, b: NPRPC.Flat.Optional_Direct2<Flat_test.AAA_Direct>): boolean/*boolean*/;
  OutEmpty(a: typeof NPRPC.Flat.Optional_Direct1_u32): void;
  Out(a: typeof NPRPC.Flat.Optional_Direct1_u32): void;
}

export class _ITestOptional_Servant extends NPRPC.ObjectServant {
  public static _get_class(): string { return "test/TestOptional"; }
  public readonly get_class = () => { return _ITestOptional_Servant._get_class(); }
  public readonly dispatch = (buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean) => {
    _ITestOptional_Servant._dispatch(this, buf, remote_endpoint, from_parent);
  }
  static _dispatch(obj: _ITestOptional_Servant, buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean): void {
  let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
  switch(__ch.function_idx) {
    case 0: {
      let ia = new Flat_test.test_M3_Direct(buf, 32);
let __ret_val: boolean/*boolean*/;
      __ret_val = (obj as any).InEmpty(ia._1);
      let obuf = buf;
      obuf.consume(obuf.size);
      obuf.prepare(17);
      obuf.commit(17);
      let oa = new Flat_test.test_M1_Direct(obuf,16);
  oa._1 = __ret_val;
      obuf.write_len(obuf.size - 4);
      obuf.write_msg_id(NPRPC.impl.MessageId.BlockResponse);
      obuf.write_msg_type(NPRPC.impl.MessageType.Answer);
      break;
    }
    case 1: {
      let ia = new Flat_test.test_M4_Direct(buf, 32);
let __ret_val: boolean/*boolean*/;
      __ret_val = (obj as any).In(ia._1, ia._2);
      let obuf = buf;
      obuf.consume(obuf.size);
      obuf.prepare(17);
      obuf.commit(17);
      let oa = new Flat_test.test_M1_Direct(obuf,16);
  oa._1 = __ret_val;
      obuf.write_len(obuf.size - 4);
      obuf.write_msg_id(NPRPC.impl.MessageId.BlockResponse);
      obuf.write_msg_type(NPRPC.impl.MessageType.Answer);
      break;
    }
    case 2: {
      let obuf = buf;
      obuf.consume(obuf.size);
      obuf.prepare(148);
      obuf.commit(20);
      let oa = new Flat_test.test_M3_Direct(obuf,16);
      (obj as any).OutEmpty(oa._1);
      obuf.write_len(obuf.size - 4);
      obuf.write_msg_id(NPRPC.impl.MessageId.BlockResponse);
      obuf.write_msg_type(NPRPC.impl.MessageType.Answer);
      break;
    }
    case 3: {
      let obuf = buf;
      obuf.consume(obuf.size);
      obuf.prepare(148);
      obuf.commit(20);
      let oa = new Flat_test.test_M3_Direct(obuf,16);
      (obj as any).Out(oa._1);
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

export interface CCC {
  a: string;
  b: string;
  c?: boolean/*boolean*/;
}

export namespace Flat_test {
export class CCC_Direct extends NPRPC.Flat.Flat {
  public get a() {
    const offset = this.offset + 0;
    const n = this.buffer.dv.getUint32(offset + 4, true);
    return n > 0 ? u8dec.decode(new DataView(this.buffer.array_buffer, offset + this.buffer.dv.getUint32(offset, true), n)) : ""
  }
  public set a(str: string) {
    const bytes = u8enc.encode(str);
    const offset = NPRPC.Flat._alloc(this.buffer, this.offset + 0, bytes.length, 1, 1);
    new Uint8Array(this.buffer.array_buffer, offset).set(bytes);
  }
  public get b() {
    const offset = this.offset + 4;
    const n = this.buffer.dv.getUint32(offset + 4, true);
    return n > 0 ? u8dec.decode(new DataView(this.buffer.array_buffer, offset + this.buffer.dv.getUint32(offset, true), n)) : ""
  }
  public set b(str: string) {
    const bytes = u8enc.encode(str);
    const offset = NPRPC.Flat._alloc(this.buffer, this.offset + 4, bytes.length, 1, 1);
    new Uint8Array(this.buffer.array_buffer, offset).set(bytes);
  }
  public c() {
    return new NPRPC.Flat.Optional_Direct1_boolean(this.buffer, this.offset + 8, 1, 1);
  }
}
} // namespace Flat 
export interface BBB {
  a: Array<AAA>;
  b: Array<CCC>;
}

export namespace Flat_test {
export class BBB_Direct extends NPRPC.Flat.Flat {
  public a(elements_size: number): void { 
    NPRPC.Flat._alloc(this.buffer, this.offset + 0, elements_size, 20, 4);
  }
  public a_d() { return new NPRPC.Flat.Vector_Direct2<Flat_test.AAA_Direct>(this.buffer, this.offset + 0, 20, Flat_test.AAA_Direct); }
  public b(elements_size: number): void { 
    NPRPC.Flat._alloc(this.buffer, this.offset + 8, elements_size, 20, 4);
  }
  public b_d() { return new NPRPC.Flat.Vector_Direct2<Flat_test.CCC_Direct>(this.buffer, this.offset + 8, 20, Flat_test.CCC_Direct); }
}
} // namespace Flat 
export class TestNested extends NPRPC.ObjectProxy {
  public static get servant_t(): new() => _ITestNested_Servant {
    return _ITestNested_Servant;
  }


  public async Out(a?: /*out*/BBB): Promise<void> {
    let interface_idx = (arguments.length == 1 ? 0 : arguments[arguments.length - 1]);
    let buf = NPRPC.FlatBuffer.create();
    buf.prepare(32);
    buf.commit(32);
    buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
    buf.write_msg_type(NPRPC.impl.MessageType.Request);
    let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
    __ch.object_id = this.data.object_id;
    __ch.poa_idx = this.data.poa_idx;
    __ch.interface_idx = interface_idx;
    __ch.function_idx = 0;
    buf.write_len(buf.size - 4);
    await NPRPC.rpc.call(
      this.endpoint, buf, this.timeout
    );
    let std_reply = NPRPC.handle_standart_reply(buf);
    if (std_reply != -1) {
      console.log("received an unusual reply for function with output arguments");
      throw new NPRPC.Exception("Unknown Error");
    }
  let out = new Flat_test.test_M5_Direct(buf, 16);
  {
    let opt = out._1();
    if (opt.has_value) {
      let value = opt.value;
        (a as any) = new Object();
  {
  let vv = value.a_d(), index_1 = 0;
  (a.a as Array<any>) = new Array<any>(vv.elements_size)
  for (let e of vv) {
    (a.a[index_1] as any) = new Object();
  (a.a[index_1] as any) = new Object();
  a.a[index_1].a = e.a;
  a.a[index_1].b = e.b;
  a.a[index_1].c = e.c;
    ++index_1;
  }
  }
  {
  let vv = value.b_d(), index_2 = 0;
  (a.b as Array<any>) = new Array<any>(vv.elements_size)
  for (let e of vv) {
    (a.b[index_2] as any) = new Object();
  (a.b[index_2] as any) = new Object();
  a.b[index_2].a = e.a;
  a.b[index_2].b = e.b;
  {
    if (e.c.has_value) {
      a.b[index_2].c = e.c.value
    } else {
      a.b[index_2].c = null
    }
  }
    ++index_2;
  }
  }
    } else {
      a = null
    }
  }
}

};

export interface ITestNested_Servant
{
  Out(a: NPRPC.Flat.Optional_Direct2<Flat_test.BBB_Direct>): void;
}

export class _ITestNested_Servant extends NPRPC.ObjectServant {
  public static _get_class(): string { return "test/TestNested"; }
  public readonly get_class = () => { return _ITestNested_Servant._get_class(); }
  public readonly dispatch = (buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean) => {
    _ITestNested_Servant._dispatch(this, buf, remote_endpoint, from_parent);
  }
  static _dispatch(obj: _ITestNested_Servant, buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean): void {
  let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
  switch(__ch.function_idx) {
    case 0: {
      let obuf = buf;
      obuf.consume(obuf.size);
      obuf.prepare(148);
      obuf.commit(20);
      let oa = new Flat_test.test_M5_Direct(obuf,16);
      (obj as any).Out(oa._1);
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

export class TestBadInput extends NPRPC.ObjectProxy {
  public static get servant_t(): new() => _ITestBadInput_Servant {
    return _ITestBadInput_Servant;
  }


  public async In(a: /*in*/Array<number>): Promise<void> {
    let interface_idx = (arguments.length == 1 ? 0 : arguments[arguments.length - 1]);
    let buf = NPRPC.FlatBuffer.create();
    buf.prepare(168);
    buf.commit(40);
    buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
    buf.write_msg_type(NPRPC.impl.MessageType.Request);
    let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
    __ch.object_id = this.data.object_id;
    __ch.poa_idx = this.data.poa_idx;
    __ch.interface_idx = interface_idx;
    __ch.function_idx = 0;
  let _ = new Flat_test.test_M6_Direct(buf,32);
  _._1(a.length);
  _._1_d().copy_from_ts_array(a); 
    buf.write_len(buf.size - 4);
    await NPRPC.rpc.call(
      this.endpoint, buf, this.timeout
    );
    let std_reply = NPRPC.handle_standart_reply(buf);
    if (std_reply != 0) {
      console.log("received an unusual reply for function with no output arguments");
    }
}

};

export interface ITestBadInput_Servant
{
  In(a: typeof NPRPC.Flat.Vector_Direct1_u8): void;
}

export class _ITestBadInput_Servant extends NPRPC.ObjectServant {
  public static _get_class(): string { return "test/TestBadInput"; }
  public readonly get_class = () => { return _ITestBadInput_Servant._get_class(); }
  public readonly dispatch = (buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean) => {
    _ITestBadInput_Servant._dispatch(this, buf, remote_endpoint, from_parent);
  }
  static _dispatch(obj: _ITestBadInput_Servant, buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean): void {
  let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
  switch(__ch.function_idx) {
    case 0: {
      let ia = new Flat_test.test_M6_Direct(buf, 32);
      (obj as any).In(ia._1_d());
      NPRPC.make_simple_answer(buf, NPRPC.impl.MessageId.Success);
      break;
    }
    default:
      NPRPC.make_simple_answer(buf, NPRPC.impl.MessageId.Error_UnknownFunctionIdx);
  }
  }
}

export interface test_M1 {
  _1: boolean/*boolean*/;
}

export namespace Flat_test {
export class test_M1_Direct extends NPRPC.Flat.Flat {
  public get _1() { return (this.buffer.dv.getUint8(this.offset+0) === 0x01); }
  public set _1(value: boolean) { this.buffer.dv.setUint8(this.offset+0, value === true ? 0x01 : 0x00); }
}
} // namespace Flat 
export interface test_M2 {
  _1: number/*u32*/;
  _2: boolean/*boolean*/;
  _3: Array<number/*u8*/>;
}

export namespace Flat_test {
export class test_M2_Direct extends NPRPC.Flat.Flat {
  public get _1() { return this.buffer.dv.getUint32(this.offset+0,true); }
  public set _1(value: number) { this.buffer.dv.setUint32(this.offset+0,value,true); }
  public get _2() { return (this.buffer.dv.getUint8(this.offset+4) === 0x01); }
  public set _2(value: boolean) { this.buffer.dv.setUint8(this.offset+4, value === true ? 0x01 : 0x00); }
  public _3(elements_size: number): void { 
    NPRPC.Flat._alloc(this.buffer, this.offset + 8, elements_size, 1, 1);
  }
  public _3_d() { return new NPRPC.Flat.Vector_Direct1_u8(this.buffer, this.offset + 8); }
}
} // namespace Flat 
export interface test_M3 {
  _1?: number/*u32*/;
}

export namespace Flat_test {
export class test_M3_Direct extends NPRPC.Flat.Flat {
  public _1() {
    return new NPRPC.Flat.Optional_Direct1_u32(this.buffer, this.offset + 0, 4, 4);
  }
}
} // namespace Flat 
export interface test_M4 {
  _1?: number/*u32*/;
  _2?: AAA;
}

export namespace Flat_test {
export class test_M4_Direct extends NPRPC.Flat.Flat {
  public _1() {
    return new NPRPC.Flat.Optional_Direct1_u32(this.buffer, this.offset + 0, 4, 4);
  }
  public _2() {
    return new NPRPC.Flat.Optional_Direct2<Flat_test.AAA_Direct>(this.buffer, this.offset + 4, 20, 4, Flat_test.AAA_Direct);
  }
}
} // namespace Flat 
export interface test_M5 {
  _1?: BBB;
}

export namespace Flat_test {
export class test_M5_Direct extends NPRPC.Flat.Flat {
  public _1() {
    return new NPRPC.Flat.Optional_Direct2<Flat_test.BBB_Direct>(this.buffer, this.offset + 0, 16, 4, Flat_test.BBB_Direct);
  }
}
} // namespace Flat 
export interface test_M6 {
  _1: Array<number/*u8*/>;
}

export namespace Flat_test {
export class test_M6_Direct extends NPRPC.Flat.Flat {
  public _1(elements_size: number): void { 
    NPRPC.Flat._alloc(this.buffer, this.offset + 0, elements_size, 1, 1);
  }
  public _1_d() { return new NPRPC.Flat.Vector_Direct1_u8(this.buffer, this.offset + 0); }
}
} // namespace Flat 
