// HAIRMONY 3000 - command-line renderer for listening tests and analysis.
// Usage: RenderTool <in.f32> <sampleRate> <outPrefix> [choir 0-3] [mic 0-4] [flags...]
//   in.f32: mono float32 raw samples. Writes <outPrefix>_<n>.f32 (interleaved) and a .txt index.
//   flags: up down third fifth below mono key=<0-11> scale=<0-11> seed=<n> voicing=<0-2>
#include "../Source/core/ChoirEngine.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

using namespace hm;

int main (int argc, char** argv)
{
    if (argc < 4) { std::fprintf (stderr, "usage: RenderTool in.f32 sampleRate outPrefix [choir] [mic] [flags]\n"); return 2; }
    std::ifstream in (argv[1], std::ios::binary);
    std::vector<float> x;
    float v;
    while (in.read (reinterpret_cast<char*> (&v), sizeof (float))) x.push_back (v);
    const double sr = std::atof (argv[2]);

    RenderSettings s;
    if (argc > 4) s.choir = (ChoirModel) std::atoi (argv[4]);
    if (argc > 5) s.mic = (MicModel) std::atoi (argv[5]);
    for (int i = 6; i < argc; ++i)
    {
        const std::string f = argv[i];
        if (f == "up") s.octaveUp = true;
        else if (f == "down") s.octaveDown = true;
        else if (f == "third") s.thirdAbove = true;
        else if (f == "fifth") s.fifthAbove = true;
        else if (f == "below") s.below = true;
        else if (f == "mono") s.stereo = false;
        else if (f.rfind ("key=", 0) == 0) s.key = std::atoi (f.c_str() + 4);
        else if (f.rfind ("scale=", 0) == 0) s.scale = (Scale) std::atoi (f.c_str() + 6);
        else if (f.rfind ("seed=", 0) == 0) s.seed = (uint64_t) std::atoll (f.c_str() + 5);
        else if (f.rfind ("voicing=", 0) == 0) s.voicing = (Voicing) std::atoi (f.c_str() + 8);
    }

    const auto stems = renderChoir (x, sr, s);
    std::ofstream idx (std::string (argv[3]) + ".txt");
    for (size_t i = 0; i < stems.size(); ++i)
    {
        const auto& st = stems[i];
        const std::string name = std::string (argv[3]) + "_" + std::to_string (i) + ".f32";
        std::ofstream o (name, std::ios::binary);
        for (int n = 0; n < st.numSamples(); ++n)
            for (auto& ch : st.channels) o.write (reinterpret_cast<const char*> (&ch[(size_t) n]), sizeof (float));
        idx << name << "\t" << st.channels.size() << "\t" << st.name << "\n";
    }
    std::printf ("%zu stems\n", stems.size());
    return 0;
}
