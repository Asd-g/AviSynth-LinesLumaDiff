#include <limits>
#include <vector>
#include <fstream>

#include "avisynth.h"

// from avs_core/filters/conditional/conditional_functions.cpp
template<typename pixel_t>
AVS_FORCEINLINE double get_sad_c(const uint8_t* c_plane8, const uint8_t* t_plane8, size_t height, size_t width, size_t c_pitch, size_t t_pitch)
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
    const char* _ffile;
    int _left, _top, _right, _bottom;
    double _tl, _tt, _tr, _tb;
    PClip luma;
    std::vector<unsigned> framen{};

    double ComparePlane(PVideoFrame& src, PVideoFrame& src1);

public:
    LinesLumaDiff(PClip _child, const char* ffile, int left, int top, int right, int bottom, double tl, double tt, double tr, double tb, PClip _luma);
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
    int __stdcall SetCacheHints(int cachehints, int frame_range)
    {
        return cachehints == CACHE_GET_MTMODE ? MT_SERIALIZED : 0;
    }
};

// from avs_core/filters/conditional/conditional_functions.cpp
double LinesLumaDiff::ComparePlane(PVideoFrame& src, PVideoFrame& src1)
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

    double sad = 0;
    // for c: width, for sse: rowsize
    if (pixelsize == 1)
        sad = get_sad_c<uint8_t>(srcp, srcp2, height, width, pitch, pitch2);
    else if (pixelsize == 2)
        sad = get_sad_c<uint16_t>(srcp, srcp2, height, width, pitch, pitch2);
    else // pixelsize==4
        sad = get_sad_c<float>(srcp, srcp2, height, width, pitch, pitch2);

    double f = (sad / (static_cast<double>(height) * width));

    if (bits_per_pixel != 32)
        f /= static_cast<double>((static_cast<int64_t>(1) << bits_per_pixel) - 1);

    return f;
}

LinesLumaDiff::LinesLumaDiff(PClip _child, const char* ffile, int left, int top, int right, int bottom, double tl, double tt, double tr, double tb, PClip _luma)
    : GenericVideoFilter(_child), _ffile(ffile), _left(left), _top(top), _right(right), _bottom(bottom), _tl(tl), _tt(tt), _tr(tr),_tb(tb), luma(_luma)
{
    if (_ffile)
        framen.reserve(vi.num_frames);
}

PVideoFrame __stdcall LinesLumaDiff::GetFrame(int n, IScriptEnvironment* env)
{
    PVideoFrame frame = child->GetFrame(n, env);
    const AVSMap* props = env->getFramePropsRO(frame);

    if (_left > 0)
    {
        for (int j = 0; j < _left; ++j)
        {
            const AVSValue args[5] = { luma, j, 0, 1, 0 };
            PClip clip = env->Invoke("Crop", AVSValue(args, 5)).AsClip();
            clip = env->Invoke("InternalCache", clip).AsClip();

            const AVSValue args1[5] = { luma, j + 1, 0, 1, 0 };
            PClip clip1 = env->Invoke("Crop", AVSValue(args1, 5)).AsClip();
            clip1 = env->Invoke("InternalCache", clip1).AsClip();

            PVideoFrame src = clip->GetFrame(n, env);
            PVideoFrame src1 = clip1->GetFrame(n, env);
            const double diff = ComparePlane(src, src1);

            if (diff > _tl)
            {
                env->propSetFloat(env->getFramePropsRW(frame), "_LinesDiff", diff, 0);

                if (_ffile)
                    framen.emplace_back(n);

                break;
            }
        }
    }

    if (env->propGetType(props, "_LinesDiff") == 'u' && _top > 0)
    {
        for (int j = 0; j < _top; ++j)
        {
            const AVSValue args[5] = { luma, 0, j, 0, 1 };
            PClip clip = env->Invoke("Crop", AVSValue(args, 5)).AsClip();
            clip = env->Invoke("InternalCache", clip).AsClip();

            const AVSValue args1[5] = { luma, 0, j + 1, 0, 1 };
            PClip clip1 = env->Invoke("Crop", AVSValue(args1, 5)).AsClip();
            clip1 = env->Invoke("InternalCache", clip1).AsClip();

            PVideoFrame src = clip->GetFrame(n, env);
            PVideoFrame src1 = clip1->GetFrame(n, env);
            const double diff = ComparePlane(src, src1);

            if (diff > _tt)
            {
                env->propSetFloat(env->getFramePropsRW(frame), "_LinesDiff", diff, 0);
                
                if (_ffile)
                    framen.emplace_back(n);

                break;
            }
        }
    }

    if (env->propGetType(props, "_LinesDiff") == 'u' && _right > 0)
    {
        for (int j = 0; j < _right; ++j)
        {
            const AVSValue args[5] = { luma, vi.width - (j + 1), 0, 1, 0 };
            PClip clip = env->Invoke("Crop", AVSValue(args, 5)).AsClip();
            clip = env->Invoke("InternalCache", clip).AsClip();

            const AVSValue args1[5] = { luma, vi.width - (j + 2), 0, 1, 0 };
            PClip clip1 = env->Invoke("Crop", AVSValue(args1, 5)).AsClip();
            clip1 = env->Invoke("InternalCache", clip1).AsClip();

            PVideoFrame src = clip->GetFrame(n, env);
            PVideoFrame src1 = clip1->GetFrame(n, env);
            const double diff = ComparePlane(src, src1);

            if (diff > _tr)
            {
                env->propSetFloat(env->getFramePropsRW(frame), "_LinesDiff", diff, 0);
                
                if (_ffile)
                    framen.emplace_back(n);

                break;
            }
        }
    }

    if (env->propGetType(props, "_LinesDiff") == 'u' && _bottom > 0)
    {
        for (int j = 0; j < _bottom; ++j)
        {
            const AVSValue args[5] = { luma, 0, vi.height - (j + 1), 0, 1 };
            PClip clip = env->Invoke("Crop", AVSValue(args, 5)).AsClip();
            clip = env->Invoke("InternalCache", clip).AsClip();

            const AVSValue args1[5] = { luma, 0, vi.height - (j + 2), 0, 1 };
            PClip clip1 = env->Invoke("Crop", AVSValue(args1, 5)).AsClip();
            clip1 = env->Invoke("InternalCache", clip1).AsClip();

            PVideoFrame src = clip->GetFrame(n, env);
            PVideoFrame src1 = clip1->GetFrame(n, env);
            const double diff = ComparePlane(src, src1);

            if (diff > _tb)
            {
                env->propSetFloat(env->getFramePropsRW(frame), "_LinesDiff", diff, 0);
                
                if (_ffile)
                    framen.emplace_back(n);

                break;
            }
        }
    }

    if (_ffile && n == vi.num_frames - 1)
    {
        std::ofstream file(_ffile, std::ios::out | std::ios::binary);
        for (unsigned i = 0; i < framen.size(); ++i)
            file << framen[i] << "\n";
    }

    return frame;
}

