// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

// Marshalling helper functions for the new simplified approach
import { FlatBuffer, HeapViews } from './flat_buffer';
import { Flat, _alloc, _alloc1 } from './flat';

const u8enc = new TextEncoder();
const u8dec = new TextDecoder();

export function marshal_string(buf: FlatBuffer, offset: number, str: string): void {
	const bytes = u8enc.encode(str);
	const data_offset = _alloc(buf, offset, bytes.length, 1, 1);
	buf.heap.HEAPU8.set(bytes, data_offset);
}

export function unmarshal_string(buf: FlatBuffer, offset: number): string {
	const relative_offset = buf.dv.getUint32(offset, true);
	const n = buf.dv.getUint32(offset + 4, true);
	
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
	const relative_offset = buf.dv.getUint32(offset, true);
	const n = buf.dv.getUint32(offset + 4, true);
	
	console.log(`[unmarshal_typed_array] offset=${offset}, rel_offset=${relative_offset}, n=${n}, elem_size=${elem_size}`);
	
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

export function marshal_optional_fundamental(buf: FlatBuffer, offset: number, value: any, elem_size: number): void {
	buf.dv.setUint8(offset, 1); // has value
	const value_offset = offset + elem_size;
	switch (elem_size) {
		case 1: buf.dv.setUint8(value_offset, value); break;
		case 2: buf.dv.setUint16(value_offset, value, true); break;
		case 4: buf.dv.setUint32(value_offset, value, true); break;
		case 8: buf.dv.setBigUint64(value_offset, value, true); break;
	}
}

export function unmarshal_optional_fundamental(buf: FlatBuffer, offset: number, elem_size: number): any | undefined {
	const value_offset = offset + elem_size;
	switch (elem_size) {
		case 1: return buf.dv.getUint8(value_offset);
		case 2: return buf.dv.getUint16(value_offset, true);
		case 4: return buf.dv.getUint32(value_offset, true);
		case 8: return buf.dv.getBigUint64(value_offset, true);
	}
}

export function marshal_optional_struct(buf: FlatBuffer, offset: number, value: any, marshal_fn: (buf: FlatBuffer, offset: number, data: any) => void, elem_size: number, elem_align: number): void {
	buf.dv.setUint8(offset, 1); // has value
	const data_offset = _alloc1(buf, offset, elem_size, elem_align);
	marshal_fn(buf, data_offset, value);
}

export function unmarshal_optional_struct(buf: FlatBuffer, offset: number, unmarshal_fn: (buf: FlatBuffer, offset: number) => any): any | undefined {
	const rel_offset = buf.dv.getUint32(offset, true);
	if (rel_offset === 0) return undefined;
	return unmarshal_fn(buf, offset + rel_offset);
}

type TypedArray = Int8Array | Uint8Array | Int16Array | Uint16Array | Int32Array | Uint32Array | BigInt64Array | BigUint64Array | Float32Array | Float64Array;
