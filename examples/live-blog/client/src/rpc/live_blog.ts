import * as NPRPC from 'nprpc'

const u8enc = new TextEncoder();
const u8dec = new TextDecoder();

export type binary = Uint8Array;
export enum PresenceEventKind { //u32
  Joined,
  Left,
  Typing
}

export interface AuthorPreview {
  id: bigint/*u64*/;
  slug: string;
  name: string;
  bio: string;
  avatar_url: string;
}

export function marshal_AuthorPreview(buf: NPRPC.FlatBuffer, offset: number, data: AuthorPreview): void {
buf.dv.setBigUint64(offset + 0, data.id, true);
NPRPC.marshal_string(buf, offset + 8, data.slug);
NPRPC.marshal_string(buf, offset + 16, data.name);
NPRPC.marshal_string(buf, offset + 24, data.bio);
NPRPC.marshal_string(buf, offset + 32, data.avatar_url);
}

export function unmarshal_AuthorPreview(buf: NPRPC.FlatBuffer, offset: number): AuthorPreview {
const result = {} as AuthorPreview;
result.id = buf.dv.getBigUint64(offset + 0, true);
result.slug = NPRPC.unmarshal_string(buf, offset + 8);
result.name = NPRPC.unmarshal_string(buf, offset + 16);
result.bio = NPRPC.unmarshal_string(buf, offset + 24);
result.avatar_url = NPRPC.unmarshal_string(buf, offset + 32);
return result;
}

export interface PostPreview {
  id: bigint/*u64*/;
  slug: string;
  title: string;
  excerpt: string;
  published_at: string;
  cover_url?: string;
  author: AuthorPreview;
}

export function marshal_PostPreview(buf: NPRPC.FlatBuffer, offset: number, data: PostPreview): void {
buf.dv.setBigUint64(offset + 0, data.id, true);
NPRPC.marshal_string(buf, offset + 8, data.slug);
NPRPC.marshal_string(buf, offset + 16, data.title);
NPRPC.marshal_string(buf, offset + 24, data.excerpt);
NPRPC.marshal_string(buf, offset + 32, data.published_at);
if (data.cover_url !== undefined) {
  NPRPC.marshal_optional_struct(buf, offset + 40, data.cover_url, NPRPC.marshal_string, 8, 4);
} else {
  buf.dv.setUint32(offset + 40, 0, true); // nullopt
}
marshal_AuthorPreview(buf, offset + 48, data.author);
}

export function unmarshal_PostPreview(buf: NPRPC.FlatBuffer, offset: number): PostPreview {
const result = {} as PostPreview;
result.id = buf.dv.getBigUint64(offset + 0, true);
result.slug = NPRPC.unmarshal_string(buf, offset + 8);
result.title = NPRPC.unmarshal_string(buf, offset + 16);
result.excerpt = NPRPC.unmarshal_string(buf, offset + 24);
result.published_at = NPRPC.unmarshal_string(buf, offset + 32);
if (buf.dv.getUint32(offset + 40, true) !== 0) {
  result.cover_url = NPRPC.unmarshal_optional_struct(buf, offset + 40, NPRPC.unmarshal_string, 4);
} else {
  result.cover_url = undefined;
}
result.author = unmarshal_AuthorPreview(buf, offset + 48);
return result;
}

export interface PostPage {
  page: number/*u32*/;
  page_size: number/*u32*/;
  total_posts: number/*u32*/;
  posts: Array<PostPreview>;
}

export function marshal_PostPage(buf: NPRPC.FlatBuffer, offset: number, data: PostPage): void {
buf.dv.setUint32(offset + 0, data.page, true);
buf.dv.setUint32(offset + 4, data.page_size, true);
buf.dv.setUint32(offset + 8, data.total_posts, true);
NPRPC.marshal_struct_array(buf, offset + 12, data.posts, marshal_PostPreview, 88, 8);
}

export function unmarshal_PostPage(buf: NPRPC.FlatBuffer, offset: number): PostPage {
const result = {} as PostPage;
result.page = buf.dv.getUint32(offset + 0, true);
result.page_size = buf.dv.getUint32(offset + 4, true);
result.total_posts = buf.dv.getUint32(offset + 8, true);
result.posts = NPRPC.unmarshal_struct_array(buf, offset + 12, unmarshal_PostPreview, 88);
return result;
}

export interface PostDetail {
  id: bigint/*u64*/;
  slug: string;
  title: string;
  summary: string;
  body_html: string;
  published_at: string;
  author: AuthorPreview;
  tags: Array<string>;
}

export function marshal_PostDetail(buf: NPRPC.FlatBuffer, offset: number, data: PostDetail): void {
buf.dv.setBigUint64(offset + 0, data.id, true);
NPRPC.marshal_string(buf, offset + 8, data.slug);
NPRPC.marshal_string(buf, offset + 16, data.title);
NPRPC.marshal_string(buf, offset + 24, data.summary);
NPRPC.marshal_string(buf, offset + 32, data.body_html);
NPRPC.marshal_string(buf, offset + 40, data.published_at);
marshal_AuthorPreview(buf, offset + 48, data.author);
NPRPC.marshal_string_array(buf, offset + 88, data.tags);
}

export function unmarshal_PostDetail(buf: NPRPC.FlatBuffer, offset: number): PostDetail {
const result = {} as PostDetail;
result.id = buf.dv.getBigUint64(offset + 0, true);
result.slug = NPRPC.unmarshal_string(buf, offset + 8);
result.title = NPRPC.unmarshal_string(buf, offset + 16);
result.summary = NPRPC.unmarshal_string(buf, offset + 24);
result.body_html = NPRPC.unmarshal_string(buf, offset + 32);
result.published_at = NPRPC.unmarshal_string(buf, offset + 40);
result.author = unmarshal_AuthorPreview(buf, offset + 48);
result.tags = NPRPC.unmarshal_string_array(buf, offset + 88);
return result;
}

export interface Comment {
  id: bigint/*u64*/;
  author_name: string;
  body: string;
  created_at: string;
}

export function marshal_Comment(buf: NPRPC.FlatBuffer, offset: number, data: Comment): void {
buf.dv.setBigUint64(offset + 0, data.id, true);
NPRPC.marshal_string(buf, offset + 8, data.author_name);
NPRPC.marshal_string(buf, offset + 16, data.body);
NPRPC.marshal_string(buf, offset + 24, data.created_at);
}

export function unmarshal_Comment(buf: NPRPC.FlatBuffer, offset: number): Comment {
const result = {} as Comment;
result.id = buf.dv.getBigUint64(offset + 0, true);
result.author_name = NPRPC.unmarshal_string(buf, offset + 8);
result.body = NPRPC.unmarshal_string(buf, offset + 16);
result.created_at = NPRPC.unmarshal_string(buf, offset + 24);
return result;
}

export interface ChatEnvelope {
  author: string;
  body: string;
  created_at: string;
}

export function marshal_ChatEnvelope(buf: NPRPC.FlatBuffer, offset: number, data: ChatEnvelope): void {
NPRPC.marshal_string(buf, offset + 0, data.author);
NPRPC.marshal_string(buf, offset + 8, data.body);
NPRPC.marshal_string(buf, offset + 16, data.created_at);
}

export function unmarshal_ChatEnvelope(buf: NPRPC.FlatBuffer, offset: number): ChatEnvelope {
const result = {} as ChatEnvelope;
result.author = NPRPC.unmarshal_string(buf, offset + 0);
result.body = NPRPC.unmarshal_string(buf, offset + 8);
result.created_at = NPRPC.unmarshal_string(buf, offset + 16);
return result;
}

export interface PresenceEvent {
  user_name: string;
  kind: PresenceEventKind;
}

export function marshal_PresenceEvent(buf: NPRPC.FlatBuffer, offset: number, data: PresenceEvent): void {
NPRPC.marshal_string(buf, offset + 0, data.user_name);
buf.dv.setUint32(offset + 8, data.kind, true);
}

