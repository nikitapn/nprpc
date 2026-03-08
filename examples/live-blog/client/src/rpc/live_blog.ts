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
  tags1111: number/*u8*/;
  tags11: Array<string>;
}

export function marshal_PostDetail(buf: NPRPC.FlatBuffer, offset: number, data: PostDetail): void {
buf.dv.setBigUint64(offset + 0, data.id, true);
NPRPC.marshal_string(buf, offset + 8, data.slug);
NPRPC.marshal_string(buf, offset + 16, data.title);
NPRPC.marshal_string(buf, offset + 24, data.summary);
NPRPC.marshal_string(buf, offset + 32, data.body_html);
NPRPC.marshal_string(buf, offset + 40, data.published_at);
marshal_AuthorPreview(buf, offset + 48, data.author);
buf.dv.setUint8(offset + 88, data.tags1111);
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
result.tags1111 = buf.dv.getUint8(offset + 88);
return result;
}

