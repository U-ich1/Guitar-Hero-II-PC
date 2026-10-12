#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace gh2::temporal {
using Vector4=std::array<double,4>;
using Matrix4=std::array<Vector4,4>;
inline Vector4 Transform(const Matrix4& matrix,const Vector4& vector) {
    Vector4 result{};
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)result[row]+=matrix[row][col]*vector[col];
    return result;
}
inline Matrix4 Multiply(const Matrix4& a,const Matrix4& b) {
    Matrix4 result{};
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)for(unsigned k=0;k<4;++k)result[i][j]+=a[i][k]*b[k][j];
    return result;
}
struct Viewport {double x_scale,x_offset,y_scale,y_offset;uint32_t width,height;};
inline std::optional<std::array<double,2>> ProjectPixel(const Vector4& clip,const Viewport& viewport) {
    // Reject points on/behind the eye plane rather than inventing zero motion.
    for(double v:clip)if(!std::isfinite(v))return {};
    if(clip[3]<=1e-9 || !viewport.width || !viewport.height ||
       !std::isfinite(viewport.x_scale)||!std::isfinite(viewport.y_scale)||
       !std::isfinite(viewport.x_offset)||!std::isfinite(viewport.y_offset))return {};
    return std::array<double,2>{clip[0]/clip[3]*viewport.x_scale+viewport.x_offset,
                               clip[1]/clip[3]*viewport.y_scale+viewport.y_offset};
}
// NVIDIA motion convention: pixel displacement from current to previous.
// Callers must already have matched object/vertex history, including skinning.
inline std::optional<std::array<double,2>> BackwardMotion(
        const Vector4& current,const Vector4& previous,const Viewport& now,const Viewport& before) {
    if(now.width!=before.width || now.height!=before.height)return {};
    auto c=ProjectPixel(current,now),p=ProjectPixel(previous,before);
    if(!c || !p)return {};
    return std::array<double,2>{(*p)[0]-(*c)[0],(*p)[1]-(*c)[1]};
}
// Modify clip X/Y by pixel jitter times clip W, including negative Y scale.
inline std::optional<Matrix4> JitterProjection(Matrix4 projection,const Viewport& viewport,
                                             double x_pixels,double y_pixels) {
    if(!std::isfinite(x_pixels)||!std::isfinite(y_pixels)||
       !std::isfinite(viewport.x_scale)||!std::isfinite(viewport.y_scale)||
       std::abs(viewport.x_scale)<1e-9||std::abs(viewport.y_scale)<1e-9)return {};
    for(unsigned col=0;col<4;++col) {
        projection[0][col]+=x_pixels/viewport.x_scale*projection[3][col];
        projection[1][col]+=y_pixels/viewport.y_scale*projection[3][col];
    }
    return projection;
}
// The title packs separate stage/highway/HUD ranges into a shared depth target.
// This only reverses viewport range mapping; it does not linearize depth or
// identify which layer owns the pixel. That ownership must come from the draw.
inline std::optional<double> UndoDepthRange(double depth,double scale,double offset) {
    if(!std::isfinite(depth)||!std::isfinite(scale)||!std::isfinite(offset)||std::abs(scale)<1e-9)return {};
    return (depth-offset)/scale;
}
}
