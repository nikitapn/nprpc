// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

/** Base class held by StreamManager - type-erased. */
abstract class StreamReaderBase {
  abstract push_chunk(data: Uint8Array, sequence: bigint): void;
  abstract complete(): void;
  abstract fail(error_code: number): void;
}

/**
 * Client-side typed stream reader.
 * @param T - the deserialized element type
 *
 * For stream<u8>:  StreamReader<Uint8Array> with identity deserializer.
 * For stream<Foo>: StreamReader<Foo>        with generated unmarshal function.
 */
export class StreamReader<T> extends StreamReaderBase {
  private chunks: T[] = [];
  private resolve_fn: (() => void) | null = null;
  private done_ = false;
  private error_: Error | null = null;
  private next_expected_seq = 0n;
  private out_of_order = new Map<bigint, T>();

  /**
   * @param deserialize - converts raw chunk bytes to T.
   *   For stream<u8> pass the identity: `data => data`
   *   For stream<Foo> pass the generated unmarshal: `data => unmarshal_Foo(data)`
   */
  constructor(private readonly deserialize: (data: Uint8Array) => T) {
    super();
  }

  /** Called by StreamManager when a chunk arrives (always raw bytes on the wire). */
  push_chunk(data: Uint8Array, sequence: bigint): void {
    const value = this.deserialize(data);
    if (sequence === this.next_expected_seq) {
      this.chunks.push(value);
      this.next_expected_seq++;
      // Drain buffered out-of-order chunks
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
      if (this.error_) throw this.error_;
      while (this.chunks.length > 0) yield this.chunks.shift()!;
      if (this.done_) return;
    }
  }

  /** Convenience: read one element (returns null when stream ends). */
  async read(): Promise<T | null> {
    const result = await this[Symbol.asyncIterator]().next();
    return result.done ? null : result.value;
  }

  public get is_done(): boolean { return this.done_; }
}

/** Identity deserializer - used by stream<u8> */
export const bytes_deserializer = (data: Uint8Array): Uint8Array => data;

/**
 * Per-connection stream manager. Tracks active StreamReaders by stream_id.
 */
export class StreamManager {
  private readers = new Map<bigint, StreamReaderBase>();
  private id_counter = 0n;
  private static readonly random_base =
    (BigInt(Math.floor(Math.random() * 0xFFFFFFFF)) << 32n) |
    BigInt(Math.floor(Math.random() * 0xFFFFFFFF));

  generate_stream_id(): bigint {
    return StreamManager.random_base ^ (++this.id_counter);
  }

  create_reader<T>(stream_id: bigint, deserialize: (data: Uint8Array) => T): StreamReader<T> {
    const reader = new StreamReader<T>(deserialize);
    this.readers.set(stream_id, reader);
    return reader;
  }

  on_chunk_received(stream_id: bigint, data: Uint8Array, sequence: bigint): void {
    const reader = this.readers.get(stream_id);
    if (reader) {
      reader.push_chunk(data, sequence);
    } else {
      console.warn(`StreamManager: chunk for unknown stream ${stream_id}`);
    }
  }

  on_stream_complete(stream_id: bigint): void {
    const reader = this.readers.get(stream_id);
    if (reader) {
      reader.complete();
      this.readers.delete(stream_id);
    } else {
      console.warn(`StreamManager: completion for unknown stream ${stream_id}`);
    }
  }

  on_stream_error(stream_id: bigint, error_code: number): void {
    const reader = this.readers.get(stream_id);
    if (reader) {
      reader.fail(error_code);
      this.readers.delete(stream_id);
    } else {
      console.warn(`StreamManager: error for unknown stream ${stream_id}`);
    }
  }

  cancel_all(): void {
    for (const reader of this.readers.values()) {
      reader.fail(0);
    }
    this.readers.clear();
  }
}