export function unmarshal_PresenceEvent(buf: NPRPC.FlatBuffer, offset: number): PresenceEvent {
const result = {} as PresenceEvent;
result.user_name = NPRPC.unmarshal_string(buf, offset + 0);
result.kind = buf.dv.getUint32(offset + 8, true);
return result;
}

export interface ChatServerEvent {
  message: ChatEnvelope;
  presence: PresenceEvent;
}

export function marshal_ChatServerEvent(buf: NPRPC.FlatBuffer, offset: number, data: ChatServerEvent): void {
marshal_ChatEnvelope(buf, offset + 0, data.message);
marshal_PresenceEvent(buf, offset + 24, data.presence);
}

export function unmarshal_ChatServerEvent(buf: NPRPC.FlatBuffer, offset: number): ChatServerEvent {
const result = {} as ChatServerEvent;
result.message = unmarshal_ChatEnvelope(buf, offset + 0);
result.presence = unmarshal_PresenceEvent(buf, offset + 24);
return result;
}

export class BlogService extends NPRPC.ObjectProxy {
  public static get servant_t(): new() => _IBlogService_Servant {
    return _IBlogService_Servant;
  }


  public async ListPosts(page: /*in*/number, page_size: /*in*/number): Promise<PostPage> {
    let interface_idx = (arguments.length == 2 ? 0 : arguments[arguments.length - 1]);
    const buf = NPRPC.FlatBuffer.create();
    buf.prepare(40);
    buf.commit(40);
    buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
    buf.write_msg_type(NPRPC.impl.MessageType.Request);
    // Write CallHeader directly
    buf.dv.setUint16(16 + 0, this.data.poa_idx, true);
    buf.dv.setUint8(16 + 2, interface_idx);
    buf.dv.setUint8(16 + 3, 0);
    buf.dv.setBigUint64(16 + 8, this.data.object_id, true);
    marshal_live_blog_M1(buf, 32, {_1: page, _2: page_size});
    buf.write_len(buf.size - 4);
    const __dbg_t0 = Date.now();
    const __dbg_id = (globalThis as any).__nprpc_debug?.call_start({direction:'client',class_id:_IBlogService_Servant._get_class(),poa_idx:this.data.poa_idx,object_id:String(this.data.object_id),interface_idx,func_idx:0,method_name:'ListPosts',endpoint:{hostname:this.endpoint.hostname,port:this.endpoint.port,transport:NPRPC.EndPoint.to_string(this.endpoint.type).replace('://','') as any},request_args:{page:page,page_size:page_size},request_bytes:buf.size});
    await NPRPC.rpc.call(this.endpoint, buf, this.timeout);
    let std_reply = NPRPC.handle_standart_reply(buf);
    if (std_reply != -1) {
      console.log("received an unusual reply for function with output arguments");
      throw new NPRPC.Exception("Unknown Error");
    }
    const out = unmarshal_live_blog_M2(buf, 16);
    (globalThis as any).__nprpc_debug?.call_end(__dbg_id,{status:'success',duration_ms:Date.now()-__dbg_t0,response_bytes:buf.size,response_args:out});
    return out._1;
  }
  public async GetPost(slug: /*in*/string): Promise<PostDetail> {
    let interface_idx = (arguments.length == 1 ? 0 : arguments[arguments.length - 1]);
    const buf = NPRPC.FlatBuffer.create();
    buf.prepare(168);
    buf.commit(40);
    buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
    buf.write_msg_type(NPRPC.impl.MessageType.Request);
    // Write CallHeader directly
    buf.dv.setUint16(16 + 0, this.data.poa_idx, true);
    buf.dv.setUint8(16 + 2, interface_idx);
    buf.dv.setUint8(16 + 3, 1);
    buf.dv.setBigUint64(16 + 8, this.data.object_id, true);
    marshal_live_blog_M3(buf, 32, {_1: slug});
    buf.write_len(buf.size - 4);
    const __dbg_t0 = Date.now();
    const __dbg_id = (globalThis as any).__nprpc_debug?.call_start({direction:'client',class_id:_IBlogService_Servant._get_class(),poa_idx:this.data.poa_idx,object_id:String(this.data.object_id),interface_idx,func_idx:1,method_name:'GetPost',endpoint:{hostname:this.endpoint.hostname,port:this.endpoint.port,transport:NPRPC.EndPoint.to_string(this.endpoint.type).replace('://','') as any},request_args:{slug:slug},request_bytes:buf.size});
    await NPRPC.rpc.call(this.endpoint, buf, this.timeout);
    let std_reply = NPRPC.handle_standart_reply(buf);
    if (std_reply != -1) {
      console.log("received an unusual reply for function with output arguments");
      throw new NPRPC.Exception("Unknown Error");
    }
    const out = unmarshal_live_blog_M4(buf, 16);
    (globalThis as any).__nprpc_debug?.call_end(__dbg_id,{status:'success',duration_ms:Date.now()-__dbg_t0,response_bytes:buf.size,response_args:out});
    return out._1;
  }
  public async ListComments(post_id: /*in*/bigint, page: /*in*/number, page_size: /*in*/number): Promise<Array<Comment>> {
    let interface_idx = (arguments.length == 3 ? 0 : arguments[arguments.length - 1]);
    const buf = NPRPC.FlatBuffer.create();
    buf.prepare(48);
    buf.commit(48);
    buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
    buf.write_msg_type(NPRPC.impl.MessageType.Request);
    // Write CallHeader directly
    buf.dv.setUint16(16 + 0, this.data.poa_idx, true);
    buf.dv.setUint8(16 + 2, interface_idx);
    buf.dv.setUint8(16 + 3, 2);
    buf.dv.setBigUint64(16 + 8, this.data.object_id, true);
    marshal_live_blog_M5(buf, 32, {_1: post_id, _2: page, _3: page_size});
    buf.write_len(buf.size - 4);
    const __dbg_t0 = Date.now();
    const __dbg_id = (globalThis as any).__nprpc_debug?.call_start({direction:'client',class_id:_IBlogService_Servant._get_class(),poa_idx:this.data.poa_idx,object_id:String(this.data.object_id),interface_idx,func_idx:2,method_name:'ListComments',endpoint:{hostname:this.endpoint.hostname,port:this.endpoint.port,transport:NPRPC.EndPoint.to_string(this.endpoint.type).replace('://','') as any},request_args:{post_id:post_id,page:page,page_size:page_size},request_bytes:buf.size});
    await NPRPC.rpc.call(this.endpoint, buf, this.timeout);
    let std_reply = NPRPC.handle_standart_reply(buf);
    if (std_reply != -1) {
      console.log("received an unusual reply for function with output arguments");
      throw new NPRPC.Exception("Unknown Error");
    }
    const out = unmarshal_live_blog_M6(buf, 16);
    (globalThis as any).__nprpc_debug?.call_end(__dbg_id,{status:'success',duration_ms:Date.now()-__dbg_t0,response_bytes:buf.size,response_args:out});
    return out._1;
  }
  public async ListAuthorPosts(author_slug: /*in*/string, page: /*in*/number, page_size: /*in*/number): Promise<Array<PostPreview>> {
    let interface_idx = (arguments.length == 3 ? 0 : arguments[arguments.length - 1]);
    const buf = NPRPC.FlatBuffer.create();
    buf.prepare(176);
    buf.commit(48);
    buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
    buf.write_msg_type(NPRPC.impl.MessageType.Request);
    // Write CallHeader directly
    buf.dv.setUint16(16 + 0, this.data.poa_idx, true);
    buf.dv.setUint8(16 + 2, interface_idx);
    buf.dv.setUint8(16 + 3, 3);
    buf.dv.setBigUint64(16 + 8, this.data.object_id, true);
    marshal_live_blog_M7(buf, 32, {_1: author_slug, _2: page, _3: page_size});
    buf.write_len(buf.size - 4);
    const __dbg_t0 = Date.now();
    const __dbg_id = (globalThis as any).__nprpc_debug?.call_start({direction:'client',class_id:_IBlogService_Servant._get_class(),poa_idx:this.data.poa_idx,object_id:String(this.data.object_id),interface_idx,func_idx:3,method_name:'ListAuthorPosts',endpoint:{hostname:this.endpoint.hostname,port:this.endpoint.port,transport:NPRPC.EndPoint.to_string(this.endpoint.type).replace('://','') as any},request_args:{author_slug:author_slug,page:page,page_size:page_size},request_bytes:buf.size});
    await NPRPC.rpc.call(this.endpoint, buf, this.timeout);
    let std_reply = NPRPC.handle_standart_reply(buf);
    if (std_reply != -1) {
      console.log("received an unusual reply for function with output arguments");
      throw new NPRPC.Exception("Unknown Error");
    }
    const out = unmarshal_live_blog_M8(buf, 16);
    (globalThis as any).__nprpc_debug?.call_end(__dbg_id,{status:'success',duration_ms:Date.now()-__dbg_t0,response_bytes:buf.size,response_args:out});
    return out._1;
  }
  public async GetAuthor(author_slug: /*in*/string): Promise<AuthorPreview> {
    let interface_idx = (arguments.length == 1 ? 0 : arguments[arguments.length - 1]);
    const buf = NPRPC.FlatBuffer.create();
    buf.prepare(168);
    buf.commit(40);
    buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
    buf.write_msg_type(NPRPC.impl.MessageType.Request);
    // Write CallHeader directly
    buf.dv.setUint16(16 + 0, this.data.poa_idx, true);
    buf.dv.setUint8(16 + 2, interface_idx);
    buf.dv.setUint8(16 + 3, 4);
    buf.dv.setBigUint64(16 + 8, this.data.object_id, true);
    marshal_live_blog_M3(buf, 32, {_1: author_slug});
    buf.write_len(buf.size - 4);
    const __dbg_t0 = Date.now();
    const __dbg_id = (globalThis as any).__nprpc_debug?.call_start({direction:'client',class_id:_IBlogService_Servant._get_class(),poa_idx:this.data.poa_idx,object_id:String(this.data.object_id),interface_idx,func_idx:4,method_name:'GetAuthor',endpoint:{hostname:this.endpoint.hostname,port:this.endpoint.port,transport:NPRPC.EndPoint.to_string(this.endpoint.type).replace('://','') as any},request_args:{author_slug:author_slug},request_bytes:buf.size});
    await NPRPC.rpc.call(this.endpoint, buf, this.timeout);
    let std_reply = NPRPC.handle_standart_reply(buf);
    if (std_reply != -1) {
      console.log("received an unusual reply for function with output arguments");
      throw new NPRPC.Exception("Unknown Error");
    }
    const out = unmarshal_live_blog_M9(buf, 16);
    (globalThis as any).__nprpc_debug?.call_end(__dbg_id,{status:'success',duration_ms:Date.now()-__dbg_t0,response_bytes:buf.size,response_args:out});
    return out._1;
  }

