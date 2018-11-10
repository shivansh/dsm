# Distributed Shared Memory
This implementation models a two process distributed shared memory system as a
proof of concept for a n-process system. Made as a part of the course Distributed
Systems (CS632).


## Build 
For building, execute -
```
make
```

## Testing
For running the tests available in `test.c`, follow the instructions below.

On master machine, execute -

```
./dsm master <num-pages> <master-IP> <master-PORT> <slave-IP> <slave_PORT>
```

On slave machine, execute -

```
./dsm master <num-pages> <master-IP> <master-PORT> <slave-IP> <slave_PORT>
```
