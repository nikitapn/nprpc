export class NetBuffer {
    public array_buffer: ArrayBuffer;
    private size: number;
    private capacity: number;
    public dv: DataView;

    public prepare(capacity: number): void {
        if (this.capacity + capacity > this.capacity) {
            this.capacity = Math.max(this.capacity * 2, this.capacity + capacity);
            let newBuffer = new ArrayBuffer(this.capacity);
            new Uint8Array(newBuffer).set(new Uint8Array(this.array_buffer));
            this.array_buffer = newBuffer;
            this.dv = new DataView(this.array_buffer);
        }
    }

    public commit(size: number): void {
        this.size += size;
    }

    public detach(): ArrayBuffer {
        return this.array_buffer.slice(0, this.size);
    }

    public get offset() {
        return this.size;
    }

    public static from_array_buffer(array_buffer: ArrayBuffer) {
        let b = new NetBuffer();
        b.array_buffer = array_buffer;
        b.size = array_buffer.byteLength;
        b.capacity = array_buffer.byteLength;
        b.dv = new DataView(b.array_buffer);
        return b;
    }

    constructor() {
        this.capacity = 0;
        this.size = 0;
    }
}

export class U8 {
    private buffer: NetBuffer;
    private offset: number;

    public get val() {
        return this.buffer.dv.getUint8(this.offset);
    }
    public set val(value: number) {
        this.buffer.dv.setUint8(this.offset, value);
    }

    constructor(buffer: NetBuffer, offset: number) {
        this.buffer = buffer;
        this.offset = offset;
    }
}

export class I8 {
    private buffer: NetBuffer;
    private offset: number;

    public get val() {
        return this.buffer.dv.getInt8(this.offset);
    }
    public set val(value: number) {
        this.buffer.dv.setInt8(this.offset, value);
    }

    constructor(buffer: NetBuffer, offset: number) {
        this.buffer = buffer;
        this.offset = offset;
    }
}

export class U16 {
    private buffer: NetBuffer;
    private offset: number;

    public get val() {
        return this.buffer.dv.getUint16(this.offset, true);
    }
    public set val(value: number) {
        this.buffer.dv.setUint16(this.offset, value, true);
    }

    constructor(buffer: NetBuffer, offset: number) {
        this.buffer = buffer;
        this.offset = offset;
    }
}

export class I16 {
    private buffer: NetBuffer;
    private offset: number;

    public get val() {
        return this.buffer.dv.getInt16(this.offset, true);
    }
    public set val(value: number) {
        this.buffer.dv.setInt16(this.offset, value, true);
    }

    constructor(buffer: NetBuffer, offset: number) {
        this.buffer = buffer;
        this.offset = offset;
    }
}

export class U32 {
    private buffer: NetBuffer;
    private offset: number;

    public get val() {
        return this.buffer.dv.getUint32(this.offset, true);
    }
    public set val(value: number) {
        this.buffer.dv.setUint32(this.offset, value, true);
    }

    constructor(buffer: NetBuffer, offset: number) {
        this.buffer = buffer;
        this.offset = offset;
    }
}

export class I32 {
    private buffer: NetBuffer;
    private offset: number;

    public get val() {
        return this.buffer.dv.getInt32(this.offset, true);
    }
    public set val(value: number) {
        this.buffer.dv.setInt32(this.offset, value, true);
    }

    constructor(buffer: NetBuffer, offset: number) {
        this.buffer = buffer;
        this.offset = offset;
    }
}

export class Float32 {
    private buffer: NetBuffer;
    private offset: number;

    public get val() {
        return this.buffer.dv.getFloat32(this.offset, true);
    }
    public set val(value: number) {
        this.buffer.dv.setFloat32(this.offset, value, true);
    }

    constructor(buffer: NetBuffer, offset: number) {
        this.buffer = buffer;
        this.offset = offset;
    }
}

export class Float64 {
    private buffer: NetBuffer;
    private offset: number;

    public get val() {
        return this.buffer.dv.getFloat64(this.offset, true);
    }
    public set val(value: number) {
        this.buffer.dv.setFloat64(this.offset, value, true);
    }

    constructor(buffer: NetBuffer, offset: number) {
        this.buffer = buffer;
        this.offset = offset;
    }
}

export function _alloc(
    buffer: NetBuffer,
    vector_offset: number,
    n: number,
    element_size: number,
    align: number)
    : number {
    let rem = (buffer.offset + buffer.offset) % align;
    let offset = rem
        ? buffer.offset + (align - rem)
        : buffer.offset;
    let add_size = n * element_size + (offset - buffer.offset);

    buffer.prepare(add_size);
    buffer.commit(add_size);

    buffer.dv.setUint32(vector_offset, offset - vector_offset, true)
    buffer.dv.setUint32(vector_offset + 4, n, true)

    return offset;
}

export class SpanBase {
    protected buffer: NetBuffer;
    protected first: number;
    protected last: number;
    protected pod_size: number;

    constructor(buffer: NetBuffer, first: number, last: number, pod_size: number) {
        this.buffer = buffer;
        this.first = first;
        this.last = last;
        this.pod_size = pod_size;
    }
}

export class Span_U8 extends SpanBase {
    *[Symbol.iterator]() {
        for (let addr = this.first; addr < this.last; addr += this.pod_size) {
            yield new U8(this.buffer, addr);
        }
    }
}

export class Span_I8 extends SpanBase {
    *[Symbol.iterator]() {
        for (let addr = this.first; addr < this.last; addr += this.pod_size) {
            yield new I8(this.buffer, addr);
        }
    }
}

export class Span_U16 extends SpanBase {
    *[Symbol.iterator]() {
        for (let addr = this.first; addr < this.last; addr += this.pod_size) {
            yield new U16(this.buffer, addr);
        }
    }
}

export class Span_I16 extends SpanBase {
    *[Symbol.iterator]() {
        for (let addr = this.first; addr < this.last; addr += this.pod_size) {
            yield new I16(this.buffer, addr);
        }
    }
}

export class Span_U32 extends SpanBase {
    *[Symbol.iterator]() {
        for (let addr = this.first; addr < this.last; addr += this.pod_size) {
            yield new U32(this.buffer, addr);
        }
    }
}

export class Span_I32 extends SpanBase {
    *[Symbol.iterator]() {
        for (let addr = this.first; addr < this.last; addr += this.pod_size) {
            yield new I32(this.buffer, addr);
        }
    }
}