  // HTTP Transport (alternative to WebSocket)
  public readonly http = {
    ListPosts: async (page: /*in*/number, page_size: /*in*/number): Promise<PostPage> => {
      const buf = NPRPC.FlatBuffer.create();
      buf.prepare(40);
      buf.commit(40);
      buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
      buf.write_msg_type(NPRPC.impl.MessageType.Request);
      buf.dv.setUint16(16 + 0, this.data.poa_idx, true);
      buf.dv.setUint8(16 + 2, 0);
      buf.dv.setUint8(16 + 3, 0);
      buf.dv.setBigUint64(16 + 8, this.data.object_id, true);
      marshal_live_blog_M1(buf, 32, {_1: page, _2: page_size});
      buf.write_len(buf.size - 4);

      const __dbg_t0 = Date.now();
      const __dbg_id = (globalThis as any).__nprpc_debug?.call_start({direction:'client',class_id:_IBlogService_Servant._get_class(),poa_idx:this.data.poa_idx,object_id:String(this.data.object_id),interface_idx:0,func_idx:0,method_name:'ListPosts',endpoint:{hostname:this.endpoint.hostname,port:this.endpoint.port,transport:'http'},request_args:{page:page,page_size:page_size},request_bytes:buf.size});

      const url = `http${this.endpoint.is_ssl() ? 's' : ''}://${this.endpoint.hostname}:${this.endpoint.port}/rpc`;
      const response = await fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        credentials: 'include',
        body: buf.array_buffer
      }
);

      if (!response.ok) throw new NPRPC.Exception(`HTTP error: ${response.status}`);
      const response_data = await response.arrayBuffer();
      buf.set_buffer(response_data);

