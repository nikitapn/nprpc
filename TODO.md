

* Optimize shared memory server session to avoid unnecessary copies when receiving messages.
* Generate npnameserver stubs when building nprpc target, to avoid full rebuilds when npnameserver is built separately.