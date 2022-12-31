import * as Flat from './flat';
import { FlatBuffer } from './flat_buffer';
export { Flat, FlatBuffer };
export * from './exception';
export * from "./nprpc";
export * from "./nprpc_base";
import { MyPromise } from "./utils";
import { impl, detail, oid_t, poa_idx_t, DebugLevel } from "./nprpc_base";
export declare let rpc: Rpc;
export declare class EndPoint {
    ip4: number;
    port: number;
    hostname: string;
    secured: boolean;
    constructor(ip4: number, port: number, hostname: string, secured: boolean);
    equal(other: EndPoint): boolean;
}
interface HostInfo {
    secured: boolean;
    objects: any;
}
interface Work {
    buffer: FlatBuffer;
    promise: MyPromise<void, Error>;
}
export declare class Connection {
    endpoint: EndPoint;
    ws: WebSocket;
    queue: Work[];
    private perform_one;
    private on_open;
    send_receive(buffer: FlatBuffer, timeout_ms: number): Promise<any>;
    private on_read;
    private on_close;
    private on_error;
    constructor(endpoint: EndPoint);
}
export declare class Rpc {
    host_info: HostInfo;
    get_connection(endpoint: EndPoint): Connection;
    create_poa(poa_size: number): Poa;
    get_poa(poa_idx: number): Poa;
    call(endpoint: EndPoint, buffer: FlatBuffer, timeout_ms?: number): Promise<any>;
    static read_host(): Promise<HostInfo>;
    constructor(host_info: HostInfo);
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
    endpoint(): EndPoint;
    get timeout(): number;
    add_ref(): number;
    release(): number;
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
export declare const oid_create_from_flat: (o: detail.Flat_nprpc_base.ObjectId_Direct) => detail.ObjectId;
export declare const narrow: <T extends ObjectProxy>(from: ObjectProxy, to: new () => T) => T;
export declare const convert_object_ip: (oid: detail.ObjectId, remote_ip: number) => detail.ObjectId;
export declare const create_object_from_flat: (oid: detail.Flat_nprpc_base.ObjectId_Direct, remote_ip: number) => ObjectProxy;
export declare const init: () => Promise<Rpc>;
export declare const set_debug_level: (debug_level: DebugLevel) => void;