      let std_reply = NPRPC.handle_standart_reply(buf);
      if (std_reply != -1) throw new NPRPC.Exception("Unexpected reply");
      const out = unmarshal_live_blog_M2(buf, 16);
      (globalThis as any).__nprpc_debug?.call_end(__dbg_id,{status:'success',duration_ms:Date.now()-__dbg_t0,response_bytes:buf.size,response_args:out});
      return out._1;
    },
    GetPost: async (slug: /*in*/string): Promise<PostDetail> => {
      const buf = NPRPC.FlatBuffer.create();
      buf.prepare(168);
      buf.commit(40);
      buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
      buf.write_msg_type(NPRPC.impl.MessageType.Request);
      buf.dv.setUint16(16 + 0, this.data.poa_idx, true);
      buf.dv.setUint8(16 + 2, 0);
      buf.dv.setUint8(16 + 3, 1);
      buf.dv.setBigUint64(16 + 8, this.data.object_id, true);
      marshal_live_blog_M3(buf, 32, {_1: slug});
      buf.write_len(buf.size - 4);

      const __dbg_t0 = Date.now();
      const __dbg_id = (globalThis as any).__nprpc_debug?.call_start({direction:'client',class_id:_IBlogService_Servant._get_class(),poa_idx:this.data.poa_idx,object_id:String(this.data.object_id),interface_idx:0,func_idx:1,method_name:'GetPost',endpoint:{hostname:this.endpoint.hostname,port:this.endpoint.port,transport:'http'},request_args:{slug:slug},request_bytes:buf.size});

      const url = `http${this.endpoint.is_ssl() ? 's' : ''}://${this.endpoint.hostname}:${this.endpoint.port}/rpc`;
      const response = await fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        credentials: 'include',
        body: buf.array_buffer
      }
);

      if (!response.ok) throw new NPRPC.Exception(`HTTP error: ${response.status}`);
      const response_data = await response.arrayBuffer();
      buf.set_buffer(response_data);

      let std_reply = NPRPC.handle_standart_reply(buf);
      if (std_reply != -1) throw new NPRPC.Exception("Unexpected reply");
      const out = unmarshal_live_blog_M4(buf, 16);
      (globalThis as any).__nprpc_debug?.call_end(__dbg_id,{status:'success',duration_ms:Date.now()-__dbg_t0,response_bytes:buf.size,response_args:out});
      return out._1;
    },
    ListComments: async (post_id: /*in*/bigint, page: /*in*/number, page_size: /*in*/number): Promise<Array<Comment>> => {
      const buf = NPRPC.FlatBuffer.create();
      buf.prepare(48);
      buf.commit(48);
      buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
      buf.write_msg_type(NPRPC.impl.MessageType.Request);
      buf.dv.setUint16(16 + 0, this.data.poa_idx, true);
      buf.dv.setUint8(16 + 2, 0);
      buf.dv.setUint8(16 + 3, 2);
      buf.dv.setBigUint64(16 + 8, this.data.object_id, true);
      marshal_live_blog_M5(buf, 32, {_1: post_id, _2: page, _3: page_size});
      buf.write_len(buf.size - 4);

      const __dbg_t0 = Date.now();
      const __dbg_id = (globalThis as any).__nprpc_debug?.call_start({direction:'client',class_id:_IBlogService_Servant._get_class(),poa_idx:this.data.poa_idx,object_id:String(this.data.object_id),interface_idx:0,func_idx:2,method_name:'ListComments',endpoint:{hostname:this.endpoint.hostname,port:this.endpoint.port,transport:'http'},request_args:{post_id:post_id,page:page,page_size:page_size},request_bytes:buf.size});

      const url = `http${this.endpoint.is_ssl() ? 's' : ''}://${this.endpoint.hostname}:${this.endpoint.port}/rpc`;
      const response = await fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        credentials: 'include',
        body: buf.array_buffer
      }
);

      if (!response.ok) throw new NPRPC.Exception(`HTTP error: ${response.status}`);
      const response_data = await response.arrayBuffer();
      buf.set_buffer(response_data);

      let std_reply = NPRPC.handle_standart_reply(buf);
      if (std_reply != -1) throw new NPRPC.Exception("Unexpected reply");
      const out = unmarshal_live_blog_M6(buf, 16);
      (globalThis as any).__nprpc_debug?.call_end(__dbg_id,{status:'success',duration_ms:Date.now()-__dbg_t0,response_bytes:buf.size,response_args:out});
      return out._1;
    },
    ListAuthorPosts: async (author_slug: /*in*/string, page: /*in*/number, page_size: /*in*/number): Promise<Array<PostPreview>> => {
      const buf = NPRPC.FlatBuffer.create();
      buf.prepare(176);
      buf.commit(48);
      buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
      buf.write_msg_type(NPRPC.impl.MessageType.Request);
      buf.dv.setUint16(16 + 0, this.data.poa_idx, true);
      buf.dv.setUint8(16 + 2, 0);
      buf.dv.setUint8(16 + 3, 3);
      buf.dv.setBigUint64(16 + 8, this.data.object_id, true);
      marshal_live_blog_M7(buf, 32, {_1: author_slug, _2: page, _3: page_size});
      buf.write_len(buf.size - 4);

      const __dbg_t0 = Date.now();
      const __dbg_id = (globalThis as any).__nprpc_debug?.call_start({direction:'client',class_id:_IBlogService_Servant._get_class(),poa_idx:this.data.poa_idx,object_id:String(this.data.object_id),interface_idx:0,func_idx:3,method_name:'ListAuthorPosts',endpoint:{hostname:this.endpoint.hostname,port:this.endpoint.port,transport:'http'},request_args:{author_slug:author_slug,page:page,page_size:page_size},request_bytes:buf.size});

      const url = `http${this.endpoint.is_ssl() ? 's' : ''}://${this.endpoint.hostname}:${this.endpoint.port}/rpc`;
      const response = await fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        credentials: 'include',
        body: buf.array_buffer
      }
);

      if (!response.ok) throw new NPRPC.Exception(`HTTP error: ${response.status}`);
      const response_data = await response.arrayBuffer();
      buf.set_buffer(response_data);

      let std_reply = NPRPC.handle_standart_reply(buf);
      if (std_reply != -1) throw new NPRPC.Exception("Unexpected reply");
      const out = unmarshal_live_blog_M8(buf, 16);
      (globalThis as any).__nprpc_debug?.call_end(__dbg_id,{status:'success',duration_ms:Date.now()-__dbg_t0,response_bytes:buf.size,response_args:out});
      return out._1;
    },
    GetAuthor: async (author_slug: /*in*/string): Promise<AuthorPreview> => {
      const buf = NPRPC.FlatBuffer.create();
      buf.prepare(168);
      buf.commit(40);
      buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
      buf.write_msg_type(NPRPC.impl.MessageType.Request);
      buf.dv.setUint16(16 + 0, this.data.poa_idx, true);
      buf.dv.setUint8(16 + 2, 0);
      buf.dv.setUint8(16 + 3, 4);
      buf.dv.setBigUint64(16 + 8, this.data.object_id, true);
      marshal_live_blog_M3(buf, 32, {_1: author_slug});
      buf.write_len(buf.size - 4);

      const __dbg_t0 = Date.now();
      const __dbg_id = (globalThis as any).__nprpc_debug?.call_start({direction:'client',class_id:_IBlogService_Servant._get_class(),poa_idx:this.data.poa_idx,object_id:String(this.data.object_id),interface_idx:0,func_idx:4,method_name:'GetAuthor',endpoint:{hostname:this.endpoint.hostname,port:this.endpoint.port,transport:'http'},request_args:{author_slug:author_slug},request_bytes:buf.size});

      const url = `http${this.endpoint.is_ssl() ? 's' : ''}://${this.endpoint.hostname}:${this.endpoint.port}/rpc`;
      const response = await fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        credentials: 'include',
        body: buf.array_buffer
      }
);

      if (!response.ok) throw new NPRPC.Exception(`HTTP error: ${response.status}`);
      const response_data = await response.arrayBuffer();
      buf.set_buffer(response_data);

      let std_reply = NPRPC.handle_standart_reply(buf);
      if (std_reply != -1) throw new NPRPC.Exception("Unexpected reply");
      const out = unmarshal_live_blog_M9(buf, 16);
      (globalThis as any).__nprpc_debug?.call_end(__dbg_id,{status:'success',duration_ms:Date.now()-__dbg_t0,response_bytes:buf.size,response_args:out});
      return out._1;
    }
  };
}
export interface IBlogService_Servant
{
  ListPosts(page: /*in*/number, page_size: /*in*/number): PostPage;
  GetPost(slug: /*in*/string): PostDetail;
  ListComments(post_id: /*in*/bigint, page: /*in*/number, page_size: /*in*/number): Array<Comment>;
  ListAuthorPosts(author_slug: /*in*/string, page: /*in*/number, page_size: /*in*/number): Array<PostPreview>;
  GetAuthor(author_slug: /*in*/string): AuthorPreview;
}
export class _IBlogService_Servant extends NPRPC.ObjectServant {
  public static _get_class(): string { return "live_blog/live_blog.BlogService"; }
  public readonly get_class = () => { return _IBlogService_Servant._get_class(); }
  public readonly dispatch = (buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean) => {
    _IBlogService_Servant._dispatch(this, buf, remote_endpoint, from_parent);
  }
  public readonly dispatch_stream = (buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean) => {
    _IBlogService_Servant._dispatch_stream(this, buf, remote_endpoint, from_parent);
  }
  static _dispatch(obj: _IBlogService_Servant, buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean): void {
    // Read CallHeader directly
    const function_idx = buf.dv.getUint8(16 + 3);
    switch(function_idx) {
      case 0: {
        const ia = unmarshal_live_blog_M1(buf, 32);
        const obuf = buf;
        obuf.consume(obuf.size);
        obuf.prepare(164);
        obuf.commit(36);
        let __ret_val: PostPage = {} as PostPage;
        const __dbg_t0 = Date.now();
        const __dbg_id = (globalThis as any).__nprpc_debug?.call_start({direction:'server',class_id:_IBlogService_Servant._get_class(),poa_idx:obj.poa.index,object_id:String(obj.oid),interface_idx:0,func_idx:0,method_name:'ListPosts',endpoint:{hostname:remote_endpoint.hostname,port:remote_endpoint.port,transport:NPRPC.EndPoint.to_string(remote_endpoint.type).replace('://','') as any},request_args:ia});
        __ret_val = (obj as any).ListPosts(ia._1, ia._2);
        (globalThis as any).__nprpc_debug?.call_end(__dbg_id,{status:'success',duration_ms:Date.now()-__dbg_t0});
        const out_data = {_1: __ret_val};
        marshal_live_blog_M2(obuf, 16, out_data);
        obuf.write_len(obuf.size - 4);
        obuf.write_msg_id(NPRPC.impl.MessageId.BlockResponse);
        obuf.write_msg_type(NPRPC.impl.MessageType.Answer);
        break;
      }
      case 1: {
        const ia = unmarshal_live_blog_M3(buf, 32);
        const obuf = buf;
        obuf.consume(obuf.size);
        obuf.prepare(240);
        obuf.commit(112);
        let __ret_val: PostDetail = {} as PostDetail;
        const __dbg_t0 = Date.now();
        const __dbg_id = (globalThis as any).__nprpc_debug?.call_start({direction:'server',class_id:_IBlogService_Servant._get_class(),poa_idx:obj.poa.index,object_id:String(obj.oid),interface_idx:0,func_idx:1,method_name:'GetPost',endpoint:{hostname:remote_endpoint.hostname,port:remote_endpoint.port,transport:NPRPC.EndPoint.to_string(remote_endpoint.type).replace('://','') as any},request_args:ia});
        __ret_val = (obj as any).GetPost(ia._1);
        (globalThis as any).__nprpc_debug?.call_end(__dbg_id,{status:'success',duration_ms:Date.now()-__dbg_t0});
        const out_data = {_1: __ret_val};
        marshal_live_blog_M4(obuf, 16, out_data);
        obuf.write_len(obuf.size - 4);
        obuf.write_msg_id(NPRPC.impl.MessageId.BlockResponse);
        obuf.write_msg_type(NPRPC.impl.MessageType.Answer);
        break;
      }
      case 2: {
        const ia = unmarshal_live_blog_M5(buf, 32);
        const obuf = buf;
        obuf.consume(obuf.size);
        obuf.prepare(152);
        obuf.commit(24);
        let __ret_val: Array<Comment> = [];
        const __dbg_t0 = Date.now();
        const __dbg_id = (globalThis as any).__nprpc_debug?.call_start({direction:'server',class_id:_IBlogService_Servant._get_class(),poa_idx:obj.poa.index,object_id:String(obj.oid),interface_idx:0,func_idx:2,method_name:'ListComments',endpoint:{hostname:remote_endpoint.hostname,port:remote_endpoint.port,transport:NPRPC.EndPoint.to_string(remote_endpoint.type).replace('://','') as any},request_args:ia});
        __ret_val = (obj as any).ListComments(ia._1, ia._2, ia._3);
        (globalThis as any).__nprpc_debug?.call_end(__dbg_id,{status:'success',duration_ms:Date.now()-__dbg_t0});
        const out_data = {_1: __ret_val};
        marshal_live_blog_M6(obuf, 16, out_data);
        obuf.write_len(obuf.size - 4);
        obuf.write_msg_id(NPRPC.impl.MessageId.BlockResponse);
        obuf.write_msg_type(NPRPC.impl.MessageType.Answer);
        break;
      }
      case 3: {
        const ia = unmarshal_live_blog_M7(buf, 32);
        const obuf = buf;
        obuf.consume(obuf.size);
        obuf.prepare(152);
        obuf.commit(24);
        let __ret_val: Array<PostPreview> = [];
        const __dbg_t0 = Date.now();
        const __dbg_id = (globalThis as any).__nprpc_debug?.call_start({direction:'server',class_id:_IBlogService_Servant._get_class(),poa_idx:obj.poa.index,object_id:String(obj.oid),interface_idx:0,func_idx:3,method_name:'ListAuthorPosts',endpoint:{hostname:remote_endpoint.hostname,port:remote_endpoint.port,transport:NPRPC.EndPoint.to_string(remote_endpoint.type).replace('://','') as any},request_args:ia});
        __ret_val = (obj as any).ListAuthorPosts(ia._1, ia._2, ia._3);
        (globalThis as any).__nprpc_debug?.call_end(__dbg_id,{status:'success',duration_ms:Date.now()-__dbg_t0});
        const out_data = {_1: __ret_val};
        marshal_live_blog_M8(obuf, 16, out_data);
        obuf.write_len(obuf.size - 4);
        obuf.write_msg_id(NPRPC.impl.MessageId.BlockResponse);
        obuf.write_msg_type(NPRPC.impl.MessageType.Answer);
        break;
      }
      case 4: {
        const ia = unmarshal_live_blog_M3(buf, 32);
        const obuf = buf;
        obuf.consume(obuf.size);
        obuf.prepare(184);
        obuf.commit(56);
        let __ret_val: AuthorPreview = {} as AuthorPreview;
        const __dbg_t0 = Date.now();
        const __dbg_id = (globalThis as any).__nprpc_debug?.call_start({direction:'server',class_id:_IBlogService_Servant._get_class(),poa_idx:obj.poa.index,object_id:String(obj.oid),interface_idx:0,func_idx:4,method_name:'GetAuthor',endpoint:{hostname:remote_endpoint.hostname,port:remote_endpoint.port,transport:NPRPC.EndPoint.to_string(remote_endpoint.type).replace('://','') as any},request_args:ia});
        __ret_val = (obj as any).GetAuthor(ia._1);
        (globalThis as any).__nprpc_debug?.call_end(__dbg_id,{status:'success',duration_ms:Date.now()-__dbg_t0});
        const out_data = {_1: __ret_val};
        marshal_live_blog_M9(obuf, 16, out_data);
        obuf.write_len(obuf.size - 4);
        obuf.write_msg_id(NPRPC.impl.MessageId.BlockResponse);
        obuf.write_msg_type(NPRPC.impl.MessageType.Answer);
        break;
      }
      default:
        NPRPC.make_simple_answer(buf, NPRPC.impl.MessageId.Error_UnknownFunctionIdx);
    }
  }
  static _dispatch_stream(obj: _IBlogService_Servant, buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean): void {
    const init = NPRPC.impl.unmarshal_StreamInit(buf, 16);
    const function_idx = init.func_idx;
    const conn = NPRPC.rpc.get_connection(remote_endpoint);
    switch(function_idx) {
      default:
        NPRPC.make_simple_answer(buf, NPRPC.impl.MessageId.Error_UnknownFunctionIdx);
    }
  }
}

