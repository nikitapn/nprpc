import * as Flat from './flat';
import { FlatBuffer } from './flat_buffer';

export { Flat, FlatBuffer };
export * from './exception'


export * from "./nprpc";
export * from "./nprpc_base";

import { Exception } from "./exception";
import { sip4_to_u32, ip4tostr, MyPromise } from "./utils";
//import { FlatBuffer } from "./flat_buffer";
import {
	impl, detail, oid_t, poa_idx_t,
	DebugLevel, 
	ExceptionObjectNotExist, 
	ExceptionUnknownFunctionIndex, 
	ExceptionUnknownMessageId, 
	ExceptionCommFailure, 
	ExceptionUnsecuredObject,
	ExceptionBadAccess
} from "./nprpc_base"

const header_size = 16;
const invalid_object_id = 0xFFFFFFFFFFFFFFFFn;
const localhost_ip4 = 0x7F000001;

export let rpc: Rpc;

let g_debug_level: DebugLevel = DebugLevel.DebugLevel_Critical;

export class EndPoint {
	constructor(public ip4: number, public port: number, public hostname: string, public secured: boolean) {}

	public equal(other: EndPoint): boolean {
		if (this.secured === false) {
			return this.ip4 === other.ip4 && this.port === other.port;
		} else {
			return this.hostname === other.hostname && this.port === other.port;
		}
	}
};

interface HostInfo {
	secured: boolean;
	objects: any;
}

interface Work {
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
	queue: Work[];

	private async perform_one() {
		this.ws.send(this.queue[0].buffer.writable_view);
	}

	private on_open() {
		if (this.queue.length) this.perform_one();
	}

	public async send_receive(buffer: FlatBuffer, timeout_ms: number): Promise<any> {
		let promise = new MyPromise<void, Error>();
		this.queue.push({ buffer: buffer, promise: promise });
		if (this.ws.readyState && this.queue.length == 1) this.perform_one();
		return promise.$;
	}

