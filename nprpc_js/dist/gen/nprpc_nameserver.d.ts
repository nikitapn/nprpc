import * as NPRPC from '../index_internal';
export declare class Nameserver extends NPRPC.ObjectProxy {
    static get servant_t(): new () => _INameserver_Servant;
    Bind(obj: NPRPC.detail.ObjectId, name: string): Promise<void>;
    Resolve(name: string, obj: NPRPC.ref<NPRPC.ObjectProxy>): Promise<boolean>;
}
export interface INameserver_Servant {
    Bind(obj: NPRPC.ObjectProxy, name: string): void;
    Resolve(name: string, obj: NPRPC.detail.Flat_nprpc_base.ObjectId_Direct): boolean;
}
export declare class _INameserver_Servant extends NPRPC.ObjectServant {
    static _get_class(): string;
    readonly get_class: () => string;
    readonly dispatch: (buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean) => void;
    static _dispatch(obj: _INameserver_Servant, buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean): void;
}
export interface nprpc_nameserver_M1 {
    _1: NPRPC.detail.ObjectId;
    _2: string;
}
export declare namespace Flat_nprpc_nameserver {
    class nprpc_nameserver_M1_Direct extends NPRPC.Flat.Flat {
        get _1(): NPRPC.detail.Flat_nprpc_base.ObjectId_Direct;
        get _2(): string;
        set _2(str: string);
    }
}
export interface nprpc_nameserver_M2 {
    _1: string;
}
export declare namespace Flat_nprpc_nameserver {
    class nprpc_nameserver_M2_Direct extends NPRPC.Flat.Flat {
        get _1(): string;
        set _1(str: string);
    }
}
export interface nprpc_nameserver_M3 {
    _1: boolean;
    _2: NPRPC.detail.ObjectId;
}
export declare namespace Flat_nprpc_nameserver {
    class nprpc_nameserver_M3_Direct extends NPRPC.Flat.Flat {
        get _1(): boolean;
        set _1(value: boolean);
        get _2(): NPRPC.detail.Flat_nprpc_base.ObjectId_Direct;
    }
}
