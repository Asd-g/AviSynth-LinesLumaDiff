#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "avisynth.h"

// from avs_core/filters/conditional/conditional_functions.cpp
template <typename pixel_t, int peak>
AVS_FORCEINLINE double get_sad_c(PVideoFrame src, PVideoFrame src1)
{
    const size_t c_pitch{ src->GetPitch(PLANAR_Y) / sizeof(pixel_t) };
    const size_t t_pitch{ src1->GetPitch(PLANAR_Y) / sizeof(pixel_t) };
    const size_t width{ src->GetRowSize(PLANAR_Y) / sizeof(pixel_t) };
    const int height{ src->GetHeight(PLANAR_Y) };
    const pixel_t* c_plane{ reinterpret_cast<const pixel_t*>(src->GetReadPtr(PLANAR_Y)) };
    const pixel_t* t_plane{ reinterpret_cast<const pixel_t*>(src1->GetReadPtr(PLANAR_Y)) };

    typedef typename std::conditional < sizeof(pixel_t) == 4, double, int64_t>::type sum_t;
    sum_t accum{ 0 }; // int32 holds sum of maximum 16 Mpixels for 8 bit, and 65536 pixels for uint16_t pixels

    for (size_t y{ 0 }; y < height; ++y)
    {
        for (size_t x{ 0 }; x < width; ++x)
            accum += std::abs(t_plane[x] - c_plane[x]);

        c_plane += c_pitch;
        t_plane += t_pitch;
    }

    if constexpr (std::is_integral_v<pixel_t>)
        return (static_cast<double>(accum) / (height * width)) / peak;
    else
        return static_cast<double>(accum) / (height * width);
}

class LinesLumaDiff : public GenericVideoFilter
{
    const char* ffile;
    int left, top, right, bottom;
    double tl, tt, tr, tb;
    bool flush;
    PClip luma;
    std::vector<std::string> framen;

