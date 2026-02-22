// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import { FlatBuffer } from './flat_buffer';
import { MyPromise } from "./utils";
import { StreamManager, StreamReader, bytes_deserializer } from './stream';
export { StreamReader, bytes_deserializer } from './stream';
import {
  impl, detail, oid_t, poa_idx_t,
  LogLevel,
  ExceptionObjectNotExist, 
  ExceptionUnknownFunctionIndex, 
  ExceptionUnknownMessageId, 
  ExceptionCommFailure, 
  ExceptionUnsecuredObject,
  ExceptionBadAccess,
  ExceptionBadInput,
  EndPointType,
  unmarshal_ExceptionCommFailure
} from "./gen/nprpc_base"

import { Exception } from "./base";

const header_size = 16;
const invalid_object_id = 0xFFFFFFFFFFFFFFFFn;
const localhost_ip4 = 0x7F000001;

const u8enc = new TextEncoder();
const u8dec = new TextDecoder();

type ObjectId = detail.ObjectId;
export type { ObjectId };

export let rpc: Rpc;
let host_info: HostInfo = {secured: false, objects: {}};

let gLogLevel: LogLevel = LogLevel.error;

export class EndPoint {
  constructor(
    public type: EndPointType,
    public hostname: string, // or ip address
    public port: number)
  {
  }

  public static to_string(type: EndPointType): string {
    switch (type) {
      case EndPointType.WebSocket:
        return "ws://";
      case EndPointType.SecuredWebSocket:
        return "wss://";
      case EndPointType.Http:
        return "http://";
      case EndPointType.SecuredHttp:
        return "https://";
      default:
        throw new Exception("Unknown EndPointType");
    };
  };

  public static from_string(str: string): EndPoint {
    let type: EndPointType;
    if (str.startsWith("ws://")) {
      type = EndPointType.WebSocket;
    } else if (str.startsWith("wss://")) {
      type = EndPointType.SecuredWebSocket;
    } else if (str.startsWith("http://")) {
      type = EndPointType.Http;
    } else if (str.startsWith("https://")) {
      type = EndPointType.SecuredHttp;
    } else {
      throw new Exception("Invalid EndPoint string: " + str);
    }
    let idx = str.indexOf("://") + 3;
    let colon_idx = str.indexOf(":", idx);
    if (colon_idx < 0) {
      throw new Exception("Invalid EndPoint string: " + str);
    }
    let hostname = str.substring(idx, colon_idx);
    let port_str = str.substring(colon_idx + 1);
    let port = parseInt(port_str, 10);
    if (isNaN(port) || port < 0 || port > 65535) {
      throw new Exception("Invalid port in EndPoint string: " + str);
    }
    return new EndPoint(type, hostname, port);
  }

  public equal(other: EndPoint): boolean {
    return this.type === other.type &&
      this.hostname === other.hostname &&
      this.port === other.port;
  }

  public to_string(): string {
    return `${EndPoint.to_string(this.type)}${this.hostname}:${this.port}`;
  }

  public is_ssl(): boolean {
    return this.type === EndPointType.SecuredWebSocket ||
      this.type === EndPointType.SecuredHttp;
  }
};

interface HostInfo {
  secured: boolean;
  objects: any;
}

interface PendingRequest {
  buffer: FlatBuffer;
  promise: MyPromise<void, Error>;
}

function get_object(buffer: FlatBuffer, poa_idx: poa_idx_t, object_id: bigint) {
  do {
    let poa = rpc.get_poa(poa_idx);
    if (!poa) {
      make_simple_answer(buffer, impl.MessageId.Error_PoaNotExist);
      console.log("Bad poa index. " + poa_idx);
      break;
    }
    let obj = poa.get_object(object_id);
    if (!obj) {
      make_simple_answer(buffer, impl.MessageId.Error_ObjectNotExist);
      console.log("Object not found. " + object_id);
      break;
    }
    return obj;
  } while (true);

  return null;
}

export class Connection {
  endpoint: EndPoint;
  ws: WebSocket;
  pending_requests: Map<number, PendingRequest>;
  next_request_id: number;
  stream_manager: StreamManager;