export class ChatService extends NPRPC.ObjectProxy {
  public static get servant_t(): new() => _IChatService_Servant {
    return _IChatService_Servant;
  }


  public async JoinPostChat(post_id: /*in*/bigint, user_name: /*in*/string): Promise<NPRPC.BidiStream<ChatEnvelope, ChatServerEvent>> {
    const interface_idx = (arguments.length == 2 ? 0 : arguments[arguments.length - 1]);
    const conn = NPRPC.rpc.get_connection(this.endpoint);
    const stream_id = conn.stream_manager.generate_stream_id();
    const buf = NPRPC.FlatBuffer.create();
    buf.prepare(192);
    buf.commit(64);
    buf.write_msg_id(NPRPC.impl.MessageId.StreamInitialization);
    buf.write_msg_type(NPRPC.impl.MessageType.Request);
    NPRPC.impl.marshal_StreamInit(buf, 16, {
      stream_id,
      poa_idx: this.data.poa_idx,
      interface_idx,
      object_id: this.data.object_id,
      func_idx: 0,
      stream_kind: NPRPC.impl.StreamKind.Bidi
    });
    marshal_live_blog_M10(buf, 48, {_1: post_id, _2: user_name});
    buf.write_len(buf.size - 4);
    (globalThis as any).__nprpc_debug?.stream_start({direction:'client',class_id:_IChatService_Servant._get_class(),poa_idx:this.data.poa_idx,object_id:String(this.data.object_id),interface_idx,func_idx:0,method_name:'JoinPostChat',endpoint:{hostname:this.endpoint.hostname,port:this.endpoint.port,transport:NPRPC.EndPoint.to_string(this.endpoint.type).replace('://','') as any},stream_id:String(stream_id),stream_kind:'bidi',request_args:{post_id:post_id,user_name:user_name},request_bytes:buf.size});
    return await NPRPC.rpc.open_bidi_stream(this.endpoint, buf, stream_id, this.timeout, ((value: ChatEnvelope) => { const buf = NPRPC.FlatBuffer.create(152); buf.commit(24); marshal_ChatEnvelope(buf, 0, value); return new Uint8Array(buf.array_buffer, 0, buf.size); }), ((data: Uint8Array) => unmarshal_ChatServerEvent(NPRPC.FlatBuffer.from_array_buffer(data.slice().buffer), 0)));
  }

