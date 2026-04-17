# lock_free

A collection of lock free utilities and data structures for C++ language.

## Requirements

C++20 standard

## Usage

### CMake

```cmake
cmake_minimum_required(VERSION 3.15)
project(myproject)
add_executable(myexe main.cpp)
set_target_properties(myexe PROPERTIES CXX_STANDARD 20) # required by this library
target_include_directories(myexe PRIVATE lock_free/include)
```

---

```C++
#include <lock_free/....hpp>
// ...
```

## Utilities

### `hazard_ptr`

Hazard pointer (https://en.wikipedia.org/wiki/Hazard_pointer) implementation,
based on: https://www.modernescpp.com/index.php/a-lock-free-stack-a-hazard-pointer-implementation/,
but with actually lock free memory releasing algorithm and extended to many hazard pointers per thread.

## Data structures

### `hazard_queue`

Implementation of Michael and Scott non-blocking queue (https://www.cs.rochester.edu/u/scott/papers/1996_PODC_queues.pdf),
but using hazard pointers to solve the ABA problem.