  private async perform_request(request_id: number, buffer: FlatBuffer) {
    // Inject request ID into the message header
    buffer.write_request_id(request_id);
    this.ws.send(buffer.writable_view);
  }

  private on_open() {
    // WebSocket is ready, all pending requests will be sent when needed
  }

  public async send_receive(buffer: FlatBuffer, timeout_ms: number): Promise<any> {
    const request_id = this.next_request_id++;
    const promise = new MyPromise<void, Error>();
    
    // Store the pending request
    this.pending_requests.set(request_id, { buffer: buffer, promise: promise });

    // Send the request if WebSocket is ready
    if (this.ws.readyState === WebSocket.OPEN) {
      this.perform_request(request_id, buffer);
    } else {
      // If WebSocket is not ready, wait for it to open
      const originalOnOpen = this.ws.onopen;
      this.ws.onopen = (event) => {
        if (originalOnOpen) originalOnOpen.call(this.ws, event);
        if (this.pending_requests.has(request_id)) {
          this.perform_request(request_id, buffer);
        }
      };
    }
    
    return promise.$;
  }

  private on_read(ev: MessageEvent<any>) {
    let buf = FlatBuffer.from_array_buffer(ev.data as ArrayBuffer);
    if (buf.read_msg_type() == impl.MessageType.Answer) {
      // Extract request ID from the response
      const request_id = buf.read_request_id();
      const pending_request = this.pending_requests.get(request_id);
      
      if (pending_request) {
        // Update the buffer with response data
        pending_request.buffer.set_buffer(ev.data as ArrayBuffer);
        this.pending_requests.delete(request_id);
        pending_request.promise.set_promise();
      } else {
        console.warn("Received response for unknown request ID:", request_id);
      }
    } else {
      const msg_id = buf.read_msg_id();

      // Stream messages are fire-and-forget (no reply needed)
      switch (msg_id) {
        case impl.MessageId.StreamDataChunk: {
          const chunk = impl.unmarshal_StreamChunk(buf, header_size);
          this.stream_manager.on_chunk_received(chunk.stream_id, chunk.data, chunk.sequence);
          return;
        }
        case impl.MessageId.StreamCompletion: {
          const msg = impl.unmarshal_StreamComplete(buf, header_size);
          this.stream_manager.on_stream_complete(msg.stream_id);
          return;
        }
        case impl.MessageId.StreamError: {
          const msg = impl.unmarshal_StreamError(buf, header_size);
          this.stream_manager.on_stream_error(msg.stream_id, msg.error_code);
          return;
        }
        case impl.MessageId.StreamCancellation: {
          const msg = impl.unmarshal_StreamCancel(buf, header_size);
          this.stream_manager.on_stream_complete(msg.stream_id); // treat as completion
          return;
        }
      }

      // Remaining messages require a reply - store request_id to echo back
      const request_id = buf.read_request_id();
      
      switch (msg_id) {
        case impl.MessageId.FunctionCall: {
          let ch = impl.unmarshal_CallHeader(buf, header_size);

          if (gLogLevel >= LogLevel.trace) {
            console.log("FunctionCall. interface_idx: " + ch.interface_idx + " , fn_idx: " + ch.function_idx 
              + " , poa_idx: " + ch.poa_idx + " , oid: " + ch.object_id);
          }
      
          let obj = get_object(buf, ch.poa_idx, ch.object_id)
          if (obj) {
            //console.log(obj);
            obj.dispatch(buf, this.endpoint, false);
          }
          break;
        }
        case impl.MessageId.AddReference: {
          let msg = detail.unmarshal_ObjectIdLocal(buf, header_size);

          //detail::ObjectIdLocal oid{ msg.poa_idx(), msg.object_id() };
          if (gLogLevel >= LogLevel.trace) {
            console.log("AddReference. poa_idx: " + msg.poa_idx + " , oid: " + msg.object_id);
          }

  
          let obj = get_object(buf, msg.poa_idx, msg.object_id)
  
          if (obj) {
            //    std::cout << "Refference added." << std::endl;

            make_simple_answer(buf, impl.MessageId.Success);
          } 

          break;
        }
        case impl.MessageId.ReleaseObject: {
          let msg = detail.unmarshal_ObjectIdLocal(buf, header_size);
          //detail::ObjectIdLocal oid{ msg.poa_idx(), msg.object_id() };

          //  std::cout << "ReleaseObject. " << "poa_idx: " << oid.poa_idx << ", oid: " << oid.object_id << std::endl;

          //if (ref_list_.remove_ref(msg.poa_idx(), msg.object_id())) {
          //  make_simple_answer(rx_buffer_(), nprpc::impl::MessageId::Success);
          //} else {
          //  make_simple_answer(rx_buffer_(), nprpc::impl::MessageId::Error_ObjectNotExist);
          //}

          break;
        }
        default:
          make_simple_answer(buf, impl.MessageId.Error_UnknownMessageId);
          break;
      }
      
      // Restore the request ID before sending the response
      buf.write_request_id(request_id);
      this.ws.send(buf.writable_view);
    }
  }

