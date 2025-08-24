import { FlatBuffer } from './flat_buffer';
import { MyPromise } from "./utils";
import { impl, detail, oid_t, poa_idx_t, DebugLevel, EndPointType } from "./gen/nprpc_base";
export declare let rpc: Rpc;
export declare class EndPoint {
    type: EndPointType;
    hostname: string;
    port: number;
    constructor(type: EndPointType, hostname: string, // or ip address
    port: number);
    static to_string(type: EndPointType): string;
    static from_string(str: string): EndPoint;
    equal(other: EndPoint): boolean;
    to_string(): string;
}
interface HostInfo {
    secured: boolean;
    objects: any;
}
interface PendingRequest {
    buffer: FlatBuffer;
    promise: MyPromise<void, Error>;
}
export declare class Connection {
    endpoint: EndPoint;
    ws: WebSocket;
    pending_requests: Map<number, PendingRequest>;
    next_request_id: number;
    private perform_request;
    private on_open;
    send_receive(buffer: FlatBuffer, timeout_ms: number): Promise<any>;
    private on_read;
    private on_close;
    private on_error;
    constructor(endpoint: EndPoint);
}
export declare class Rpc {
    host_info: HostInfo;
    create_poa(poa_size: number): Poa;
    call(endpoint: EndPoint, buffer: FlatBuffer, timeout_ms?: number): Promise<any>;
}
export declare class Poa {
    get index(): number;
    get_object(oid: bigint): ObjectServant;
    activate_object(obj: ObjectServant): detail.ObjectId;
    deactivate_object(object_id: oid_t): void;
    constructor(index: poa_idx_t, max_size: number);
}
export declare class ObjectProxy {
    data: detail.ObjectId;
    constructor(data?: detail.ObjectId);
    get endpoint(): EndPoint;
    get timeout(): number;
    add_ref(): number;
    release(): number;
    select_endpoint(remote_endpoint?: EndPoint): void;
}
export declare abstract class ObjectServant {
    poa_: Poa;
    object_id_: oid_t;
    activation_time_: number;
    ref_cnt_: number;
    abstract dispatch(buf: FlatBuffer, endpoint: EndPoint, from_parent: boolean): void;
    abstract get_class(): string;
    get poa(): Poa;
    get oid(): oid_t;
    add_ref(): number;
    constructor();
}
export declare const make_simple_answer: (buf: FlatBuffer, message_id: impl.MessageId) => void;
export declare class ReferenceList {
    refs: Array<{
        object_id: detail.ObjectIdLocal;
        obj: ObjectServant;
    }>;
    add_ref(obj: ObjectServant): void;
    remove_ref(poa_idx: poa_idx_t, oid: oid_t): boolean;
    constructor();
}
export declare const handle_standart_reply: (buf: FlatBuffer) => number;
export declare const oid_create_from_flat: (src: detail.Flat_nprpc_base.ObjectId_Direct) => detail.ObjectId;
export declare const oid_assign_from_ts: (dest: detail.Flat_nprpc_base.ObjectId_Direct, src: detail.ObjectId) => void;
export declare const narrow: <T extends ObjectProxy>(from: ObjectProxy, to: new () => T) => T;
export declare const create_object_from_flat: (direct: detail.Flat_nprpc_base.ObjectId_Direct, remote_endpoint: EndPoint) => ObjectProxy;
export declare const init: (use_host_json?: boolean) => Promise<Rpc>;
export declare const set_debug_level: (debug_level: DebugLevel) => void;
export {};
