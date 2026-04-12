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
    await stream.writer.write(Uint8Array.of(value));
  }
  await stream.writer.close();

  const chunks = await collectStream(stream.reader);
  return {
    mask,
    input,
    values: chunks.map(chunk => chunk[0]),
    endpointType: testStreams.endpoint.type,
  };
}

export async function stressConcurrentServerStreams(streamCount: number, chunksPerStream: number) {
  const testBasic = await resolveProxy<TestBasic>('nprpc_test_basic', TestBasic);

  const previousDebug = (globalThis as any).__nprpc_debug;
  const previousStreamEvent = previousDebug?.stream_event;
  const incomingOrder: string[] = [];

  (globalThis as any).__nprpc_debug = {
    ...previousDebug,
    stream_start: previousDebug?.stream_start ?? (() => undefined),
    call_start: previousDebug?.call_start ?? (() => undefined),
    call_end: previousDebug?.call_end ?? (() => undefined),
    stream_event(streamId: string, event: { kind?: string; direction?: string }) {
      previousStreamEvent?.(streamId, event);
      if (event.direction === 'incoming' && event.kind === 'chunk') {
        incomingOrder.push(streamId);
      }
    },
  };

  try {
    const testServerStreams = await resolveProxy<TestStreams>('streams_test', TestStreams);
    const readers = await Promise.all(
      Array.from({ length: streamCount }, () => testServerStreams.GetStringStream(chunksPerStream)),
    );
    const streamIds = readers.map(reader => String((reader as any).stream_id));
    const expected = Array.from({ length: streamCount }, () =>
      Array.from({ length: chunksPerStream }, (_, index) => `item_${index}`),
    );
    const outputPromises = readers.map(reader => collectStream(reader));

    const controlPromise = testBasic.ReturnU32();

    const controlResult = await controlPromise;

    const outputs = await Promise.all(outputPromises);

    const perStreamCounts = new Map<string, number>();
    for (const streamId of incomingOrder) {
      if (!streamIds.includes(streamId)) {
        continue;
      }
      perStreamCounts.set(streamId, (perStreamCounts.get(streamId) ?? 0) + 1);
    }

    const firstWindow = incomingOrder.filter(streamId => streamIds.includes(streamId)).slice(0, streamCount * 4);

    return {
      controlResult,
      endpointType: testServerStreams.endpoint.type,
      streamIds,
      expected,
      received: outputs,
      firstWindow,
      firstWindowUnique: new Set(firstWindow).size,
      perStreamCounts: streamIds.map(streamId => perStreamCounts.get(streamId) ?? 0),
    };
  } finally {
    if (previousDebug === undefined) {
      delete (globalThis as any).__nprpc_debug;
    } else {
      (globalThis as any).__nprpc_debug = previousDebug;
    }
  }
}

export async function stressConcurrentBidiStreams(streamCount: number, chunksPerStream: number) {
  const testBasic = await resolveProxy<TestBasic>('nprpc_test_basic', TestBasic);

  const previousDebug = (globalThis as any).__nprpc_debug;
  const previousStreamEvent = previousDebug?.stream_event;
  const incomingOrder: string[] = [];

  (globalThis as any).__nprpc_debug = {
    ...previousDebug,
    stream_start: previousDebug?.stream_start ?? (() => undefined),
    call_start: previousDebug?.call_start ?? (() => undefined),
    call_end: previousDebug?.call_end ?? (() => undefined),
    stream_event(streamId: string, event: { kind?: string; direction?: string }) {
      previousStreamEvent?.(streamId, event);
      if (event.direction === 'incoming' && event.kind === 'chunk') {
        incomingOrder.push(streamId);
      }
    },
  };

  try {
    const testBidiStreams = await resolveProxy<TestStreams>('bidi_stream_test', TestStreams);
    const streams = await Promise.all(
      Array.from({ length: streamCount }, (_, index) => testBidiStreams.EchoByteStream(0x10 + index)),
    );
    const streamIds = streams.map(stream => String((stream.writer as any).stream_id));
    const sent = Array.from({ length: streamCount }, () => [] as number[]);
    const outputPromises = streams.map(stream => collectStream(stream.reader));

    const firstHalf = Math.floor(chunksPerStream / 2);
    for (let round = 0; round < firstHalf; ++round) {
      for (let streamIndex = 0; streamIndex < streamCount; ++streamIndex) {
        const value = (streamIndex * 53 + round) & 0xff;
        sent[streamIndex].push(value);
        await streams[streamIndex].writer.write(Uint8Array.of(value));
      }
    }

    const controlPromise = testBasic.ReturnU32();

    for (let round = firstHalf; round < chunksPerStream; ++round) {
      for (let streamIndex = 0; streamIndex < streamCount; ++streamIndex) {
        const value = (streamIndex * 53 + round) & 0xff;
        sent[streamIndex].push(value);
        await streams[streamIndex].writer.write(Uint8Array.of(value));
      }
    }

    const controlResult = await controlPromise;

    for (const stream of streams) {
      await stream.writer.close();
    }

    const outputs = await Promise.all(outputPromises);
    const received = outputs.map(chunks => chunks.map(chunk => chunk[0]));

    const perStreamCounts = new Map<string, number>();
    for (const streamId of incomingOrder) {
      if (!streamIds.includes(streamId)) {
        continue;
      }
      perStreamCounts.set(streamId, (perStreamCounts.get(streamId) ?? 0) + 1);
    }

    const firstWindow = incomingOrder.filter(streamId => streamIds.includes(streamId)).slice(0, streamCount * 4);

    return {
      controlResult,
      endpointType: testBidiStreams.endpoint.type,
      streamIds,
      sent,
      received,
      firstWindow,
      firstWindowUnique: new Set(firstWindow).size,
      perStreamCounts: streamIds.map(streamId => perStreamCounts.get(streamId) ?? 0),
    };
  } finally {
    if (previousDebug === undefined) {
      delete (globalThis as any).__nprpc_debug;
    } else {
      (globalThis as any).__nprpc_debug = previousDebug;
    }
  }
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

export async function testCancellation() {
  const testBasic = await resolveProxy<TestBasic>('nprpc_test_basic', TestBasic);

  // WebTransport pre-flight cancellation
  const ac1 = new AbortController();
  ac1.abort();
  let wtThrew = false;
  let wtErrorName = '';
  try {
    await testBasic.ReturnU32(ac1.signal);
  } catch (e: any) {
    wtThrew = true;
    wtErrorName = e.name;
  }

  // HTTP pre-flight cancellation
  const ac2 = new AbortController();
  ac2.abort();
  let httpThrew = false;
  let httpErrorName = '';
  try {
    await testBasic.http.ReturnU32(ac2.signal);
  } catch (e: any) {
    httpThrew = true;
    httpErrorName = e.name;
  }

  return {
    wtThrew,
    wtErrorName,
    httpThrew,
    httpErrorName,
    endpointType: testBasic.endpoint.type,
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