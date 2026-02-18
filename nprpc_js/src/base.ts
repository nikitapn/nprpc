// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

export * from './utils'
export { FlatBuffer, _alloc, _alloc1 } from './flat_buffer';
export * from './marshal_helpers'

export class Exception extends Error {
  constructor(message: string) {
    super(message);
  }
}
