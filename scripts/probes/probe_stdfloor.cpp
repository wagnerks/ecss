// Every standard header ecss includes directly, and no ecss at all.
//
// This is the floor: the part of a TU's include cost that no amount of work on this
// library can remove, because an ECS that stores components in growable buffers and
// publishes them to lock-free readers genuinely needs <vector>, <atomic> and <memory>.
// Subtract this from probe_include to see what ecss itself is responsible for.
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

int probeStdFloor() { return 0; }
