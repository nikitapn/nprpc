import { impl } from "./nprpc_base";
export declare class FlatBuffer {
    private capacity;
    private size_;
    array_buffer: ArrayBuffer;
    dv: DataView;
    static from_array_buffer(array_buffer: ArrayBuffer): FlatBuffer;
    static create(initial_capacity?: number): FlatBuffer;
    prepare(n: number): void;
    consume(n: number): void;
    commit(n: number): void;
    get offset(): number;
    get size(): number;
    write_len(msg_len: number): void;
    write_msg_id(msg: impl.MessageId): void;
    read_msg_id(): impl.MessageId;
    write_msg_type(msg: impl.MessageType): void;
    read_msg_type(): impl.MessageType;
    read_exception_number(): number;
    get writable_view(): DataView;
    set_buffer(abuf: ArrayBuffer): void;
    dump(): void;
}