AVSValue __cdecl Create_LinesLumaDiff(AVSValue args, void* user_data, IScriptEnvironment* env)
{
    PClip child;

    PClip clip = [&]() {
        child = args[0].AsClip();
        const VideoInfo& vi = child->GetVideoInfo();

        if (!vi.IsPlanar() || vi.IsRGB())
            env->ThrowError("LinesLumaDiff: clip must be in YUV planar format.");

        if (vi.NumComponents() > 1)
            return env->Invoke("ExtractY", AVSValue(child, 1)).AsClip();
        else
            return child;
    }();

    const char* frames_file = args[1].AsString(nullptr);
    const int left = args[2].AsInt(5);
    const int top = args[3].AsInt(5);
    const int right = args[4].AsInt(5);
    const int bottom = args[5].AsInt(5);
    const double tl = args[6].AsFloatf(0.14f);
    const double tt = args[7].AsFloatf(0.14f);
    const double tr = args[8].AsFloatf(0.14f);
    const double tb = args[9].AsFloatf(0.14f);
    
    if (left < 0 || top < 0 || right < 0 || bottom < 0)
        env->ThrowError("LinesLumaDiff: left, top, right, bottom must be greater than or equal to 0.");
    if (tl < 0.0 || tl > 1.0)
        env->ThrowError("LinesLumaDiff: tl must be between 0.0..1.0.");
    if (tt < 0.0 || tt > 1.0)
        env->ThrowError("LinesLumaDiff: tt must be between 0.0..1.0.");
    if (tr < 0.0 || tr > 1.0)
        env->ThrowError("LinesLumaDiff: tr must be between 0.0..1.0.");
    if (tb < 0.0 || tb > 1.0)
        env->ThrowError("LinesLumaDiff: tb must be between 0.0..1.0.");

    return new LinesLumaDiff(
        child,
        frames_file,
        left,
        top,
        right,
        bottom,
        tl,
        tt,
        tr,
        tb,
        clip);
}

const AVS_Linkage* AVS_linkage;

extern "C" __declspec(dllexport)
const char* __stdcall AvisynthPluginInit3(IScriptEnvironment * env, const AVS_Linkage* const vectors)
{
    AVS_linkage = vectors;

    env->AddFunction("LinesLumaDiff", "c[output]s[left]i[top]i[right]i[bottom]i[tl]f[tt]f[tr]f[tb]f", Create_LinesLumaDiff, 0);
    return "LinesLumaDiff";
}
