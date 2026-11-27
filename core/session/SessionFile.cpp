#include "SessionFile.h"

namespace locus
{
    namespace
    {
        constexpr int formatVersion = 1;

        juce::var makeObject() { return juce::var (new juce::DynamicObject()); }

        juce::DynamicObject& obj (juce::var& v) { return *v.getDynamicObject(); }

        double number (const juce::var& v, const char* key, double fallback)
        {
            const auto x = v.getProperty (key, fallback);
            return x.isDouble() || x.isInt() || x.isInt64() ? (double) x : fallback;
        }

        /** Parameters as { id: value }, so the order of the tables can change between versions. */
        template <size_t N>
        juce::var paramsToJson (const std::array<float, N>& values, const std::vector<ParamSpec>& specs)
        {
            auto o = makeObject();
            for (size_t i = 0; i < N; ++i)
                obj (o).setProperty (specs[i].id, values[i]);
            return o;
        }

        template <size_t N>
        void paramsFromJson (const juce::var& v, std::array<float, N>& values, const std::vector<ParamSpec>& specs)
        {
            for (size_t i = 0; i < N; ++i)
            {
                const auto x = v.getProperty (specs[i].id, juce::var());
                values[i] = x.isDouble() || x.isInt() || x.isInt64() || x.isBool() ? specs[i].clamp ((float) (double) x)
                                                                                    : specs[i].defaultValue;
            }
        }

        template <size_t N>
        void fillDefaults (std::array<float, N>& values, const std::vector<ParamSpec>& specs)
        {
            for (size_t i = 0; i < N; ++i)
                values[i] = specs[i].defaultValue;
        }
    }

    //==============================================================================
    juce::var speakersToJson (const SpeakerLayout& layout)
    {
        juce::Array<juce::var> speakers;
        for (const auto& s : layout.speakers)
        {
            const auto p = s.getPosition();
            auto o = makeObject();
            obj (o).setProperty ("label", s.label);
            obj (o).setProperty ("x", juce::roundToInt (p.x * 1000.0) / 1000.0); // millimetre precision
            obj (o).setProperty ("y", juce::roundToInt (p.y * 1000.0) / 1000.0);
            obj (o).setProperty ("z", juce::roundToInt (p.z * 1000.0) / 1000.0);
            obj (o).setProperty ("output", s.output);
            speakers.add (o);
        }

        auto root = makeObject();
        obj (root).setProperty ("format", "locus-speakers");
        obj (root).setProperty ("version", formatVersion);
        obj (root).setProperty ("speakers", speakers);
        return root;
    }

    std::optional<SpeakerLayout> speakersFromJson (const juce::var& root, juce::String& error)
    {
        if (root.getProperty ("format", juce::var()).toString() != "locus-speakers")
        {
            error = "Not a Locus speaker file.";
            return std::nullopt;
        }

        const auto* list = root.getProperty ("speakers", juce::var()).getArray();
        if (list == nullptr)
        {
            error = "The file has no speaker list.";
            return std::nullopt;
        }

        SpeakerLayout layout;
        layout.name = customSetupName;

        for (const auto& item : *list)
        {
            Speaker s;
            s.label = item.getProperty ("label", juce::var()).toString().substring (0, maxSpeakerLabelChars);
            s.setPosition ({ number (item, "x", 0.0), number (item, "y", defaultSpeakerRadius), number (item, "z", 0.0) });
            s.output = (int) number (item, "output", (double) (layout.size() + 1));
            layout.speakers.push_back (s);
        }

        return layout;
    }

    //==============================================================================
    SessionData SessionData::defaults()
    {
        SessionData d;
        fillDefaults (d.room, getRoomParamSpecs());
        fillDefaults (d.output, getOutputParamSpecs());
        for (auto& s : d.sources)
            fillDefaults (s.params, getSourceParamSpecs());
        return d;
    }

