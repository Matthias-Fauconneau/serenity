#include "signal.h"

#include "array.cc"
template struct array< delegate<void> >;
template struct array< signal<> >;
