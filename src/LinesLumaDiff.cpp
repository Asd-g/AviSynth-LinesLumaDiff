#include <limits>
#include <vector>
#include <fstream>

#include "avisynth.h"

// from avs_core/filters/conditional/conditional_functions.cpp
template<typename pixel_t>
static double get_sad_c(const uint8_t* c_plane8, const uint8_t* t_plane8, size_t height, size_t width, size_t c_pitch, size_t t_pitch)
{
    const pixel_t* c_plane = reinterpret_cast<const pixel_t*>(c_plane8);
    const pixel_t* t_plane = reinterpret_cast<const pixel_t*>(t_plane8);
    c_pitch /= sizeof(pixel_t);
    t_pitch /= sizeof(pixel_t);
    typedef typename std::conditional < sizeof(pixel_t) == 4, double, int64_t>::type sum_t;
    sum_t accum = 0; // int32 holds sum of maximum 16 Mpixels for 8 bit, and 65536 pixels for uint16_t pixels

    for (size_t y = 0; y < height; ++y)
    {
        for (size_t x = 0; x < width; ++x)
            accum += std::abs(t_plane[x] - c_plane[x]);

        c_plane += c_pitch;
        t_plane += t_pitch;
    }

    return static_cast<double>(accum);
}

class LinesLumaDiff : public GenericVideoFilter
{
    int _left, _top, _right, _bottom;
    double _tl, _tt, _tr, _tb;
    const char* _ffile;
    std::vector<int> framen;

    float ComparePlane(PVideoFrame& src, PVideoFrame& src1);

public:
    LinesLumaDiff(PClip _child, int left, int top, int right, int bottom, double tl, double tt, double tr, double tb, const char* ffile);
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
    int __stdcall SetCacheHints(int cachehints, int frame_range)
    {
        return cachehints == CACHE_GET_MTMODE ? MT_SERIALIZED : 0;
    }
};

// from avs_core/filters/conditional/conditional_functions.cpp
float LinesLumaDiff::ComparePlane(PVideoFrame& src, PVideoFrame& src1)
{
    const int pixelsize = vi.ComponentSize();
    const int bits_per_pixel = vi.BitsPerComponent();
    const uint8_t* srcp = src->GetReadPtr(PLANAR_Y);
    const uint8_t* srcp2 = src1->GetReadPtr(PLANAR_Y);
    int height = src->GetHeight(PLANAR_Y);
    int rowsize = src->GetRowSize(PLANAR_Y);
    int width = rowsize / pixelsize;
    int pitch = src->GetPitch(PLANAR_Y);
    int pitch2 = src1->GetPitch(PLANAR_Y);

    int total_pixels = width * height;
    bool sum_in_32bits;
    if (pixelsize == 4)
        sum_in_32bits = false;
    else // worst case check
        sum_in_32bits = ((int64_t)total_pixels * ((static_cast<int64_t>(1) << bits_per_pixel) - 1)) <= std::numeric_limits<int>::max();

    double sad = 0;
    // for c: width, for sse: rowsize
    if (pixelsize == 1)
        sad = get_sad_c<uint8_t>(srcp, srcp2, height, width, pitch, pitch2);
    else if (pixelsize == 2)
        sad = get_sad_c<uint16_t>(srcp, srcp2, height, width, pitch, pitch2);
    else // pixelsize==4
        sad = get_sad_c<float>(srcp, srcp2, height, width, pitch, pitch2);

    float f = static_cast<float>((sad / (static_cast<double>(height) * width)));

    if (bits_per_pixel != 32)
        f /= static_cast<float>((static_cast<int64_t>(1) << bits_per_pixel) - 1);

    return f;
}

LinesLumaDiff::LinesLumaDiff(PClip _child, int left, int top, int right, int bottom, double tl, double tt, double tr, double tb, const char* ffile)
    : GenericVideoFilter(_child), _left(left), _top(top), _right(right), _bottom(bottom), _tl(tl), _tt(tt), _tr(tr),_tb(tb), _ffile(ffile)
{
    framen.reserve(vi.num_frames + static_cast<int64_t>(1));
    
    if (vi.ComponentSize() < 4)
    {
        const int peak = (1 << vi.BitsPerComponent()) - 1;
        _tl /= peak;
        _tt /= peak;
        _tr /= peak;
        _tb /= peak;
    }

}