    juce::var sessionToJson (const SessionData& d)
    {
        auto root = makeObject();
        obj (root).setProperty ("format", "locus-session");
        obj (root).setProperty ("version", formatVersion);

        auto speakers = makeObject();
        obj (speakers).setProperty ("setup", d.setupName);
        obj (speakers).setProperty ("radius", d.speakerRadius);
        obj (speakers).setProperty ("align", d.align);
        if (d.setupName == customSetupName)
            obj (speakers).setProperty ("custom", speakersToJson (d.customLayout));
        obj (root).setProperty ("speakers", speakers);

        obj (root).setProperty ("room", paramsToJson (d.room, getRoomParamSpecs()));
        obj (root).setProperty ("output", paramsToJson (d.output, getOutputParamSpecs()));

        juce::Array<juce::var> sources;
        for (int slot = 0; slot < maxSources; ++slot)
        {
            const auto& s = d.sources[(size_t) slot];
            if (! s.known)
                continue;

            auto o = makeObject();
            obj (o).setProperty ("slot", slot + 1);
            obj (o).setProperty ("name", s.name);
            obj (o).setProperty ("params", paramsToJson (s.params, getSourceParamSpecs()));
            sources.add (o);
        }
        obj (root).setProperty ("sources", sources);

        return root;
    }

    std::optional<SessionData> sessionFromJson (const juce::var& root, juce::String& error)
    {
        if (root.getProperty ("format", juce::var()).toString() != "locus-session")
        {
            error = "Not a Locus session file.";
            return std::nullopt;
        }

        auto d = SessionData::defaults();

        const auto speakers = root.getProperty ("speakers", juce::var());
        d.speakerRadius = juce::jlimit (minSpeakerRadius, maxSpeakerRadius, number (speakers, "radius", defaultSpeakerRadius));
        d.align = (bool) speakers.getProperty ("align", true);

        const auto setup = speakers.getProperty ("setup", "5.0").toString();
        if (setup == customSetupName)
        {
            juce::String customError;
            auto custom = speakersFromJson (speakers.getProperty ("custom", juce::var()), customError);
            if (! custom || ! custom->validate().isEmpty())
            {
                error = "The session's custom speaker setup is invalid. " + customError;
                return std::nullopt;
            }
            d.setupName = setup;
            d.customLayout = *custom;
        }
        else if (makePresetLayout (setup).has_value())
        {
            d.setupName = setup;
        }

        paramsFromJson (root.getProperty ("room", juce::var()), d.room, getRoomParamSpecs());
        paramsFromJson (root.getProperty ("output", juce::var()), d.output, getOutputParamSpecs());

        if (const auto* list = root.getProperty ("sources", juce::var()).getArray())
            for (const auto& item : *list)
            {
                const auto slot = (int) number (item, "slot", 0.0) - 1;
                if (slot < 0 || slot >= maxSources)
                    continue;

                auto& s = d.sources[(size_t) slot];
                s.known = true;
                s.name = item.getProperty ("name", juce::var()).toString();
                paramsFromJson (item.getProperty ("params", juce::var()), s.params, getSourceParamSpecs());
            }

        return d;
    }

    //==============================================================================
    bool writeJsonFile (const juce::File& file, const juce::var& json, juce::String& error)
    {
        file.getParentDirectory().createDirectory();
        if (! file.replaceWithText (juce::JSON::toString (json)))
        {
            error = "Could not write " + file.getFullPathName();
            return false;
        }
        return true;
    }

    std::optional<juce::var> readJsonFile (const juce::File& file, juce::String& error)
    {
        if (! file.existsAsFile())
        {
            error = "File not found: " + file.getFullPathName();
            return std::nullopt;
        }

        juce::var result;
        const auto parse = juce::JSON::parse (file.loadFileAsString(), result);
        if (parse.failed())
        {
            error = "Could not read " + file.getFileName() + ": " + parse.getErrorMessage();
            return std::nullopt;
        }
        return result;
    }
}
