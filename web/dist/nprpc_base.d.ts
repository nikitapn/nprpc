import * as NPRPC from './base';
export declare type poa_idx_t = number;
export declare type oid_t = bigint;
export declare type ifs_idx_t = number;
export declare type fn_idx_t = number;
export declare class ExceptionCommFailure extends NPRPC.Exception {
    constructor();
}
export declare namespace Flat_nprpc_base {
    class ExceptionCommFailure_Direct extends NPRPC.Flat.Flat {
        get __ex_id(): number;
        set __ex_id(value: number);
    }
}
export declare class ExceptionTimeout extends NPRPC.Exception {
    constructor();
}
export declare namespace Flat_nprpc_base {
    class ExceptionTimeout_Direct extends NPRPC.Flat.Flat {
        get __ex_id(): number;
        set __ex_id(value: number);
    }
}
export declare class ExceptionObjectNotExist extends NPRPC.Exception {
    constructor();
}
export declare namespace Flat_nprpc_base {
    class ExceptionObjectNotExist_Direct extends NPRPC.Flat.Flat {
        get __ex_id(): number;
        set __ex_id(value: number);
    }
}
export declare class ExceptionUnknownFunctionIndex extends NPRPC.Exception {
    constructor();
}
export declare namespace Flat_nprpc_base {
    class ExceptionUnknownFunctionIndex_Direct extends NPRPC.Flat.Flat {
        get __ex_id(): number;
        set __ex_id(value: number);
    }
}
export declare class ExceptionUnknownMessageId extends NPRPC.Exception {
    constructor();
}
export declare namespace Flat_nprpc_base {
    class ExceptionUnknownMessageId_Direct extends NPRPC.Flat.Flat {
        get __ex_id(): number;
        set __ex_id(value: number);
    }
}
export declare class ExceptionUnsecuredObject extends NPRPC.Exception {
    class_id?: string;
    constructor(class_id?: string);
}
export declare namespace Flat_nprpc_base {
    class ExceptionUnsecuredObject_Direct extends NPRPC.Flat.Flat {
        get __ex_id(): number;
        set __ex_id(value: number);
        get class_id(): string;
        set class_id(str: string);
    }
}
export declare class ExceptionBadAccess extends NPRPC.Exception {
    constructor();
}
export declare namespace Flat_nprpc_base {
    class ExceptionBadAccess_Direct extends NPRPC.Flat.Flat {
        get __ex_id(): number;
        set __ex_id(value: number);
    }
}
export declare class ExceptionBadInput extends NPRPC.Exception {
    constructor();
}
export declare namespace Flat_nprpc_base {
    class ExceptionBadInput_Direct extends NPRPC.Flat.Flat {
        get __ex_id(): number;
        set __ex_id(value: number);
    }
}
export declare const enum DebugLevel {
    DebugLevel_Critical = 0,
    DebugLevel_InactiveTimeout = 1,
    DebugLevel_EveryCall = 2,
    DebugLevel_EveryMessageContent = 3,
    DebugLevel_TraceAll = 4
}
export declare namespace detail {
    interface ObjectIdLocal {
        poa_idx: poa_idx_t;
        object_id: oid_t;
    }
    namespace Flat_nprpc_base {
        class ObjectIdLocal_Direct extends NPRPC.Flat.Flat {
            get poa_idx(): number;
            set poa_idx(value: number);
            get object_id(): bigint;
            set object_id(value: bigint);
        }
    }
    const enum ObjectFlag {
        Policy_Lifespan = 0,
        WebObject = 1,
        Secured = 2
    }
    interface ObjectId {
        object_id: oid_t;
        ip4: number;
        port: number;
        websocket_port: number;
        poa_idx: poa_idx_t;
        flags: number;
        class_id: string;
        hostname: string;
    }
    namespace Flat_nprpc_base {
        class ObjectId_Direct extends NPRPC.Flat.Flat {
            get object_id(): bigint;
            set object_id(value: bigint);
            get ip4(): number;
            set ip4(value: number);
            get port(): number;
            set port(value: number);
            get websocket_port(): number;
            set websocket_port(value: number);
            get poa_idx(): number;
            set poa_idx(value: number);
            get flags(): number;
            set flags(value: number);
            get class_id(): string;
            set class_id(str: string);
            get hostname(): string;
            set hostname(str: string);
        }
    }
}
export declare namespace impl {
    const enum MessageId {
        FunctionCall = 0,
        BlockResponse = 1,
        AddReference = 2,
        ReleaseObject = 3,
        Success = 4,
        Exception = 5,
        Error_PoaNotExist = 6,
        Error_ObjectNotExist = 7,
        Error_CommFailure = 8,
        Error_UnknownFunctionIdx = 9,
        Error_UnknownMessageId = 10,
        Error_BadAccess = 11,
        Error_BadInput = 12
    }
    const enum MessageType {
        Request = 0,
        Answer = 1
    }
    interface Header {
        size: number;
        msg_id: MessageId;
        msg_type: MessageType;
        reserved: number;
    }
    namespace Flat_nprpc_base {
        class Header_Direct extends NPRPC.Flat.Flat {
            get size(): number;
            set size(value: number);
            get msg_id(): MessageId;
            set msg_id(value: MessageId);
            get msg_type(): MessageType;
            set msg_type(value: MessageType);
            get reserved(): number;
            set reserved(value: number);
        }
    }
    interface CallHeader {
        poa_idx: poa_idx_t;
        interface_idx: ifs_idx_t;
        function_idx: fn_idx_t;
        object_id: oid_t;
    }
    namespace Flat_nprpc_base {
        class CallHeader_Direct extends NPRPC.Flat.Flat {
            get poa_idx(): number;
            set poa_idx(value: number);
            get interface_idx(): number;
            set interface_idx(value: number);
            get function_idx(): number;
            set function_idx(value: number);
            get object_id(): bigint;
            set object_id(value: bigint);
        }
    }
}
