#include "Parameters.h"

namespace locus
{
    namespace
    {
        ParamSpec continuous (const char* id, const char* name, float lo, float hi, float def, const char* unit, float midpoint = 0.0f)
        {
            ParamSpec s;
            s.id = id;
            s.name = name;
            s.type = ParamType::continuous;
            s.minValue = lo;
            s.maxValue = hi;
            s.defaultValue = def;
            s.unit = juce::String::fromUTF8 (unit); // units include non-ASCII (degree, cubed, times)
            s.midpoint = midpoint;
            return s;
        }

        ParamSpec toggle (const char* id, const char* name, bool def)
        {
            ParamSpec s;
            s.id = id;
            s.name = name;
            s.type = ParamType::toggle;
            s.minValue = 0.0f;
            s.maxValue = 1.0f;
            s.defaultValue = def ? 1.0f : 0.0f;
            return s;
        }

        ParamSpec choice (const char* id, const char* name, juce::StringArray choices, int def)
        {
            ParamSpec s;
            s.id = id;
            s.name = name;
            s.type = ParamType::choice;
            s.minValue = 0.0f;
            s.maxValue = (float) (choices.size() - 1);
            s.defaultValue = (float) def;
            s.choices = std::move (choices);
            return s;
        }

        /** Axis and Omni share one EQ parameter set (spec §6). */
        void addEqParams (std::vector<ParamSpec>& v, const char* prefix, const char* namePrefix)
        {
            const juce::String p (prefix), n (namePrefix);
            auto add = [&] (ParamSpec s) { v.push_back (std::move (s)); };

            add (continuous ((p + "Low").toRawUTF8(),      (n + " Low").toRawUTF8(),       -24.0f, 24.0f,    0.0f,    "dB"));
            add (continuous ((p + "Mid").toRawUTF8(),      (n + " Mid").toRawUTF8(),       -24.0f, 24.0f,    0.0f,    "dB"));
            add (continuous ((p + "High").toRawUTF8(),     (n + " High").toRawUTF8(),      -24.0f, 24.0f,    0.0f,    "dB"));
            add (continuous ((p + "FreqLow").toRawUTF8(),  (n + " Freq Low").toRawUTF8(),   20.0f, 1000.0f,  177.0f,  "Hz", 141.0f));
            add (continuous ((p + "FreqHigh").toRawUTF8(), (n + " Freq High").toRawUTF8(), 1000.0f, 20000.0f, 5660.0f, "Hz", 4472.0f));
            add (continuous ((p + "Gain").toRawUTF8(),     (n + " Gain").toRawUTF8(),      -60.0f, 12.0f,    0.0f,    "dB"));
            add (toggle ((p + "Bypass").toRawUTF8(), (n + " Bypass").toRawUTF8(), false));
            add (toggle ((p + "Mute").toRawUTF8(),   (n + " Mute").toRawUTF8(),   false));
        }
    }

