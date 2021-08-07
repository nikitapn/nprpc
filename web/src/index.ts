// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

export * from "./nprpc";
export * from "./nprpc_base";

import { Exception } from "./exception";
import { FlatBuffer } from "./flat_buffer";
import { impl, detail, oid_t, poa_idx_t } from "./nprpc_base"

const header_size = 16;

export interface EndPoint {
	ip4: number;
	port: number;
};


export interface ref<T> {
	value: T;
}

export function make_ref(): ref<any> {
	return { value: undefined };
}

class MyPromise<T, E extends Exception>  {
	private actual_promise_: Promise<T>
	private resolve_: (x: T) => void;
	private reject_: (e: E) => void;
	
	constructor() {
		this.actual_promise_ = new Promise<T>((resolve, reject) => { 
			this.resolve_ = (x) => { resolve(x) }; 
			this.reject_ = (x) => { reject(x)}; 
		});
	}

	public get $() { return this.actual_promise_; }

	public set_promise(value: T): void {
		this.resolve_(value);
	}

	public set_exception(ec: E): void {
		this.reject_(ec);
	}
}

interface Work {
	buffer: FlatBuffer;
	promise: MyPromise<void, Error>;
}

export class Connection {
	endpoint: EndPoint;
	ws: WebSocket;
	queue: Work[];

	private perform_one() {
		this.ws.send(this.queue[0].buffer.writable_view)
	}

	private on_open() {
		if (this.queue.length) this.perform_one();
	}

	public async send_receive(buffer: FlatBuffer, timeout_ms: number): Promise<any> {
		let promise = new MyPromise<void, Error>();
		this.queue.push({buffer: buffer, promise: promise} );
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
		
		}
	}

	private on_close() {}

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

		let ip4tostr = (ip4: number): string => (ip4 >> 24 & 0xFF) + '.' + (ip4 >> 16 & 0xFF) + '.' + (ip4 >> 8 & 0xFF) + '.' + (ip4 & 0xFF);

		this.ws = new WebSocket('ws://' + ip4tostr(this.endpoint.ip4) + ':' + this.endpoint.port.toString(10));
		this.ws.binaryType = 'arraybuffer';
		this.ws.onopen = this.on_open.bind(this);
		this.ws.onclose = this.on_close.bind(this);
		this.ws.onmessage = this.on_read.bind(this);
		this.ws.onerror = this.on_error.bind(this);
	}
}

export class Rpc {
	opened_connections_: Connection[];

	 get_connection(endpoint: EndPoint): Connection {
		let founded: Connection = this.opened_connections_.find(c => c.endpoint == endpoint);
		if (founded) return founded;
		
		let con = new Connection(endpoint);
		this.opened_connections_.push(con);
		return con;
	}

	public async call(endpoint: EndPoint, buffer: FlatBuffer, timeout_ms: number = 2500): Promise<any> {
		return this.get_connection(endpoint).send_receive(buffer, timeout_ms);
	}

	constructor () {
		this.opened_connections_ = new Array<Connection>();
	}
}

export class Poa {
	constructor (private index_: poa_idx_t) {}

	public get index() { return this.index_; }

	public activate_object(obj: ObjectServant): detail.ObjectId {
		//obj->poa_ = this;
		//obj->activation_time_ = std::chrono::steady_clock::now();

		//ObjectId oid;
		//oid.ip4 = localhost_ip4;
		//oid.port = g_orb->port();
		//oid.poa_idx = get_index();
		//obj->object_id_ = oid.object_id = object_map_.add(obj);
		//oid.flags = (pl_lifespan << Object::Flags::Lifespan);
		//oid.class_id = obj->get_class();
		
		//if (pl_lifespan == Policy_Lifespan::Type::Transient) {
		//	std::lock_guard<std::mutex> lk(g_orb->new_activated_objects_mut_);
		//	g_orb->new_activated_objects_.push_back(obj);
		//}

		//return oid;
		return null;
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
}

export class ObjectProxy  {
	public data: detail.ObjectId;
	
	private timeout_ms_: number;
	
	constructor() { 
		(this.data as any) = new Object()
		this.timeout_ms_ = 1000;
	}

	public get timeout() { return this.timeout_ms_; }

}

export class ObjectServant {
	poa_: Poa;
	object_id_: oid_t;

	ref_cnt_: number;
	
	constructor () {
		this.ref_cnt_ = 0;
	}
	
	public get poa(): Poa { return this.poa_; }
	public get oid(): oid_t { return this.object_id_; }
	public get poa_index(): poa_idx_t { return this.poa_.index; }

	public add_ref(): number {
		let cnt = ++this.ref_cnt_;

		//if (cnt == 1 && static_cast<impl::PoaImpl*>(poa())->pl_lifespan == Policy_Lifespan::Transient) {
		//	std::lock_guard<std::mutex> lk(impl::g_orb->new_activated_objects_mut_);
		//	auto& list = impl::g_orb->new_activated_objects_;
		//	list.erase(std::find(begin(list), end(list), this));
		//}

		return cnt;
	}
};

export function make_simple_answer(buf: FlatBuffer, message_id: impl.MessageId) {
	buf.consume(buf.size);
	buf.prepare(header_size);
	buf.write_len(header_size - 4);
	buf.write_msg_id(message_id);
	buf.write_msg_type(impl.MessageType.Answer);
	buf.commit(header_size);
}

export class ReferenceList {
	refs: Array<{object_id: detail.ObjectIdLocal, obj: ObjectServant}>;

	public add_ref(obj: ObjectServant): void {
		this.refs.push({
			object_id: {poa_idx: obj.poa_index, object_id: obj.oid}, 
			obj: obj
		});
		obj.add_ref();
	}
	// false - reference not exist
	public remove_ref(poa_idx: poa_idx_t, oid: oid_t): boolean {
		return false;
	}

	constructor() {
		this.refs = new Array<{object_id: detail.ObjectIdLocal, obj: ObjectServant}>();
	}
};


//  0 - Success
//  1 - exception
// -1 - not handled
export function handle_standart_reply(buf: FlatBuffer): number {
	//if (buf.size < 8) throw new ExceptionCommFailure();
	switch (buf.read_msg_id()) {
	case impl.MessageId.Success:
		return 0;
	case impl.MessageId.Exception:
		return 1;
	case impl.MessageId.Error_ObjectNotExist:
		//throw ExceptionObjectNotExist();
	case impl.MessageId.Error_CommFailure:
		//throw ExceptionCommFailure();
	case impl.MessageId.Error_UnknownFunctionIdx:
		//throw ExceptionUnknownFunctionIndex();
	case impl.MessageId.Error_UnknownMessageId:
		//throw ExceptionUnknownMessageId();
	default:
		return -1;
	}
}




interface Global {
	rpc: Rpc;
}

let g: Global = 
{
	rpc: null
}

export function init() {
	g.rpc = new Rpc();
}

export { g }