  private on_close() { 
    // Cancel all pending requests and streams on connection close
    for (const [, pending_request] of this.pending_requests) {
      pending_request.promise.set_exception(new Error("Connection closed") as any);
    }
    this.pending_requests.clear();
    this.stream_manager.cancel_all();
  }

  private on_error(ev: Event) {
    // Cancel all pending requests and streams on connection error
    for (const [, pending_request] of this.pending_requests) {
      pending_request.promise.set_exception(new Error("Connection error") as any);
    }
    this.pending_requests.clear();
    this.stream_manager.cancel_all();
  }

  constructor(endpoint: EndPoint) {
    this.endpoint = endpoint;
    this.pending_requests = new Map<number, PendingRequest>();
    this.next_request_id = 1; // Start from 1, avoid 0 as it might be treated as invalid

    if (host_info.secured) {
      this.ws = new WebSocket('wss://' + this.endpoint.hostname + ':' + this.endpoint.port.toString(10));
    } else {
      // prefer hostname over ip address
      const name_or_ip = this.endpoint.hostname; 
      this.ws = new WebSocket('ws://' + name_or_ip + ':' + this.endpoint.port.toString(10));
    }

    this.ws.binaryType = 'arraybuffer';
    this.ws.onopen = this.on_open.bind(this);
    this.ws.onclose = this.on_close.bind(this);
    this.ws.onmessage = this.on_read.bind(this);
    this.ws.onerror = this.on_error.bind(this);
    this.stream_manager = new StreamManager();
  }
}

export class Rpc {
  public host_info: HostInfo;
  /** @internal */
  private last_poa_id_: number;
  /** @internal */
  private opened_connections_: Connection[];
  /** @internal */
  private poa_list_: Poa[];


  /** @internal */
  get_connection(endpoint: EndPoint): Connection {
    let existed = this.opened_connections_.find(c => c.endpoint.equal(endpoint));
    if (existed) return existed;

    let con = new Connection(endpoint);
    this.opened_connections_.push(con);
    return con;
  }

  public create_poa(poa_size: number): Poa {
    let poa = new Poa(this.last_poa_id_++, poa_size);
    this.poa_list_.push(poa)
    return poa;
  }

  /** @internal */
  public get_poa(poa_idx: number): Poa {
    if (poa_idx >= this.poa_list_.length || poa_idx < 0)
      return null;
    return this.poa_list_[poa_idx];
  }

  public async call(endpoint: EndPoint, buffer: FlatBuffer, timeout_ms: number = 2500): Promise<any> {
    return this.get_connection(endpoint).send_receive(buffer, timeout_ms);
  }

  /**
   * Initiate a server→client stream. Sends StreamInitialization and returns a StreamReader.
   * Generated stubs should use this instead of calling rpc.call() directly for stream methods.
   */
  public async open_stream<T = Uint8Array>(
    endpoint: EndPoint,
    poa_idx: number,
    object_id: bigint,
    interface_idx: number,
    func_idx: number,
    timeout_ms: number = 2500,
    deserialize: (data: Uint8Array) => T = bytes_deserializer as unknown as (data: Uint8Array) => T
  ): Promise<StreamReader<T>> {
    const conn = this.get_connection(endpoint);
    const stream_id = conn.stream_manager.generate_stream_id();
    const reader = conn.stream_manager.create_reader(stream_id, deserialize);

    const buf = FlatBuffer.create();
    const init_size = 32; // StreamInit fixed layout: stream_id(8)+poa_idx(2)+pad(6)+object_id(8)+func_idx(1)+pad(7)
    buf.prepare(header_size + init_size);
    buf.commit(header_size + init_size);
    buf.write_msg_id(impl.MessageId.StreamInitialization);
    buf.write_msg_type(impl.MessageType.Request);
    impl.marshal_StreamInit(buf, header_size, {
      stream_id,
      poa_idx,
      interface_idx,
      object_id,
      func_idx,
    });
    buf.write_len(buf.size - 4);

    // send_receive will block until the server sends the ACK (Success)
    await conn.send_receive(buf, timeout_ms);

    // After ACK, chunks will arrive asynchronously via on_read → stream_manager
    return reader;
  }