    const std::vector<ParamSpec>& getSourceParamSpecs()
    {
        static const std::vector<ParamSpec> specs = []
        {
            std::vector<ParamSpec> v;
            v.push_back (continuous ("azimuth",     "Azimuth",     -180.0f, 180.0f, 0.0f,   "\xc2\xb0"));
            v.push_back (continuous ("distance",    "Distance",       0.0f,  50.0f, 1.0f,   "m", 5.0f));
            v.push_back (continuous ("elevation",   "Elevation",    -90.0f,  90.0f, 0.0f,   "\xc2\xb0"));
            v.push_back (toggle     ("doppler",     "Doppler", false));
            v.push_back (continuous ("orientation", "Orientation", -180.0f, 180.0f, 0.0f,   "\xc2\xb0"));
            v.push_back (continuous ("aperture",    "Aperture",      10.0f, 360.0f, 80.0f,  "\xc2\xb0"));
            v.push_back (continuous ("width",       "Width",          0.0f, 180.0f, 30.0f,  "\xc2\xb0"));
            v.push_back (continuous ("spread",      "Spread",         0.0f, 100.0f, 0.0f,   "%"));
            v.push_back (continuous ("panRev",      "Pan Rev",        0.0f, 100.0f, 0.0f,   "%"));
            v.push_back (choice     ("panning",     "Panning", { "Angular", "Nearest", "Ambisonics" }, 0));
            v.push_back (toggle     ("air",         "Air", true));
            v.push_back (continuous ("airFreq",     "Air Freq",    1000.0f, 20000.0f, 10000.0f, "Hz", 4472.0f));
            v.push_back (continuous ("nearRadius",  "Near Radius",    0.1f,  10.0f, 1.0f,   "m", 1.0f));
            v.push_back (choice     ("dropMode",    "Drop Mode", { "Lin", "Log" }, 1));
            v.push_back (continuous ("drop",        "Drop",           0.0f,  24.0f, 6.0f,   "dB"));
            addEqParams (v, "axis", "Axis");
            addEqParams (v, "omni", "Omni");
            jassert ((int) v.size() == SourceParam::count);
            return v;
        }();

        return specs;
    }

    const std::vector<ParamSpec>& getRoomParamSpecs()
    {
        static const std::vector<ParamSpec> specs = []
        {
            std::vector<ParamSpec> v;
            v.push_back (continuous ("roomSize",     "Room Size",     10.0f, 100000.0f, 2500.0f,  "m\xc2\xb3", 1000.0f));
            v.push_back (continuous ("offset",       "Offset",         0.0f,    100.0f,    0.0f,  "ms"));
            v.push_back (continuous ("earlyShape",   "Early Shape",    0.0f,    100.0f,   50.0f,  "%"));
            v.push_back (continuous ("decay",        "Decay",          0.1f,     10.0f,    2.5f,  "s", 1.0f));
            v.push_back (toggle     ("infinite",     "Infinite", false));
            v.push_back (continuous ("decayLow",     "Low",            0.1f,      2.0f,    1.0f,  "\xc3\x97"));
            v.push_back (continuous ("decayHigh",    "High",           0.1f,      2.0f,    0.5f,  "\xc3\x97"));
            v.push_back (continuous ("freqLow",      "Freq Low",      50.0f,   1000.0f,  250.0f,  "Hz", 224.0f));
            v.push_back (continuous ("freqHigh",     "Freq High",   1000.0f,  16000.0f, 8000.0f,  "Hz", 4000.0f));
            v.push_back (toggle     ("hd",           "HD", false));
            v.push_back (toggle     ("air",          "Air", true));
            v.push_back (continuous ("airFreq",      "Air Freq",    1000.0f,  20000.0f, 10000.0f, "Hz", 4472.0f));
            v.push_back (continuous ("earlyLevel",   "Early",        -60.0f,     12.0f,    0.0f,  "dB"));
            v.push_back (continuous ("clusterLevel", "Cluster",      -60.0f,     12.0f,    0.0f,  "dB"));
            v.push_back (continuous ("reverbLevel",  "Reverb",       -60.0f,     12.0f,    0.0f,  "dB"));
            v.push_back (toggle     ("earlyOn",      "Early On", true));
            v.push_back (toggle     ("clusterOn",    "Cluster On", true));
            v.push_back (toggle     ("reverbOn",     "Reverb On", true));
            jassert ((int) v.size() == RoomParam::count);
            return v;
        }();

        return specs;
    }

    const std::vector<ParamSpec>& getOutputParamSpecs()
    {
        static const std::vector<ParamSpec> specs = []
        {
            std::vector<ParamSpec> v;
            v.push_back (continuous ("volume", "Volume", -60.0f, 12.0f, 0.0f, "dB"));
            v.push_back (toggle ("limiter", "Limiter", true));
            jassert ((int) v.size() == OutputParam::count);
            return v;
        }();

        return specs;
    }
}
