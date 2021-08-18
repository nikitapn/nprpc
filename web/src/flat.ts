// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

import { FlatBuffer } from './flat_buffer'

export class Flat {
	constructor(protected buffer: FlatBuffer, protected offset: number) { }
}

export function _alloc(buffer: FlatBuffer, vector_offset: number, n: number, element_size: number, align: number): number {
	let rem = buffer.offset % align;
	let offset = rem ? buffer.offset + (align - rem) : buffer.offset;
	{
		let added_size = n * element_size + (offset - buffer.offset);

		buffer.prepare(added_size);
		buffer.commit(added_size);
	}
	buffer.dv.setUint32(vector_offset, offset - vector_offset, true);
	buffer.dv.setUint32(vector_offset + 4, n, true);
	//	console.log({
	//		offset: offset,
	//		vector_offset: vector_offset,
	//		voffset: buffer.dv.getUint32(vector_offset,  true)
	//	});
	return offset;
}

class Vector extends Flat {
	public get elements_offset(): number { return this.offset + this.buffer.dv.getUint32(this.offset, true); }
	public get elements_size(): number { return this.buffer.dv.getUint32(this.offset + 4, true); }
}

class FlatArray extends Flat {
	public get elements_offset() { return this.offset; }
	public get elements_size() { return this.size; }
	constructor(protected buffer: FlatBuffer, protected offset: number, private size: number) {
		super(buffer, offset);
	}
}

type Constructor = new (...args: any[]) => {};

function Iterable<TBase extends Constructor>(Base: TBase) {
	return class _Iterable extends Base {
		// *[Symbol.iterator]() {
		// 	let size: number = (this as any).elements_size;
		// 	for (let ix = 0; ix < size; ++ix) {
		// 		yield (this as any).get_val(ix);
		// 	}
		// }
		[Symbol.iterator]() {
			let this_ = this as any;
			const size: number = this_.elements_size;
			let index = 0;
			return {
				next: () => {
					if (index < size) return { value: this_.get_val(index++), done: false }
					return { value: size, done: true }
				}
			};
		}
	};
}

function Accessor_U8<TBase extends Constructor>(Base: TBase) {
	return class _Accessor_U8 extends Base {
		public get_val(ix: number): number { return (this as any).buffer.dv.getUint8((this as any).elements_offset + ix); }
		public set_val(ix: number, value: number): void { (this as any).buffer.dv.setUint8((this as any).elements_offset + ix, value); }
		public copy_from_ts_array(arr: number[]): void {
			let offset = (this as any).elements_offset;
			for (let n of arr) {
				(this as any).buffer.dv.setUint8(offset, n);
				offset += 1;
			}
		}
		public get array_buffer(): ArrayBuffer {
			let offset: number = (this as any).elements_offset;
			return ((this as any).buffer as FlatBuffer).array_buffer.slice(offset, offset + (this as any).elements_size);
		}
	};
}

function Accessor_I8<TBase extends Constructor>(Base: TBase) {
	return class _Accessor_I8 extends Base {
		public get_val(ix: number) { return (this as any).buffer.dv.getInt8((this as any).elements_offset + ix); }
		public set_val(ix: number, value: number) { (this as any).buffer.dv.setInt8((this as any).elements_offset + ix, value); }
		public copy_from_ts_array(arr: number[]): void {
			let offset = (this as any).elements_offset;
			for (let n of arr) {
				(this as any).buffer.dv.setInt8(offset, n);
				offset += 1;
			}
		}
	};
}

function Accessor_U16<TBase extends Constructor>(Base: TBase) {
	return class _Accessor_U16 extends Base {
		public get_val(ix: number): number { return (this as any).buffer.dv.getUint16((this as any).elements_offset + 2*ix, true); }
		public set_val(ix: number, value: number): void { (this as any).buffer.dv.setUint16((this as any).elements_offset + 2*ix, value, true); }
		public copy_from_ts_array(arr: number[]): void {
			let offset = (this as any).elements_offset;
			for (let n of arr) {
				(this as any).buffer.dv.setUint16(offset, n, true);
				offset += 2;
			}
		}
	};
}