  /** @internal */
  public static async read_host(): Promise<HostInfo> {
    let x = await fetch("./host.json");
    if (!x.ok)
      throw "read_host error: " + x.statusText;


    const reviver = (key:string, value: any) => {
      if (key === 'object_id')
        return BigInt(value);

      return value;
    }

    let info = JSON.parse(await x.text(), reviver);
    if (info.secured == undefined) info.secured = false;

    host_info.secured = info.secured;

    for (let key of Object.keys(info.objects)) {
      try {
        const obj = info.objects[key] = new ObjectProxy(info.objects[key]);
        obj.select_endpoint();
      } catch (e) {
        console.error("Error creating ObjectProxy for key: " + key, e);
        delete info.objects[key];
      }
    }

    return info;
  }

  /** @internal */
  constructor(host_info: HostInfo) {
    this.last_poa_id_ = 0;
    this.opened_connections_ = new Array<Connection>();
    this.poa_list_ = new Array<Poa>();
    host_info = this.host_info = host_info;
  }
}

interface StorageData<T> {
  oid: bigint,
  obj: T
}

const index = (oid: bigint): number => {
  return Number(0xFFFFFFFFn & oid);
}

const generation_index = (oid: bigint): number => {
  return Number(oid >> 32n);
}

class Storage<T> {
  private max_size_: number;
  private data_: Array<StorageData<T>>;
  private tail_idx_: number;

  public add(val: T): bigint {
    let old_free_idx = this.tail_idx_;
    if (old_free_idx == this.max_size_)
      return invalid_object_id;

    let old_free = this.data_[this.tail_idx_];
    this.tail_idx_ = index(old_free.oid); // next free
    old_free.obj = val;

    return BigInt(old_free_idx) | (BigInt(generation_index(old_free.oid)) << 32n);
  }

  public remove(oid: bigint): void {
    let idx = index(oid);

    this.data_[idx].oid = BigInt(this.tail_idx_) | BigInt(generation_index(oid) + 1);
    this.data_[idx].obj = null;

    this.tail_idx_ = idx;
  }

  public get(oid: bigint): T {
    let idx = index(oid);
    if (idx >= this.max_size_)
      return null;

    let obj = this.data_[idx];

    if (generation_index(obj.oid) != generation_index(oid))
      return null;

    return obj.obj;
  }

  constructor(max_size: number) {
    this.max_size_ = max_size;
    this.data_ = new Array<StorageData<T>>(this.max_size_);
    for (let i = 0; i < this.max_size_; ++i)
      this.data_[i] = { oid: BigInt(i + 1), obj: null };

    Object.seal(this.data_);
    this.tail_idx_ = 0;
  }
}

export class Poa {
  /** @internal */
  private object_map_: Storage<ObjectServant>;
  /** @internal */
  private index_: poa_idx_t

  public get index() { return this.index_; }

  public get_object(oid: bigint) {
    return this.object_map_.get(oid);
  }

  public activate_object(obj: ObjectServant): ObjectId {
    obj.poa_ = this;
    obj.activation_time_ = Date.now();

    let object_id_internal = this.object_map_.add(obj);
    if (object_id_internal === invalid_object_id)
      throw new Exception("Poa fixed size has been exceeded")

    obj.object_id_ = object_id_internal;
    obj.ref_cnt_ = 0;

    let oid: ObjectId = {
      object_id: object_id_internal,
      poa_idx: this.index,
      flags: detail.ObjectFlag.Tethered,
      origin: new Uint8Array(16).fill(0), // origin is not used in JS
      class_id: obj.get_class(),
      urls: "", // urls is not used in JS
    };

    return oid;
  }

