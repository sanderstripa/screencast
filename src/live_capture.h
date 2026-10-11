#pragma once
#include <memory>
// Construct only when requested; disabling releases the capture device immediately.
template<class Capture,class Factory> bool setCaptureEnabled(std::unique_ptr<Capture>& capture,bool enabled,Factory create) {
    if(enabled && !capture){capture=create();return true;}
    if(!enabled && capture){capture.reset();return true;}
    return false;
}
