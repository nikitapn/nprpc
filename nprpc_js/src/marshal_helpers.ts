// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

// Marshalling helper functions for the new simplified approach
import { FlatBuffer, HeapViews } from './flat_buffer';
import { Flat, _alloc, _alloc1 } from './flat';

const u8enc = new TextEncoder();
const u8dec = new TextDecoder();

export function marshal_object_id(buf: FlatBuffer, offset: number, obj: any): void {
	const u32 = buf.heap.HEAPU32;
	const base = offset >> 2;
	u32[base] = obj.object_id;
	u32[base + 1] = obj.poa_idx;
	u32[base + 2] = obj.ip;
	u32[base + 3] = obj.port;
	u32[base + 4] = obj.websocket_port;
	u32[base + 5] = obj.priority;
}

export function unmarshal_object_id(buf: FlatBuffer, offset: number): any {
	const u32 = buf.heap.HEAPU32;
	const base = offset >> 2;
	return {
		object_id: u32[base],
		poa_idx: u32[base + 1],
		ip: u32[base + 2],
		port: u32[base + 3],
		websocket_port: u32[base + 4],
		priority: u32[base + 5]
	};
}

export function marshal_string(buf: FlatBuffer, offset: number, str: string): void {
	const bytes = u8enc.encode(str);
	const data_offset = _alloc(buf, offset, bytes.length, 1, 1);
	buf.heap.HEAPU8.set(bytes, data_offset);
}

export function unmarshal_string(buf: FlatBuffer, offset: number): string {
	const u32 = buf.heap.HEAPU32;
	const str_offset_addr = offset >> 2;
	const n_addr = str_offset_addr + 1;
	
	const relative_offset = u32[str_offset_addr];
	const n = u32[n_addr];
	
	if (n === 0) return "";
	
	const data_offset = offset + relative_offset;
	return u8dec.decode(new DataView(buf.array_buffer, data_offset, n));
}

export function marshal_typed_array(buf: FlatBuffer, offset: number, arr: TypedArray, elem_size: number, elem_align: number): void {
	const data_offset = _alloc(buf, offset, arr.length, elem_size, elem_align);
	const bytes = new Uint8Array(arr.buffer, arr.byteOffset, arr.byteLength);
	buf.heap.HEAPU8.set(bytes, data_offset);
}

export function unmarshal_typed_array(buf: FlatBuffer, offset: number, elem_size: number): TypedArray {
	const u32 = buf.heap.HEAPU32;
	const relative_offset = u32[offset >> 2];
	const n = u32[(offset >> 2) + 1];
	
	if (n === 0) {
		switch (elem_size) {
			case 1: return new Uint8Array(0);
			case 2: return new Uint16Array(0);
			case 4: return new Uint32Array(0);
			case 8: return new BigUint64Array(0);
			default: return new Uint8Array(0);
		}
	}
	
	const data_offset = offset + relative_offset;
	switch (elem_size) {
		case 1: return new Uint8Array(buf.array_buffer, data_offset, n);
		case 2: return new Uint16Array(buf.array_buffer, data_offset, n);
		case 4: return new Uint32Array(buf.array_buffer, data_offset, n);
		case 8: return new BigUint64Array(buf.array_buffer, data_offset, n);
		default: return new Uint8Array(buf.array_buffer, data_offset, n);
	}
}

export function marshal_struct_array(buf: FlatBuffer, offset: number, arr: any[], marshal_fn: (buf: FlatBuffer, offset: number, data: any) => void, elem_size: number, elem_align: number): void {
	const data_offset = _alloc(buf, offset, arr.length, elem_size, elem_align);
	for (let i = 0; i < arr.length; i++) {
		marshal_fn(buf, data_offset + i * elem_size, arr[i]);
	}
}

export function unmarshal_struct_array(buf: FlatBuffer, offset: number, unmarshal_fn: (buf: FlatBuffer, offset: number) => any, elem_size: number): any[] {
	const u32 = buf.heap.HEAPU32;
	const relative_offset = u32[offset >> 2];
	const n = u32[(offset >> 2) + 1];
	
	if (n === 0) return [];
	
	const data_offset = offset + relative_offset;
	const result = [];
	for (let i = 0; i < n; i++) {
		result.push(unmarshal_fn(buf, data_offset + i * elem_size));
	}
	return result;
}

export function marshal_optional_fundamental(buf: FlatBuffer, offset: number, value: any, elem_size: number): void {
	buf.heap.HEAPU8[offset] = 1; // has value
	const value_offset = offset + elem_size;
	switch (elem_size) {
		case 1: buf.heap.HEAPU8[value_offset] = value; break;
		case 2: buf.heap.HEAPU16[value_offset >> 1] = value; break;
		case 4: buf.heap.HEAPU32[value_offset >> 2] = value; break;
		case 8: buf.heap.HEAPU64[value_offset >> 3] = value; break;
	}
}

export function unmarshal_optional_fundamental(buf: FlatBuffer, offset: number, elem_size: number): any | undefined {
	const value_offset = offset + elem_size;
	switch (elem_size) {
		case 1: return buf.heap.HEAPU8[value_offset];
		case 2: return buf.heap.HEAPU16[value_offset >> 1];
		case 4: return buf.heap.HEAPU32[value_offset >> 2];
		case 8: return buf.heap.HEAPU64[value_offset >> 3];
	}
}

export function marshal_optional_struct(buf: FlatBuffer, offset: number, value: any, marshal_fn: (buf: FlatBuffer, offset: number, data: any) => void, elem_size: number, elem_align: number): void {
	buf.heap.HEAPU8[offset] = 1; // has value
	const data_offset = _alloc1(buf, offset, elem_size, elem_align);
	marshal_fn(buf, data_offset, value);
}

export function unmarshal_optional_struct(buf: FlatBuffer, offset: number, unmarshal_fn: (buf: FlatBuffer, offset: number) => any): any | undefined {
	const u32 = buf.heap.HEAPU32;
	const rel_offset = u32[offset >> 2];
	if (rel_offset === 0) return undefined;
	return unmarshal_fn(buf, offset + rel_offset);
}

type TypedArray = Int8Array | Uint8Array | Int16Array | Uint16Array | Int32Array | Uint32Array | BigInt64Array | BigUint64Array | Float32Array | Float64Array;