	private on_read(ev: MessageEvent<any>) {
		let buf = FlatBuffer.from_array_buffer(ev.data as ArrayBuffer);
		if (buf.read_msg_type() == impl.MessageType.Answer) {
			let task = this.queue[0];
			task.buffer.set_buffer(ev.data as ArrayBuffer);
			this.queue.shift();
			task.promise.set_promise();
			if (this.queue.length) this.perform_one();
		} else {
			switch (buf.read_msg_id()) {
				case impl.MessageId.FunctionCall: {
					let ch = new impl.Flat_nprpc_base.CallHeader_Direct(buf, header_size);

					if (g_debug_level >= DebugLevel.DebugLevel_EveryCall) {
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
					let msg = new detail.Flat_nprpc_base.ObjectIdLocal_Direct(buf, header_size);

					//detail::ObjectIdLocal oid{ msg.poa_idx(), msg.object_id() };
					if (g_debug_level >= DebugLevel.DebugLevel_EveryCall) {
						console.log("AddReference. poa_idx: " + msg.poa_idx + " , oid: " + msg.object_id);
					}

	
					let obj = get_object(buf, msg.poa_idx, msg.object_id)
	
					if (obj) {
						//	if (g_cfg.debug_level >= DebugLevel_EveryCall) {
						//		std::cout << "Refference added." << std::endl;
						//	}

						make_simple_answer(buf, impl.MessageId.Success);
					} 

					break;
				}
				case impl.MessageId.ReleaseObject: {
					let msg = new detail.Flat_nprpc_base.ObjectIdLocal_Direct(buf, header_size);
					//detail::ObjectIdLocal oid{ msg.poa_idx(), msg.object_id() };

					//if (g_cfg.debug_level >= DebugLevel_EveryCall) {
					//	std::cout << "ReleaseObject. " << "poa_idx: " << oid.poa_idx << ", oid: " << oid.object_id << std::endl;
					//}

					//if (ref_list_.remove_ref(msg.poa_idx(), msg.object_id())) {
					//	make_simple_answer(rx_buffer_(), nprpc::impl::MessageId::Success);
					//} else {
					//	make_simple_answer(rx_buffer_(), nprpc::impl::MessageId::Error_ObjectNotExist);
					//}

					break;
				}
				default:
					make_simple_answer(buf, impl.MessageId.Error_UnknownMessageId);
					break;
			}
			this.ws.send(buf.writable_view);
		}
	}

	private on_close() { }

	private on_error(ev: Event) {
		///	if (buf.read_msg_type() == impl.MessageType.Answer) {
		//		let task = this.queue[0];
		//		task.buffer.set_buffer(ev.data as ArrayBuffer);
		//		this.queue.shift();
		//		task.promise.set_promise();
		//	}
	}

	constructor(endpoint: EndPoint) {
		this.endpoint = endpoint;
		this.queue = new Array<Work>();

		if (rpc.host_info.secured) {
			this.ws = new WebSocket('wss://' + this.endpoint.hostname + ':' + this.endpoint.port.toString(10));
		} else {
			// prefer hostname over ip address
			const name_or_ip = this.endpoint.hostname ?? ip4tostr(this.endpoint.ip4); 
			this.ws = new WebSocket('ws://' + name_or_ip + ':' + this.endpoint.port.toString(10));
		}

		this.ws.binaryType = 'arraybuffer';
		this.ws.onopen = this.on_open.bind(this);
		this.ws.onclose = this.on_close.bind(this);
		this.ws.onmessage = this.on_read.bind(this);
		this.ws.onerror = this.on_error.bind(this);
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
	
	get_connection(endpoint: EndPoint): Connection {
		let founded: Connection = this.opened_connections_.find(c => c.endpoint.equal(endpoint));
		if (founded) return founded;

		let con = new Connection(endpoint);
		this.opened_connections_.push(con);
		return con;
	}

	public create_poa(poa_size: number): Poa {
		let poa = new Poa(this.last_poa_id_++, poa_size);
		this.poa_list_.push(poa)
		return poa; 
	}

	public get_poa(poa_idx: number): Poa {
		if (poa_idx >= this.poa_list_.length || poa_idx < 0) return null;
		return this.poa_list_[poa_idx];
	}

	public async call(endpoint: EndPoint, buffer: FlatBuffer, timeout_ms: number = 2500): Promise<any> {
		return this.get_connection(endpoint).send_receive(buffer, timeout_ms);
	}

	public static async read_host(): Promise<HostInfo> {
		let x = await fetch("./host.json");
		if (!x.ok) throw "read_host error: " + x.statusText;
	
		const reviver = (key:string, value: any) => {
			if (key === 'object_id') {
				return BigInt(value);
			}
			return value;
		}

		let info = JSON.parse(await x.text(), reviver);
		if (info.secured == undefined) info.secured = false;

		for (let key of Object.keys(info.objects)) {
			info.objects[key] = new ObjectProxy(info.objects[key]);
		}

		return info;
	}

	constructor(host_info: HostInfo) {
		this.last_poa_id_ = 0;
		this.opened_connections_ = new Array<Connection>();
		this.poa_list_ = new Array<Poa>();
		this.host_info = host_info;
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
		if (old_free_idx == this.max_size_) return invalid_object_id;

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
		if (idx >= this.max_size_) return null;

		let obj = this.data_[idx];

		if (generation_index(obj.oid) != generation_index(oid)) return null;

		return obj.obj;
	}

	constructor(max_size: number) {
		this.max_size_ = max_size;
		this.data_ = new Array<StorageData<T>>(this.max_size_);
		for (let i = 0; i < this.max_size_; ++i) {
			this.data_[i] = { oid: BigInt(i + 1), obj: null };
		}
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
	public activate_object(obj: ObjectServant): detail.ObjectId {
		obj.poa_ = this;
		obj.activation_time_ = Date.now();

		let object_id_internal = this.object_map_.add(obj);
		if (object_id_internal === invalid_object_id) throw new Exception("Poa fixed size has been exceeded")
		obj.object_id_ = object_id_internal;
		obj.ref_cnt_ = 0;

		let oid: detail.ObjectId = {
			object_id: object_id_internal,
			ip4: localhost_ip4,
			port: 0,
			websocket_port: 0,
			poa_idx: this.index,
			flags: (1 << detail.ObjectFlag.WebObject), // + policy
			class_id: obj.get_class(),
			hostname: "" // clients doesn't have a hostname
		};

		//if (pl_lifespan == Policy_Lifespan::Type::Transient) {
		//	std::lock_guard<std::mutex> lk(g_orb->new_activated_objects_mut_);
		//	g_orb->new_activated_objects_.push_back(obj);
		//}

		return oid;
	}

	public deactivate_object(object_id: oid_t): void {
		//auto obj = object_map_.get(object_id);
		//if (obj) {
		//	obj->to_delete_.store(true);
		//	object_map_.remove(object_id);
		//} else {
		//	std::cerr << "deactivate_object: object not found. id = " << object_id << '\n';
		//}
	}

	constructor(index: poa_idx_t, max_size: number) {
		this.index_ = index;
		this.object_map_ = new Storage<ObjectServant>(max_size);
	}
}

export class ObjectProxy {
	public data: detail.ObjectId;
	/** @internal */
	local_ref_cnt_: number;
	/** @internal */
	timeout_ms_: number;
	/** @internal */
	endpoint_: EndPoint;

	constructor(data?: detail.ObjectId) {
		if (!data) (this.data as any) = new Object()
		else this.data = data;
		this.timeout_ms_ = 1000;
		this.local_ref_cnt_ = 0;
	}

	public endpoint(): EndPoint {
		if (this.endpoint_ !== undefined) return this.endpoint_;
		return (this.endpoint_ = new EndPoint(this.data.ip4, 
			this.data.websocket_port, 
			this.data.hostname, 
			(this.data.flags & (1 << detail.ObjectFlag.Secured)) == 0 ? false : true 
			));
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
		let msg = new detail.Flat_nprpc_base.ObjectIdLocal_Direct(buf, header_size);
		msg.poa_idx = this.data.poa_idx;
		msg.object_id = this.data.object_id;
		buf.commit(msg_size);

		rpc.call(this.endpoint(), buf, this.timeout).then().catch();

		return this.local_ref_cnt_;
	}

	public release(): number {
		if (--this.local_ref_cnt_ != 0) return this.local_ref_cnt_;

		const msg_size = header_size + 16;
		let buf = FlatBuffer.create(msg_size);
		buf.write_len(msg_size - 4);
		buf.write_msg_id(impl.MessageId.ReleaseObject);
		buf.write_msg_type(impl.MessageType.Request);
		let msg = new detail.Flat_nprpc_base.ObjectIdLocal_Direct(buf, header_size);
		msg.poa_idx = this.data.poa_idx;
		msg.object_id = this.data.object_id;
		buf.commit(msg_size);

		rpc.call(this.endpoint(), buf, this.timeout).then().catch();

		return 0;
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
		//	std::lock_guard<std::mutex> lk(impl::g_orb->new_activated_objects_mut_);
		//	auto& list = impl::g_orb->new_activated_objects_;
		//	list.erase(std::find(begin(list), end(list), this));
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


//  0 - Success
//  1 - exception
// -1 - not handled
export const handle_standart_reply = (buf: FlatBuffer): number => {
	//if (buf.size < 8) throw new ExceptionCommFailure();
	switch (buf.read_msg_id()) {
		case impl.MessageId.Success:
			return 0;
		case impl.MessageId.Exception:
			return 1;
		case impl.MessageId.Error_ObjectNotExist:
			throw new ExceptionObjectNotExist();
		case impl.MessageId.Error_CommFailure:
			throw new ExceptionCommFailure();
		case impl.MessageId.Error_UnknownFunctionIdx:
			throw new ExceptionUnknownFunctionIndex();
		case impl.MessageId.Error_UnknownMessageId:
			throw new ExceptionUnknownMessageId();
			case impl.MessageId.Error_BadAccess:
			throw new ExceptionBadAccess();
		default:
			return -1;
	}
}

export const oid_create_from_flat = (o: detail.Flat_nprpc_base.ObjectId_Direct): detail.ObjectId => {
	return {
		object_id: o.object_id,
		ip4: o.ip4,
		port: o.port,
		websocket_port: o.websocket_port,
		poa_idx: o.poa_idx,
		flags: o.flags,
		class_id: o.class_id,
		hostname: o.hostname
	}
}

export const narrow = <T extends ObjectProxy>(from: ObjectProxy, to: new () => T): T => {
	if (from.data.class_id !== (to as any).servant_t._get_class()) return null;
	let obj = new to();

	obj.data = from.data;

	obj.local_ref_cnt_ = (from as ObjectProxy).local_ref_cnt_;
	obj.timeout_ms_ = (from as ObjectProxy).timeout_ms_;

	from.data = null;
	(from as ObjectProxy).local_ref_cnt_ = 0;

	return obj;
}

export const convert_object_ip = (oid: detail.ObjectId, remote_ip: number): detail.ObjectId => {
	if (remote_ip == localhost_ip4) {
		// could be the object on the same machine or from any other location
	} else {
		if (oid.ip4 == localhost_ip4) {
			// remote object has localhost ip converting to endpoint ip
			oid.ip4 = remote_ip;
		} else {
			// remote object with ip != localhost
		}
	}

	if (rpc.host_info.secured && !(oid.flags & (1 << detail.ObjectFlag.Secured))) { //received unsecured object under the ssl context
		throw new ExceptionUnsecuredObject(oid.class_id);
	}

	return oid;
}

export const create_object_from_flat = (oid: detail.Flat_nprpc_base.ObjectId_Direct, remote_ip: number): ObjectProxy => {
	if (oid.object_id == invalid_object_id) return null;

	let obj = new ObjectProxy();

	obj.data.object_id = oid.object_id;
	obj.data.ip4 = oid.ip4;
	obj.data.port = oid.port;
	obj.data.websocket_port = oid.websocket_port;
	obj.data.poa_idx = oid.poa_idx;
	obj.data.flags = oid.flags;
	obj.data.class_id = oid.class_id;
	obj.data.hostname = oid.hostname;

	convert_object_ip(obj.data, remote_ip);

	return obj;
}

export const init = async (): Promise<Rpc> => {
	if (rpc !== undefined) return rpc;
	return (rpc = new Rpc(await Rpc.read_host()));
}

export const set_debug_level = (debug_level: DebugLevel): void => {
	g_debug_level = debug_level;
}