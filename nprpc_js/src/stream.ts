// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import { FlatBuffer } from './flat_buffer';
import { impl } from './gen/nprpc_base';

const header_size = 16;

abstract class StreamReaderBase {
  abstract push_chunk(data: Uint8Array, sequence: bigint): void;
  abstract complete(): void;
  abstract fail(error_code: number): void;
}

export class StreamReader<T> extends StreamReaderBase {
  private chunks: T[] = [];
  private resolve_fn: (() => void) | null = null;
  private done_ = false;
  private error_: Error | null = null;
  private next_expected_seq = 0n;
  private out_of_order = new Map<bigint, T>();

  constructor(
    private readonly deserialize: (data: Uint8Array) => T,
    private readonly manager?: StreamManager,
    private readonly stream_id?: bigint,
  ) {
    super();
  }

  push_chunk(data: Uint8Array, sequence: bigint): void {
    const value = this.deserialize(data);
    if (sequence === this.next_expected_seq) {
      this.chunks.push(value);
      this.next_expected_seq++;
      while (this.out_of_order.has(this.next_expected_seq)) {
        this.chunks.push(this.out_of_order.get(this.next_expected_seq)!);
        this.out_of_order.delete(this.next_expected_seq);
        this.next_expected_seq++;
      }
    } else {
      this.out_of_order.set(sequence, value);
    }
    this.signal();
  }

  complete(): void {
    this.done_ = true;
    this.signal();
  }

  fail(error_code: number): void {
    this.error_ = new Error(`Stream error (code=${error_code})`);
    this.done_ = true;
    this.signal();
  }

  cancel(): void {
    if (this.done_ || !this.manager || this.stream_id === undefined) {
      return;
    }
    this.manager.cancel_reader(this.stream_id, this);
    this.done_ = true;
    this.signal();
  }

  private signal(): void {
    if (this.resolve_fn) {
      const fn = this.resolve_fn;
      this.resolve_fn = null;
      fn();
    }
  }

  async *[Symbol.asyncIterator](): AsyncGenerator<T> {
    while (true) {
      while (this.chunks.length === 0 && !this.done_) {
        await new Promise<void>(resolve => { this.resolve_fn = resolve; });
      }
      if (this.error_) {
        throw this.error_;
      }
      while (this.chunks.length > 0) {
        yield this.chunks.shift()!;
        // Grant one credit back to the server after each consumed chunk.
        if (this.manager && this.stream_id !== undefined) {
          this.manager.send_window_update(this.stream_id, 1);
        }
      }
      if (this.done_) {
        return;
      }
    }
  }

  async read(): Promise<T | null> {
    const result = await this[Symbol.asyncIterator]().next();
    return result.done ? null : result.value;
  }

  public get is_done(): boolean { return this.done_; }
}

export class StreamWriter<T> {
  private sequence = 0n;
  private closed = false;

  constructor(
    private readonly serialize: (value: T) => Uint8Array,
    private readonly manager: StreamManager,
    private readonly stream_id: bigint,
  ) {}

  write(value: T): void {
    if (this.closed) {
      throw new Error('Stream is already closed');
    }
    this.manager.send_chunk(this.stream_id, this.serialize(value), this.sequence++);
  }

  close(): void {
    if (this.closed) {
      return;
    }
    this.manager.send_complete(this.stream_id, this.sequence === 0n ? 0n : this.sequence - 1n);
    this.closed = true;
  }

  abort(error_code: number = 1): void {
    if (this.closed) {
      return;
    }
    this.manager.send_error(this.stream_id, error_code, new Uint8Array(0));
    this.closed = true;
  }

  cancel(): void {
    if (this.closed) {
      return;
    }
    this.manager.send_cancel(this.stream_id);
    this.closed = true;
  }
}

export class BidiStream<TIn, TOut> {
  constructor(
    public readonly writer: StreamWriter<TIn>,
    public readonly reader: StreamReader<TOut>,
  ) {}
}

export const bytes_deserializer = (data: Uint8Array): Uint8Array => data;

export class StreamManager {
  private readers = new Map<bigint, StreamReaderBase>();
  private id_counter = 0n;
  private static readonly random_base =
    (BigInt(Math.floor(Math.random() * 0xFFFFFFFF)) << 32n) |
    BigInt(Math.floor(Math.random() * 0xFFFFFFFF));

  constructor(
    private readonly send_message: (payload: ArrayBufferView) => void,
  ) {}

  generate_stream_id(): bigint {
    return StreamManager.random_base ^ (++this.id_counter);
  }

  register_reader(stream_id: bigint, reader: StreamReaderBase): void {
    this.readers.set(stream_id, reader);
  }

  unregister_reader(stream_id: bigint, reader: StreamReaderBase): void {
    const current = this.readers.get(stream_id);
    if (current === reader) {
      this.readers.delete(stream_id);
    }
  }