  // HTTP Transport (alternative to WebSocket)
  public readonly http = {

  };
}
export interface IChatService_Servant
{
  JoinPostChat(post_id: /*in*/bigint, user_name: /*in*/string, stream: NPRPC.BidiStream<ChatServerEvent, ChatEnvelope>): void | Promise<void>;
}
export class _IChatService_Servant extends NPRPC.ObjectServant {
  public static _get_class(): string { return "live_blog/live_blog.ChatService"; }
  public readonly get_class = () => { return _IChatService_Servant._get_class(); }
  public readonly dispatch = (buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean) => {
    _IChatService_Servant._dispatch(this, buf, remote_endpoint, from_parent);
  }
  public readonly dispatch_stream = (buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean) => {
    _IChatService_Servant._dispatch_stream(this, buf, remote_endpoint, from_parent);
  }
  static _dispatch(obj: _IChatService_Servant, buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean): void {
    // Read CallHeader directly
    const function_idx = buf.dv.getUint8(16 + 3);
    switch(function_idx) {
      default:
        NPRPC.make_simple_answer(buf, NPRPC.impl.MessageId.Error_UnknownFunctionIdx);
    }
  }
  static _dispatch_stream(obj: _IChatService_Servant, buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean): void {
    const init = NPRPC.impl.unmarshal_StreamInit(buf, 16);
    const function_idx = init.func_idx;
    const conn = NPRPC.rpc.get_connection(remote_endpoint);
    switch(function_idx) {
      case 0: {
        const ia = unmarshal_live_blog_M10(buf, 48);
        (globalThis as any).__nprpc_debug?.stream_start({direction:'server',class_id:_IChatService_Servant._get_class(),poa_idx:obj.poa.index,object_id:String(obj.oid),interface_idx:init.interface_idx,func_idx:0,method_name:'JoinPostChat',endpoint:{hostname:remote_endpoint.hostname,port:remote_endpoint.port,transport:NPRPC.EndPoint.to_string(remote_endpoint.type).replace('://','') as any},stream_id:String(init.stream_id),stream_kind:'bidi',request_args:ia,request_bytes:buf.size});
        const stream = conn.stream_manager.create_bidi_stream(init.stream_id, ((value: ChatServerEvent) => { const buf = NPRPC.FlatBuffer.create(164); buf.commit(36); marshal_ChatServerEvent(buf, 0, value); return new Uint8Array(buf.array_buffer, 0, buf.size); }), ((data: Uint8Array) => unmarshal_ChatEnvelope(NPRPC.FlatBuffer.from_array_buffer(data.slice().buffer), 0)));
        NPRPC.make_simple_answer(buf, NPRPC.impl.MessageId.Success);
        void (async () => {
          try {
            await (obj as any).JoinPostChat(ia._1, ia._2, stream);
          } catch (e) {
            stream.writer.abort();
            stream.reader.cancel();
            console.error('Stream handler failed', e);
          }
          })();
          return;
        }
        default:
          NPRPC.make_simple_answer(buf, NPRPC.impl.MessageId.Error_UnknownFunctionIdx);
      }
    }
  }

  export class MediaService extends NPRPC.ObjectProxy {
    public static get servant_t(): new() => _IMediaService_Servant {
      return _IMediaService_Servant;
    }


    public async OpenPostVideo(post_id: /*in*/bigint): Promise<NPRPC.StreamReader<binary>> {
      const interface_idx = (arguments.length == 1 ? 0 : arguments[arguments.length - 1]);
      const conn = NPRPC.rpc.get_connection(this.endpoint);
      const stream_id = conn.stream_manager.generate_stream_id();
      const buf = NPRPC.FlatBuffer.create();
      buf.prepare(56);
      buf.commit(56);
      buf.write_msg_id(NPRPC.impl.MessageId.StreamInitialization);
      buf.write_msg_type(NPRPC.impl.MessageType.Request);
      NPRPC.impl.marshal_StreamInit(buf, 16, {
        stream_id,
        poa_idx: this.data.poa_idx,
        interface_idx,
        object_id: this.data.object_id,
        func_idx: 0,
        stream_kind: NPRPC.impl.StreamKind.Server
      });
      marshal_live_blog_M11(buf, 48, {_1: post_id});
      buf.write_len(buf.size - 4);
      (globalThis as any).__nprpc_debug?.stream_start({direction:'client',class_id:_IMediaService_Servant._get_class(),poa_idx:this.data.poa_idx,object_id:String(this.data.object_id),interface_idx,func_idx:0,method_name:'OpenPostVideo',endpoint:{hostname:this.endpoint.hostname,port:this.endpoint.port,transport:NPRPC.EndPoint.to_string(this.endpoint.type).replace('://','') as any},stream_id:String(stream_id),stream_kind:'server',request_args:{post_id:post_id},request_bytes:buf.size});
      return await NPRPC.rpc.open_server_stream(this.endpoint, buf, stream_id, this.timeout, ((data: Uint8Array) => NPRPC.unmarshal_typed_array(NPRPC.FlatBuffer.from_array_buffer(data.slice().buffer), 0, Uint8Array) as Uint8Array));
    }
    public async GetVideoDashManifest(post_id: /*in*/bigint): Promise<string> {
      let interface_idx = (arguments.length == 1 ? 0 : arguments[arguments.length - 1]);
      const buf = NPRPC.FlatBuffer.create();
      buf.prepare(40);
      buf.commit(40);
      buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
      buf.write_msg_type(NPRPC.impl.MessageType.Request);
      // Write CallHeader directly
      buf.dv.setUint16(16 + 0, this.data.poa_idx, true);
      buf.dv.setUint8(16 + 2, interface_idx);
      buf.dv.setUint8(16 + 3, 1);
      buf.dv.setBigUint64(16 + 8, this.data.object_id, true);
      marshal_live_blog_M11(buf, 32, {_1: post_id});
      buf.write_len(buf.size - 4);
      const __dbg_t0 = Date.now();
      const __dbg_id = (globalThis as any).__nprpc_debug?.call_start({direction:'client',class_id:_IMediaService_Servant._get_class(),poa_idx:this.data.poa_idx,object_id:String(this.data.object_id),interface_idx,func_idx:1,method_name:'GetVideoDashManifest',endpoint:{hostname:this.endpoint.hostname,port:this.endpoint.port,transport:NPRPC.EndPoint.to_string(this.endpoint.type).replace('://','') as any},request_args:{post_id:post_id},request_bytes:buf.size});
      await NPRPC.rpc.call(this.endpoint, buf, this.timeout);
      let std_reply = NPRPC.handle_standart_reply(buf);
      if (std_reply != -1) {
        console.log("received an unusual reply for function with output arguments");
        throw new NPRPC.Exception("Unknown Error");
      }
      const out = unmarshal_live_blog_M3(buf, 16);
      (globalThis as any).__nprpc_debug?.call_end(__dbg_id,{status:'success',duration_ms:Date.now()-__dbg_t0,response_bytes:buf.size,response_args:out});
      return out._1;
    }
    public async GetVideoDashSegmentRange(post_id: /*in*/bigint, byte_offset: /*in*/bigint, byte_length: /*in*/bigint): Promise<NPRPC.StreamReader<binary>> {
      const interface_idx = (arguments.length == 3 ? 0 : arguments[arguments.length - 1]);
      const conn = NPRPC.rpc.get_connection(this.endpoint);
      const stream_id = conn.stream_manager.generate_stream_id();
      const buf = NPRPC.FlatBuffer.create();
      buf.prepare(72);
      buf.commit(72);
      buf.write_msg_id(NPRPC.impl.MessageId.StreamInitialization);
      buf.write_msg_type(NPRPC.impl.MessageType.Request);
      NPRPC.impl.marshal_StreamInit(buf, 16, {
        stream_id,
        poa_idx: this.data.poa_idx,
        interface_idx,
        object_id: this.data.object_id,
        func_idx: 2,
        stream_kind: NPRPC.impl.StreamKind.Server
      });
      marshal_live_blog_M12(buf, 48, {_1: post_id, _2: byte_offset, _3: byte_length});
      buf.write_len(buf.size - 4);
      (globalThis as any).__nprpc_debug?.stream_start({direction:'client',class_id:_IMediaService_Servant._get_class(),poa_idx:this.data.poa_idx,object_id:String(this.data.object_id),interface_idx,func_idx:2,method_name:'GetVideoDashSegmentRange',endpoint:{hostname:this.endpoint.hostname,port:this.endpoint.port,transport:NPRPC.EndPoint.to_string(this.endpoint.type).replace('://','') as any},stream_id:String(stream_id),stream_kind:'server',request_args:{post_id:post_id,byte_offset:byte_offset,byte_length:byte_length},request_bytes:buf.size});
      return await NPRPC.rpc.open_server_stream(this.endpoint, buf, stream_id, this.timeout, ((data: Uint8Array) => NPRPC.unmarshal_typed_array(NPRPC.FlatBuffer.from_array_buffer(data.slice().buffer), 0, Uint8Array) as Uint8Array));
    }

    // HTTP Transport (alternative to WebSocket)
    public readonly http = {
      GetVideoDashManifest: async (post_id: /*in*/bigint): Promise<string> => {
        const buf = NPRPC.FlatBuffer.create();
        buf.prepare(40);
        buf.commit(40);
        buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);
        buf.write_msg_type(NPRPC.impl.MessageType.Request);
        buf.dv.setUint16(16 + 0, this.data.poa_idx, true);
        buf.dv.setUint8(16 + 2, 0);
        buf.dv.setUint8(16 + 3, 1);
        buf.dv.setBigUint64(16 + 8, this.data.object_id, true);
        marshal_live_blog_M11(buf, 32, {_1: post_id});
        buf.write_len(buf.size - 4);

        const __dbg_t0 = Date.now();
        const __dbg_id = (globalThis as any).__nprpc_debug?.call_start({direction:'client',class_id:_IMediaService_Servant._get_class(),poa_idx:this.data.poa_idx,object_id:String(this.data.object_id),interface_idx:0,func_idx:1,method_name:'GetVideoDashManifest',endpoint:{hostname:this.endpoint.hostname,port:this.endpoint.port,transport:'http'},request_args:{post_id:post_id},request_bytes:buf.size});

        const url = `http${this.endpoint.is_ssl() ? 's' : ''}://${this.endpoint.hostname}:${this.endpoint.port}/rpc`;
        const response = await fetch(url, {
          method: 'POST',
          headers: { 'Content-Type': 'application/octet-stream' },
          credentials: 'include',
          body: buf.array_buffer
        }
);

        if (!response.ok) throw new NPRPC.Exception(`HTTP error: ${response.status}`);
        const response_data = await response.arrayBuffer();
        buf.set_buffer(response_data);

        let std_reply = NPRPC.handle_standart_reply(buf);
        if (std_reply != -1) throw new NPRPC.Exception("Unexpected reply");
        const out = unmarshal_live_blog_M3(buf, 16);
        (globalThis as any).__nprpc_debug?.call_end(__dbg_id,{status:'success',duration_ms:Date.now()-__dbg_t0,response_bytes:buf.size,response_args:out});
        return out._1;
      }
    };
  }
  export interface IMediaService_Servant
  {
    OpenPostVideo(post_id: /*in*/bigint): AsyncIterable<binary> | Iterable<binary> | Promise<AsyncIterable<binary> | Iterable<binary>>;
    GetVideoDashManifest(post_id: /*in*/bigint): string;
    GetVideoDashSegmentRange(post_id: /*in*/bigint, byte_offset: /*in*/bigint, byte_length: /*in*/bigint): AsyncIterable<binary> | Iterable<binary> | Promise<AsyncIterable<binary> | Iterable<binary>>;
  }
  export class _IMediaService_Servant extends NPRPC.ObjectServant {
    public static _get_class(): string { return "live_blog/live_blog.MediaService"; }
    public readonly get_class = () => { return _IMediaService_Servant._get_class(); }
    public readonly dispatch = (buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean) => {
      _IMediaService_Servant._dispatch(this, buf, remote_endpoint, from_parent);
    }
    public readonly dispatch_stream = (buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean) => {
      _IMediaService_Servant._dispatch_stream(this, buf, remote_endpoint, from_parent);
    }
    static _dispatch(obj: _IMediaService_Servant, buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean): void {
      // Read CallHeader directly
      const function_idx = buf.dv.getUint8(16 + 3);
      switch(function_idx) {
        case 1: {
          const ia = unmarshal_live_blog_M11(buf, 32);
          const obuf = buf;
          obuf.consume(obuf.size);
          obuf.prepare(152);
          obuf.commit(24);
          let __ret_val: string = '';
          const __dbg_t0 = Date.now();
          const __dbg_id = (globalThis as any).__nprpc_debug?.call_start({direction:'server',class_id:_IMediaService_Servant._get_class(),poa_idx:obj.poa.index,object_id:String(obj.oid),interface_idx:0,func_idx:1,method_name:'GetVideoDashManifest',endpoint:{hostname:remote_endpoint.hostname,port:remote_endpoint.port,transport:NPRPC.EndPoint.to_string(remote_endpoint.type).replace('://','') as any},request_args:ia});
          __ret_val = (obj as any).GetVideoDashManifest(ia._1);
          (globalThis as any).__nprpc_debug?.call_end(__dbg_id,{status:'success',duration_ms:Date.now()-__dbg_t0});
          const out_data = {_1: __ret_val};
          marshal_live_blog_M3(obuf, 16, out_data);
          obuf.write_len(obuf.size - 4);
          obuf.write_msg_id(NPRPC.impl.MessageId.BlockResponse);
          obuf.write_msg_type(NPRPC.impl.MessageType.Answer);
          break;
        }
        default:
          NPRPC.make_simple_answer(buf, NPRPC.impl.MessageId.Error_UnknownFunctionIdx);
      }
    }
    static _dispatch_stream(obj: _IMediaService_Servant, buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean): void {
      const init = NPRPC.impl.unmarshal_StreamInit(buf, 16);
      const function_idx = init.func_idx;
      const conn = NPRPC.rpc.get_connection(remote_endpoint);
      switch(function_idx) {
        case 0: {
          const ia = unmarshal_live_blog_M11(buf, 48);
          (globalThis as any).__nprpc_debug?.stream_start({direction:'server',class_id:_IMediaService_Servant._get_class(),poa_idx:obj.poa.index,object_id:String(obj.oid),interface_idx:init.interface_idx,func_idx:0,method_name:'OpenPostVideo',endpoint:{hostname:remote_endpoint.hostname,port:remote_endpoint.port,transport:NPRPC.EndPoint.to_string(remote_endpoint.type).replace('://','') as any},stream_id:String(init.stream_id),stream_kind:'server',request_args:ia,request_bytes:buf.size});
          const writer = conn.stream_manager.create_writer(init.stream_id, ((value: Uint8Array) => { const buf = NPRPC.FlatBuffer.create(8 + value.byteLength); buf.commit(8); NPRPC.marshal_typed_array(buf, 0, value, 1, 1); return new Uint8Array(buf.array_buffer, 0, buf.size); }));
          NPRPC.make_simple_answer(buf, NPRPC.impl.MessageId.Success);
          void (async () => {
            try {
              const source = await (obj as any).OpenPostVideo(ia._1);
              for await (const chunk of source as any) {
                writer.write(chunk);
              }
              writer.close();
            } catch (e) {
              writer.abort();
              console.error('Stream handler failed', e);
            }
            })();
            return;
          }
          case 2: {
            const ia = unmarshal_live_blog_M12(buf, 48);
            (globalThis as any).__nprpc_debug?.stream_start({direction:'server',class_id:_IMediaService_Servant._get_class(),poa_idx:obj.poa.index,object_id:String(obj.oid),interface_idx:init.interface_idx,func_idx:2,method_name:'GetVideoDashSegmentRange',endpoint:{hostname:remote_endpoint.hostname,port:remote_endpoint.port,transport:NPRPC.EndPoint.to_string(remote_endpoint.type).replace('://','') as any},stream_id:String(init.stream_id),stream_kind:'server',request_args:ia,request_bytes:buf.size});
            const writer = conn.stream_manager.create_writer(init.stream_id, ((value: Uint8Array) => { const buf = NPRPC.FlatBuffer.create(8 + value.byteLength); buf.commit(8); NPRPC.marshal_typed_array(buf, 0, value, 1, 1); return new Uint8Array(buf.array_buffer, 0, buf.size); }));
            NPRPC.make_simple_answer(buf, NPRPC.impl.MessageId.Success);
            void (async () => {
              try {
                const source = await (obj as any).GetVideoDashSegmentRange(ia._1, ia._2, ia._3);
                for await (const chunk of source as any) {
                  writer.write(chunk);
                }
                writer.close();
              } catch (e) {
                writer.abort();
                console.error('Stream handler failed', e);
              }
              })();
              return;
            }
            default:
              NPRPC.make_simple_answer(buf, NPRPC.impl.MessageId.Error_UnknownFunctionIdx);
          }
        }
      }