  public deactivate_object(object_id: oid_t): void {
    //auto obj = object_map_.get(object_id);
    //if (obj) {
    //  obj->to_delete_.store(true);
    //  object_map_.remove(object_id);
    //} else {
    //  std::cerr << "deactivate_object: object not found. id = " << object_id << '\n';
    //}
  }

  constructor(index: poa_idx_t, max_size: number) {
    this.index_ = index;
    this.object_map_ = new Storage<ObjectServant>(max_size);
  }
}

export class ObjectProxy {
  public data: ObjectId;
  /** @internal */
  local_ref_cnt_: number;
  /** @internal */
  timeout_ms_: number;
  /** @internal */
  endpoint_: EndPoint;

  constructor(data?: ObjectId) {
    if (!data) (this.data as any) = {}
    else this.data = data;
    this.timeout_ms_ = 1000;
    this.local_ref_cnt_ = 0;
  }

  public get endpoint(): EndPoint {
    if (this.endpoint_ !== undefined)
      return this.endpoint_;

    this.select_endpoint();
    return this.endpoint_;
  }

  public get timeout() { return this.timeout_ms_; }

  public add_ref(): number {
    this.local_ref_cnt_++;
    if (this.local_ref_cnt_ != 1) return this.local_ref_cnt_;

    const msg_size = header_size + 16;
    let buf = FlatBuffer.create(msg_size);
    buf.write_len(msg_size - 4);
    buf.write_msg_id(impl.MessageId.AddReference);
    buf.write_msg_type(impl.MessageType.Request);
    let msg: detail.ObjectIdLocal = {
      poa_idx: this.data.poa_idx,
      object_id: this.data.object_id
    };
    detail.marshal_ObjectIdLocal(buf, header_size, msg);
    buf.commit(msg_size);

    rpc.call(this.endpoint, buf, this.timeout).then().catch();

    return this.local_ref_cnt_;
  }

  public release(): number {
    if (--this.local_ref_cnt_ != 0)
      return this.local_ref_cnt_;

    const msg_size = header_size + 16;
    let buf = FlatBuffer.create(msg_size);
    buf.write_len(msg_size - 4);
    buf.write_msg_id(impl.MessageId.ReleaseObject);
    buf.write_msg_type(impl.MessageType.Request);
    let msg: detail.ObjectIdLocal = {
      poa_idx: this.data.poa_idx,
      object_id: this.data.object_id
    };
    detail.marshal_ObjectIdLocal(buf, header_size, msg);
    buf.commit(msg_size);

    rpc.call(this.endpoint, buf, this.timeout).then().catch();

    return 0;
  }

  public select_endpoint(remote_endpoint?: EndPoint): void {
    const oid = this.data;
    const urls = oid.urls.split(";");

    if (host_info.secured) {
      const wss = urls.find(url => url.startsWith("wss://"));
      if (!wss) throw new Exception("Object has no urls for secured connection");
      this.endpoint_ = EndPoint.from_string(wss);
    } else {
      const ws = urls.find(url => url.startsWith("ws://"));
      if (!ws) throw new Exception("Object has no urls for unsecured connection");
      this.endpoint_ = EndPoint.from_string(ws);
    }

    if (!remote_endpoint)
      return;

    // if remote_endpoint is provided, use it to override the hostname
    if (this.endpoint_.hostname === "localhost" || 
      this.endpoint_.hostname === "127.0.0.1")
    {
      this.endpoint_.hostname = remote_endpoint.hostname;
    }
  }

