// WebSocket polyfill for Node.js environment
// This allows the NPRPC library (designed for browsers) to work in Node.js

import { WebSocket as NodeWebSocket } from 'ws';

// Polyfill WebSocket for Node.js environment
if (typeof global !== 'undefined') {
  // We're in Node.js
  if (typeof (global as any).WebSocket === 'undefined') {
    console.log('Setting up WebSocket polyfill for Node.js...');
    
    // Create a WebSocket-compatible wrapper
    class WebSocketPolyfill {
      public static readonly CONNECTING = 0;
      public static readonly OPEN = 1;
      public static readonly CLOSING = 2;
      public static readonly CLOSED = 3;

      public readonly CONNECTING = WebSocketPolyfill.CONNECTING;
      public readonly OPEN = WebSocketPolyfill.OPEN;
      public readonly CLOSING = WebSocketPolyfill.CLOSING;
      public readonly CLOSED = WebSocketPolyfill.CLOSED;

      private ws: NodeWebSocket;
      
      public binaryType: 'blob' | 'arraybuffer' = 'arraybuffer';
      public onopen: ((event: Event) => void) | null = null;
      public onclose: ((event: CloseEvent) => void) | null = null;
      public onmessage: ((event: MessageEvent) => void) | null = null;
      public onerror: ((event: Event) => void) | null = null;

      constructor(url: string, protocols?: string | string[]) {
        this.ws = new NodeWebSocket(url, protocols);
        
        this.ws.on('open', () => {
          if (this.onopen) {
            this.onopen(new Event('open'));
          }
        });

        this.ws.on('close', (code: number, reason: Buffer) => {
          if (this.onclose) {
            const event = new CloseEvent('close', {
              code,
              reason: reason.toString(),
              wasClean: code === 1000
            });
            this.onclose(event);
          }
        });

        this.ws.on('message', (data: Buffer) => {
          if (this.onmessage) {
            let processedData: any;
            
            if (this.binaryType === 'arraybuffer') {
              // Convert Buffer to ArrayBuffer
              processedData = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
            } else {
              processedData = data;
            }
            
            const event = new MessageEvent('message', {
              data: processedData
            });
            this.onmessage(event);
          }
        });

        this.ws.on('error', (error: Error) => {
          if (this.onerror) {
            const event = new ErrorEvent('error', {
              error,
              message: error.message
            });
            this.onerror(event);
          }
        });
      }

      get readyState(): number {
        switch (this.ws.readyState) {
          case NodeWebSocket.CONNECTING:
            return this.CONNECTING;
          case NodeWebSocket.OPEN:
            return this.OPEN;
          case NodeWebSocket.CLOSING:
            return this.CLOSING;
          case NodeWebSocket.CLOSED:
            return this.CLOSED;
          default:
            return this.CLOSED;
        }
      }

      get url(): string {
        return this.ws.url;
      }

      send(data: string | ArrayBuffer | ArrayBufferView): void {
        if (data instanceof ArrayBuffer) {
          this.ws.send(Buffer.from(data));
        } else if (ArrayBuffer.isView(data)) {
          this.ws.send(Buffer.from(data.buffer, data.byteOffset, data.byteLength));
        } else {
          this.ws.send(data);
        }
      }

      close(code?: number, reason?: string): void {
        this.ws.close(code, reason);
      }
    }

    // Set up global WebSocket
    (global as any).WebSocket = WebSocketPolyfill;
    
    // Also set up Event classes if they don't exist
    if (typeof (global as any).Event === 'undefined') {
      (global as any).Event = class Event {
        constructor(public type: string, public eventInitDict?: EventInit) {}
      };
    }
    
    if (typeof (global as any).MessageEvent === 'undefined') {
      (global as any).MessageEvent = class MessageEvent {
        public data: any;
        public type: string;
        
        constructor(type: string, eventInitDict?: MessageEventInit) {
          this.type = type;
          this.data = eventInitDict?.data;
        }
      };
    }
    
    if (typeof (global as any).CloseEvent === 'undefined') {
      (global as any).CloseEvent = class CloseEvent {
        public code: number;
        public reason: string;
        public wasClean: boolean;
        public type: string;
        
        constructor(type: string, eventInitDict?: CloseEventInit) {
          this.type = type;
          this.code = eventInitDict?.code || 0;
          this.reason = eventInitDict?.reason || '';
          this.wasClean = eventInitDict?.wasClean || false;
        }
      };
    }
    
    if (typeof (global as any).ErrorEvent === 'undefined') {
      (global as any).ErrorEvent = class ErrorEvent {
        public error: any;
        public message: string;
        public type: string;
        
        constructor(type: string, eventInitDict?: ErrorEventInit) {
          this.type = type;
          this.error = eventInitDict?.error;
          this.message = eventInitDict?.message || '';
        }
      };
    }
    
    console.log('WebSocket polyfill setup complete');
  }
}

export {};
