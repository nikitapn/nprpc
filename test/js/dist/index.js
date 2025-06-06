var __awaiter = (this && this.__awaiter) || function (thisArg, _arguments, P, generator) {
    function adopt(value) { return value instanceof P ? value : new P(function (resolve) { resolve(value); }); }
    return new (P || (P = Promise))(function (resolve, reject) {
        function fulfilled(value) { try { step(generator.next(value)); } catch (e) { reject(e); } }
        function rejected(value) { try { step(generator["throw"](value)); } catch (e) { reject(e); } }
        function step(result) { result.done ? resolve(result.value) : adopt(result.value).then(fulfilled, rejected); }
        step((generator = generator.apply(thisArg, _arguments || [])).next());
    });
};
System.register("my_modules/nprpc", ["../../../../nprpc_js/dist"], function (exports_1, context_1) {
    "use strict";
    var __moduleName = context_1 && context_1.id;
    function exportStar_1(m) {
        var exports = {};
        for (var n in m) {
            if (n !== "default") exports[n] = m[n];
        }
        exports_1(exports);
    }
    return {
        setters: [
            function (dist_1_1) {
                exportStar_1(dist_1_1);
            }
        ],
        execute: function () {
        }
    };
});
System.register("gen/test", ["my_modules/nprpc"], function (exports_2, context_2) {
    "use strict";
    var NPRPC, u8enc, u8dec, Flat_test, TestBasic, _ITestBasic_Servant, TestOptional, _ITestOptional_Servant, TestNested, _ITestNested_Servant, TestBadInput, _ITestBadInput_Servant;
    var __moduleName = context_2 && context_2.id;
    return {
        setters: [
            function (NPRPC_1) {
                NPRPC = NPRPC_1;
            }
        ],
        execute: function () {
            u8enc = new TextEncoder();
            u8dec = new TextDecoder();
            (function (Flat_test) {
                class AAA_Direct extends NPRPC.Flat.Flat {
                    get a() { return this.buffer.dv.getUint32(this.offset + 0, true); }
                    set a(value) { this.buffer.dv.setUint32(this.offset + 0, value, true); }
                    get b() {
                        const offset = this.offset + 4;
                        const n = this.buffer.dv.getUint32(offset + 4, true);
                        return n > 0 ? u8dec.decode(new DataView(this.buffer.array_buffer, offset + this.buffer.dv.getUint32(offset, true), n)) : "";
                    }
                    set b(str) {
                        const bytes = u8enc.encode(str);
                        const offset = NPRPC.Flat._alloc(this.buffer, this.offset + 4, bytes.length, 1, 1);
                        new Uint8Array(this.buffer.array_buffer, offset).set(bytes);
                    }
                    get c() {
                        const offset = this.offset + 8;
                        const n = this.buffer.dv.getUint32(offset + 4, true);
                        return n > 0 ? u8dec.decode(new DataView(this.buffer.array_buffer, offset + this.buffer.dv.getUint32(offset, true), n)) : "";
                    }
                    set c(str) {
                        const bytes = u8enc.encode(str);
                        const offset = NPRPC.Flat._alloc(this.buffer, this.offset + 8, bytes.length, 1, 1);
                        new Uint8Array(this.buffer.array_buffer, offset).set(bytes);
                    }
                }
                Flat_test.AAA_Direct = AAA_Direct;
            })(Flat_test || (exports_2("Flat_test", Flat_test = {}))); // namespace Flat 
            TestBasic = class TestBasic extends NPRPC.ObjectProxy {
                static get servant_t() {
                    return _ITestBasic_Servant;
                }
                ReturnBoolean() {
                    var arguments_1 = arguments;
                    return __awaiter(this, void 0, void 0, function* () {
                        let interface_idx = (arguments_1.length == 0 ? 0 : arguments_1[arguments_1.length - 1]);
                        let buf = NPRPC.FlatBuffer.create();
                        buf.prepare(32);
                        buf.commit(32);
                        buf.write_msg_id(0 /* NPRPC.impl.MessageId.FunctionCall */);
                        buf.write_msg_type(0 /* NPRPC.impl.MessageType.Request */);
                        let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
                        __ch.object_id = this.data.object_id;
                        __ch.poa_idx = this.data.poa_idx;
                        __ch.interface_idx = interface_idx;
                        __ch.function_idx = 0;
                        buf.write_len(buf.size - 4);
                        yield NPRPC.rpc.call(this.endpoint, buf, this.timeout);
                        let std_reply = NPRPC.handle_standart_reply(buf);
                        if (std_reply != -1) {
                            console.log("received an unusual reply for function with output arguments");
                            throw new NPRPC.Exception("Unknown Error");
                        }
                        let out = new Flat_test.test_M1_Direct(buf, 16);
                        let __ret_value /*boolean*/;
                        __ret_value = out._1;
                        return __ret_value;
                    });
                }
                In(a, b, c) {
                    var arguments_2 = arguments;
                    return __awaiter(this, void 0, void 0, function* () {
                        let interface_idx = (arguments_2.length == 3 ? 0 : arguments_2[arguments_2.length - 1]);
                        let buf = NPRPC.FlatBuffer.create();
                        buf.prepare(176);
                        buf.commit(48);
                        buf.write_msg_id(0 /* NPRPC.impl.MessageId.FunctionCall */);
                        buf.write_msg_type(0 /* NPRPC.impl.MessageType.Request */);
                        let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
                        __ch.object_id = this.data.object_id;
                        __ch.poa_idx = this.data.poa_idx;
                        __ch.interface_idx = interface_idx;
                        __ch.function_idx = 1;
                        let _ = new Flat_test.test_M2_Direct(buf, 32);
                        _._1 = a;
                        _._2 = b;
                        _._3(c.length);
                        _._3_d().copy_from_ts_array(c);
                        buf.write_len(buf.size - 4);
                        yield NPRPC.rpc.call(this.endpoint, buf, this.timeout);
                        let std_reply = NPRPC.handle_standart_reply(buf);
                        if (std_reply != -1) {
                            console.log("received an unusual reply for function with output arguments");
                            throw new NPRPC.Exception("Unknown Error");
                        }
                        let out = new Flat_test.test_M1_Direct(buf, 16);
                        let __ret_value /*boolean*/;
                        __ret_value = out._1;
                        return __ret_value;
                    });
                }
                Out(a, b, c) {
                    var arguments_3 = arguments;
                    return __awaiter(this, void 0, void 0, function* () {
                        let interface_idx = (arguments_3.length == 3 ? 0 : arguments_3[arguments_3.length - 1]);
                        let buf = NPRPC.FlatBuffer.create();
                        buf.prepare(32);
                        buf.commit(32);
                        buf.write_msg_id(0 /* NPRPC.impl.MessageId.FunctionCall */);
                        buf.write_msg_type(0 /* NPRPC.impl.MessageType.Request */);
                        let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
                        __ch.object_id = this.data.object_id;
                        __ch.poa_idx = this.data.poa_idx;
                        __ch.interface_idx = interface_idx;
                        __ch.function_idx = 2;
                        buf.write_len(buf.size - 4);
                        yield NPRPC.rpc.call(this.endpoint, buf, this.timeout);
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
                    });
                }
            };
            exports_2("TestBasic", TestBasic);
            ;
            _ITestBasic_Servant = class _ITestBasic_Servant extends NPRPC.ObjectServant {
                constructor() {
                    super(...arguments);
                    this.get_class = () => { return _ITestBasic_Servant._get_class(); };
                    this.dispatch = (buf, remote_endpoint, from_parent) => {
                        _ITestBasic_Servant._dispatch(this, buf, remote_endpoint, from_parent);
                    };
                }
                static _get_class() { return "test/TestBasic"; }
                static _dispatch(obj, buf, remote_endpoint, from_parent) {
                    let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
                    switch (__ch.function_idx) {
                        case 0: {
                            let __ret_val /*boolean*/;
                            __ret_val = obj.ReturnBoolean();
                            let obuf = buf;
                            obuf.consume(obuf.size);
                            obuf.prepare(17);
                            obuf.commit(17);
                            let oa = new Flat_test.test_M1_Direct(obuf, 16);
                            oa._1 = __ret_val;
                            obuf.write_len(obuf.size - 4);
                            obuf.write_msg_id(1 /* NPRPC.impl.MessageId.BlockResponse */);
                            obuf.write_msg_type(1 /* NPRPC.impl.MessageType.Answer */);
                            break;
                        }
                        case 1: {
                            let ia = new Flat_test.test_M2_Direct(buf, 32);
                            let __ret_val /*boolean*/;
                            __ret_val = obj.In(ia._1, ia._2, ia._3_d());
                            let obuf = buf;
                            obuf.consume(obuf.size);
                            obuf.prepare(17);
                            obuf.commit(17);
                            let oa = new Flat_test.test_M1_Direct(obuf, 16);
                            oa._1 = __ret_val;
                            obuf.write_len(obuf.size - 4);
                            obuf.write_msg_id(1 /* NPRPC.impl.MessageId.BlockResponse */);
                            obuf.write_msg_type(1 /* NPRPC.impl.MessageType.Answer */);
                            break;
                        }
                        case 2: {
                            let obuf = buf;
                            obuf.consume(obuf.size);
                            obuf.prepare(160);
                            obuf.commit(32);
                            let oa = new Flat_test.test_M2_Direct(obuf, 16);
                            obj.Out(oa._1, oa._2, oa._3_d);
                            obuf.write_len(obuf.size - 4);
                            obuf.write_msg_id(1 /* NPRPC.impl.MessageId.BlockResponse */);
                            obuf.write_msg_type(1 /* NPRPC.impl.MessageType.Answer */);
                            break;
                        }
                        default:
                            NPRPC.make_simple_answer(buf, 9 /* NPRPC.impl.MessageId.Error_UnknownFunctionIdx */);
                    }
                }
            };
            exports_2("_ITestBasic_Servant", _ITestBasic_Servant);
            TestOptional = class TestOptional extends NPRPC.ObjectProxy {
                static get servant_t() {
                    return _ITestOptional_Servant;
                }
                InEmpty(a) {
                    var arguments_4 = arguments;
                    return __awaiter(this, void 0, void 0, function* () {
                        let interface_idx = (arguments_4.length == 1 ? 0 : arguments_4[arguments_4.length - 1]);
                        let buf = NPRPC.FlatBuffer.create();
                        buf.prepare(164);
                        buf.commit(36);
                        buf.write_msg_id(0 /* NPRPC.impl.MessageId.FunctionCall */);
                        buf.write_msg_type(0 /* NPRPC.impl.MessageType.Request */);
                        let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
                        __ch.object_id = this.data.object_id;
                        __ch.poa_idx = this.data.poa_idx;
                        __ch.interface_idx = interface_idx;
                        __ch.function_idx = 0;
                        let _ = new Flat_test.test_M3_Direct(buf, 32);
                        {
                            let opt = _._1();
                            if (a) {
                                opt.alloc();
                                opt.value = a;
                            }
                            else {
                                opt.set_nullopt();
                            }
                        }
                        buf.write_len(buf.size - 4);
                        yield NPRPC.rpc.call(this.endpoint, buf, this.timeout);
                        let std_reply = NPRPC.handle_standart_reply(buf);
                        if (std_reply != -1) {
                            console.log("received an unusual reply for function with output arguments");
                            throw new NPRPC.Exception("Unknown Error");
                        }
                        let out = new Flat_test.test_M1_Direct(buf, 16);
                        let __ret_value /*boolean*/;
                        __ret_value = out._1;
                        return __ret_value;
                    });
                }
                In(a, b) {
                    var arguments_5 = arguments;
                    return __awaiter(this, void 0, void 0, function* () {
                        let interface_idx = (arguments_5.length == 2 ? 0 : arguments_5[arguments_5.length - 1]);
                        let buf = NPRPC.FlatBuffer.create();
                        buf.prepare(168);
                        buf.commit(40);
                        buf.write_msg_id(0 /* NPRPC.impl.MessageId.FunctionCall */);
                        buf.write_msg_type(0 /* NPRPC.impl.MessageType.Request */);
                        let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
                        __ch.object_id = this.data.object_id;
                        __ch.poa_idx = this.data.poa_idx;
                        __ch.interface_idx = interface_idx;
                        __ch.function_idx = 1;
                        let _ = new Flat_test.test_M4_Direct(buf, 32);
                        {
                            let opt = _._1();
                            if (a) {
                                opt.alloc();
                                opt.value = a;
                            }
                            else {
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
                            }
                            else {
                                opt.set_nullopt();
                            }
                        }
                        buf.write_len(buf.size - 4);
                        yield NPRPC.rpc.call(this.endpoint, buf, this.timeout);
                        let std_reply = NPRPC.handle_standart_reply(buf);
                        if (std_reply != -1) {
                            console.log("received an unusual reply for function with output arguments");
                            throw new NPRPC.Exception("Unknown Error");
                        }
                        let out = new Flat_test.test_M1_Direct(buf, 16);
                        let __ret_value /*boolean*/;
                        __ret_value = out._1;
                        return __ret_value;
                    });
                }
                OutEmpty(a) {
                    var arguments_6 = arguments;
                    return __awaiter(this, void 0, void 0, function* () {
                        let interface_idx = (arguments_6.length == 1 ? 0 : arguments_6[arguments_6.length - 1]);
                        let buf = NPRPC.FlatBuffer.create();
                        buf.prepare(32);
                        buf.commit(32);
                        buf.write_msg_id(0 /* NPRPC.impl.MessageId.FunctionCall */);
                        buf.write_msg_type(0 /* NPRPC.impl.MessageType.Request */);
                        let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
                        __ch.object_id = this.data.object_id;
                        __ch.poa_idx = this.data.poa_idx;
                        __ch.interface_idx = interface_idx;
                        __ch.function_idx = 2;
                        buf.write_len(buf.size - 4);
                        yield NPRPC.rpc.call(this.endpoint, buf, this.timeout);
                        let std_reply = NPRPC.handle_standart_reply(buf);
                        if (std_reply != -1) {
                            console.log("received an unusual reply for function with output arguments");
                            throw new NPRPC.Exception("Unknown Error");
                        }
                        let out = new Flat_test.test_M3_Direct(buf, 16);
                        {
                            if (out._1.has_value) {
                                a = out._1.value;
                            }
                            else {
                                a = null;
                            }
                        }
                    });
                }
                Out(a) {
                    var arguments_7 = arguments;
                    return __awaiter(this, void 0, void 0, function* () {
                        let interface_idx = (arguments_7.length == 1 ? 0 : arguments_7[arguments_7.length - 1]);
                        let buf = NPRPC.FlatBuffer.create();
                        buf.prepare(32);
                        buf.commit(32);
                        buf.write_msg_id(0 /* NPRPC.impl.MessageId.FunctionCall */);
                        buf.write_msg_type(0 /* NPRPC.impl.MessageType.Request */);
                        let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
                        __ch.object_id = this.data.object_id;
                        __ch.poa_idx = this.data.poa_idx;
                        __ch.interface_idx = interface_idx;
                        __ch.function_idx = 3;
                        buf.write_len(buf.size - 4);
                        yield NPRPC.rpc.call(this.endpoint, buf, this.timeout);
                        let std_reply = NPRPC.handle_standart_reply(buf);
                        if (std_reply != -1) {
                            console.log("received an unusual reply for function with output arguments");
                            throw new NPRPC.Exception("Unknown Error");
                        }
                        let out = new Flat_test.test_M3_Direct(buf, 16);
                        {
                            if (out._1().has_value) {
                                a = out._1().value;
                            }
                            else {
                                a = null;
                            }
                        }
                    });
                }
            };
            exports_2("TestOptional", TestOptional);
            ;
            _ITestOptional_Servant = class _ITestOptional_Servant extends NPRPC.ObjectServant {
                constructor() {
                    super(...arguments);
                    this.get_class = () => { return _ITestOptional_Servant._get_class(); };
                    this.dispatch = (buf, remote_endpoint, from_parent) => {
                        _ITestOptional_Servant._dispatch(this, buf, remote_endpoint, from_parent);
                    };
                }
                static _get_class() { return "test/TestOptional"; }
                static _dispatch(obj, buf, remote_endpoint, from_parent) {
                    let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
                    switch (__ch.function_idx) {
                        case 0: {
                            let ia = new Flat_test.test_M3_Direct(buf, 32);
                            let __ret_val /*boolean*/;
                            __ret_val = obj.InEmpty(ia._1);
                            let obuf = buf;
                            obuf.consume(obuf.size);
                            obuf.prepare(17);
                            obuf.commit(17);
                            let oa = new Flat_test.test_M1_Direct(obuf, 16);
                            oa._1 = __ret_val;
                            obuf.write_len(obuf.size - 4);
                            obuf.write_msg_id(1 /* NPRPC.impl.MessageId.BlockResponse */);
                            obuf.write_msg_type(1 /* NPRPC.impl.MessageType.Answer */);
                            break;
                        }
                        case 1: {
                            let ia = new Flat_test.test_M4_Direct(buf, 32);
                            let __ret_val /*boolean*/;
                            __ret_val = obj.In(ia._1, ia._2);
                            let obuf = buf;
                            obuf.consume(obuf.size);
                            obuf.prepare(17);
                            obuf.commit(17);
                            let oa = new Flat_test.test_M1_Direct(obuf, 16);
                            oa._1 = __ret_val;
                            obuf.write_len(obuf.size - 4);
                            obuf.write_msg_id(1 /* NPRPC.impl.MessageId.BlockResponse */);
                            obuf.write_msg_type(1 /* NPRPC.impl.MessageType.Answer */);
                            break;
                        }
                        case 2: {
                            let obuf = buf;
                            obuf.consume(obuf.size);
                            obuf.prepare(148);
                            obuf.commit(20);
                            let oa = new Flat_test.test_M3_Direct(obuf, 16);
                            obj.OutEmpty(oa._1);
                            obuf.write_len(obuf.size - 4);
                            obuf.write_msg_id(1 /* NPRPC.impl.MessageId.BlockResponse */);
                            obuf.write_msg_type(1 /* NPRPC.impl.MessageType.Answer */);
                            break;
                        }
                        case 3: {
                            let obuf = buf;
                            obuf.consume(obuf.size);
                            obuf.prepare(148);
                            obuf.commit(20);
                            let oa = new Flat_test.test_M3_Direct(obuf, 16);
                            obj.Out(oa._1);
                            obuf.write_len(obuf.size - 4);
                            obuf.write_msg_id(1 /* NPRPC.impl.MessageId.BlockResponse */);
                            obuf.write_msg_type(1 /* NPRPC.impl.MessageType.Answer */);
                            break;
                        }
                        default:
                            NPRPC.make_simple_answer(buf, 9 /* NPRPC.impl.MessageId.Error_UnknownFunctionIdx */);
                    }
                }
            };
            exports_2("_ITestOptional_Servant", _ITestOptional_Servant);
            (function (Flat_test) {
                class CCC_Direct extends NPRPC.Flat.Flat {
                    get a() {
                        const offset = this.offset + 0;
                        const n = this.buffer.dv.getUint32(offset + 4, true);
                        return n > 0 ? u8dec.decode(new DataView(this.buffer.array_buffer, offset + this.buffer.dv.getUint32(offset, true), n)) : "";
                    }
                    set a(str) {
                        const bytes = u8enc.encode(str);
                        const offset = NPRPC.Flat._alloc(this.buffer, this.offset + 0, bytes.length, 1, 1);
                        new Uint8Array(this.buffer.array_buffer, offset).set(bytes);
                    }
                    get b() {
                        const offset = this.offset + 4;
                        const n = this.buffer.dv.getUint32(offset + 4, true);
                        return n > 0 ? u8dec.decode(new DataView(this.buffer.array_buffer, offset + this.buffer.dv.getUint32(offset, true), n)) : "";
                    }
                    set b(str) {
                        const bytes = u8enc.encode(str);
                        const offset = NPRPC.Flat._alloc(this.buffer, this.offset + 4, bytes.length, 1, 1);
                        new Uint8Array(this.buffer.array_buffer, offset).set(bytes);
                    }
                    c() {
                        return new NPRPC.Flat.Optional_Direct1_boolean(this.buffer, this.offset + 8, 1, 1);
                    }
                }
                Flat_test.CCC_Direct = CCC_Direct;
            })(Flat_test || (exports_2("Flat_test", Flat_test = {}))); // namespace Flat 
            (function (Flat_test) {
                class BBB_Direct extends NPRPC.Flat.Flat {
                    a(elements_size) {
                        NPRPC.Flat._alloc(this.buffer, this.offset + 0, elements_size, 20, 4);
                    }
                    a_d() { return new NPRPC.Flat.Vector_Direct2(this.buffer, this.offset + 0, 20, Flat_test.AAA_Direct); }
                    b(elements_size) {
                        NPRPC.Flat._alloc(this.buffer, this.offset + 8, elements_size, 20, 4);
                    }
                    b_d() { return new NPRPC.Flat.Vector_Direct2(this.buffer, this.offset + 8, 20, Flat_test.CCC_Direct); }
                }
                Flat_test.BBB_Direct = BBB_Direct;
            })(Flat_test || (exports_2("Flat_test", Flat_test = {}))); // namespace Flat 
            TestNested = class TestNested extends NPRPC.ObjectProxy {
                static get servant_t() {
                    return _ITestNested_Servant;
                }
                Out(a) {
                    var arguments_8 = arguments;
                    return __awaiter(this, void 0, void 0, function* () {
                        let interface_idx = (arguments_8.length == 1 ? 0 : arguments_8[arguments_8.length - 1]);
                        let buf = NPRPC.FlatBuffer.create();
                        buf.prepare(32);
                        buf.commit(32);
                        buf.write_msg_id(0 /* NPRPC.impl.MessageId.FunctionCall */);
                        buf.write_msg_type(0 /* NPRPC.impl.MessageType.Request */);
                        let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
                        __ch.object_id = this.data.object_id;
                        __ch.poa_idx = this.data.poa_idx;
                        __ch.interface_idx = interface_idx;
                        __ch.function_idx = 0;
                        buf.write_len(buf.size - 4);
                        yield NPRPC.rpc.call(this.endpoint, buf, this.timeout);
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
                                a = new Object();
                                {
                                    let vv = value.a_d(), index_1 = 0;
                                    a.a = new Array(vv.elements_size);
                                    for (let e of vv) {
                                        a.a[index_1] = new Object();
                                        a.a[index_1] = new Object();
                                        a.a[index_1].a = e.a;
                                        a.a[index_1].b = e.b;
                                        a.a[index_1].c = e.c;
                                        ++index_1;
                                    }
                                }
                                {
                                    let vv = value.b_d(), index_2 = 0;
                                    a.b = new Array(vv.elements_size);
                                    for (let e of vv) {
                                        a.b[index_2] = new Object();
                                        a.b[index_2] = new Object();
                                        a.b[index_2].a = e.a;
                                        a.b[index_2].b = e.b;
                                        {
                                            if (e.c.has_value) {
                                                a.b[index_2].c = e.c.value;
                                            }
                                            else {
                                                a.b[index_2].c = null;
                                            }
                                        }
                                        ++index_2;
                                    }
                                }
                            }
                            else {
                                a = null;
                            }
                        }
                    });
                }
            };
            exports_2("TestNested", TestNested);
            ;
            _ITestNested_Servant = class _ITestNested_Servant extends NPRPC.ObjectServant {
                constructor() {
                    super(...arguments);
                    this.get_class = () => { return _ITestNested_Servant._get_class(); };
                    this.dispatch = (buf, remote_endpoint, from_parent) => {
                        _ITestNested_Servant._dispatch(this, buf, remote_endpoint, from_parent);
                    };
                }
                static _get_class() { return "test/TestNested"; }
                static _dispatch(obj, buf, remote_endpoint, from_parent) {
                    let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
                    switch (__ch.function_idx) {
                        case 0: {
                            let obuf = buf;
                            obuf.consume(obuf.size);
                            obuf.prepare(148);
                            obuf.commit(20);
                            let oa = new Flat_test.test_M5_Direct(obuf, 16);
                            obj.Out(oa._1);
                            obuf.write_len(obuf.size - 4);
                            obuf.write_msg_id(1 /* NPRPC.impl.MessageId.BlockResponse */);
                            obuf.write_msg_type(1 /* NPRPC.impl.MessageType.Answer */);
                            break;
                        }
                        default:
                            NPRPC.make_simple_answer(buf, 9 /* NPRPC.impl.MessageId.Error_UnknownFunctionIdx */);
                    }
                }
            };
            exports_2("_ITestNested_Servant", _ITestNested_Servant);
            TestBadInput = class TestBadInput extends NPRPC.ObjectProxy {
                static get servant_t() {
                    return _ITestBadInput_Servant;
                }
                In(a) {
                    var arguments_9 = arguments;
                    return __awaiter(this, void 0, void 0, function* () {
                        let interface_idx = (arguments_9.length == 1 ? 0 : arguments_9[arguments_9.length - 1]);
                        let buf = NPRPC.FlatBuffer.create();
                        buf.prepare(168);
                        buf.commit(40);
                        buf.write_msg_id(0 /* NPRPC.impl.MessageId.FunctionCall */);
                        buf.write_msg_type(0 /* NPRPC.impl.MessageType.Request */);
                        let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
                        __ch.object_id = this.data.object_id;
                        __ch.poa_idx = this.data.poa_idx;
                        __ch.interface_idx = interface_idx;
                        __ch.function_idx = 0;
                        let _ = new Flat_test.test_M6_Direct(buf, 32);
                        _._1(a.length);
                        _._1_d().copy_from_ts_array(a);
                        buf.write_len(buf.size - 4);
                        yield NPRPC.rpc.call(this.endpoint, buf, this.timeout);
                        let std_reply = NPRPC.handle_standart_reply(buf);
                        if (std_reply != 0) {
                            console.log("received an unusual reply for function with no output arguments");
                        }
                    });
                }
            };
            exports_2("TestBadInput", TestBadInput);
            ;
            _ITestBadInput_Servant = class _ITestBadInput_Servant extends NPRPC.ObjectServant {
                constructor() {
                    super(...arguments);
                    this.get_class = () => { return _ITestBadInput_Servant._get_class(); };
                    this.dispatch = (buf, remote_endpoint, from_parent) => {
                        _ITestBadInput_Servant._dispatch(this, buf, remote_endpoint, from_parent);
                    };
                }
                static _get_class() { return "test/TestBadInput"; }
                static _dispatch(obj, buf, remote_endpoint, from_parent) {
                    let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, 16);
                    switch (__ch.function_idx) {
                        case 0: {
                            let ia = new Flat_test.test_M6_Direct(buf, 32);
                            obj.In(ia._1_d());
                            NPRPC.make_simple_answer(buf, 4 /* NPRPC.impl.MessageId.Success */);
                            break;
                        }
                        default:
                            NPRPC.make_simple_answer(buf, 9 /* NPRPC.impl.MessageId.Error_UnknownFunctionIdx */);
                    }
                }
            };
            exports_2("_ITestBadInput_Servant", _ITestBadInput_Servant);
            (function (Flat_test) {
                class test_M1_Direct extends NPRPC.Flat.Flat {
                    get _1() { return (this.buffer.dv.getUint8(this.offset + 0) === 0x01); }
                    set _1(value) { this.buffer.dv.setUint8(this.offset + 0, value === true ? 0x01 : 0x00); }
                }
                Flat_test.test_M1_Direct = test_M1_Direct;
            })(Flat_test || (exports_2("Flat_test", Flat_test = {}))); // namespace Flat 
            (function (Flat_test) {
                class test_M2_Direct extends NPRPC.Flat.Flat {
                    get _1() { return this.buffer.dv.getUint32(this.offset + 0, true); }
                    set _1(value) { this.buffer.dv.setUint32(this.offset + 0, value, true); }
                    get _2() { return (this.buffer.dv.getUint8(this.offset + 4) === 0x01); }
                    set _2(value) { this.buffer.dv.setUint8(this.offset + 4, value === true ? 0x01 : 0x00); }
                    _3(elements_size) {
                        NPRPC.Flat._alloc(this.buffer, this.offset + 8, elements_size, 1, 1);
                    }
                    _3_d() { return new NPRPC.Flat.Vector_Direct1_u8(this.buffer, this.offset + 8); }
                }
                Flat_test.test_M2_Direct = test_M2_Direct;
            })(Flat_test || (exports_2("Flat_test", Flat_test = {}))); // namespace Flat 
            (function (Flat_test) {
                class test_M3_Direct extends NPRPC.Flat.Flat {
                    _1() {
                        return new NPRPC.Flat.Optional_Direct1_u32(this.buffer, this.offset + 0, 4, 4);
                    }
                }
                Flat_test.test_M3_Direct = test_M3_Direct;
            })(Flat_test || (exports_2("Flat_test", Flat_test = {}))); // namespace Flat 
            (function (Flat_test) {
                class test_M4_Direct extends NPRPC.Flat.Flat {
                    _1() {
                        return new NPRPC.Flat.Optional_Direct1_u32(this.buffer, this.offset + 0, 4, 4);
                    }
                    _2() {
                        return new NPRPC.Flat.Optional_Direct2(this.buffer, this.offset + 4, 20, 4, Flat_test.AAA_Direct);
                    }
                }
                Flat_test.test_M4_Direct = test_M4_Direct;
            })(Flat_test || (exports_2("Flat_test", Flat_test = {}))); // namespace Flat 
            (function (Flat_test) {
                class test_M5_Direct extends NPRPC.Flat.Flat {
                    _1() {
                        return new NPRPC.Flat.Optional_Direct2(this.buffer, this.offset + 0, 16, 4, Flat_test.BBB_Direct);
                    }
                }
                Flat_test.test_M5_Direct = test_M5_Direct;
            })(Flat_test || (exports_2("Flat_test", Flat_test = {}))); // namespace Flat 
            (function (Flat_test) {
                class test_M6_Direct extends NPRPC.Flat.Flat {
                    _1(elements_size) {
                        NPRPC.Flat._alloc(this.buffer, this.offset + 0, elements_size, 1, 1);
                    }
                    _1_d() { return new NPRPC.Flat.Vector_Direct1_u8(this.buffer, this.offset + 0); }
                }
                Flat_test.test_M6_Direct = test_M6_Direct;
            })(Flat_test || (exports_2("Flat_test", Flat_test = {}))); // namespace Flat 
        }
    };
});
System.register("index", [], function (exports_3, context_3) {
    "use strict";
    var hello;
    var __moduleName = context_3 && context_3.id;
    return {
        setters: [],
        execute: function () {
            hello = 'Hello, world!';
            console.log(hello);
        }
    };
});
//# sourceMappingURL=index.js.map