  /**
   * Serialize this object reference to a string (like CORBA IOR).
   * Format: "NPRPC1:<base64_encoded_data>"
   */
  public toString(): string {
    const oid = this.data;
    
    // Calculate total size
    const class_id_bytes = u8enc.encode(oid.class_id);
    const urls_bytes = u8enc.encode(oid.urls);
    
    // Binary format: object_id(8) + poa_idx(2) + flags(2) + origin(16) + 
    //                class_id_len(4) + class_id + urls_len(4) + urls
    const total_size = 8 + 2 + 2 + 16 + 4 + class_id_bytes.length + 4 + urls_bytes.length;
    const buffer = new ArrayBuffer(total_size);
    const view = new DataView(buffer);
    const bytes = new Uint8Array(buffer);
    
    let offset = 0;
    
    // object_id (u64, little-endian)
    view.setBigUint64(offset, oid.object_id, true);
    offset += 8;
    
    // poa_idx (u16, little-endian)
    view.setUint16(offset, oid.poa_idx, true);
    offset += 2;
    
    // flags (u16, little-endian)
    view.setUint16(offset, oid.flags, true);
    offset += 2;
    
    // origin (16 bytes UUID)
    bytes.set(oid.origin, offset);
    offset += 16;
    
    // class_id_len (u32, little-endian) + class_id
    view.setUint32(offset, class_id_bytes.length, true);
    offset += 4;
    bytes.set(class_id_bytes, offset);
    offset += class_id_bytes.length;
    
    // urls_len (u32, little-endian) + urls
    view.setUint32(offset, urls_bytes.length, true);
    offset += 4;
    bytes.set(urls_bytes, offset);
    
    // Base64 encode
    const base64 = btoa(String.fromCharCode(...bytes));
    return "NPRPC1:" + base64;
  }

  /**
   * Create an ObjectProxy from a serialized string.
   * @param str The string in format "NPRPC1:<base64_encoded_data>"
   * @returns A new ObjectProxy, or null if parsing fails
   */
  public static fromString(str: string): ObjectProxy | null {
    const prefix = "NPRPC1:";
    if (!str.startsWith(prefix)) {
      return null;
    }
    
    try {
      const base64 = str.substring(prefix.length);
      const binary = atob(base64);
      const bytes = new Uint8Array(binary.length);
      for (let i = 0; i < binary.length; i++) {
        bytes[i] = binary.charCodeAt(i);
      }
      
      const view = new DataView(bytes.buffer);
      let offset = 0;
      
      // object_id (u64, little-endian)
      const object_id = view.getBigUint64(offset, true);
      offset += 8;
      
      // poa_idx (u16, little-endian)
      const poa_idx = view.getUint16(offset, true);
      offset += 2;
      
      // flags (u16, little-endian)
      const flags = view.getUint16(offset, true);
      offset += 2;
      
      // origin (16 bytes UUID)
      const origin = new Uint8Array(bytes.buffer, offset, 16);
      offset += 16;
      
      // class_id_len (u32, little-endian) + class_id
      const class_id_len = view.getUint32(offset, true);
      offset += 4;
      const class_id = u8dec.decode(new Uint8Array(bytes.buffer, offset, class_id_len));
      offset += class_id_len;
      
      // urls_len (u32, little-endian) + urls
      const urls_len = view.getUint32(offset, true);
      offset += 4;
      const urls = u8dec.decode(new Uint8Array(bytes.buffer, offset, urls_len));
      
      const oid: ObjectId = {
        object_id,
        poa_idx,
        flags,
        origin: new Uint8Array(origin), // copy to avoid detaching issues
        class_id,
        urls
      };
      
      return new ObjectProxy(oid);
    } catch (e) {
      return null;
    }
  }
}

export abstract class ObjectServant {
  poa_: Poa;
  object_id_: oid_t;
  activation_time_: number;
  ref_cnt_: number;

  public abstract dispatch(buf: FlatBuffer, endpoint: EndPoint, from_parent: boolean): void;
  public abstract get_class(): string;

  public get poa(): Poa { return this.poa_; }
  public get oid(): oid_t { return this.object_id_; }

  public add_ref(): number {
    let cnt = ++this.ref_cnt_;

    //if (cnt == 1 && static_cast<impl::PoaImpl*>(poa())->pl_lifespan == Policy_Lifespan::Transient) {
    //  std::lock_guard<std::mutex> lk(impl::g_rpc->new_activated_objects_mut_);
    //  auto& list = impl::g_rpc->new_activated_objects_;
    //  list.erase(std::find(begin(list), end(list), this));
    //}

    return cnt;
  }

  constructor() {
    this.ref_cnt_ = 0;
  }
};

