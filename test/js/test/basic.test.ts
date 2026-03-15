import { describe, it } from 'mocha';
import { expect } from 'chai';
import * as NPRPC from 'nprpc';

// Simple tests without NPRPC imports for now
describe('JavaScript Test Setup', function() {
    this.timeout(10000);

    it('should verify test framework is working', function() {
        expect(true).to.be.true;
    });

    it('should verify TypeScript compilation', function() {
        const testString: string = "TypeScript compilation works";
        expect(testString).to.equal("TypeScript compilation works");
    });

    it('should verify async/await support', async function() {
        const delay = (ms: number) => new Promise(resolve => setTimeout(resolve, ms));
        
        const start = Date.now();
        await delay(100);
        const elapsed = Date.now() - start;
        
        expect(elapsed).to.be.at.least(95); // Allow some tolerance for timing
    });

    it('should round-trip optional signed, unsigned, and floating fundamentals', function() {
        const i64buf = NPRPC.FlatBuffer.create(32);
        i64buf.commit(8);
        NPRPC.marshal_optional_fundamental(i64buf, 0, -123n, 'i64');
        expect(NPRPC.unmarshal_optional_fundamental(i64buf, 0, 'i64')).to.equal(-123n);

        const u64buf = NPRPC.FlatBuffer.create(32);
        u64buf.commit(8);
        NPRPC.marshal_optional_fundamental(u64buf, 0, 123n, 'u64');
        expect(NPRPC.unmarshal_optional_fundamental(u64buf, 0, 'u64')).to.equal(123n);

        const f64buf = NPRPC.FlatBuffer.create(32);
        f64buf.commit(8);
        NPRPC.marshal_optional_fundamental(f64buf, 0, Math.PI, 'f64');
        expect(NPRPC.unmarshal_optional_fundamental(f64buf, 0, 'f64')).to.be.closeTo(Math.PI, 1e-12);

        const i32buf = NPRPC.FlatBuffer.create(32);
        i32buf.commit(8);
        NPRPC.marshal_optional_fundamental(i32buf, 0, -42, 'i32');
        expect(NPRPC.unmarshal_optional_fundamental(i32buf, 0, 'i32')).to.equal(-42);
    });

    it('should preserve typed array kinds when unmarshalling', function() {
        const cases = [
            new Int8Array([-1, 2, -3]),
            new Int16Array([-1024, 2048]),
            new Int32Array([-123456789, 987654321]),
            new Float32Array([Math.PI, -1.5]),
            new Float64Array([Math.PI, -Math.E]),
            new BigInt64Array([-1n, 2n, -3n]),
            new BigUint64Array([1n, 2n, 3n]),
        ];

        for (const value of cases) {
            const buf = NPRPC.FlatBuffer.create(64 + value.byteLength);
            buf.commit(8);
            NPRPC.marshal_typed_array(buf, 0, value, value.BYTES_PER_ELEMENT, value.BYTES_PER_ELEMENT);

            const roundTripped = NPRPC.unmarshal_typed_array(buf, 0, value.constructor as never);

            expect(roundTripped).to.be.instanceOf(value.constructor);
            expect(Array.from(roundTripped as ArrayLike<number | bigint>)).to.deep.equal(
                Array.from(value as ArrayLike<number | bigint>)
            );
        }
    });

    it('should preserve typed array kinds when unmarshalling by fundamental kind', function() {
        const cases = [
            { value: new Int8Array([-1, 2, -3]), kind: 'i8' as const },
            { value: new Uint8Array([1, 2, 3]), kind: 'u8' as const },
            { value: new Int16Array([-1024, 2048]), kind: 'i16' as const },
            { value: new Uint16Array([1024, 2048]), kind: 'u16' as const },
            { value: new Int32Array([-123456789, 987654321]), kind: 'i32' as const },
            { value: new Uint32Array([123456789, 987654321]), kind: 'u32' as const },
            { value: new Float32Array([Math.PI, -1.5]), kind: 'f32' as const },
            { value: new Float64Array([Math.PI, -Math.E]), kind: 'f64' as const },
            { value: new BigInt64Array([-1n, 2n, -3n]), kind: 'i64' as const },
            { value: new BigUint64Array([1n, 2n, 3n]), kind: 'u64' as const },
        ];

        for (const { value, kind } of cases) {
            const buf = NPRPC.FlatBuffer.create(64 + value.byteLength);
            buf.commit(8);
            NPRPC.marshal_typed_array(buf, 0, value, value.BYTES_PER_ELEMENT, value.BYTES_PER_ELEMENT);

            const roundTripped = NPRPC.unmarshal_typed_array(buf, 0, kind as never);

            expect(roundTripped).to.be.instanceOf(value.constructor);
            expect(Array.from(roundTripped as ArrayLike<number | bigint>)).to.deep.equal(
                Array.from(value as ArrayLike<number | bigint>)
            );
        }
    });
});