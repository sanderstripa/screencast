#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
struct BilinearPoint {
    int x0,y0,x1,y1;float dx,dy;
    BilinearPoint(int width,int height,float x,float y) {
        x=std::clamp(x,0.f,float(width-1));y=std::clamp(y,0.f,float(height-1));
        x0=int(x);y0=int(y);x1=std::min(x0+1,width-1);y1=std::min(y0+1,height-1);dx=x-x0;dy=y-y0;
    }
    template<class Read> float sample(Read read) const {
        float top=read(x0,y0)*(1-dx)+read(x1,y0)*dx;
        float bottom=read(x0,y1)*(1-dx)+read(x1,y1)*dx;
        return top*(1-dy)+bottom*dy;
    }
};
inline std::array<float,4> sampleBgra(uint8_t const* pixels,int width,int height,float x,float y) {
    BilinearPoint point(width,height,x,y);std::array<float,4> value{};
    for(int c=0;c<4;++c)value[c]=point.sample([&](int sx,int sy){return pixels[(static_cast<size_t>(sy)*width+sx)*4+c];});
    return value;
}
