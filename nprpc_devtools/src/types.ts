// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

export type Transport = 'ws' | 'wss' | 'http' | 'https';
export type CallDirection = 'client' | 'server';
export type CallStatus  = 'pending' | 'success' | 'error';

export interface RpcEndpointInfo {
  hostname : string;
  port     : number;
  transport: Transport;
}

export interface RpcEvent {
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

type Key = {
  /** Combination of id | (seq << 16) to get a unique, display-order-preserving */
  key: number;
}

export type RpcEventWithKey = Omit<RpcEvent & Key, 'id'>;

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

export type DebugMsg = DebugMsgCallStart | DebugMsgCallEnd;