function Accessor_I16<TBase extends Constructor>(Base: TBase) {
	return class _Accessor_I16 extends Base {
		public get_val(ix: number) { return (this as any).buffer.dv.getInt16((this as any).elements_offset + 2*ix, true); }
		public set_val(ix: number, value: number) { (this as any).buffer.dv.setInt16((this as any).elements_offset + 2*ix, value, true); }
		public copy_from_ts_array(arr: number[]): void {
			let offset = (this as any).elements_offset;
			for (let n of arr) {
				(this as any).buffer.dv.setInt16(offset, n, true);
				offset += 2;
			}
		}
	};
}

function Accessor_U32<TBase extends Constructor>(Base: TBase) {
	return class _Accessor_U32 extends Base {
		public get_val(ix: number): number { return (this as any).buffer.dv.getUint32((this as any).elements_offset + 4*ix, true); }
		public set_val(ix: number, value: number): void { (this as any).buffer.dv.setUint32((this as any).elements_offset + 4*ix, value, true); }
		public copy_from_ts_array(arr: number[]): void {
			let offset = (this as any).elements_offset;
			for (let n of arr) {
				(this as any).buffer.dv.setUint32(offset, n, true);
				offset += 4;
			}
		}
	};
}

function Accessor_I32<TBase extends Constructor>(Base: TBase) {
	return class _Accessor_I32 extends Base {
		public get_val(ix: number) { return (this as any).buffer.dv.getInt32((this as any).elements_offset + 4*ix, true); }
		public set_val(ix: number, value: number) { (this as any).buffer.dv.setInt32((this as any).elements_offset + 4*ix, value, true); }
		public copy_from_ts_array(arr: number[]): void {
			let offset = (this as any).elements_offset;
			for (let n of arr) {
				(this as any).buffer.dv.setInt32(offset, n, true);
				offset += 4;
			}
		}
	};
}

function Accessor_U64<TBase extends Constructor>(Base: TBase) {
	return class _Accessor_U64 extends Base {
		public get_val(ix: number) { return (this as any).buffer.dv.getBigUint64((this as any).elements_offset + 8*ix, true); }
		public set_val(ix: number, value: bigint) { (this as any).buffer.dv.setBigUint64((this as any).elements_offset + 8*ix, value, true); }
		public copy_from_ts_array(arr: bigint[]): void {
			let offset = (this as any).elements_offset;
			for (let n of arr) {
				(this as any).buffer.dv.setBigUint64(offset, n, true);
				offset += 8;
			}
		}
	};
}

function Accessor_I64<TBase extends Constructor>(Base: TBase) {
	return class _Accessor_I64 extends Base {
		public get_val(ix: number) { return (this as any).buffer.dv.getBigInt64((this as any).elements_offset + 8*ix, true); }
		public set_val(ix: number, value: bigint) { (this as any).buffer.dv.setBigInt64((this as any).elements_offset + 8*ix, value, true); }
		public copy_from_ts_array(arr: bigint[]): void {
			let offset = (this as any).elements_offset;
			for (let n of arr) {
				(this as any).buffer.dv.setBigInt64(offset, n, true);
				offset += 8;
			}
		}
	};
}

function Accessor_Float32<TBase extends Constructor>(Base: TBase) {
	return class _Accessor_Float32 extends Base {
		public get_val(ix: number) { return (this as any).buffer.dv.getFloat32((this as any).elements_offset + 4*ix, true); }
		public set_val(ix: number, value: number) { (this as any).buffer.dv.setFloat32((this as any).elements_offset + 4*ix, value, true); }
		public copy_from_ts_array(arr: number[]): void {
			let offset = (this as any).elements_offset;
			for (let n of arr) {
				(this as any).buffer.dv.setFloat32(offset, n, true);
				offset += 4;
			}
		}
	};
}

