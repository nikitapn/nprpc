
export * from './utils'
export { FlatBuffer, _alloc, _alloc1 } from './flat_buffer';
export * from './marshal_helpers'

export class Exception extends Error {
  constructor(message: string) {
    super(message);
  }
}
