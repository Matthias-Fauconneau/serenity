#include "window.h"
#include "text.h"
#include "slice.h"
//#include "synthetic.h" // Links Synthetic operator

#if INTERFACE

// Clean abstract interface method
#include "interface.h"
#include "operation.h"
unique<Operation> synthetic = Interface<Operation>::instance("Synthetic"_);

struct Tomography {
    Result result {"source",0,{},{},buffer<byte>(synthetic->outputSize({},{},0))};
    SliceView slice;
    Window window {&slice, int2(-1), "Tomography"_};
    Tomography() {
        synthetic->execute({},{&result},{});
        slice.view(result.metadata,result.name,result.data);
        window.show();
    }
} tomography;

#else

// Direct implementation
Volume8 synthetic(int3 size);

struct Tomography {
    SliceView slice = ::synthetic(128);
    Window window {&slice, int2(-1), "Tomography"_};
    Tomography() {
        window.actions[Escape] = []{ exit(); };
        window.show();
    }
} tomography;

#endif
