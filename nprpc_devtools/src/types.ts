// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

export type Transport = 'ws' | 'wss' | 'http' | 'https' | 'wt';
export type CallDirection = 'client' | 'server';
export type CallStatus  = 'pending' | 'success' | 'error' | 'cancelled';
export type DebugEntryKind = 'rpc' | 'stream';
export type StreamKind = 'server' | 'client' | 'bidi';
export type StreamMessageKind = 'chunk' | 'complete' | 'error' | 'cancel' | 'window_update';
export type StreamMessageDirection = 'incoming' | 'outgoing';

export interface RpcEndpointInfo {
  hostname : string;
  port     : number;
  transport: Transport;
}

export interface RpcEvent {
  entry_kind: 'rpc';
  /** Unique monotonic call ID assigned by inject.js */
  id: number;
  /** 1-based display sequence number (reset on Clear) */
  seq: number;

  direction : CallDirection;
  status    : CallStatus;
  /** Date.now() at call start */
  timestamp : number;
  /** Populated on call_end */
  duration_ms?: number;

  // ── Object identity ────────────────────────────────────────────
  class_id     : string;
  poa_idx      : number;
  /** Stringified BigInt so it survives JSON serialization */
  object_id    : string;
  interface_idx: number;
  func_idx     : number;
  /** Optional – populated only when generated stubs emit hooks */
  method_name? : string;

  // ── Network ────────────────────────────────────────────────────
  endpoint: RpcEndpointInfo;

  // ── Payload ────────────────────────────────────────────────────
  request_args?  : unknown;
  response_args? : unknown;
  error?         : string;

  // ── Wire sizes ─────────────────────────────────────────────────
  request_bytes?  : number;
  response_bytes? : number;
}

export interface StreamMessageEvent {
  index: number;
  timestamp: number;
  kind: StreamMessageKind;
  direction: StreamMessageDirection;
  sequence?: string;
  bytes?: number;
  credits?: number;
  error_code?: number;
  payload?: unknown;
  payload_summary?: string;
}

export interface StreamEvent {
  entry_kind: 'stream';
  id: number;
  seq: number;

  direction: CallDirection;
  status: CallStatus;
  timestamp: number;
  duration_ms?: number;

  class_id     : string;
  poa_idx      : number;
  object_id    : string;
  interface_idx: number;
  func_idx     : number;
  method_name? : string;

  endpoint: RpcEndpointInfo;

  stream_id: string;
  stream_kind: StreamKind;
  request_args?: unknown;
  request_bytes?: number;
  sent_bytes: number;
  received_bytes: number;
  message_count: number;
  messages: StreamMessageEvent[];
  error?: string;
}

export type DebugEntry = RpcEvent | StreamEvent;

// ── Messages shared between inject / content / background / panel ──

export interface DebugMsgCallStart {
  type  : 'nprpc_call_start';
  source: 'nprpc_content';
  data  : Omit<RpcEvent, 'seq'>;
}

export interface DebugMsgCallEnd {
  type  : 'nprpc_call_end';
  source: 'nprpc_content';
  data  : Pick<RpcEvent, 'id' | 'status' | 'duration_ms' | 'response_args' | 'response_bytes' | 'error'>;
}

export interface StreamStartData {
  id: number;
  direction: CallDirection;
  status: 'pending';
  timestamp: number;
  class_id: string;
  poa_idx: number;
  object_id: string;
  interface_idx: number;
  func_idx: number;
  method_name?: string;
  endpoint: RpcEndpointInfo;
  stream_id: string;
  stream_kind: StreamKind;
  request_args?: unknown;
  request_bytes?: number;
}

export interface DebugMsgStreamStart {
  type: 'nprpc_stream_start';
  source: 'nprpc_content';
  data: StreamStartData;
}

export interface StreamMessageData {
  id: number;
  stream_id: string;
  message: Omit<StreamMessageEvent, 'index'>;
}

export interface DebugMsgStreamMessage {
  type: 'nprpc_stream_message';
  source: 'nprpc_content';
  data: StreamMessageData;
}

export type DebugMsg = DebugMsgCallStart | DebugMsgCallEnd | DebugMsgStreamStart | DebugMsgStreamMessage;
