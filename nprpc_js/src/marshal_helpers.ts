// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// Marshalling helper functions for the new simplified approach
import { FlatBuffer, _alloc, _alloc1 } from './flat_buffer';

const u8enc = new TextEncoder();
const u8dec = new TextDecoder();

export function marshal_string(buf: FlatBuffer, offset: number, str: string): void {
	const bytes = u8enc.encode(str);
	const data_offset = _alloc(buf, offset, bytes.length, 1, 1);
	new Uint8Array(buf.array_buffer, data_offset, bytes.length).set(bytes);
}

export function unmarshal_string(buf: FlatBuffer, offset: number): string {
	const relative_offset = buf.dv.getUint32(offset, true);
	const n = buf.dv.getUint32(offset + 4, true);
	
	if (n === 0) return "";
	
	const data_offset = offset + relative_offset;
	return u8dec.decode(new DataView(buf.array_buffer, data_offset, n));
}

export function marshal_string_array(buf: FlatBuffer, offset: number, arr: string[]): void {
	const data_offset = _alloc(buf, offset, arr.length, 8, 4);
	for (let i = 0; i < arr.length; i++) {
		marshal_string(buf, data_offset + i * 8, arr[i]);
	}
}

export function unmarshal_string_array(buf: FlatBuffer, offset: number): string[] {
	const relative_offset = buf.dv.getUint32(offset, true);
	const n = buf.dv.getUint32(offset + 4, true);
	
	if (n === 0) return [];
	
	const data_offset = offset + relative_offset;
	const result = [];
	for (let i = 0; i < n; i++) {
		result.push(unmarshal_string(buf, data_offset + i * 8));
	}
	return result;
}

export function marshal_typed_array(buf: FlatBuffer, offset: number, arr: TypedArray, elem_size: number, elem_align: number): void {
	const data_offset = _alloc(buf, offset, arr.length, elem_size, elem_align);
	const bytes = new Uint8Array(arr.buffer, arr.byteOffset, arr.byteLength);
	new Uint8Array(buf.array_buffer, data_offset, bytes.length).set(bytes);
}

type FundamentalKind = 'bool' | 'i8' | 'u8' | 'i16' | 'u16' | 'i32' | 'u32' | 'i64' | 'u64' | 'f32' | 'f64';

function get_typed_array_constructor(elem: FundamentalKind | TypedArrayConstructor): TypedArrayConstructor {
	if (typeof elem !== 'string') {
		return elem;
	}

	switch (elem) {
		case 'bool':
		case 'u8':
			return Uint8Array;
		case 'i8':
			return Int8Array;
		case 'i16':
			return Int16Array;
		case 'u16':
			return Uint16Array;
		case 'i32':
			return Int32Array;
		case 'u32':
			return Uint32Array;
		case 'i64':
			return BigInt64Array;
		case 'u64':
			return BigUint64Array;
		case 'f32':
			return Float32Array;
		case 'f64':
			return Float64Array;
	}
}

export function unmarshal_typed_array(buf: FlatBuffer, offset: number, elem: FundamentalKind | TypedArrayConstructor): TypedArray {
	const relative_offset = buf.dv.getUint32(offset, true);
	const n = buf.dv.getUint32(offset + 4, true);
	const ctor = get_typed_array_constructor(elem);
	
	if (n === 0) {
		return new ctor(0);
	}
	
	const data_offset = offset + relative_offset;
	return new ctor(buf.array_buffer, data_offset, n);
}

export function marshal_struct_array(buf: FlatBuffer, offset: number, arr: any[], marshal_fn: (buf: FlatBuffer, offset: number, data: any) => void, elem_size: number, elem_align: number): void {
	const data_offset = _alloc(buf, offset, arr.length, elem_size, elem_align);
	for (let i = 0; i < arr.length; i++) {
		marshal_fn(buf, data_offset + i * elem_size, arr[i]);
	}
}

export function unmarshal_struct_array(buf: FlatBuffer, offset: number, unmarshal_fn: (buf: FlatBuffer, offset: number) => any, elem_size: number): any[] {
	const relative_offset = buf.dv.getUint32(offset, true);
	const n = buf.dv.getUint32(offset + 4, true);
	
	if (n === 0) return [];
	
	const data_offset = offset + relative_offset;
	const result = [];
	for (let i = 0; i < n; i++) {
		result.push(unmarshal_fn(buf, data_offset + i * elem_size));
	}
	return result;
}

