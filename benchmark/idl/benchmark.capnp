@0xf8a0c8e9d7b6a5c4;

interface BenchmarkService {
  emptyCall @0 () -> ();
  callWithReturn @1 () -> (result :Int32);
  smallStringCall @2 (data :Text) -> (result :Text);
}
