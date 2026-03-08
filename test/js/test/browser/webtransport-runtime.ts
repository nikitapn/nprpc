import * as NPRPC from 'nprpc';
import { TestBasic, TestStreams } from '../../src/gen/nprpc_test';

function createNameserverClient() {
  return NPRPC.get_nameserver('localhost');
}

async function resolveProxy<T>(name: string, type: any): Promise<T> {
  const nameserver = createNameserverClient();
  const objectRef = NPRPC.make_ref();
  const found = await nameserver.Resolve(name, objectRef);
  if (!found) {
    throw new Error(`Failed to resolve ${name}`);
  }

  const proxy = NPRPC.narrow(objectRef.value, type);
  if (!proxy) {
    throw new Error(`Failed to narrow ${name}`);
  }

  return proxy as T;
}

async function collectStream<T>(reader: AsyncIterable<T>): Promise<T[]> {
  const values: T[] = [];
  for await (const value of reader) {
    values.push(value);
  }
  return values;
}

export async function init(hashBytes: number[]) {
  if (typeof globalThis.WebTransport === 'undefined') {
    return { unsupported: true };
  }

  await NPRPC.init(false, {
    secured: true,
    webtransport: true,
    webtransport_options: {
      serverCertificateHashes: [
        {
          algorithm: 'sha-256',
          value: new Uint8Array(hashBytes),
        },
      ],
    },
    objects: {},
  });

  return {
    unsupported: false,
    secureContext: globalThis.isSecureContext,
  };
}

export async function openExplicitConnection() {
  const endpoint = new NPRPC.EndPoint(NPRPC.EndPointType.WebTransport, 'localhost', 22223);
  const connection = new NPRPC.Connection(endpoint);
  await connection.ready_;

  const closedState = await Promise.race([
    connection.wt.closed.then(
      () => 'closed',
      () => 'closed',
    ),
    new Promise((resolve) => setTimeout(() => resolve('open'), 250)),
  ]);

  return {
    endpointType: connection.endpoint.type,
    closedState,
  };
}

export async function resolveMany(names: string[]) {
  const nameserver = createNameserverClient();
  const resolved = [];

  for (const name of names) {
    const objectRef = NPRPC.make_ref();
    const found = await nameserver.Resolve(name, objectRef);
    resolved.push({
      name,
      found,
      endpointType: objectRef.value?.endpoint?.type,
      classId: objectRef.value?.data?.class_id,
    });
  }

  return {
    resolved,
    connectionCount: (NPRPC.rpc as any).opened_connections_.length,
  };
}

export async function callTestBasic() {
  const testBasic = await resolveProxy<TestBasic>('nprpc_test_basic', TestBasic);
  const booleanResult = await testBasic.ReturnBoolean();
  const u32Result = await testBasic.ReturnU32();

  return {
    booleanResult,
    u32Result,
    endpointType: testBasic.endpoint.type,
    connectionCount: (NPRPC.rpc as any).opened_connections_.length,
  };
}

export async function repeatedTestBasicCalls(callCount: number) {
  const testBasic = await resolveProxy<TestBasic>('nprpc_test_basic', TestBasic);

  const before = (NPRPC.rpc as any).opened_connections_.length;
  const values = [];
  for (let index = 0; index < callCount; ++index) {
    values.push(await testBasic.ReturnU32());
  }
  const after = (NPRPC.rpc as any).opened_connections_.length;

  return {
    values,
    before,
    after,
  };
}

export async function receiveByteStream() {
  const testStreams = await resolveProxy<TestStreams>('streams_test', TestStreams);
  const chunks = await collectStream(await testStreams.GetByteStream(5n));

  return {
    values: chunks.map(chunk => chunk[0]),
    endpointType: testStreams.endpoint.type,
    connectionCount: (NPRPC.rpc as any).opened_connections_.length,
  };
}

export async function echoBidiByteStream() {
  const testStreams = await resolveProxy<TestStreams>('bidi_stream_test', TestStreams);
  const mask = 0x5a;
  const input = [10, 11, 12, 13];
  const stream = await testStreams.EchoByteStream(mask);

  for (const value of input) {
    stream.writer.write(Uint8Array.of(value));
  }
  stream.writer.close();

  const chunks = await collectStream(stream.reader);
  return {
    mask,
    input,
    values: chunks.map(chunk => chunk[0]),
    endpointType: testStreams.endpoint.type,
  };
}

export async function callTestBasicHttp() {
  const testBasic = await resolveProxy<TestBasic>('nprpc_test_basic', TestBasic);
  const booleanResult = await testBasic.http.ReturnBoolean();
  const u32Result = await testBasic.http.ReturnU32();

  return {
    booleanResult,
    u32Result,
  };
}

export async function repeatedTestBasicHttpCalls(callCount: number) {
  const testBasic = await resolveProxy<TestBasic>('nprpc_test_basic', TestBasic);
  const values = [];
  for (let index = 0; index < callCount; ++index) {
    values.push(await testBasic.http.ReturnU32());
  }

  return {
    values,
  };
}