function get_fundamental_size(kind: FundamentalKind): number {
	switch (kind) {
		case 'bool':
		case 'i8':
		case 'u8':
			return 1;
		case 'i16':
		case 'u16':
			return 2;
		case 'i32':
		case 'u32':
		case 'f32':
			return 4;
		case 'i64':
		case 'u64':
		case 'f64':
			return 8;
	}
}

export function marshal_optional_fundamental(buf: FlatBuffer, offset: number, value: any, kind: FundamentalKind): void {
	// Optional layout: 4-byte relative offset (same as optional struct)
	// Allocate space for the fundamental value
	const elem_size = get_fundamental_size(kind);
	const data_offset = _alloc1(buf, offset, elem_size, elem_size);
	// Write the value to the allocated space
	switch (kind) {
		case 'bool': buf.dv.setUint8(data_offset, value ? 1 : 0); break;
		case 'i8': buf.dv.setInt8(data_offset, value); break;
		case 'u8': buf.dv.setUint8(data_offset, value); break;
		case 'i16': buf.dv.setInt16(data_offset, value, true); break;
		case 'u16': buf.dv.setUint16(data_offset, value, true); break;
		case 'i32': buf.dv.setInt32(data_offset, value, true); break;
		case 'u32': buf.dv.setUint32(data_offset, value, true); break;
		case 'i64': buf.dv.setBigInt64(data_offset, value, true); break;
		case 'u64': buf.dv.setBigUint64(data_offset, value, true); break;
		case 'f32': buf.dv.setFloat32(data_offset, value, true); break;
		case 'f64': buf.dv.setFloat64(data_offset, value, true); break;
	}
}

export function unmarshal_optional_fundamental(buf: FlatBuffer, offset: number, kind: FundamentalKind): any | undefined {
	// Optional layout: 4-byte relative offset at 'offset'
	// Caller already checked that offset is non-zero, so read the value
	const rel_offset = buf.dv.getUint32(offset, true);
	const data_offset = offset + rel_offset;
	switch (kind) {
		case 'bool': return buf.dv.getUint8(data_offset) !== 0;
		case 'i8': return buf.dv.getInt8(data_offset);
		case 'u8': return buf.dv.getUint8(data_offset);
		case 'i16': return buf.dv.getInt16(data_offset, true);
		case 'u16': return buf.dv.getUint16(data_offset, true);
		case 'i32': return buf.dv.getInt32(data_offset, true);
		case 'u32': return buf.dv.getUint32(data_offset, true);
		case 'i64': return buf.dv.getBigInt64(data_offset, true);
		case 'u64': return buf.dv.getBigUint64(data_offset, true);
		case 'f32': return buf.dv.getFloat32(data_offset, true);
		case 'f64': return buf.dv.getFloat64(data_offset, true);
	}
}

export function marshal_optional_struct(buf: FlatBuffer, offset: number, value: any, marshal_fn: (buf: FlatBuffer, offset: number, data: any) => void, elem_size: number, elem_align: number): void {
	// Optional layout in C++: just a uint32_t relative offset (0 = no value)
	// No separate has_value byte - the offset itself indicates presence
	const data_offset = _alloc1(buf, offset, elem_size, elem_align);
	marshal_fn(buf, data_offset, value);
}

export function unmarshal_optional_struct(buf: FlatBuffer, offset: number, unmarshal_fn: (buf: FlatBuffer, offset: number) => any, elem_align: number): any | undefined {
	// Optional layout: just a uint32_t relative offset at 'offset'
	const rel_offset = buf.dv.getUint32(offset, true);
	if (rel_offset === 0) return undefined;
	return unmarshal_fn(buf, offset + rel_offset);
}

type TypedArray = Int8Array | Uint8Array | Int16Array | Uint16Array | Int32Array | Uint32Array | BigInt64Array | BigUint64Array | Float32Array | Float64Array;
type TypedArrayConstructor = {
	new(length: number): TypedArray;
	new(buffer: ArrayBufferLike, byteOffset: number, length: number): TypedArray;
};