export interface live_blog_M1 {
  _1: number/*u32*/;
  _2: number/*u32*/;
    }

    export function marshal_live_blog_M1(buf: NPRPC.FlatBuffer, offset: number, data: live_blog_M1): void {
    buf.dv.setUint32(offset + 0, data._1, true);
    buf.dv.setUint32(offset + 4, data._2, true);
  }

  export function unmarshal_live_blog_M1(buf: NPRPC.FlatBuffer, offset: number): live_blog_M1 {
  const result = {} as live_blog_M1;
  result._1 = buf.dv.getUint32(offset + 0, true);
  result._2 = buf.dv.getUint32(offset + 4, true);
  return result;
}

export interface live_blog_M2 {
  _1: PostPage;
}

export function marshal_live_blog_M2(buf: NPRPC.FlatBuffer, offset: number, data: live_blog_M2): void {
marshal_PostPage(buf, offset + 0, data._1);
}

export function unmarshal_live_blog_M2(buf: NPRPC.FlatBuffer, offset: number): live_blog_M2 {
const result = {} as live_blog_M2;
result._1 = unmarshal_PostPage(buf, offset + 0);
return result;
}

export interface live_blog_M3 {
  _1: string;
}

export function marshal_live_blog_M3(buf: NPRPC.FlatBuffer, offset: number, data: live_blog_M3): void {
NPRPC.marshal_string(buf, offset + 0, data._1);
}

