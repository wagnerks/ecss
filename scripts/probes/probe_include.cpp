// What a TU pays for the include alone, before it does anything with the library.
//
// This is the number that decides a project's total build time. Per-type cost lands only
// on the TU that names the type, and is linear; this lands on every TU that includes the
// header, and is paid whether or not the TU uses a single component.
#include <ecss/Registry.h>

int probeInclude() { return 0; }