    double (*compare)(PVideoFrame src, PVideoFrame src1);

public:
    LinesLumaDiff(PClip _child, const char* ffile_, int left_, int top_, int right_, int bottom_, double tl_, double tt_, double tr_, double tb_, bool flush_, PClip luma_)
        : GenericVideoFilter(_child), ffile(ffile_), left(left_), top(top_), right(right_), bottom(bottom_), tl(tl_), tt(tt_), tr(tr_), tb(tb_), flush(flush_), luma(luma_)
    {
        if (ffile)
            framen.reserve(vi.num_frames);

        switch (vi.ComponentSize())
        {
            case 1: compare = get_sad_c<uint8_t, 255>; break;
            case 2:
            {
                switch (vi.BitsPerComponent())
                {
                    case 10: compare = get_sad_c<uint16_t, 1023>; break;
                    case 12: compare = get_sad_c<uint16_t, 4095>; break;
                    case 14: compare = get_sad_c<uint16_t, 16383>; break;
                    case 16: compare = get_sad_c<uint16_t, 65535>; break;
                }
                break;
            }
            default: compare = get_sad_c<float, 1>;
        }
    }

    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env)
    {
        PVideoFrame frame{ child->GetFrame(n, env) };
        const AVSMap* props{ env->getFramePropsRO(frame) };
        int fn{ -99 };

        if (left > 0)
        {
            for (int j{ 0 }; j < left; ++j)
            {
                const AVSValue args[5]{ luma, j, 0, 1, 0 };
                const AVSValue args1[5]{ luma, j + 1, 0, 1, 0 };

                const double diff{ compare(env->Invoke("Crop", AVSValue(args, 5)).AsClip()->GetFrame(n, env),
                    env->Invoke("Crop", AVSValue(args1, 5)).AsClip()->GetFrame(n, env)) };

                if (diff > tl)
                {
                    env->propSetFloat(env->getFramePropsRW(frame), "LinesDiffLeft", diff, 0);

                    if (ffile)
                    {
                        framen.emplace_back(std::to_string(n) + " # left, diff: " + std::to_string(diff));

                        if (flush)
                            fn = n;
                    }

                    break;
                }
            }
        }

        if (top > 0 && env->propGetType(props, "LinesDiffLeft") == 'u')
        {
            for (int j{ 0 }; j < top; ++j)
            {
                const AVSValue args[5]{ luma, 0, j, 0, 1 };
                const AVSValue args1[5]{ luma, 0, j + 1, 0, 1 };

                const double diff{ compare(env->Invoke("Crop", AVSValue(args, 5)).AsClip()->GetFrame(n, env),
                    env->Invoke("Crop", AVSValue(args1, 5)).AsClip()->GetFrame(n, env)) };

                if (diff > tt)
                {
                    env->propSetFloat(env->getFramePropsRW(frame), "LinesDiffTop", diff, 0);

                    if (ffile)
                    {
                        framen.emplace_back(std::to_string(n) + " # top, diff: " + std::to_string(diff));

                        if (flush)
                            fn = n;
                    }

                    break;
                }
            }
        }

        if (right > 0 && env->propGetType(props, "LinesDiffLeft") == 'u' && env->propGetType(props, "LinesDiffTop") == 'u')
        {
            for (int j{ 0 }; j < right; ++j)
            {
                const AVSValue args[5]{ luma, vi.width - (j + 1), 0, 1, 0 };
                const AVSValue args1[5]{ luma, vi.width - (j + 2), 0, 1, 0 };

                const double diff{ compare(env->Invoke("Crop", AVSValue(args, 5)).AsClip()->GetFrame(n, env),
                    env->Invoke("Crop", AVSValue(args1, 5)).AsClip()->GetFrame(n, env)) };

                if (diff > tr)
                {
                    env->propSetFloat(env->getFramePropsRW(frame), "LinesDiffRight", diff, 0);

                    if (ffile)
                    {
                        framen.emplace_back(std::to_string(n) + " # right, diff: " + std::to_string(diff));

                        if (flush)
                            fn = n;
                    }

                    break;
                }
            }
        }

        if (bottom > 0 && env->propGetType(props, "LinesDiffLeft") == 'u' && env->propGetType(props, "LinesDiffTop") == 'u' && env->propGetType(props, "LinesDiffRight") == 'u')
        {
            for (int j{ 0 }; j < bottom; ++j)
            {
                const AVSValue args[5]{ luma, 0, vi.height - (j + 1), 0, 1 };
                const AVSValue args1[5]{ luma, 0, vi.height - (j + 2), 0, 1 };

                const double diff{ compare(env->Invoke("Crop", AVSValue(args, 5)).AsClip()->GetFrame(n, env),
                    env->Invoke("Crop", AVSValue(args1, 5)).AsClip()->GetFrame(n, env)) };

                if (diff > tb)
                {
                    env->propSetFloat(env->getFramePropsRW(frame), "LinesDiffBottom", diff, 0);

                    if (ffile)
                    {
                        framen.emplace_back(std::to_string(n) + " # bottom, diff: " + std::to_string(diff));

                        if (flush)
                            fn = n;
                    }

                    break;
                }
            }
        }

        if (ffile)
        {
            if (flush)
            {
                if (fn == n && !framen.empty())
                {
                    std::ofstream file{ ffile, std::ios::app };
                    file << framen.back() << '\n';
                }
            }
            else
            {
                if (n == vi.num_frames - 1 && !framen.empty())
                {
                    std::ofstream file{ ffile };
                    for (unsigned i = 0; i < framen.size(); ++i)
                        file << framen[i] << '\n';
                }
            }
        }

        return frame;
    }

    int __stdcall SetCacheHints(int cachehints, int frame_range)
    {
        return cachehints == CACHE_GET_MTMODE ? MT_SERIALIZED : 0;
    }
};

AVSValue __cdecl Create_LinesLumaDiff(AVSValue args, void* user_data, IScriptEnvironment* env)
{
    PClip child{ args[0].AsClip() };

    PClip clip{ [&]() {
        const VideoInfo& vi = child->GetVideoInfo();

        if (!vi.IsPlanar() || vi.IsRGB())
            env->ThrowError("LinesLumaDiff: clip must be in YUV planar format.");

        if (vi.NumComponents() > 1)
            return env->Invoke("InternalCache", env->Invoke("ExtractY", AVSValue(child, 1)).AsClip()).AsClip();
        else
            return child;
    }() };

    const int left{ args[2].AsInt(5) };
    const int top{ args[3].AsInt(5) };
    const int right{ args[4].AsInt(5) };
    const int bottom{ args[5].AsInt(5) };
    const double tl{ args[6].AsFloatf(0.14f) };
    const double tt{ args[7].AsFloatf(0.14f) };
    const double tr{ args[8].AsFloatf(0.14f) };
    const double tb{ args[9].AsFloatf(0.14f) };

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
        args[1].AsString(nullptr),
        left,
        top,
        right,
        bottom,
        tl,
        tt,
        tr,
        tb,
        args[10].AsBool(false),
        clip);
}

const AVS_Linkage* AVS_linkage;

extern "C" __declspec(dllexport)
const char* __stdcall AvisynthPluginInit3(IScriptEnvironment * env, const AVS_Linkage* const vectors)
{
    AVS_linkage = vectors;

    env->AddFunction("LinesLumaDiff", "c[output]s[left]i[top]i[right]i[bottom]i[tl]f[tt]f[tr]f[tb]f[flush]b", Create_LinesLumaDiff, 0);
    return "LinesLumaDiff";
}
