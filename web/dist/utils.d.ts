import { Exception } from "./exception";
export interface ref<T> {
    value: T;
}
export declare function make_ref<T = any>(): ref<T>;
export declare const sip4_to_u32: (str: string) => number;
export declare const ip4tostr: (ip4: number) => string;
export declare class MyPromise<T = void, E extends Exception = Exception> {
    private actual_promise_;
    private resolve_;
    private reject_;
    constructor();
    get $(): Promise<T>;
    set_promise(value: T): void;
    set_exception(ec: E): void;
}
