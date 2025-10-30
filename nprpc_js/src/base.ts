
export * from './utils'
export * as Flat from './flat';
export { FlatBuffer } from './flat_buffer';
export * from './marshal_helpers'

export class Exception extends Error {
  constructor(message: string) {
    super(message);
  }
}
