@0xf8a0c8e9d7b6a5c4;

struct Person {
  name @0 :Text;
  age @1 :UInt32;
  email @2 :Text;
}

struct Address {
  street @0 :Text;
  city @1 :Text;
  country @2 :Text;
  zipCode @3 :UInt32;
}

struct Employee {
  person @0 :Person;
  address @1 :Address;
  employeeId @2 :UInt64;
  salary @3 :Float64;
  skills @4 :List(Text);
}

interface BenchmarkService {
  emptyCall @0 () -> ();
  callWithReturn @1 () -> (result :Int32);
  smallStringCall @2 (data :Text) -> (result :Text);
  
  # Complex nested data benchmark
  processEmployee @3 (employee :Employee) -> (result :Employee);
  
  # Large data benchmark
  processLargeData @4 (data :Data) -> (result :Data);
}
