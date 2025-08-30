import { FlatBuffer } from './flat_buffer';
export declare class Flat {
    buffer: FlatBuffer;
    offset: number;
    constructor(buffer: FlatBuffer, offset: number);
}
export declare function _alloc(buffer: FlatBuffer, vector_offset: number, n: number, element_size: number, align: number): number;
export declare function _alloc1(buffer: FlatBuffer, flat_offset: number, element_size: number, align: number): number;
interface Container {
    readonly elements_offset: number;
    readonly elements_size: number;
}
type FundamentalArray = Uint8Array | Int8Array | Uint8Array | Int16Array | Uint16Array | Int32Array | Uint32Array | Float32Array | Float64Array;
declare class Vector extends Flat implements Container {
    get elements_offset(): number;
    get elements_size(): number;
}
declare class FlatArray extends Flat implements Container {
    buffer: FlatBuffer;
    offset: number;
    size: number;
    constructor(buffer: FlatBuffer, offset: number, size: number);
    get elements_offset(): number;
    get elements_size(): number;
}
export declare const Vector_Direct1_u8: {
    new (...args: any[]): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    };
} & {
    new (...args: any[]): {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Uint8Array;
        readonly array: number[];
        readonly array_buffer: ArrayBuffer;
        readonly data_view: DataView;
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    };
} & typeof Vector;
export declare const Vector_Direct1_i8: {
    new (...args: any[]): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    };
} & {
    new (...args: any[]): {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Int8Array;
        readonly array: number[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    };
} & typeof Vector;
export declare const Vector_Direct1_u16: {
    new (...args: any[]): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    };
} & {
    new (...args: any[]): {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Uint16Array;
        readonly array: number[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    };
} & typeof Vector;
export declare const Vector_Direct1_i16: {
    new (...args: any[]): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    };
} & {
    new (...args: any[]): {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Int16Array;
        readonly array: number[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    };
} & typeof Vector;
export declare const Vector_Direct1_u32: {
    new (...args: any[]): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    };
} & {
    new (...args: any[]): {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Uint32Array;
        readonly array: number[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    };
} & typeof Vector;
export declare const Vector_Direct1_i32: {
    new (...args: any[]): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    };
} & {
    new (...args: any[]): {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Int32Array;
        readonly array: number[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    };
} & typeof Vector;
export declare const Vector_Direct1_u64: {
    new (...args: any[]): {
        [Symbol.iterator](): {
            next: () => {
                value: bigint;
                done: boolean;
            } | {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): bigint;
        set(ix: number, value: bigint): void;
        copy_from_typed_array(arr: BigInt64Array | BigUint64Array): void;
        copy_from_ts_array(arr: bigint[]): void;
    };
} & {
    new (...args: any[]): {
        at(ix: number): bigint;
        set(ix: number, value: bigint): void;
        copy_from_typed_array(arr: BigUint64Array): void;
        copy_from_ts_array(arr: bigint[]): void;
        readonly typed_array: BigUint64Array;
        readonly array: bigint[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    };
} & typeof Vector;
export declare const Vector_Direct1_i64: {
    new (...args: any[]): {
        [Symbol.iterator](): {
            next: () => {
                value: bigint;
                done: boolean;
            } | {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): bigint;
        set(ix: number, value: bigint): void;
        copy_from_typed_array(arr: BigInt64Array | BigUint64Array): void;
        copy_from_ts_array(arr: bigint[]): void;
    };
} & {
    new (...args: any[]): {
        at(ix: number): bigint;
        set(ix: number, value: bigint): void;
        copy_from_typed_array(arr: BigInt64Array): void;
        copy_from_ts_array(arr: bigint[]): void;
        readonly typed_array: BigInt64Array;
        readonly array: bigint[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    };
} & typeof Vector;
export declare const Vector_Direct1_f32: {
    new (...args: any[]): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    };
} & {
    new (...args: any[]): {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Float32Array;
        readonly array: number[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    };
} & typeof Vector;
export declare const Vector_Direct1_f64: {
    new (...args: any[]): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    };
} & {
    new (...args: any[]): {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Float64Array;
        readonly array: number[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    };
} & typeof Vector;
export declare class Vector_Direct2<T extends Flat> extends Vector {
    private element_size;
    private flat_struct_constructor;
    constructor(buffer: FlatBuffer, offset: number, element_size: number, flat_struct_constructor: new (buffer: FlatBuffer, offset: number) => T);
    [Symbol.iterator](): Generator<T, void, unknown>;
}
export declare const Array_Direct1_u8: {
    new (...args: any[]): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    };
} & {
    new (...args: any[]): {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Uint8Array;
        readonly array: number[];
        readonly array_buffer: ArrayBuffer;
        readonly data_view: DataView;
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    };
} & typeof FlatArray;
export declare const Array_Direct1_i8: {
    new (...args: any[]): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    };
} & {
    new (...args: any[]): {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Int8Array;
        readonly array: number[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    };
} & typeof FlatArray;
export declare const Array_Direct1_u16: {
    new (...args: any[]): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    };
} & {
    new (...args: any[]): {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Uint16Array;
        readonly array: number[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    };
} & typeof FlatArray;
export declare const Array_Direct1_i16: {
    new (...args: any[]): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    };
} & {
    new (...args: any[]): {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Int16Array;
        readonly array: number[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    };
} & typeof FlatArray;
export declare const Array_Direct1_u32: {
    new (...args: any[]): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    };
} & {
    new (...args: any[]): {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Uint32Array;
        readonly array: number[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    };
} & typeof FlatArray;
export declare const Array_Direct1_i32: {
    new (...args: any[]): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    };
} & {
    new (...args: any[]): {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Int32Array;
        readonly array: number[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    };
} & typeof FlatArray;
export declare const Array_Direct1_u64: {
    new (...args: any[]): {
        [Symbol.iterator](): {
            next: () => {
                value: bigint;
                done: boolean;
            } | {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): bigint;
        set(ix: number, value: bigint): void;
        copy_from_typed_array(arr: BigInt64Array | BigUint64Array): void;
        copy_from_ts_array(arr: bigint[]): void;
    };
} & {
    new (...args: any[]): {
        at(ix: number): bigint;
        set(ix: number, value: bigint): void;
        copy_from_typed_array(arr: BigUint64Array): void;
        copy_from_ts_array(arr: bigint[]): void;
        readonly typed_array: BigUint64Array;
        readonly array: bigint[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    };
} & typeof FlatArray;
export declare const Array_Direct1_i64: {
    new (...args: any[]): {
        [Symbol.iterator](): {
            next: () => {
                value: bigint;
                done: boolean;
            } | {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): bigint;
        set(ix: number, value: bigint): void;
        copy_from_typed_array(arr: BigInt64Array | BigUint64Array): void;
        copy_from_ts_array(arr: bigint[]): void;
    };
} & {
    new (...args: any[]): {
        at(ix: number): bigint;
        set(ix: number, value: bigint): void;
        copy_from_typed_array(arr: BigInt64Array): void;
        copy_from_ts_array(arr: bigint[]): void;
        readonly typed_array: BigInt64Array;
        readonly array: bigint[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    };
} & typeof FlatArray;
export declare const Array_Direct1_f32: {
    new (...args: any[]): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    };
} & {
    new (...args: any[]): {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Float32Array;
        readonly array: number[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    };
} & typeof FlatArray;
export declare const Array_Direct1_f64: {
    new (...args: any[]): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    };
} & {
    new (...args: any[]): {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Float64Array;
        readonly array: number[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    };
} & typeof FlatArray;
export declare class Array_Direct2<T extends Flat> extends FlatArray {
    private element_size;
    private flat_struct_constructor;
    constructor(buffer: FlatBuffer, offset: number, element_size: number, flat_struct_constructor: new (buffer: FlatBuffer, offset: number) => T, size: number);
    [Symbol.iterator](): Generator<T, void, unknown>;
}
declare const String_Direct1_base: {
    new (...args: any[]): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    };
} & {
    new (...args: any[]): {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Uint8Array;
        readonly array: number[];
        readonly array_buffer: ArrayBuffer;
        readonly data_view: DataView;
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    };
} & typeof Vector;
export declare class String_Direct1 extends String_Direct1_base {
    assign(str: string): void;
}
declare class Optional extends Flat {
    constructor(buffer: FlatBuffer, offset: number, 
    /** @internal */ ut_size: number, 
    /** @internal */ ut_align: number);
    protected get value_offset(): number;
    alloc(): void;
    get has_value(): boolean;
    set_nullopt(): void;
}
export declare class Optional_Direct1_u8 extends Optional {
    get value(): number;
    set value(value: number);
}
export declare class Optional_Direct1_boolean extends Optional {
    get value(): boolean;
    set value(value: boolean);
}
export declare class Optional_Direct1_i8 extends Optional {
    get value(): number;
    set value(value: number);
}
export declare class Optional_Direct1_u16 extends Optional {
    get value(): number;
    set value(value: number);
}
export declare class Optional_Direct1_i16 extends Optional {
    get value(): number;
    set value(value: number);
}
export declare class Optional_Direct1_u32 extends Optional {
    get value(): number;
    set value(value: number);
}
export declare class Optional_Direct1_i32 extends Optional {
    get value(): number;
    set value(value: number);
}
export declare class Optional_Direct1_u64 extends Optional {
    get value(): bigint;
    set value(value: bigint);
}
export declare class Optional_Direct1_i64 extends Optional {
    get value(): bigint;
    set value(value: bigint);
}
export declare class Optional_Direct1_f32 extends Optional {
    get value(): number;
    set value(value: number);
}
export declare class Optional_Direct1_f64 extends Optional {
    get value(): number;
    set value(value: number);
}
export declare class Optional_Direct1_Vector_u8 extends Optional {
    value(elements_size: number): void;
    value_d(): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    } & {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Uint8Array;
        readonly array: number[];
        readonly array_buffer: ArrayBuffer;
        readonly data_view: DataView;
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    } & Vector;
}
export declare const Optional_Direct1_Vector_boolean: typeof Optional_Direct1_Vector_u8;
export declare class Optional_Direct1_Vector_i8 extends Optional {
    value(elements_size: number): void;
    value_d(): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    } & {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Int8Array;
        readonly array: number[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    } & Vector;
}
export declare class Optional_Direct1_Vector_u16 extends Optional {
    value(elements_size: number): void;
    value_d(): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    } & {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Uint16Array;
        readonly array: number[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    } & Vector;
}
export declare class Optional_Direct1_Vector_i16 extends Optional {
    value(elements_size: number): void;
    value_d(): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    } & {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Int16Array;
        readonly array: number[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    } & Vector;
}
export declare class Optional_Direct1_Vector_u32 extends Optional {
    value(elements_size: number): void;
    value_d(): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    } & {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Uint32Array;
        readonly array: number[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    } & Vector;
}
export declare class Optional_Direct1_Vector_i32 extends Optional {
    value(elements_size: number): void;
    value_d(): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    } & {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Int32Array;
        readonly array: number[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    } & Vector;
}
export declare class Optional_Direct1_Vector_f32 extends Optional {
    value(elements_size: number): void;
    value_d(): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    } & {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Float32Array;
        readonly array: number[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    } & Vector;
}
export declare class Optional_Direct1_Vector_f64 extends Optional {
    value(elements_size: number): void;
    value_d(): {
        [Symbol.iterator](): {
            next: () => {
                value: number;
                done: boolean;
            };
        };
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
    } & {
        at(ix: number): number;
        set(ix: number, value: number): void;
        copy_from_typed_array(arr: FundamentalArray): void;
        copy_from_ts_array(arr: number[]): void;
        readonly typed_array: Float64Array;
        readonly array: number[];
        buffer: FlatBuffer;
        offset: number;
        readonly elements_offset: number;
        readonly elements_size: number;
    } & Vector;
}
export declare class Optional_Direct2<T extends Flat> extends Optional {
    private flat_struct_constructor;
    constructor(buffer: FlatBuffer, offset: number, ut_size: number, ut_align: number, flat_struct_constructor: new (buffer: FlatBuffer, offset: number) => T);
    get value(): T;
}
export {};