  create_reader<T>(stream_id: bigint, deserialize: (data: Uint8Array) => T): StreamReader<T> {
    const reader = new StreamReader<T>(deserialize, this, stream_id);
    this.register_reader(stream_id, reader);
    return reader;
  }

  create_writer<T>(stream_id: bigint, serialize: (value: T) => Uint8Array): StreamWriter<T> {
    return new StreamWriter<T>(serialize, this, stream_id);
  }

  create_bidi_stream<TIn, TOut>(
    stream_id: bigint,
    serialize: (value: TIn) => Uint8Array,
    deserialize: (data: Uint8Array) => TOut,
  ): BidiStream<TIn, TOut> {
    return new BidiStream(
      this.create_writer(stream_id, serialize),
      this.create_reader(stream_id, deserialize),
    );
  }

  cancel_reader(stream_id: bigint, reader: StreamReaderBase): void {
    this.unregister_reader(stream_id, reader);
    this.send_cancel(stream_id);
  }

  on_chunk_received(stream_id: bigint, data: Uint8Array, sequence: bigint): void {
    const reader = this.readers.get(stream_id);
    if (!reader) {
      console.warn(`StreamManager: chunk for unknown stream ${stream_id}`);
      return;
    }
    reader.push_chunk(data, sequence);
  }

  on_stream_complete(stream_id: bigint): void {
    const reader = this.readers.get(stream_id);
    if (!reader) {
      console.warn(`StreamManager: completion for unknown stream ${stream_id}`);
      return;
    }
    reader.complete();
    this.readers.delete(stream_id);
  }

  on_stream_error(stream_id: bigint, error_code: number): void {
    const reader = this.readers.get(stream_id);
    if (!reader) {
      console.warn(`StreamManager: error for unknown stream ${stream_id}`);
      return;
    }
    reader.fail(error_code);
    this.readers.delete(stream_id);
  }

  on_stream_cancel(stream_id: bigint): void {
    const reader = this.readers.get(stream_id);
    if (!reader) {
      return;
    }
    reader.complete();
    this.readers.delete(stream_id);
  }

  send_chunk(stream_id: bigint, data: Uint8Array, sequence: bigint): void {
    const buf = FlatBuffer.create(header_size + 32 + data.byteLength);
    buf.commit(header_size + 28);
    buf.write_msg_id(impl.MessageId.StreamDataChunk);
    buf.write_msg_type(impl.MessageType.Request);
    impl.marshal_StreamChunk(buf, header_size, {
      stream_id,
      sequence,
      data,
      window_size: 0,
    });
    buf.write_len(buf.size - 4);
    this.send_message(buf.writable_view);
  }

  send_complete(stream_id: bigint, final_sequence: bigint): void {
    const buf = FlatBuffer.create(header_size + 16);
    buf.commit(header_size + 16);
    buf.write_msg_id(impl.MessageId.StreamCompletion);
    buf.write_msg_type(impl.MessageType.Request);
    impl.marshal_StreamComplete(buf, header_size, { stream_id, final_sequence });
    buf.write_len(buf.size - 4);
    this.send_message(buf.writable_view);
  }

  send_error(stream_id: bigint, error_code: number, error_data: Uint8Array): void {
    const buf = FlatBuffer.create(header_size + 32 + error_data.byteLength);
    buf.commit(header_size + 20);
    buf.write_msg_id(impl.MessageId.StreamError);
    buf.write_msg_type(impl.MessageType.Request);
    impl.marshal_StreamError(buf, header_size, { stream_id, error_code, error_data });
    buf.write_len(buf.size - 4);
    this.send_message(buf.writable_view);
  }

  send_cancel(stream_id: bigint): void {
    const buf = FlatBuffer.create(header_size + 8);
    buf.commit(header_size + 8);
    buf.write_msg_id(impl.MessageId.StreamCancellation);
    buf.write_msg_type(impl.MessageType.Request);
    impl.marshal_StreamCancel(buf, header_size, { stream_id });
    buf.write_len(buf.size - 4);
    this.send_message(buf.writable_view);
  }

  send_window_update(stream_id: bigint, credits: number): void {
    // stream_id (u64, 8 bytes) + credits (u32, 4 bytes) = 12 bytes payload
    const buf = FlatBuffer.create(header_size + 12);
    buf.commit(header_size + 12);
    buf.write_msg_id(impl.MessageId.StreamWindowUpdate);
    buf.write_msg_type(impl.MessageType.Request);
    impl.marshal_StreamWindowUpdate(buf, header_size, { stream_id, credits });
    buf.write_len(buf.size - 4);
    this.send_message(buf.writable_view);
  }

  // Called when a remote stream consumer grants credits to a local StreamWriter.
  on_stream_window_update(stream_id: bigint, credits: number): void {
    // For TS-side writers this is currently a no-op because the TS StreamWriter
    // does not implement backpressure yet. Reserved for future client-stream use.
    void stream_id; void credits;
  }

  cancel_all(): void {
    for (const reader of this.readers.values()) {
      reader.fail(0);
    }
    this.readers.clear();
  }
}