export function unmarshal_live_blog_M3(buf: NPRPC.FlatBuffer, offset: number): live_blog_M3 {
const result = {} as live_blog_M3;
result._1 = NPRPC.unmarshal_string(buf, offset + 0);
return result;
}

export interface live_blog_M4 {
  _1: PostDetail;
}

export function marshal_live_blog_M4(buf: NPRPC.FlatBuffer, offset: number, data: live_blog_M4): void {
marshal_PostDetail(buf, offset + 0, data._1);
}

export function unmarshal_live_blog_M4(buf: NPRPC.FlatBuffer, offset: number): live_blog_M4 {
const result = {} as live_blog_M4;
result._1 = unmarshal_PostDetail(buf, offset + 0);
return result;
}

export interface live_blog_M5 {
  _1: bigint/*u64*/;
  _2: number/*u32*/;
  _3: number/*u32*/;
}

export function marshal_live_blog_M5(buf: NPRPC.FlatBuffer, offset: number, data: live_blog_M5): void {
buf.dv.setBigUint64(offset + 0, data._1, true);
buf.dv.setUint32(offset + 8, data._2, true);
buf.dv.setUint32(offset + 12, data._3, true);
}

export function unmarshal_live_blog_M5(buf: NPRPC.FlatBuffer, offset: number): live_blog_M5 {
const result = {} as live_blog_M5;
result._1 = buf.dv.getBigUint64(offset + 0, true);
result._2 = buf.dv.getUint32(offset + 8, true);
result._3 = buf.dv.getUint32(offset + 12, true);
return result;
}

export interface live_blog_M6 {
  _1: Array<Comment>;
}

export function marshal_live_blog_M6(buf: NPRPC.FlatBuffer, offset: number, data: live_blog_M6): void {
NPRPC.marshal_struct_array(buf, offset + 0, data._1, marshal_Comment, 32, 8);
}

export function unmarshal_live_blog_M6(buf: NPRPC.FlatBuffer, offset: number): live_blog_M6 {
const result = {} as live_blog_M6;
result._1 = NPRPC.unmarshal_struct_array(buf, offset + 0, unmarshal_Comment, 32);
return result;
}

export interface live_blog_M7 {
  _1: string;
  _2: number/*u32*/;
  _3: number/*u32*/;
}

export function marshal_live_blog_M7(buf: NPRPC.FlatBuffer, offset: number, data: live_blog_M7): void {
NPRPC.marshal_string(buf, offset + 0, data._1);
buf.dv.setUint32(offset + 8, data._2, true);
buf.dv.setUint32(offset + 12, data._3, true);
}

export function unmarshal_live_blog_M7(buf: NPRPC.FlatBuffer, offset: number): live_blog_M7 {
const result = {} as live_blog_M7;
result._1 = NPRPC.unmarshal_string(buf, offset + 0);
result._2 = buf.dv.getUint32(offset + 8, true);
result._3 = buf.dv.getUint32(offset + 12, true);
return result;
}

export interface live_blog_M8 {
  _1: Array<PostPreview>;
}

export function marshal_live_blog_M8(buf: NPRPC.FlatBuffer, offset: number, data: live_blog_M8): void {
NPRPC.marshal_struct_array(buf, offset + 0, data._1, marshal_PostPreview, 88, 8);
}

export function unmarshal_live_blog_M8(buf: NPRPC.FlatBuffer, offset: number): live_blog_M8 {
const result = {} as live_blog_M8;
result._1 = NPRPC.unmarshal_struct_array(buf, offset + 0, unmarshal_PostPreview, 88);
return result;
}

export interface live_blog_M9 {
  _1: AuthorPreview;
}

export function marshal_live_blog_M9(buf: NPRPC.FlatBuffer, offset: number, data: live_blog_M9): void {
marshal_AuthorPreview(buf, offset + 0, data._1);
}

export function unmarshal_live_blog_M9(buf: NPRPC.FlatBuffer, offset: number): live_blog_M9 {
const result = {} as live_blog_M9;
result._1 = unmarshal_AuthorPreview(buf, offset + 0);
return result;
}

export interface live_blog_M10 {
  _1: bigint/*u64*/;
  _2: string;
}

export function marshal_live_blog_M10(buf: NPRPC.FlatBuffer, offset: number, data: live_blog_M10): void {
buf.dv.setBigUint64(offset + 0, data._1, true);
NPRPC.marshal_string(buf, offset + 8, data._2);
}

export function unmarshal_live_blog_M10(buf: NPRPC.FlatBuffer, offset: number): live_blog_M10 {
const result = {} as live_blog_M10;
result._1 = buf.dv.getBigUint64(offset + 0, true);
result._2 = NPRPC.unmarshal_string(buf, offset + 8);
return result;
}

export interface live_blog_M11 {
  _1: bigint/*u64*/;
}

export function marshal_live_blog_M11(buf: NPRPC.FlatBuffer, offset: number, data: live_blog_M11): void {
buf.dv.setBigUint64(offset + 0, data._1, true);
}

export function unmarshal_live_blog_M11(buf: NPRPC.FlatBuffer, offset: number): live_blog_M11 {
const result = {} as live_blog_M11;
result._1 = buf.dv.getBigUint64(offset + 0, true);
return result;
}

export interface live_blog_M12 {
  _1: bigint/*u64*/;
  _2: bigint/*u64*/;
  _3: bigint/*u64*/;
}

export function marshal_live_blog_M12(buf: NPRPC.FlatBuffer, offset: number, data: live_blog_M12): void {
buf.dv.setBigUint64(offset + 0, data._1, true);
buf.dv.setBigUint64(offset + 8, data._2, true);
buf.dv.setBigUint64(offset + 16, data._3, true);
}

export function unmarshal_live_blog_M12(buf: NPRPC.FlatBuffer, offset: number): live_blog_M12 {
const result = {} as live_blog_M12;
result._1 = buf.dv.getBigUint64(offset + 0, true);
result._2 = buf.dv.getBigUint64(offset + 8, true);
result._3 = buf.dv.getBigUint64(offset + 16, true);
return result;
}