PVideoFrame __stdcall LinesLumaDiff::GetFrame(int n, IScriptEnvironment* env)
{
    if (_left > 0)
    {
        for (int j = 0; j < _left; ++j)
        {
            const AVSValue args[5] = { child, j, 0, 1, 0 };
            PClip clip = env->Invoke("Crop", AVSValue(args, 5)).AsClip();
            clip = env->Invoke("InternalCache", clip).AsClip();

            const AVSValue args1[5] = { child, j + 1, 0, 1, 0 };
            PClip clip1 = env->Invoke("Crop", AVSValue(args1, 5)).AsClip();
            clip1 = env->Invoke("InternalCache", clip1).AsClip();

            PVideoFrame src = clip->GetFrame(n, env);
            PVideoFrame src1 = clip1->GetFrame(n, env);

            if (ComparePlane(src, src1) > _tl)
            {
                framen.emplace_back(n);
                break;
            }

        }
    }

    if (framen.size() == 0 || (n != framen[framen.size() - 1] && _top > 0))
    {
        for (int j = 0; j < _top; ++j)
        {
            const AVSValue args[5] = { child, 0, j, 0, 1 };
            PClip clip = env->Invoke("Crop", AVSValue(args, 5)).AsClip();
            clip = env->Invoke("InternalCache", clip).AsClip();

            const AVSValue args1[5] = { child, 0, j + 1, 0, 1 };
            PClip clip1 = env->Invoke("Crop", AVSValue(args1, 5)).AsClip();
            clip1 = env->Invoke("InternalCache", clip1).AsClip();

            PVideoFrame src = clip->GetFrame(n, env);
            PVideoFrame src1 = clip1->GetFrame(n, env);

            if (ComparePlane(src, src1) > _tt)
            {
                framen.emplace_back(n);
                break;
            }
        }
    }

    if (framen.size() == 0 || (n != framen[framen.size() - 1] && _right > 0))
    {
        for (int j = 0; j < _right; ++j)
        {
            const AVSValue args[5] = { child, vi.width - (j + 1), 0, 1, 0 };
            PClip clip = env->Invoke("Crop", AVSValue(args, 5)).AsClip();
            clip = env->Invoke("InternalCache", clip).AsClip();

            const AVSValue args1[5] = { child, vi.width - (j + 2), 0, 1, 0 };
            PClip clip1 = env->Invoke("Crop", AVSValue(args1, 5)).AsClip();
            clip1 = env->Invoke("InternalCache", clip1).AsClip();

            PVideoFrame src = clip->GetFrame(n, env);
            PVideoFrame src1 = clip1->GetFrame(n, env);

            if (ComparePlane(src, src1) > _tr)
            {
                framen.emplace_back(n);
                break;
            }
        }
    }

    if (framen.size() == 0 || (n != framen[framen.size() - 1] && _bottom > 0))
    {
        for (int j = 0; j < _bottom; ++j)
        {
            const AVSValue args[5] = { child, 0, vi.height - (j + 1), 0, 1 };
            PClip clip = env->Invoke("Crop", AVSValue(args, 5)).AsClip();
            clip = env->Invoke("InternalCache", clip).AsClip();

            const AVSValue args1[5] = { child, 0, vi.height - (j + 2), 0, 1 };
            PClip clip1 = env->Invoke("Crop", AVSValue(args1, 5)).AsClip();
            clip1 = env->Invoke("InternalCache", clip1).AsClip();

            PVideoFrame src = clip->GetFrame(n, env);
            PVideoFrame src1 = clip1->GetFrame(n, env);

            if (ComparePlane(src, src1) > _tb)
            {
                framen.emplace_back(n);
                break;
            }
        }
    }

    if (n == vi.num_frames - 1)
    {
        std::ofstream file(_ffile, std::ios::out | std::ios::binary);
        for (unsigned i = 0; i < framen.size(); ++i)
            file << framen[i] << "\n";
    }

    return child->GetFrame(n, env);
}

AVSValue __cdecl Create_LinesLumaDiff(AVSValue args, void* user_data, IScriptEnvironment* env)
{
    PClip child = args[0].AsClip();
    const VideoInfo& vi = child->GetVideoInfo();

    if (!vi.IsPlanar() || vi.IsRGB())
        env->ThrowError("LinesLumaDiff: clip must be in YUV planar format.");

    PClip clip = env->Invoke("ExtractY", AVSValue(child, 1)).AsClip();
    const int left = args[1].AsInt(5);
    const int top = args[2].AsInt(5);
    const int right = args[3].AsInt(5);
    const int bottom = args[4].AsInt(5);
    const double tl = args[5].AsFloat(2.5);
    const double tt = args[6].AsFloat(2.5);
    const double tr = args[7].AsFloat(2.5);
    const double tb = args[8].AsFloat(2.5);
    const char* frames_file = args[9].AsString();
    
    if (left < 0 || top < 0 || right < 0 || bottom < 0)
        env->ThrowError("LinesLumaDiff: left, top, right, bottom must not be negative.");
    if (tl < 0.0 || tt < 0.0 || tr < 0.0 || tb < 0.0)
        env->ThrowError("LinesLumaDiff: tl, tt, tr, tb must not be negative.");
    if (!frames_file)
        env->ThrowError("LinesLumaDiff: frames_file must be specified.");

    return new LinesLumaDiff(
        clip,
        left,
        top,
        right,
        bottom,
        tl,
        tt,
        tr,
        tb,
        frames_file);
}

const AVS_Linkage* AVS_linkage;

extern "C" __declspec(dllexport)
const char* __stdcall AvisynthPluginInit3(IScriptEnvironment * env, const AVS_Linkage* const vectors)
{
    AVS_linkage = vectors;

    env->AddFunction("LinesLumaDiff", "c[left]i[top]i[right]i[bottom]i[tl]f[tt]f[tr]f[tb]f[frames_file]s", Create_LinesLumaDiff, 0);
    return "LinesLumaDiff";
}