function Accessor_Float64<TBase extends Constructor>(Base: TBase) {
	return class _Accessor_Float64 extends Base {
		public get_val(ix: number) { return (this as any).buffer.dv.getFloat64((this as any).elements_offset + 8*ix, true); }
		public set_val(ix: number, value: number) { (this as any).buffer.dv.setFloat64((this as any).elements_offset + 8*ix, value, true); }
		public copy_from_ts_array(arr: number[]): void {
			let offset = (this as any).elements_offset;
			for (let n of arr) {
				(this as any).buffer.dv.setFloat64(offset, n, true);
				offset += 8;
			}
		}
	};
}

export class Vector_Direct2<T extends Flat> extends Vector {
	*[Symbol.iterator]() {
		let size = this.elements_size;
		for (let ix = 0; ix < size; ++ix) {
			yield new this.flat_struct_constructor(this.buffer, this.elements_offset + this.element_size * ix);
		}
	}
	constructor(buffer: FlatBuffer, offset: number, private element_size: number,
		private flat_struct_constructor: new (buffer: FlatBuffer, offset: number) => T) {
		super(buffer, offset);
	}
}

export class Array_Direct2<T extends Flat> extends FlatArray {
	*[Symbol.iterator]() {
		let size = this.elements_size;
		for (let ix = 0; ix < size; ++ix) {
			yield new this.flat_struct_constructor(this.buffer, this.elements_offset + this.element_size * ix);
		}
	}
	constructor(buffer: FlatBuffer, offset: number, private element_size: number,
		private flat_struct_constructor: new (buffer: FlatBuffer, offset: number) => T,
		size: number) {
		super(buffer, offset, size);
	}
}

export class String_Direct1 extends Iterable(Accessor_U8(Vector)) {
	public assign(str: string): void {
    let utf8_string = new TextEncoder().encode(str);
    let offset = _alloc(this.buffer, this.offset, utf8_string.length, 1, 1);
    new Uint8Array(this.buffer.array_buffer, offset).set(utf8_string);
	}
}

export class Vector_Direct1_u8 extends Iterable(Accessor_U8(Vector)) {}
export class Vector_Direct1_i8 extends Iterable(Accessor_I8(Vector)){}
export class Vector_Direct1_u16 extends Iterable(Accessor_U16(Vector)){}
export class Vector_Direct1_i16 extends Iterable(Accessor_I16(Vector)){}
export class Vector_Direct1_u32 extends Iterable(Accessor_U32(Vector)){}
export class Vector_Direct1_i32 extends Iterable(Accessor_I32(Vector)){}
export class Vector_Direct1_u64 extends Iterable(Accessor_U64(Vector)){}
export class Vector_Direct1_i64 extends Iterable(Accessor_I64(Vector)){}
export class Vector_Direct1_float32 extends Iterable(Accessor_Float32(Vector)){}
export class Vector_Direct1_float64 extends Iterable(Accessor_Float64(Vector)){}

export class Array_Direct1_u8 extends Iterable(Accessor_U8(FlatArray)){}
export class Array_Direct1_i8 extends Iterable(Accessor_I8(FlatArray)){}
export class Array_Direct1_u16 extends Iterable(Accessor_U16(FlatArray)){}
export class Array_Direct1_i16 extends Iterable(Accessor_I16(FlatArray)){}
export class Array_Direct1_u32 extends Iterable(Accessor_U32(FlatArray)){}
export class Array_Direct1_i32 extends Iterable(Accessor_I32(FlatArray)){}
export class Array_Direct1_u64 extends Iterable(Accessor_U64(FlatArray)){}
export class Array_Direct1_i64 extends Iterable(Accessor_I64(FlatArray)){}
export class Array_Direct1_float32 extends Iterable(Accessor_Float32(FlatArray)){}
export class Array_Direct1_float64 extends Iterable(Accessor_Float64(FlatArray)){}