export const make_simple_answer = (buf: FlatBuffer, message_id: impl.MessageId): void => {
  buf.consume(buf.size);
  buf.prepare(header_size);
  buf.write_len(header_size - 4);
  buf.write_msg_id(message_id);
  buf.write_msg_type(impl.MessageType.Answer);
  buf.commit(header_size);
}

export class ReferenceList {
  refs: Array<{ object_id: detail.ObjectIdLocal, obj: ObjectServant }>;

  public add_ref(obj: ObjectServant): void {
    this.refs.push({
      object_id: { poa_idx: obj.poa.index, object_id: obj.oid },
      obj: obj
    });
    obj.add_ref();
  }

  // false - reference not exist
  public remove_ref(poa_idx: poa_idx_t, oid: oid_t): boolean {
    return false;
  }

  constructor() {
    this.refs = new Array<{ object_id: detail.ObjectIdLocal, obj: ObjectServant }>();
  }
};

// Helper function to handle standard reply messages (Success, Exception, Errors).
export const handle_standart_reply = (buf: FlatBuffer): number => {
  if (buf.size < 16)
    throw new ExceptionBadInput();

  switch (buf.read_msg_id()) {
    case impl.MessageId.Success:
      return 0;

    case impl.MessageId.Exception:
      return 1;

    // Errors should be always handled
    // Don't forget to add case for each of them, when IDL changes
    case impl.MessageId.Error_PoaNotExist:
      throw new Exception("POA does not exist");
    
    case impl.MessageId.Error_ObjectNotExist:
      throw new ExceptionObjectNotExist();

    case impl.MessageId.Error_CommFailure:
      let ex_obj = unmarshal_ExceptionCommFailure(buf, 16);
      throw new ExceptionCommFailure(ex_obj.what);

    case impl.MessageId.Error_UnknownFunctionIdx:
      throw new ExceptionUnknownFunctionIndex();

    case impl.MessageId.Error_UnknownMessageId:
      throw new ExceptionUnknownMessageId();

    case impl.MessageId.Error_BadAccess:
      throw new ExceptionBadAccess();

    case impl.MessageId.Error_BadInput:
      throw new ExceptionBadInput();

    case impl.MessageId.Error_Unknown:
      throw new Exception("Unknown error");

    default:
      // Should be handled by caller (for example, for stream messages or simple answers)
      return -1;
  }
}

// Old helper functions removed - they used _Direct classes that no longer exist
// export const oid_create_from_flat = ...
// export function oid_assign_from_ts(...) { ... }

export const narrow = <T extends ObjectProxy>(from: ObjectProxy, to: new () => T): T => {
  if (from.data.class_id !== (to as any).servant_t._get_class()) {
    console.warn("fail: narrowing from " + from.data.class_id + " to " + (to as any).servant_t._get_class());
    return null;
  }

  let obj = new to();
  obj.data = from.data;
  obj.local_ref_cnt_ = from.local_ref_cnt_;
  obj.timeout_ms_ = from.timeout_ms_;

  from.data = null;
  (from as ObjectProxy).local_ref_cnt_ = 0;

  return obj;
}

/**
 * Create ObjectProxy from ObjectId and remote endpoint
 * @param oid ObjectId
 * @param remote_endpoint Remote endpoint (used if ObjectId is tethered)
 * @returns ObjectProxy or null if oid is invalid
 */
export const create_object_from_oid = (
  oid: ObjectId,
  remote_endpoint: EndPoint): ObjectProxy =>
{
  if (oid.object_id == invalid_object_id)
    return null;

  const obj = new ObjectProxy(oid);

  if (oid.flags & detail.ObjectFlag.Tethered) {
    obj.data.urls = remote_endpoint.to_string();
    obj.endpoint_ = remote_endpoint;
  } else {
    obj.select_endpoint(remote_endpoint);
  }

  return obj;
}

// Old helper function removed - used _Direct classes that no longer exist
// export const create_object_from_flat = ...

export const init = async (use_host_json: boolean = true): Promise<Rpc> => {
  return rpc ? rpc : (rpc = new Rpc(use_host_json ? await Rpc.read_host() : {} as HostInfo));
}

export const setLogLevel = (logLevel: LogLevel): void => {
  gLogLevel = logLevel;
}
