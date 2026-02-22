// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import { impl } from "./gen/nprpc_base"

export class FlatBuffer {
	private capacity: number;
	private size_: number;
	public array_buffer: ArrayBuffer;
	public dv: DataView;

	public static from_array_buffer(array_buffer: ArrayBuffer): FlatBuffer {
		let b = new FlatBuffer();
		b.array_buffer = array_buffer;
		b.size_ = b.capacity = array_buffer.byteLength;
		b.dv = new DataView(b.array_buffer);
		return b;
	}

	public static create(initial_capacity: number = 512): FlatBuffer {
		let b = new FlatBuffer();
		b.capacity = initial_capacity;
		b.size_ = 0;
		b.array_buffer = new ArrayBuffer(initial_capacity);
		b.dv = new DataView(b.array_buffer);
		return b;
	}

	public prepare(n: number): void {
		if (this.size_ + n > this.capacity) {
			this.capacity = Math.max(this.capacity * 2, this.capacity + n);
			let new_buffer = new ArrayBuffer(this.capacity);
			new Uint8Array(new_buffer).set(new Uint8Array(this.array_buffer));
			this.array_buffer = new_buffer;
			this.dv = new DataView(this.array_buffer);
		}
	}

	public consume(n: number): void { this.size_ -= n; }
	public commit(n: number): void { this.size_ += n; }

	public get offset() { return this.size_; }
	public get size() { return this.size_; }

	public write_len(msg_len: number) {
		this.dv.setUint32(0, msg_len, true);
	}
	
	public write_msg_id(msg: impl.MessageId) {
		this.dv.setUint32(4, msg, true);
	}

	public read_msg_id(): impl.MessageId {
		return this.dv.getUint32(4, true) as impl.MessageId;
	}

	public write_msg_type(msg: impl.MessageType) {
		this.dv.setUint32(8, msg, true);
	}

	public read_msg_type(): impl.MessageType {
		return this.dv.getUint32(8, true) as impl.MessageType;
	}

	public read_exception_number(): number {
		return this.dv.getUint32(16, true);
	}

	public write_request_id(request_id: number) {
		this.dv.setUint32(12, request_id, true);
	}

	public read_request_id(): number {
		return this.dv.getUint32(12, true);
	}

	public get writable_view(): DataView {
		return new DataView(this.array_buffer, 0, this.size);
	}

	public set_buffer(abuf: ArrayBuffer): void {
		this.array_buffer = abuf;
		this.size_ = this.capacity = abuf.byteLength;
		this.dv = new DataView(this.array_buffer);
	}

	public dump() {
		let s = new String();
		new Uint8Array(this.array_buffer, 0, this.size).forEach((x:number) => s += x.toString(16) + ' ');
		console.log(s);
	}
}

// Allocation functions for flat buffer layout

/**
 * Allocates space for a vector (dynamic array) in the flat buffer.
 * Writes relative offset and length at vector_offset, returns data offset.
 */
export function _alloc(buffer: FlatBuffer, vector_offset: number, n: number, element_size: number, align: number): number {
	if (n == 0) {
		buffer.dv.setUint32(vector_offset, 0, true);
		buffer.dv.setUint32(vector_offset + 4, 0, true);
		return 0;
	}

	const offset = (buffer.offset + align - 1) & ~(align - 1);
	const added_size = n * element_size + (offset - buffer.offset);

	buffer.prepare(added_size);
	buffer.commit(added_size);

	buffer.dv.setUint32(vector_offset, offset - vector_offset, true);
	buffer.dv.setUint32(vector_offset + 4, n, true);

	return offset;
}

/**
 * Allocates space for a single element (used for optionals and strings) in the flat buffer.
 * Writes relative offset at flat_offset, returns data offset.
 * Returns absolute offset of allocated element.
 */
export function _alloc1(buffer: FlatBuffer, flat_offset: number, element_size: number, align: number): number {
	const offset = (buffer.offset + align - 1) & ~(align - 1);
	const added_size = element_size + (offset - buffer.offset);
	buffer.prepare(added_size);
	buffer.commit(added_size);
	buffer.dv.setUint32(flat_offset, offset - flat_offset, true);
	return offset;
}