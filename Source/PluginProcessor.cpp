#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "UiTheme.h"

#if JUCE_MAC
 #include <CoreMIDI/CoreMIDI.h>
 #include <AudioToolbox/AudioToolbox.h>
#endif






namespace {
    constexpr double kSixteenthQ = 0.25;      // 1/16th in quarter-notes
    constexpr double kBarEps     = 1.0e-6;    // bar-start epsilon

   #if CLOCKV3_DEMO
    // Progressive demo timeout tiers (in seconds):
    //   1st launch: 30 min, 2nd: 20 min, 3rd: 10 min, 4th+: 5 min
    // Resets to tier 0 after 1 hour of inactivity (measured from last launch).
    // State is persisted to disk and shared via a process-wide static so
    // multiple plugin instances in one DAW share the same countdown.
    static constexpr int kDemoTierSeconds[] = { 30 * 60, 20 * 60, 10 * 60, 5 * 60 };
    static constexpr int kNumDemoTiers = 4;
    static constexpr juce::int64 kDemoResetCooldownMs = 60LL * 60 * 1000; // 1 hour

    struct DemoSessionState
    {
        std::atomic<juce::uint32> startTickMs { 0 };
        std::atomic<int> runtimeSeconds { kDemoTierSeconds[0] };
        std::atomic<int> launchCount { 0 };          // tier index used this session
        std::atomic<juce::int64> lastLaunchMs { 0 };  // timestamp when session started
        std::atomic<bool> initialised { false };
    };

    static DemoSessionState& getDemoSession() noexcept
    {
        static DemoSessionState state;
        return state;
    }

    static juce::File getDemoStateFile()
    {
        auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("toolBoy");
        dir.createDirectory();
        return dir.getChildFile("clock_v3_demo_state");
    }

    static void initDemoSession() noexcept
    {
        auto& session = getDemoSession();
        if (session.initialised.exchange(true, std::memory_order_acq_rel))
            return; // already initialised by another instance in this process

        int launchCount = 0;
        juce::int64 lastLaunchMs = 0;

        auto stateFile = getDemoStateFile();
        if (stateFile.existsAsFile())
        {
            auto lines = juce::StringArray::fromLines(stateFile.loadFileAsString());
            if (lines.size() >= 2)
            {
                launchCount = lines[0].getIntValue();
                lastLaunchMs = lines[1].getLargeIntValue();
            }
        }

        // Reset tier if the user has waited at least 1 hour since the last launch
        const juce::int64 nowMs = juce::Time::currentTimeMillis();
        if (lastLaunchMs > 0 && (nowMs - lastLaunchMs) >= kDemoResetCooldownMs)
            launchCount = 0;

        const int tierIndex = juce::jlimit(0, kNumDemoTiers - 1, launchCount);
        session.runtimeSeconds.store(kDemoTierSeconds[tierIndex], std::memory_order_relaxed);
        session.launchCount.store(launchCount, std::memory_order_relaxed);
        session.lastLaunchMs.store(nowMs, std::memory_order_relaxed);
        session.startTickMs.store(juce::Time::getMillisecondCounter(), std::memory_order_release);

        // Persist incremented launch count + current timestamp
        stateFile.replaceWithText(juce::String(launchCount + 1) + "\n"
                                + juce::String(nowMs) + "\n");
    }

    std::atomic<juce::uint32>& getDemoSessionStartTickMs() noexcept
    {
        return getDemoSession().startTickMs;
    }

    juce::uint32 ensureDemoSessionStartTickMs() noexcept
    {
        initDemoSession();
        return getDemoSession().startTickMs.load(std::memory_order_acquire);
    }

    int getDemoSessionRuntimeSeconds() noexcept
    {
        return getDemoSession().runtimeSeconds.load(std::memory_order_relaxed);
    }

    int getDemoSessionLaunchCount() noexcept
    {
        return getDemoSession().launchCount.load(std::memory_order_relaxed);
    }

    // Returns the runtime (in minutes) for the *next* session after this one.
    int getNextDemoRuntimeMinutes() noexcept
    {
        const int nextTier = juce::jlimit(0, kNumDemoTiers - 1,
                                          getDemoSessionLaunchCount() + 1);
        return kDemoTierSeconds[nextTier] / 60;
    }

    // Returns seconds remaining until the 1-hour cooldown resets the tier to 30 min.
    // Returns -1 if not yet at the final tier (launchCount < kNumDemoTiers - 1).
    int getDemoCooldownRemainingSeconds() noexcept
    {
        auto& session = getDemoSession();
        const int launches = session.launchCount.load(std::memory_order_relaxed);
        if (launches < kNumDemoTiers - 1) return -1; // not at final tier yet
        const juce::int64 launchMs = session.lastLaunchMs.load(std::memory_order_relaxed);
        if (launchMs <= 0) return -1;
        const juce::int64 elapsed = juce::Time::currentTimeMillis() - launchMs;
        const juce::int64 remaining = kDemoResetCooldownMs - elapsed;
        return remaining > 0 ? (int)(remaining / 1000) : 0;
    }
   #endif

    // Fast rounding for known non-negative values (avoids std::llround overhead)
    inline int fastRoundPositive(double x) { return (int) (x + 0.5); }

    inline void addBoth(juce::MidiBuffer& mainBuf, juce::MidiBuffer& extBuf, const juce::MidiMessage& msg, int sample)
    {
        mainBuf.addEvent(msg, sample);
        extBuf.addEvent(msg, sample);
    }
    
    // Compute logical 1..16 step within a bar for an absolute PPQ position,
    // using provided shift in quarter-notes (shiftQ). When shiftQ <= 0 or
    // barLenQ != 4.0, falls back to uniform 16th grid.
    inline int computeLogicalStepFromPPQ(double absolutePPQ, double barLenQ, double shiftQ)
    {
        if (!(barLenQ > 0.0)) return 1;
        double posInBar = std::fmod(absolutePPQ, barLenQ);
        if (posInBar < 0.0) posInBar += barLenQ;
        // Snap near-zero positions to exact bar start to avoid 1-sample rounding
        // producing step 2 for values that should be step 1.
        if (std::fabs(posInBar) < 1e-9)
            posInBar = 0.0;
        if (shiftQ <= 0.0 || std::fabs(barLenQ - 4.0) > 1e-6)
        {
            int step = (int) std::floor((posInBar / barLenQ) * 16.0) + 1;
            if (step < 1) step = 1; if (step > 16) step = 16;
            return step;
        }
        // Build the shuffled positions for each logical step (0..15) and pick the closest.
        // shiftQ: provided by caller (quantized when linear OFF, continuous when ON).
        double bestDiff = 1e9; int bestIdx = 0;
        for (int logical = 0; logical < 16; ++logical)
        {
            int pair = logical / 2;
            bool second = (logical & 1) == 1;
            double pairStart = pair * 0.5;
            double stepPos = second ? (pairStart + 0.25 + shiftQ) : pairStart;
            double d = std::fabs(posInBar - stepPos);
            if (d < bestDiff)
            {
                bestDiff = d; bestIdx = logical;
            }
        }
        return bestIdx + 1;
    }

    // Compute absolute PPQ position for a logical 1..16 step inside the given bar.
    // Uses the provided shiftQ mapping consistent with computeLogicalStepFromPPQ.
    inline double computeStepPPQ(double barStartPPQ, double barLenQ, int logicalStep1to16, double shiftQ)
    {
        if (logicalStep1to16 < 1) logicalStep1to16 = 1;
        if (logicalStep1to16 > 16) logicalStep1to16 = 16;
        const int idx = logicalStep1to16 - 1; // 0..15
        double posInBar;
        if (shiftQ <= 0.0 || std::fabs(barLenQ - 4.0) > 1e-6)
        {
            posInBar = (barLenQ / 16.0) * (double) idx;
        }
        else
        {
            int pair = idx / 2;
            bool second = (idx & 1) == 1;
            double pairStart = pair * 0.5;
            posInBar = second ? (pairStart + 0.25 + shiftQ) : pairStart;
        }
        // snap tiny values to exact bar start for step 1
        if (std::fabs(posInBar) < 1e-9)
            posInBar = 0.0;
        return barStartPPQ + posInBar;
    }

    inline long long computeTickIndexFromPPQ(double absolutePPQ, int resolution, double shiftQ)
    {
        if (resolution <= 0)
            return 0;

        if (shiftQ <= 1e-12)
            return (long long) std::floor(absolutePPQ * (double) resolution);

        const double pairLenQ = 0.5;
        const long long pulsesPerPair = (long long) std::llround(pairLenQ * (double) resolution);
        const int pulsesPer16th = (int) (pulsesPerPair / 2);
        if (pulsesPerPair <= 0 || pulsesPer16th <= 0)
            return (long long) std::floor(absolutePPQ * (double) resolution);

        const double firstLen = 0.25 + shiftQ;
        const double secondLen = 0.25 - shiftQ;
        const double spacingFirst = firstLen / (double) pulsesPer16th;
        const double spacingSecond = secondLen / (double) pulsesPer16th;
        const double eps = 1.0e-9;

        long long pairIndex = (long long) std::floor(absolutePPQ / pairLenQ);
        double posInPair = absolutePPQ - ((double) pairIndex * pairLenQ);
        if (posInPair < 0.0)
        {
            --pairIndex;
            posInPair += pairLenQ;
        }

        int tickInPair = 0;
        if (posInPair < firstLen - eps)
        {
            tickInPair = juce::jlimit(0, pulsesPer16th - 1,
                                      (int) std::floor((posInPair / spacingFirst) + eps));
        }
        else
        {
            tickInPair = pulsesPer16th + juce::jlimit(0, pulsesPer16th - 1,
                                      (int) std::floor(((posInPair - firstLen) / juce::jmax(spacingSecond, eps)) + eps));
        }

        return pairIndex * pulsesPerPair + (long long) tickInPair;
    }
}

#if JUCE_MAC
struct ClockSyncAudioProcessor::TimestampedCoreMidiOut final
{
        ~TimestampedCoreMidiOut()
        {
            if (endpoint != 0 && isVirtual)
                MIDIEndpointDispose(endpoint);
            if (port != 0)
                MIDIPortDispose(port);
            if (client != 0)
                MIDIClientDispose(client);
        }

        static std::shared_ptr<TimestampedCoreMidiOut> createVirtual (const juce::String& name)
        {
            MIDIClientRef client = 0;
            if (MIDIClientCreate(CFSTR("toolBoyClockCoreMIDI"), nullptr, nullptr, &client) != noErr || client == 0)
                return {};

            MIDIEndpointRef endpoint = 0;
            auto cfName = name.toCFString();
            if (MIDISourceCreate(client, cfName, &endpoint) != noErr || endpoint == 0)
            {
                if (cfName != nullptr) CFRelease(cfName);
                MIDIClientDispose(client);
                return {};
            }
            if (cfName != nullptr) CFRelease(cfName);

            auto out = std::make_shared<TimestampedCoreMidiOut>();
            out->client = client;
            out->port = 0;
            out->endpoint = endpoint;
            out->isVirtual = true;
            return out;
        }

        static std::shared_ptr<TimestampedCoreMidiOut> open (const juce::String& deviceIdentifier,
                                                             const juce::String& preferredDeviceName)
        {
            if (deviceIdentifier.isEmpty())
                return {};

            // JUCE macOS identifiers are based on CoreMIDI UniqueIDs, but may be composed.
            // Common forms include:
            //  - "<endpointUID>"
            //  - "<deviceUID> <endpointUID>" (multi-entity devices)
            //  - comma-separated lists for connected/virtual endpoints

            const auto trimmed = deviceIdentifier.trim();

            auto getIntProp = [] (MIDIObjectRef obj, CFStringRef prop, SInt32& out) -> bool
            {
                out = 0;
                return (MIDIObjectGetIntegerProperty (obj, prop, &out) == noErr);
            };

            auto getStringProp = [] (MIDIObjectRef obj, CFStringRef prop) -> juce::String
            {
                CFStringRef str = nullptr;
                if (MIDIObjectGetStringProperty (obj, prop, &str) != noErr || str == nullptr)
                    return {};
                juce::String result = juce::String::fromCFString (str);
                CFRelease (str);
                return result;
            };

            auto getObjectUidString = [&] (MIDIObjectRef obj) -> juce::String
            {
                SInt32 uid = 0;
                if (getIntProp (obj, kMIDIPropertyUniqueID, uid))
                    return juce::String (uid);
                // Fallback: some objects may only expose a string UniqueID.
                return getStringProp (obj, kMIDIPropertyUniqueID);
            };

            auto getMidiObjectInfo = [&] (MIDIObjectRef obj) -> std::pair<juce::String, juce::String>
            {
                // Returns {name, identifier} like JUCE getMidiObjectInfo
                juce::String name;
                {
                    const auto n = getStringProp (obj, kMIDIPropertyName);
                    if (n.isNotEmpty())
                        name = n;
                }

                juce::String identifier = getObjectUidString (obj);
                return { name, identifier };
            };

            auto getEndpointInfo = [&] (MIDIEndpointRef endpoint, bool isExternal) -> std::pair<juce::String, juce::String>
            {
                if (endpoint == 0)
                    return {};

                MIDIEntityRef entity = 0;
                MIDIEndpointGetEntity (endpoint, &entity);

                // probably virtual
                if (entity == 0)
                    return getMidiObjectInfo (endpoint);

                auto result = getMidiObjectInfo (endpoint);

                // endpoint is empty - try the entity
                if (result.first.isEmpty() && result.second.isEmpty())
                    result = getMidiObjectInfo (entity);

                // now consider the device
                MIDIDeviceRef device = 0;
                MIDIEntityGetDevice (entity, &device);

                if (device != 0)
                {
                    const auto deviceInfo = getMidiObjectInfo (device);

                    if (! (deviceInfo.first.isEmpty() && deviceInfo.second.isEmpty()))
                    {
                        // If an external device has only one entity, throw away the endpoint name and
                        // just use the device name.
                        if (isExternal && MIDIDeviceGetNumberOfEntities (device) < 2)
                        {
                            result = deviceInfo;
                        }
                        else if (! result.first.startsWithIgnoreCase (deviceInfo.first))
                        {
                            // prepend the device name and identifier to the entity's
                            result.first = (deviceInfo.first + " " + result.first).trimEnd();
                            result.second = deviceInfo.second + " " + result.second;
                        }
                    }
                }

                return result;
            };

            auto getConnectedEndpointInfo = [&] (MIDIEndpointRef endpoint) -> std::pair<juce::String, juce::String>
            {
                if (endpoint == 0)
                    return {};

                juce::String outName;
                juce::String outIdentifier;

                CFDataRef connections = nullptr;
                MIDIObjectGetDataProperty (endpoint, kMIDIPropertyConnectionUniqueID, &connections);

                if (connections != nullptr)
                {
                    const int numConnections = (int) CFDataGetLength (connections) / (int) sizeof (MIDIUniqueID);

                    if (numConnections > 0)
                    {
                        auto* pid = reinterpret_cast<const SInt32*> (CFDataGetBytePtr (connections));

                        for (int i = 0; i < numConnections; ++i, ++pid)
                        {
                            auto id = (MIDIUniqueID) juce::ByteOrder::swapIfLittleEndian ((juce::uint32) *pid);

                            MIDIObjectRef connObject = 0;
                            MIDIObjectType connObjectType {};
                            if (MIDIObjectFindByUniqueID (id, &connObject, &connObjectType) == noErr)
                            {
                                std::pair<juce::String, juce::String> deviceInfo;

                                if (connObjectType == kMIDIObjectType_ExternalSource
                                    || connObjectType == kMIDIObjectType_ExternalDestination)
                                {
                                    deviceInfo = getEndpointInfo ((MIDIEndpointRef) connObject, true);
                                }
                                else
                                {
                                    deviceInfo = getMidiObjectInfo (connObject);
                                }

                                if (! deviceInfo.second.isEmpty())
                                {
                                    if (deviceInfo.first.isNotEmpty())
                                    {
                                        if (outName.isNotEmpty())
                                            outName += ", ";
                                        outName += deviceInfo.first;
                                    }

                                    if (outIdentifier.isNotEmpty())
                                        outIdentifier += ", ";
                                    outIdentifier += deviceInfo.second;
                                }
                            }
                        }
                    }
                }

                if (outName.isEmpty() || outIdentifier.isEmpty())
                {
                    const auto endpointInfo = getEndpointInfo (endpoint, false);
                    if (outName.isEmpty())
                        outName = endpointInfo.first;
                    if (outIdentifier.isEmpty())
                        outIdentifier = endpointInfo.second;
                }

                return { outName, outIdentifier };
            };

            auto extractSignedUniqueIds = [] (const juce::String& s) -> std::vector<SInt32>
            {
                std::vector<SInt32> ids;
                ids.reserve (4);

                const auto text = s.toStdString();
                const char* p = text.c_str();
                while (*p != 0)
                {
                    while (*p != 0 && ! (*p == '-' || (*p >= '0' && *p <= '9')))
                        ++p;
                    if (*p == 0)
                        break;

                    const char* start = p;
                    if (*p == '-')
                        ++p;
                    bool anyDigits = false;
                    while (*p >= '0' && *p <= '9')
                    {
                        anyDigits = true;
                        ++p;
                    }
                    if (! anyDigits)
                        continue;

                    const juce::String token (juce::String::fromUTF8 (start, (int) (p - start)));
                    ids.push_back ((SInt32) token.getIntValue());
                }

                return ids;
            };

            MIDIEndpointRef found = 0;

            // First try: treat any numeric tokens as possible endpoint UniqueIDs.
            // For JUCE identifiers like "deviceUID endpointUID", the endpointUID is typically last.
            // If the identifier is a comma-separated connection list (Audio MIDI Setup external devices),
            // the tokens refer to connected objects, not the destination endpoint's own UniqueID.
            const auto candidates = extractSignedUniqueIds (trimmed);
            if (! trimmed.containsChar (',') && ! candidates.empty())
            {
                const ItemCount n = MIDIGetNumberOfDestinations();
                for (auto it = candidates.rbegin(); it != candidates.rend() && found == 0; ++it)
                {
                    const SInt32 wantUid = *it;
                    for (ItemCount i = 0; i < n; ++i)
                    {
                        const MIDIEndpointRef ep = MIDIGetDestination (i);
                        if (ep == 0)
                            continue;

                        SInt32 uid = 0;
                        if (getIntProp (ep, kMIDIPropertyUniqueID, uid) && uid == wantUid)
                        {
                            found = ep;
                            break;
                        }
                    }
                }
            }

            // Second try: compare against JUCE-style composed identifier for each endpoint.
            if (found == 0)
            {
                const ItemCount n = MIDIGetNumberOfDestinations();
                for (ItemCount i = 0; i < n; ++i)
                {
                    const MIDIEndpointRef ep = MIDIGetDestination (i);
                    if (ep == 0)
                        continue;

                    // This matches JUCE's getConnectedEndpointInfo().identifier, which is what
                    // MidiOutput::getAvailableDevices() returns on macOS.
                    const auto epInfo = getConnectedEndpointInfo (ep);
                    if (epInfo.second.isNotEmpty() && epInfo.second.trim() == trimmed)
                    {
                        found = ep;
                        break;
                    }
                }
            }

            const auto trimmedName = preferredDeviceName.trim();
            if (found == 0 && trimmedName.isNotEmpty())
            {
                const ItemCount n = MIDIGetNumberOfDestinations();
                for (ItemCount i = 0; i < n; ++i)
                {
                    const MIDIEndpointRef ep = MIDIGetDestination (i);
                    if (ep == 0)
                        continue;

                    const auto epInfo = getConnectedEndpointInfo (ep);
                    if (epInfo.first.isNotEmpty() && epInfo.first.trim().equalsIgnoreCase (trimmedName))
                    {
                        found = ep;
                        break;
                    }
                }
            }

            if (found == 0)
                return {};

            MIDIClientRef client = 0;
            if (MIDIClientCreate(CFSTR("toolBoyClockCoreMIDI"), nullptr, nullptr, &client) != noErr || client == 0)
                return {};

            MIDIPortRef port = 0;
            if (MIDIOutputPortCreate(client, CFSTR("toolBoyClockOut"), &port) != noErr || port == 0)
            {
                MIDIClientDispose(client);
                return {};
            }

            auto out = std::make_shared<TimestampedCoreMidiOut>();
            out->client = client;
            out->port = port;
            out->endpoint = found;
            return out;
        }

        void sendBlock (const juce::MidiBuffer& buffer, double sampleRate) noexcept
        {
            if ((port == 0 && ! isVirtual) || endpoint == 0)
                return;
            if (! (sampleRate > 0.0))
                return;
            if (buffer.getNumEvents() <= 0)
                return;

            // Use CoreMIDI timestamps so the system schedules delivery, instead of
            // relying on a user-space timer thread (which may be throttled when
            // the host/app is backgrounded).
            const double ticksPerSecond = juce::Time::getHighResolutionTicksPerSecond();
            const double ticksPerSample = (ticksPerSecond > 0.0) ? (ticksPerSecond / sampleRate) : 0.0;
            const MIDITimeStamp base = (MIDITimeStamp) juce::Time::getHighResolutionTicks();

            auto* list = reinterpret_cast<MIDIPacketList*>(packetStorage.data());
            auto* pkt = MIDIPacketListInit(list);
            const size_t listCapacity = packetStorage.size();

            auto flush = [&]() noexcept
            {
                if (list->numPackets > 0)
                {
                    if (isVirtual)
                        MIDIReceived(endpoint, list);
                    else
                        MIDISend(port, endpoint, list);
                }
                pkt = MIDIPacketListInit(list);
            };

            for (const auto metadata : buffer)
            {
                const auto* data = metadata.data;
                const int numBytes = metadata.numBytes;
                const int samplePos = metadata.samplePosition;

                if (data == nullptr || numBytes <= 0)
                    continue;
                if (numBytes > 256)
                    continue; // shouldn't happen for clock/start/stop/SPP

                const MIDITimeStamp ts = base + (MIDITimeStamp) std::llround((double) samplePos * ticksPerSample);

                pkt = MIDIPacketListAdd(list, listCapacity, pkt, ts, (UInt16) numBytes, data);
                if (pkt == nullptr)
                {
                    flush();
                    pkt = MIDIPacketListAdd(list, listCapacity, pkt, ts, (UInt16) numBytes, data);
                    if (pkt == nullptr)
                        break; // give up rather than looping forever
                }
            }

            flush();
        }

        MIDIClientRef client { 0 };
        MIDIPortRef port { 0 };
        MIDIEndpointRef endpoint { 0 };
        bool isVirtual { false };

        std::array<uint8_t, 65536> packetStorage {};
};
#endif

//==============================================================================
ClockSyncAudioProcessor::ClockSyncAudioProcessor()
    : juce::AudioProcessor(
#if JucePlugin_IsMidiEffect
          BusesProperties() // MIDI effect: no audio buses
#elif JucePlugin_IsSynth
          // Synth/instrument: expose audio input so it can act as a thru/router when the host provides audio.
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)
              .withOutput("Aux Output", juce::AudioChannelSet::stereo(), true)
#else
          // Effect plugin (FX): provide both input and output buses
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)
              .withOutput("Aux Output", juce::AudioChannelSet::stereo(), true)
#endif
      ),
      parameters(*this, nullptr, juce::Identifier("ClockSyncParams"), createParameterLayout())
{
    // Initialize MIDI Remote defaults
    midiRemoteStart.store(1);   // C#-2
    midiRemoteStop.store(3);    // D#-2
    midiRemoteTrigger.store(6); // F#-2
    midiRemoteResync.store(8);  // G#-2
    midiRemoteGatedSync.store(10); // A#-2

    // Initialise MIDI-remote NOTE/CC modes from persisted state (default NOTE)
    midiRemoteStartIsCC.store((bool) parameters.state.getProperty("midiRemoteStartIsCC", false), std::memory_order_relaxed);
    midiRemoteStopIsCC.store((bool) parameters.state.getProperty("midiRemoteStopIsCC", false), std::memory_order_relaxed);
    midiRemoteTriggerIsCC.store((bool) parameters.state.getProperty("midiRemoteTriggerIsCC", false), std::memory_order_relaxed);
    midiRemoteResyncIsCC.store((bool) parameters.state.getProperty("midiRemoteResyncIsCC", false), std::memory_order_relaxed);
    midiRemoteGatedSyncIsCC.store((bool) parameters.state.getProperty("midiRemoteGatedSyncIsCC", false), std::memory_order_relaxed);

    // Initialize atomic caches from current state
    sppMode.store((bool)parameters.state.getProperty("ui.sppMode", false));
    linearShuffleMode.store((bool)parameters.state.getProperty("ui.linearShuffleMode", false));

    // Restore persisted MIDI Remote input selection (default: host/track MIDI)
    midiRemoteInDeviceId = parameters.state.getProperty("ui.midiRemoteInDeviceId").toString();
    updateMidiRemoteInDevice();

    // Seed internal random generator
    random.setSeedRandomly();

    // Cache typed parameter pointers to eliminate runtime string queries
    clockRateParamObj     = dynamic_cast<juce::AudioParameterChoice*>(parameters.getParameter(paramClockRateIndex));
    runParamObj           = dynamic_cast<juce::AudioParameterBool*>(parameters.getParameter(paramRun));
    resyncOffsetParamObj  = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(paramResyncOffsetStep));
    shuffleStepParamObj   = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(paramShuffleStep));
    shuffleLinearParamObj = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter(paramShuffleLinear));
    patternBarsParamObj          = dynamic_cast<juce::AudioParameterChoice*>(parameters.getParameter(paramPatternBars));
    patternStepsParamObj         = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(paramPatternSteps));
    triggerOffsetSamplesParamObj = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(paramTriggerOffsetSamples));

   #if ! CLOCKV3_DEMO
    isLicensedCached.store(toolboy_license::LicenseManager::isLicensed(licenseConfig_), std::memory_order_release);
   #endif

    // Register listener for UI updates
    parameters.state.addListener(this);
}

ClockSyncAudioProcessor::~ClockSyncAudioProcessor()
{
    cancelPendingUpdate();
    parameters.state.removeListener(this);
    activeMidiOut.store(nullptr, std::memory_order_release);
   #if JUCE_MAC
    activeCoreMidiOut.store(nullptr, std::memory_order_release);
   #endif
}

void ClockSyncAudioProcessor::handleAsyncUpdate()
{
    while (paramQueueReadIdx.load(std::memory_order_relaxed) != paramQueueWriteIdx.load(std::memory_order_acquire))
    {
        const size_t currentRead = paramQueueReadIdx.load(std::memory_order_relaxed);
        auto change = paramQueue[currentRead];
        paramQueueReadIdx.store((currentRead + 1) % kParamQueueSize, std::memory_order_release);

        if (change.param != nullptr)
        {
            change.param->beginChangeGesture();
            change.param->setValueNotifyingHost(change.value);
            change.param->endChangeGesture();
        }
    }
}

void ClockSyncAudioProcessor::updateMidiRemoteInDevice()
{
    // Close any existing device
    if (midiRemoteIn)
    {
        midiRemoteIn->stop();
        midiRemoteIn.reset();
    }

    // Empty => host/track MIDI (no CoreMIDI device opened)
    if (midiRemoteInDeviceId.isEmpty())
        return;

    const auto devices = juce::MidiInput::getAvailableDevices();
    for (const auto& dev : devices)
    {
        if (dev.identifier == midiRemoteInDeviceId)
        {
            midiRemoteIn = juce::MidiInput::openDevice(dev.identifier, &midiRemoteCollector);
            if (midiRemoteIn)
                midiRemoteIn->start();
            return;
        }
    }

    // If the persisted identifier is no longer available, fall back to host MIDI.
    midiRemoteInDeviceId.clear();
}

void ClockSyncAudioProcessor::handleRemoteMidiMessage(const juce::MidiMessage& msg)
{
    const auto publishRemoteTriggerVisual = [this]()
    {
        uiBlinkIdx1Step.store(juce::jlimit(1, 16, uiStep16.load(std::memory_order_relaxed)),
                              std::memory_order_relaxed);
    };

    const int gatedSyncMapping = midiRemoteGatedSync.load(std::memory_order_relaxed);
    const bool gatedSyncIsCC = midiRemoteGatedSyncIsCC.load(std::memory_order_relaxed);
    const bool gatedSyncActive = (gatedSyncMapping >= 0) && syncLatchEnabled.load(std::memory_order_relaxed);

    if (gatedSyncActive)
    {
        // Imperator Mode: Gated Sync overrides other transport controls
        if (gatedSyncIsCC)
        {
            if (msg.isController() && msg.getControllerNumber() == gatedSyncMapping)
            {
                const int v = msg.getControllerValue();
                isGateOpen.store(v >= 64, std::memory_order_relaxed);
            }
        }
        else
        {
            const bool isNoteOn  = msg.isNoteOn() && msg.getVelocity() > 0;
            const bool isNoteOff = msg.isNoteOff() || (msg.isNoteOn() && msg.getVelocity() == 0);

            if (isNoteOn && msg.getNoteNumber() == gatedSyncMapping)
            {
                isGateOpen.store(true, std::memory_order_relaxed);
                // Gate Mode: Immediate start handled in generateClockAndClick via runParamCached transition
            }
            else if (isNoteOff && msg.getNoteNumber() == gatedSyncMapping)
            {
                isGateOpen.store(false, std::memory_order_relaxed);
            }
        }
        return;
    }

    // Standard Remote Control
    // Start/Stop/Trigger/Resync (Note Inputs)
    if (msg.isNoteOn() && msg.getVelocity() > 0)
    {
        const int note = msg.getNoteNumber();
        const int remoteStart    = midiRemoteStart.load(std::memory_order_relaxed);
        const int remoteStop     = midiRemoteStop.load(std::memory_order_relaxed);
        const int remoteTrigger  = midiRemoteTrigger.load(std::memory_order_relaxed);
        const int remoteResync   = midiRemoteResync.load(std::memory_order_relaxed);
        const bool startIsCC     = midiRemoteStartIsCC.load(std::memory_order_relaxed);
        const bool stopIsCC      = midiRemoteStopIsCC.load(std::memory_order_relaxed);
        const bool triggerIsCC   = midiRemoteTriggerIsCC.load(std::memory_order_relaxed);
        const bool resyncIsCC    = midiRemoteResyncIsCC.load(std::memory_order_relaxed);

        if (! startIsCC && remoteStart >= 0 && note == remoteStart)
        {
            if (runParam && runParam->load(std::memory_order_relaxed) < 0.5f)
            {
                runParam->store(1.0f, std::memory_order_relaxed);
                enqueueParamChange(runParamObj, 1.0f);
            }
        }
        else if (! stopIsCC && remoteStop >= 0 && note == remoteStop)
        {
            if (runParam && runParam->load(std::memory_order_relaxed) > 0.5f)
            {
                runParam->store(0.0f, std::memory_order_relaxed);
                enqueueParamChange(runParamObj, 0.0f);
            }
        }
        else if (! triggerIsCC && remoteTrigger >= 0 && note == remoteTrigger)
        {
            publishRemoteTriggerVisual();
            requestTriggerOnce();
        }
        else if (! resyncIsCC && remoteResync >= 0 && note == remoteResync)
        {
            notifyResyncOffsetChanged();
        }
    }

    if (msg.isController())
    {
        const int cc = msg.getControllerNumber();
        const int val = msg.getControllerValue();

        // Action remotes can optionally use CC (trigger on non-zero value)
        {
            const int remoteStart    = midiRemoteStart.load(std::memory_order_relaxed);
            const int remoteStop     = midiRemoteStop.load(std::memory_order_relaxed);
            const int remoteTrigger  = midiRemoteTrigger.load(std::memory_order_relaxed);
            const int remoteResync   = midiRemoteResync.load(std::memory_order_relaxed);
            const bool startIsCC     = midiRemoteStartIsCC.load(std::memory_order_relaxed);
            const bool stopIsCC      = midiRemoteStopIsCC.load(std::memory_order_relaxed);
            const bool triggerIsCC   = midiRemoteTriggerIsCC.load(std::memory_order_relaxed);
            const bool resyncIsCC    = midiRemoteResyncIsCC.load(std::memory_order_relaxed);

            // Special case: START and STOP share the same CC controller number.
            if (startIsCC && stopIsCC
                && remoteStart >= 0 && remoteStop >= 0
                && remoteStart == remoteStop
                && cc == remoteStart)
            {
                if (val > 63)
                {
                    if (runParam && runParam->load(std::memory_order_relaxed) < 0.5f)
                    {
                        runParam->store(1.0f, std::memory_order_relaxed);
                        enqueueParamChange(runParamObj, 1.0f);
                    }
                }
                else if (val < 64)
                {
                    if (runParam && runParam->load(std::memory_order_relaxed) > 0.5f)
                    {
                        runParam->store(0.0f, std::memory_order_relaxed);
                        enqueueParamChange(runParamObj, 0.0f);
                    }
                }
            }
            else if (val > 0)
            {
                if (startIsCC && remoteStart >= 0 && cc == remoteStart)
                {
                    if (runParam && runParam->load(std::memory_order_relaxed) < 0.5f)
                    {
                        runParam->store(1.0f, std::memory_order_relaxed);
                        enqueueParamChange(runParamObj, 1.0f);
                    }
                }
                else if (stopIsCC && remoteStop >= 0 && cc == remoteStop)
                {
                    if (runParam && runParam->load(std::memory_order_relaxed) > 0.5f)
                    {
                        runParam->store(0.0f, std::memory_order_relaxed);
                        enqueueParamChange(runParamObj, 0.0f);
                    }
                }
                else if (triggerIsCC && remoteTrigger >= 0 && cc == remoteTrigger)
                {
                    publishRemoteTriggerVisual();
                    requestTriggerOnce();
                }
                else if (resyncIsCC && remoteResync >= 0 && cc == remoteResync)
                {
                    notifyResyncOffsetChanged();
                }
            }
        }

        const int remoteOffset   = midiRemoteOffset.load(std::memory_order_relaxed);
        const int remoteShuffle  = midiRemoteShuffle.load(std::memory_order_relaxed);
        const int remoteClockDiv = midiRemoteClockDiv.load(std::memory_order_relaxed);
        const int remoteAutoFill = midiRemoteAutoFill.load(std::memory_order_relaxed);

        if (remoteOffset >= 0 && cc == remoteOffset)
        {
            int step = juce::jmap(val, 0, 127, 1, 16);
            enqueueParamChange(resyncOffsetParamObj, (float) (step - 1) / 15.0f);
        }
        else if (remoteShuffle >= 0 && cc == remoteShuffle)
        {
            if (isLinearShuffleEnabled())
            {
                enqueueParamChange(shuffleLinearParamObj, (float) val / 127.0f);
            }
            else
            {
                int step = juce::jmap(val, 0, 127, 1, 7);
                enqueueParamChange(shuffleStepParamObj, (float) (step - 1) / 6.0f);
            }
        }
        else if (remoteClockDiv >= 0 && cc == remoteClockDiv)
        {
            int idx = juce::jmap(val, 0, 127, 0, 3);
            enqueueParamChange(clockRateParamObj, (float) idx / 3.0f);
        }
        else if (remoteAutoFill >= 0 && cc == remoteAutoFill)
        {
            int idx = juce::jmap(val, 0, 127, 0, 8);
            enqueueParamChange(patternBarsParamObj, (float) idx / 8.0f);
        }
    }
}

// Helper: current shuffle shift in quarter-notes.
// - Quantized (TR-909): (step-1) * 1/48 when linear mode OFF
// - Linear: normalized slider [0..1] * 1/8 when linear mode ON
double ClockSyncAudioProcessor::getCurrentShiftQ(double barLenQ) const
{
    if (std::fabs(barLenQ - 4.0) > 1e-6)
        return 0.0; // only apply swing math for 4/4 bars
    const bool linearOn = isLinearShuffleEnabled();
    if (! linearOn)
    {
        if (currentShuffleStep <= 1) return 0.0;
        return (double)(currentShuffleStep - 1) * (1.0 / 48.0);
    }
    // Use the dedicated linear shuffle parameter (0..1) for continuous mapping
    const float norm = getEffectiveLinearShuffleValue();
    return (double) norm * (1.0 / 8.0);
}

bool ClockSyncAudioProcessor::isLinearShuffleEnabled() const
{
    return linearShuffleMode.load(std::memory_order_relaxed);
}

float ClockSyncAudioProcessor::getLinearShuffleValue() const
{
    if (shuffleLinearParamObj != nullptr)
        return juce::jlimit(0.0f, 1.0f, shuffleLinearParamObj->get());
    return 0.0f;
}

float ClockSyncAudioProcessor::getEffectiveLinearShuffleValue() const
{
    // When linear mode is enabled, we apply parameter changes only at unswung 1/8 boundaries.
    // Use the cached effective value so mid-pair changes don't perturb timing until boundary.
    return currentLinearShuffleNorm;
}

void ClockSyncAudioProcessor::requestTriggerOnce()
{
    // Original behaviour (clock_org): Arm Start at next 1/16 grid boundary (immediate retrigger).
    // Whether a bar+offset restart follows is decided later based on triggerModeEnabled.
    triggerArmedForNextSixteenth.store(true, std::memory_order_relaxed);
    // Manual trigger: allow bar restarts immediately
    deferBarRestartUntilPattern.store(false, std::memory_order_relaxed);
    
    // If trigger mode is enabled, also schedule a bar restart (resync)
    if (triggerModeEnabled.load(std::memory_order_relaxed))
    {
        pendingBarRestart.store(true, std::memory_order_relaxed);
        uiNextRestartPending.store(true, std::memory_order_relaxed);
    }
}

void ClockSyncAudioProcessor::setTriggerModeEnabled(bool enabled)
{
    triggerModeEnabled.store(enabled, std::memory_order_relaxed);
    // When disabling the trigger-mode gate, clear any pending restart/resync
    // so idx8 acts as the master gate for resync behaviour.
    if (! enabled)
    {
        pendingBarRestart.store(false, std::memory_order_relaxed);
        uiNextRestartPending.store(false, std::memory_order_relaxed);
        // Clear any already-scheduled resync target
        resyncPending.store(false, std::memory_order_relaxed);
        resyncTargetBar.store(-1, std::memory_order_relaxed);
        resyncTargetStep.store(-1, std::memory_order_relaxed);
    }
}

void ClockSyncAudioProcessor::notifyResyncOffsetChanged()
{
    // Only schedule a pending bar restart if trigger mode is enabled
    if (triggerModeEnabled.load(std::memory_order_relaxed))
    {
        resyncFromOffsetPending.store(true, std::memory_order_relaxed);
        uiNextRestartPending.store(true, std::memory_order_relaxed);
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ClockSyncAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        paramClockRateIndex, "Clock Rate", juce::StringArray{ "1/32", "1/16", "1/8", "1/4" }, 1));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramClickLevelDb, "Click Level (dB)", juce::NormalisableRange<float>(-12.0f, 0.0f), -6.0f));

    // Click rate: discrete stages 0..6 (0 = off). Default changed to 0 so rotary starts at OFF.
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        paramClickRate, "Click Rate", 0, 6, 0));

    // Pulse width (ms): 1–20, automatable
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        "pulseWidthMs", "Pulse Width (ms)", 1, 20, 1));

    // Click variant: false = 1-sample spike, true = 1ms pulse
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        paramClickPulse, "Click Pulse Variant", true));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        paramRun, "Run", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        paramClockWhileStopped, "Clock While Stopped", false));

    // Trigger mode: when enabled, a manual trigger also schedules a bar/offset restart
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        paramTriggerModeEnabled, "Trigger Mode", true));

    // Step offset within the bar where restarts occur (1..16). 1 means at bar start; 5 means 4/16 into next bar.
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        paramResyncOffsetStep, "Resync Offset Step", 1, 16, 1));

    // Shuffle (swing) intensity: 1..7 (TR-909 style). 1 = none, 7 = maximum (2nd 16th shifted to next 32nd).
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        paramShuffleStep, "Shuffle Step", 1, 7, 1));
    // Linear shuffle amount (0..1), used only when ui.linearShuffleMode is ON
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramShuffleLinear, "Shuffle Linear", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    // Sync Latch Enabled (GATE mode)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        paramSyncLatchEnabled, "Sync Latch Enabled", false));

    // Pattern bars choice: OFF,1,2,4,8,16,32,64,RND (store as indices 0..8)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        paramPatternBars, "Pattern Bars", juce::StringArray{ "OFF","1","2","4","8","16","32","64","RND" }, 0));
    // Pattern steps bitmask (0..65535) persisted; editor updates via setPatternSteps
    // Default changed to 0x0000 so pattern starts fully empty; previously 0x0010 enabled storedIndex=4 (logical step 1) implicitly.
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        paramPatternSteps, "Pattern Steps Mask", 0, 65535, 0x0000)); // default empty, user must activate steps explicitly

    // Trigger timing offset in samples (-2000..+2000, default 0) for manual & auto triggers
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        paramTriggerOffsetSamples, "Trigger Offset", -2048, 256, 0));

    return { params.begin(), params.end() };
}

//==============================================================================
void ClockSyncAudioProcessor::prepareToPlay(double sr, int /*samplesPerBlock*/)
{
    currentSampleRate = sr > 0.0 ? sr : 44100.0;
    #if CLOCKV3_DEMO
     ensureDemoSessionStartTickMs();
    #endif
    demoStopSent.store(false, std::memory_order_relaxed);
    midiRemoteCollector.reset(currentSampleRate);
    lastWasPlaying = false;
    lastTickIndex = std::numeric_limits<long long>::min();
    lastClickIndex = std::numeric_limits<long long>::min();
    lastClickRateCached = -1;
    pendingRateIndex = -1;
    currentRateIndex = 1; // 1/16 => normal speed (24)
    uiActiveRateIndex.store(1, std::memory_order_relaxed);
    // Default: armed but not running; will emit Start at scheduled offset step (or bar start)
    pendingStart = true;
    runActive = false;
    // No forced wrap to next bar in original behaviour
    forceNextBarStart = false;
    isGateOpen.store(false, std::memory_order_relaxed);
    
    // Initialize lastRunParam to current state to avoid spurious stop on startup
    if (runParam)
        lastRunParam = runParam->load() > 0.5f;
    else
        lastRunParam = false;

    clickEnv = 0.0f;
    runSignalDropSamples = 0;
    uiClockCounter.store(0, std::memory_order_relaxed);
    uiIsRunning.store(false, std::memory_order_relaxed);
    uiPendingStart.store(true, std::memory_order_relaxed);
    uiNextRestartPending.store(false, std::memory_order_relaxed);
    triggerArmedForNextSixteenth.store(false, std::memory_order_relaxed);
    pendingBarRestart.store(false, std::memory_order_relaxed);
    // Initial trigger mode from parameter
    if (auto* triggerVal = parameters.getRawParameterValue(paramTriggerModeEnabled))
    {
        const bool triggerMode = triggerVal->load() > 0.5f;
        triggerModeEnabled.store(triggerMode, std::memory_order_relaxed);
        lastTriggerModeEnabled = triggerMode;
    }
    else
    {
        triggerModeEnabled.store(true, std::memory_order_relaxed);
        lastTriggerModeEnabled = true;
    }

    // Initialize Sync Latch from parameter
    if (auto* syncLatchVal = parameters.getRawParameterValue(paramSyncLatchEnabled))
    {
        syncLatchEnabled.store(syncLatchVal->load() > 0.5f, std::memory_order_relaxed);
    }
    else
    {
        syncLatchEnabled.store(false, std::memory_order_relaxed);
    }

    // Pulse width param pointer (raw, not unique_ptr)
    pulseWidthParam = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter("pulseWidthMs"));
    if (pulseWidthParam)
        pulseWidthMs.store(pulseWidthParam->get(), std::memory_order_relaxed);

    // Cache typed parameter pointers to eliminate runtime string queries
    clockRateParamObj     = dynamic_cast<juce::AudioParameterChoice*>(parameters.getParameter(paramClockRateIndex));
    runParamObj           = dynamic_cast<juce::AudioParameterBool*>(parameters.getParameter(paramRun));
    resyncOffsetParamObj  = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(paramResyncOffsetStep));
    shuffleStepParamObj   = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(paramShuffleStep));
    shuffleLinearParamObj = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter(paramShuffleLinear));
    patternBarsParamObj          = dynamic_cast<juce::AudioParameterChoice*>(parameters.getParameter(paramPatternBars));
    patternStepsParamObj         = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(paramPatternSteps));
    triggerOffsetSamplesParamObj = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter(paramTriggerOffsetSamples));
    if (triggerOffsetSamplesParamObj != nullptr)
        triggerOffsetSamples.store(triggerOffsetSamplesParamObj->get(), std::memory_order_relaxed);

    // Cache raw parameter pointers
    runParam = parameters.getRawParameterValue(paramRun);
    clockWhileStoppedParam = parameters.getRawParameterValue(paramClockWhileStopped);
    clickPulseParam = parameters.getRawParameterValue(paramClickPulse);
    clickRateParam = parameters.getRawParameterValue(paramClickRate);

    suppressUntilRestart = false;
    firstBlock = true;
    if (resyncOffsetParamObj != nullptr)
        lastOffsetStep = juce::jlimit(1, 16, resyncOffsetParamObj->get());
    if (shuffleStepParamObj != nullptr)
        currentShuffleStep = juce::jlimit(1, 7, shuffleStepParamObj->get());
    pendingShuffleStep = -1;
    applyShuffleAtPPQ = -1.0;
    // Initialise linear shuffle cached value from parameter
    currentLinearShuffleNorm = getLinearShuffleValue();
    pendingLinearShuffleNorm = -1.0f;
    applyLinearShuffleAtPPQ = -1.0;

    updateDerivedParams();
    updateExternalOut();
    // Reset persistent accumulators on prepare (sample-rate change / reinitialise)
    resetAccumulators();

    // Reset pattern array (zero heap allocations)
    pendingPatternCount = 0;
    pendingPatternPPQ.fill(0.0);
}

void ClockSyncAudioProcessor::releaseResources()
{
    activeMidiOut.store(nullptr, std::memory_order_release);
   #if JUCE_MAC
    activeCoreMidiOut.store(nullptr, std::memory_order_release);
   #endif
}

bool ClockSyncAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    // MIDI effect: no audio buses
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::disabled()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::disabled();
#elif JucePlugin_IsSynth
    // Synth/instrument: allow mono/stereo output, and optionally mono/stereo input (for input-through routing).
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const auto aux = layouts.getChannelSet(false, 1);

    if (out == juce::AudioChannelSet::disabled())
        return false;

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (in != juce::AudioChannelSet::disabled()
        && in != juce::AudioChannelSet::mono()
        && in != juce::AudioChannelSet::stereo())
        return false;

    // Allow Aux to be disabled or stereo
    if (aux != juce::AudioChannelSet::disabled() && aux != juce::AudioChannelSet::stereo())
        return false;

    return true;
#else
    // Effect: require matching input/output channel layouts (mono or stereo)
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const auto aux = layouts.getChannelSet(false, 1);

    if (in == juce::AudioChannelSet::disabled() || out == juce::AudioChannelSet::disabled())
        return false;

    if (in != out)
        return false;

    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;

    // Allow Aux to be disabled or stereo
    if (aux != juce::AudioChannelSet::disabled() && aux != juce::AudioChannelSet::stereo())
        return false;

    return true;
#endif
}

int ClockSyncAudioProcessor::getClockResolution() const
{
    // Map current rate index (choice param) to pulses-per-quarter note.
    // Index mapping (clockRateIndex):
    // 0 => 1/32 (double speed)  => 48 PPQN
    // 1 => 1/16 (normal speed)  => 24 PPQN
    // 2 => 1/8  (half speed)    => 12 PPQN
    // 3 => 1/4  (quarter speed) => 6 PPQN
    switch (currentRateIndex)
    {
        case 0: return 48;
        case 1: return 24;
        case 2: return 12;
        case 3: return 6;
        default: return 24;
    }
}

int ClockSyncAudioProcessor::computeDemoRemainingSeconds(juce::uint32 nowTickMs) const noexcept
{
   #if CLOCKV3_DEMO
    const auto startTickMs = ensureDemoSessionStartTickMs();

    const juce::uint32 elapsedMs = nowTickMs >= startTickMs
        ? (nowTickMs - startTickMs)
        : (std::numeric_limits<juce::uint32>::max() - startTickMs + nowTickMs + 1u);

    return juce::jmax(0, getDemoSessionRuntimeSeconds() - (int) (elapsedMs / 1000u));
   #else
    juce::ignoreUnused(nowTickMs);
    return 0;
   #endif
}

int ClockSyncAudioProcessor::getDemoRemainingSecondsUI() const
{
   #if CLOCKV3_DEMO
    return computeDemoRemainingSeconds(juce::Time::getMillisecondCounter());
   #else
    return 0;
   #endif
}

int ClockSyncAudioProcessor::getDemoRuntimeMinutesUI() const
{
   #if CLOCKV3_DEMO
    return getDemoSessionRuntimeSeconds() / 60;
   #else
    return 0;
   #endif
}

int ClockSyncAudioProcessor::getNextDemoRuntimeMinutesUI() const
{
   #if CLOCKV3_DEMO
    return getNextDemoRuntimeMinutes();
   #else
    return 0;
   #endif
}

int ClockSyncAudioProcessor::getDemoCooldownRemainingSecondsUI() const
{
   #if CLOCKV3_DEMO
    return getDemoCooldownRemainingSeconds();
   #else
    return -1;
   #endif
}

bool ClockSyncAudioProcessor::isDemoExpiredUI() const
{
   #if CLOCKV3_DEMO
    return getDemoRemainingSecondsUI() <= 0;
   #else
    return false;
   #endif
}

void ClockSyncAudioProcessor::updateDerivedParams()
{
    const auto levelDb = parameters.getRawParameterValue(paramClickLevelDb)->load();
    if (! std::isfinite(lastClickLevelDbCached) || levelDb != lastClickLevelDbCached)
    {
        clickGainLinear = juce::Decibels::decibelsToGain(levelDb);
        lastClickLevelDbCached = levelDb;
    }
    // Mirror trigger mode parameter into fast atomic for RT checks
    if (auto* tv = parameters.getRawParameterValue(paramTriggerModeEnabled))
        triggerModeEnabled.store(tv->load() > 0.5f, std::memory_order_relaxed);

    // Mirror Sync Latch parameter
    if (auto* slv = parameters.getRawParameterValue(paramSyncLatchEnabled))
        syncLatchEnabled.store(slv->load() > 0.5f, std::memory_order_relaxed);

    // Update pulseWidthMs from parameter
    if (pulseWidthParam)
        pulseWidthMs.store(pulseWidthParam->get(), std::memory_order_relaxed);

    updatePatternParams();
}

void ClockSyncAudioProcessor::resetAccumulators()
{
    fracAccPrimaryBefore = 0.0;
    fracAccPrimaryAfter = 0.0;
    lastBpmForAcc = -1.0;
    lastClickIndex = std::numeric_limits<long long>::min();
    lastClickRateCached = -1;
}

bool ClockSyncAudioProcessor::isDuplicateStart(long long barForStart, int stepForStart, int sampleOffset, int toleranceSamples) const
{
    // Exact bar+step duplicate
    if (barForStart == lastStartBar.load(std::memory_order_relaxed) && stepForStart == lastStartStep.load(std::memory_order_relaxed))
        return true;
    // Per-block near-simultaneous emission tolerance (collapse duplicates within a few samples)
    if (sampleOffset >= 0)
    {
        if (lastPatternStartSampleInBlock >= 0 && std::abs(sampleOffset - lastPatternStartSampleInBlock) <= toleranceSamples)
            return true;
        if (lastBarRestartStartSampleInBlock >= 0 && std::abs(sampleOffset - lastBarRestartStartSampleInBlock) <= toleranceSamples)
            return true;
    }
    return false;
}

void ClockSyncAudioProcessor::updateExternalOut()
{
   #if JUCE_MAC
    // macOS: Use CoreMIDI timestamped packets (scheduled by the OS) so clock
    // remains stable even when the host/app is backgrounded.
    std::shared_ptr<TimestampedCoreMidiOut> newCore;
    if (externalDeviceId.isNotEmpty())
    {
        newCore = TimestampedCoreMidiOut::open(externalDeviceId, externalDeviceName);
    }

    {
        juce::ScopedLock sl(midiOutLock);
        activeCoreMidiOut.store(newCore.get(), std::memory_order_release);
        externalCoreMidiOut = newCore;
        activeMidiOut.store(nullptr, std::memory_order_release);
        externalMidiOut.reset();
    }
   #else
    // Windows and other platforms:
    // With JUCE 9 and Windows MIDI Services enabled (NEEDS_WINDOWS_MIDI_SERVICES),
    // createNewDevice creates a native Windows MIDI Services virtual endpoint
    // providing Universal MIDI Packet (UMP) transport and multi-client access.
    std::shared_ptr<juce::MidiOutput> newOut;
    if (externalDeviceId.isNotEmpty())
    {
        if (externalDeviceId == "__VIRTUAL_PORT__")
        {
            auto uniqueOut = juce::MidiOutput::createNewDevice("Clock v3 Virtual Out");
            if (uniqueOut)
            {
                newOut = std::shared_ptr<juce::MidiOutput>(uniqueOut.release());
                newOut->startBackgroundThread();
            }
        }
        else
        {
            auto uniqueOut = juce::MidiOutput::openDevice(externalDeviceId);
            if (uniqueOut)
            {
                newOut = std::shared_ptr<juce::MidiOutput>(uniqueOut.release());
                newOut->startBackgroundThread();
            }
        }
    }

    {
        juce::ScopedLock sl(midiOutLock);
        activeMidiOut.store(newOut.get(), std::memory_order_release);
        externalMidiOut = newOut;
    }
   #endif
}

void ClockSyncAudioProcessor::setExternalDevice(const juce::String& id, const juce::String& name)
{
    const auto trimmedId = id.trim();
    const auto trimmedName = name.trim();

    if (externalDeviceId == trimmedId && externalDeviceName == trimmedName)
        return;

    externalDeviceId = trimmedId;
    externalDeviceName = trimmedId.isNotEmpty() ? trimmedName : juce::String{};
    // Persist into the state tree for recall
    parameters.state.setProperty("externalDeviceId", externalDeviceId, nullptr);
    parameters.state.setProperty("externalDeviceName", externalDeviceName, nullptr);
    // Reopen according to new device selection
    updateExternalOut();
}

void ClockSyncAudioProcessor::setExternalDeviceId(const juce::String& id)
{
    setExternalDevice(id, {});
}

//==============================================================================
void ClockSyncAudioProcessor::setPulseWidthMs(int ms)
{
    if (pulseWidthParam)
        pulseWidthParam->setValueNotifyingHost((float)juce::jlimit(1, 20, ms) / 20.0f);
}

int ClockSyncAudioProcessor::getPulseWidthMs() const
{
    if (pulseWidthParam)
        return pulseWidthParam->get();
    return pulseWidthMs.load(std::memory_order_relaxed);
}
void ClockSyncAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // MIDI Remote control input source:
    // - empty selection => use host/track MIDI
    // - otherwise => use selected CoreMIDI device
    const bool useHostMidiForRemote = midiRemoteInDeviceId.isEmpty();

    if (useHostMidiForRemote)
    {
        for (const auto metadata : midi)
            handleRemoteMidiMessage(metadata.getMessage());
    }
    else
    {
        juce::MidiBuffer remoteIn;
        midiRemoteCollector.removeNextBlockOfMessages(remoteIn, buffer.getNumSamples());
        for (const auto metadata : remoteIn)
        {
            const auto message = metadata.getMessage();

            // External remote devices should still pass their note traffic through
            // to the plugin's MIDI output while remaining available as control input.
            if (message.isNoteOnOrOff())
                midi.addEvent(message, metadata.samplePosition);

            handleRemoteMidiMessage(metadata.getMessage());
        }
    }

    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    // Preserve incoming audio (passthrough). Only clear channels that have
    // no corresponding input (e.g. mismatched layouts) — current layout policy requires match.
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    // If host gives mono in -> stereo out (future-proof), duplicate channel 0.
    if (numIn == 1 && numOut > 1)
    {
        for (int ch = 1; ch < numOut; ++ch)
            buffer.copyFrom(ch, 0, buffer, 0, 0, numSamples);
    }
    // If there are extra output channels beyond inputs, clear them to silence.
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear(ch, 0, numSamples);

    juce::AudioPlayHead::CurrentPositionInfo pos;
    bool hasPos = false;
    if (auto* ph = getPlayHead())
    {
        if (auto opt = ph->getPosition())
        {
            hasPos = true;
            pos.resetToDefault();

            if (auto sig = opt->getTimeSignature())
            {
                pos.timeSigNumerator   = sig->numerator;
                pos.timeSigDenominator = sig->denominator;
            }

            if (auto loop = opt->getLoopPoints())
            {
                pos.ppqLoopStart = loop->ppqStart;
                pos.ppqLoopEnd   = loop->ppqEnd;
            }

            if (auto frame = opt->getFrameRate())
                pos.frameRate = *frame;

            if (auto timeInSeconds = opt->getTimeInSeconds())
                pos.timeInSeconds = *timeInSeconds;

            if (auto lastBarStartPpq = opt->getPpqPositionOfLastBarStart())
                pos.ppqPositionOfLastBarStart = *lastBarStartPpq;

            if (auto ppqPosition = opt->getPpqPosition())
                pos.ppqPosition = *ppqPosition;

            if (auto originTime = opt->getEditOriginTime())
                pos.editOriginTime = *originTime;

            if (auto bpm = opt->getBpm())
                pos.bpm = *bpm;

            if (auto timeInSamples = opt->getTimeInSamples())
                pos.timeInSamples = (juce::int64) *timeInSamples;

            pos.isPlaying   = opt->getIsPlaying();
            pos.isRecording = opt->getIsRecording();
            pos.isLooping   = opt->getIsLooping();
            // Cache host-provided bar number for use by the UI/editor without
            // calling getPlayHead() from the message thread (avoid race with ScopedPlayHead).
            {
                int barNumber = -1;
                if (auto barOpt = opt->getBarCount())
                {
                    barNumber = (int) *barOpt;
                }
                else if (auto ppqBarStartOpt = opt->getPpqPositionOfLastBarStart())
                {
                    double ppqBarStart = *ppqBarStartOpt; // quarter-note units
                    double quartersPerBar = 4.0; // default assume 4/4
                    if (auto tsOpt = opt->getTimeSignature())
                    {
                        auto ts = *tsOpt;
                        if (ts.denominator > 0)
                            quartersPerBar = ts.numerator * (4.0 / (double) ts.denominator);
                    }
                    if (quartersPerBar > 0.0)
                    {
                        double barIndexD = std::floor(ppqBarStart / quartersPerBar);
                        barNumber = 1 + (int) std::max(0.0, barIndexD); // 1-based
                    }
                }
                uiExternalBarNumber.store(barNumber, std::memory_order_relaxed);
            }
            // Cache time signature and last bar start PPQ for UI/diagnostics (atomics for message-thread access)
            uiTimeSigNumerator.store(pos.timeSigNumerator, std::memory_order_relaxed);
            uiTimeSigDenominator.store(pos.timeSigDenominator, std::memory_order_relaxed);
            uiPpqPositionOfLastBarStart.store(pos.ppqPositionOfLastBarStart, std::memory_order_relaxed);
        }
    }

    if (! hasPos)
    {
        // No transport info; nothing to schedule for audio clicks.
        clickEnv = 0.0f;
        // Clear cached external bar number when no host transport info is available
        uiExternalBarNumber.store(-1, std::memory_order_relaxed);
        return;
    }

    // Determine target buffer for click/signal
    auto mainBus = getBusBuffer(buffer, false, 0);
    auto auxBus = getBusBuffer(buffer, false, 1);
    
    juce::AudioBuffer<float>* targetBuffer = &mainBus;
    if (auxBus.getNumChannels() >= 2)
    {
        targetBuffer = &auxBus;
    }

    generateClockAndClick(pos, *targetBuffer, midi);
}

void ClockSyncAudioProcessor::generateClockAndClick(const juce::AudioPlayHead::CurrentPositionInfo& pos,
                                                    juce::AudioBuffer<float>& buffer,
                                                    juce::MidiBuffer& midi)
{
    // Thread Safety Note:
    // This function runs on the audio thread. Parameter access is optimised by using
    // cached raw std::atomic<float>* pointers (runParam, clickRateParam, etc.) initialized
    // in prepareToPlay(). Critical flags like audioPulsesEnabled use acquire/release
    // semantics where necessary to synchronise with state changes.

    // Acquire local raw pointer to active external MIDI output without locks or shared_ptr ref-counting.
    juce::MidiOutput* localOut = activeMidiOut.load(std::memory_order_acquire);
   #if JUCE_MAC
    TimestampedCoreMidiOut* localCoreOut = activeCoreMidiOut.load(std::memory_order_acquire);
   #endif

    const bool haveExternalOut = (localOut != nullptr)
       #if JUCE_MAC
        || (localCoreOut != nullptr)
       #endif
        ;

    const int numSamples = buffer.getNumSamples();
    // Detect callback lateness under CPU pressure.
    // If the audio callback arrives late, any MIDI clock derived from it is inherently late.
    // Additionally, timed MIDI sending uses a background thread which may be delayed too.
    const double wallNowMs = juce::Time::getMillisecondCounterHiRes();
    const double expectedBlockMs = (currentSampleRate > 0.0)
        ? (1000.0 * (double) numSamples / currentSampleRate)
        : 0.0;
    const double wallDeltaMs = (lastClockBlockWallMs >= 0.0) ? (wallNowMs - lastClockBlockWallMs) : expectedBlockMs;
    lastClockBlockWallMs = wallNowMs;
    const bool callbackLate = (expectedBlockMs > 0.0) && (wallDeltaMs > expectedBlockMs * 1.5);
    startSampleForRunSignal = -1;
    bool wasRunningAtStart = audioPulsesEnabled.load(std::memory_order_relaxed);
    
    // Detect Host Start (transition from stopped to playing)
    if (pos.isPlaying && !lastWasPlaying)
        startSampleForRunSignal = 0;

    // Apply any carried-over pulse tail from the previous block at the start of this block (left channel only).
    if (clickHoldRemainingSamples > 0)
    {
        const int carry = juce::jmin(clickHoldRemainingSamples, numSamples);
        const int leftCh = 0;
        if (leftCh < buffer.getNumChannels())
        {
            for (int s = 0; s < carry; ++s)
                buffer.addSample(leftCh, s, 1.0f);
        }
        clickHoldRemainingSamples -= carry;
    }
    
    // Clear per-block pattern/bar-start markers
    lastPatternStartSampleInBlock = -1;
    lastBarRestartStartSampleInBlock = -1;
    // Update derived params (fast, read atomics)
    updateDerivedParams();

    // Centralized external MIDI buffer: collect all external messages here and send once at function end.
    juce::MidiBuffer extClock;

    const auto flushExternalClock = [&]()
    {
        if (extClock.getNumEvents() <= 0)
            return;

       #if JUCE_MAC
        if (localCoreOut != nullptr)
        {
            localCoreOut->sendBlock(extClock, currentSampleRate);
        }
        else if (localOut != nullptr)
        {
            localOut->sendBlockOfMessages(extClock, juce::Time::getMillisecondCounterHiRes(), currentSampleRate);
        }
       #else
        if (localOut != nullptr)
            localOut->sendBlockOfMessages(extClock, juce::Time::getMillisecondCounterHiRes(), currentSampleRate);
       #endif
    };

   #if CLOCKV3_DEMO
    if (computeDemoRemainingSeconds(juce::Time::getMillisecondCounter()) <= 0)
    {
        clickEnv = 0.0f;
        clickHoldRemainingSamples = 0;
        runActive = false;
        pendingStart = false;
        pendingBarRestart.store(false, std::memory_order_relaxed);
        pendingPatternRestartTargetBar.store(-1, std::memory_order_relaxed);
        resyncPending.store(false, std::memory_order_relaxed);
        resyncTargetBar.store(-1, std::memory_order_relaxed);
        resyncTargetStep.store(-1, std::memory_order_relaxed);
        uiIsRunning.store(false, std::memory_order_relaxed);
        uiPendingStart.store(false, std::memory_order_relaxed);
        uiNextRestartPending.store(false, std::memory_order_relaxed);
        audioPulsesEnabled.store(false, std::memory_order_relaxed);
        suppressUntilRestart = false;

        if (! demoStopSent.exchange(true, std::memory_order_acq_rel))
        {
            const auto stopMsg = juce::MidiMessage::midiStop();
            midi.addEvent(stopMsg, 0);
            extClock.addEvent(stopMsg, 0);
        }

        flushExternalClock();
        lastWasPlaying = pos.isPlaying;
        return;
    }
   #endif

    // Rising edge immediate pattern scheduling: when user activates first step (mask from 0->non-zero)
    // and interval mode is active, prepare current bar and attempt in-block emission so they hear
    // pattern Starts without waiting for next interval bar.
    if (patternMaskJustActivated.load(std::memory_order_relaxed))
    {
        patternMaskJustActivated.store(false, std::memory_order_relaxed);
        const int mode = patternBarsMode.load(std::memory_order_relaxed);
        if (mode > 0) // interval/RND active
        {
            const double barLenQ = (pos.timeSigNumerator > 0 && pos.timeSigDenominator > 0)
                ? (4.0 * (double) pos.timeSigNumerator / (double) pos.timeSigDenominator)
                : 4.0;
            const double barStartPPQ = pos.ppqPositionOfLastBarStart;
            preparePatternForBar(barStartPPQ, barLenQ);
            tryFirePattern(pos.ppqPosition, barLenQ, barStartPPQ, numSamples, midi, extClock, true, /*preview=*/true);

        }
    }
    if (triggerOffsetChanged.exchange(false, std::memory_order_acq_rel))
    {
        const double barLenQ = (pos.timeSigNumerator > 0 && pos.timeSigDenominator > 0)
            ? (4.0 * (double) pos.timeSigNumerator / (double) pos.timeSigDenominator)
            : 4.0;
        const double barStartPPQ = pos.ppqPositionOfLastBarStart;
        rescheduleRemainingPatternTargets(barStartPPQ, barLenQ, pos.ppqPosition);
    }

    // --- Cache parameter reads to avoid repeated dynamic_casts and lookups in inner loops ---
    const int clickRateCached = clickRateParam ? juce::jlimit(0, 6, (int)clickRateParam->load(std::memory_order_relaxed)) : 0;
    const bool clickEnabled = (clickRateCached > 0);
    const bool clickPulseCached = clickPulseParam->load(std::memory_order_relaxed) > 0.5f;

    // Only gate click (audio pulse) output by clickEnabled and runActive (Run button ON) with Start gating


    if (clockRateParamObj != nullptr)
    {
        const int desired = clockRateParamObj->getIndex();
        if (desired != currentRateIndex && pendingRateIndex != desired)
        {
            pendingRateIndex = desired;
            uiNextRestartPending.store(true, std::memory_order_relaxed);
            // Schedule a realign (one-shot Start at next bar start) on rate change instead of emitting a rateChange Start.
            // This ensures external devices re-sync cleanly without producing a non-pattern Start inside current bar.
            const double barLenQLoc = (pos.timeSigNumerator > 0 && pos.timeSigDenominator > 0)
                ? (4.0 * (double) pos.timeSigNumerator / (double) pos.timeSigDenominator)
                : 4.0;
            long long curBar = computeCurrentBar(pos.ppqPosition, barLenQLoc);
            // Guard: only schedule realign on rate change after at least one pattern firing and mask non-empty
            const uint16_t maskNow = patternStepsMask.load(std::memory_order_relaxed);
            if (lastPatternFiredBar >= 0 && maskNow != 0)
            {
                forceNextBarStart = true;
                uiNextRestartPending.store(true, std::memory_order_relaxed);

            }
        }
    }

    // Capture the resolution (locked to 24 for MIDI clock)
    const int resolutionBefore = getClockResolution();

    // Run control param
    bool runParamCached = runParam->load(std::memory_order_relaxed) > 0.5f;
    if (syncLatchEnabled.load(std::memory_order_relaxed))
    {
        runParamCached = isGateOpen.load(std::memory_order_relaxed);
    }
    const bool keepClockStoppedCached = clockWhileStoppedParam->load(std::memory_order_relaxed) > 0.5f;
    bool allowMidiOut = runParamCached || keepClockStoppedCached;

    

    // Detect run-param transitions (true -> false) so we can emit an immediate
    // MIDI Stop to external devices when user turns Run off. This makes the
    // UI Run toggle reliably stop external drum machines even if host
    // transport/clock settings would otherwise keep running.
    if (lastRunParam != runParamCached)
    {
        if (! runParamCached)
        {
            // Send an immediate Stop message at the start of this block.
            const auto stopMsg = juce::MidiMessage::midiStop();
            midi.addEvent(stopMsg, 0);
            // Queue Stop for external output; will be flushed once at end of this function.
            extClock.addEvent(stopMsg, 0);
            
            // Send SPP if enabled
            if (sppMode.load(std::memory_order_relaxed))
            {
                int spp = (int)(pos.ppqPosition * 4.0);
                auto sppMsg = juce::MidiMessage::songPositionPointer(spp);
                midi.addEvent(sppMsg, 0);
                extClock.addEvent(sppMsg, 0);
            }

            // Clear running state
            runActive = false;
            pendingStart = false;
            uiIsRunning.store(false, std::memory_order_relaxed);
            uiPendingStart.store(false, std::memory_order_relaxed);
            // Gate audio pulses until next Start
            audioPulsesEnabled.store(false, std::memory_order_relaxed);
            // Clear any scheduled resync/restarts when Run is turned OFF (idx0)
            pendingBarRestart.store(false, std::memory_order_relaxed);
            pendingPatternRestartTargetBar.store(-1, std::memory_order_relaxed);
            resyncPending.store(false, std::memory_order_relaxed);
            resyncTargetBar.store(-1, std::memory_order_relaxed);
            resyncTargetStep.store(-1, std::memory_order_relaxed);
            uiNextRestartPending.store(false, std::memory_order_relaxed);
            suppressUntilRestart = false;

            // Reset duplicate suppression tracking so next trigger always fires (crucial for Gate Mode re-triggering)
            lastStartBar.store(-1, std::memory_order_relaxed);
            lastStartStep.store(-1, std::memory_order_relaxed);
        }
        // Keep original behaviour on Run==true: arm a pendingStart handled later in the main logic.
        lastRunParam = runParamCached;
    }

    const double bpm = (pos.bpm > 0.0 ? pos.bpm : 120.0);
    // If BPM changed since last block, conservatively reset accumulators to avoid stale carry.
    if (lastBpmForAcc < 0.0)
        lastBpmForAcc = bpm;
    else if (std::fabs(bpm - lastBpmForAcc) > 1e-6)
    {
        resetAccumulators();
        lastBpmForAcc = bpm;
    }
    uiBpm.store(bpm, std::memory_order_relaxed);
    samplesPerQuarter = currentSampleRate * 60.0 / juce::jmax(1e-6, bpm);

    const double ppqStart = pos.ppqPosition;
    bool isPlaying = pos.isPlaying;



    // Ensure we allow MIDI out when the host is playing so external gear follows the DAW
    // allowMidiOut = allowMidiOut || isPlaying;

    // Transport state transitions: emit Start/Continue/Stop when host play toggles
    int hostStartSampleThisBlock = -1; // if host started this block, mark sample 0 as a boundary (we'll allow a clock on this frame)
    bool hostForcedAllowMidiOut = false; // when host START occurs we temporarily allow MIDI clocks even if Run param is off
    bool haveHostStartMsg = false;
    juce::MidiMessage hostStartMsg;

    // Robust run state handling: don't miss edges that occur between blocks
    if (! runParamCached)
    {
        if (runActive || pendingStart)
        {
            // Stop immediately and cancel any pending start
            runActive = false;
            pendingStart = false;
            uiIsRunning.store(false, std::memory_order_relaxed);
            uiPendingStart.store(false, std::memory_order_relaxed);
            // When user has Run==off, suppress sending any MIDI stop messages.
            // Also clear/gate any scheduled restarts/resyncs and audio pulses
            pendingBarRestart.store(false, std::memory_order_relaxed);
            pendingPatternRestartTargetBar.store(-1, std::memory_order_relaxed);
            resyncPending.store(false, std::memory_order_relaxed);
            resyncTargetBar.store(-1, std::memory_order_relaxed);
            resyncTargetStep.store(-1, std::memory_order_relaxed);
            uiNextRestartPending.store(false, std::memory_order_relaxed);
            suppressUntilRestart = false;
            audioPulsesEnabled.store(false, std::memory_order_relaxed);

            // Reset duplicate suppression tracking
            lastStartBar.store(-1, std::memory_order_relaxed);
            lastStartStep.store(-1, std::memory_order_relaxed);
        }
    }
    else // runParamCached == true
    {
        if (! runActive && ! pendingStart)
        {
            if (syncLatchEnabled.load(std::memory_order_relaxed))
            {
                // Gate Mode: Immediate Start (override all scheduling)
                const auto startMsg = juce::MidiMessage::midiStart();
                midi.addEvent(startMsg, 0);
                extClock.addEvent(startMsg, 0);
                
                runActive = true;
                pendingStart = false;
                uiIsRunning.store(true, std::memory_order_relaxed);
                uiPendingStart.store(false, std::memory_order_relaxed);
                audioPulsesEnabled.store(true, std::memory_order_relaxed);
                suppressUntilRestart = false;
                
                // Reset duplicate suppression
                lastStartBar.store(-1, std::memory_order_relaxed);
                lastStartStep.store(-1, std::memory_order_relaxed);
                
                // Mark that we started at sample 0 so clocks can flow
                hostStartSampleThisBlock = 0; 
                hostForcedAllowMidiOut = true; // Ensure clocks flow this block
                
                // Clear any pending restarts
                pendingBarRestart.store(false, std::memory_order_relaxed);
                uiNextRestartPending.store(false, std::memory_order_relaxed);
            }
            else
            {
                // Arm a start at the next bar if we're currently stopped
                pendingStart = true;
                uiPendingStart.store(true, std::memory_order_relaxed);
            }
        }
    }
    // removed unused lastRunParam

    // Trigger mode rising edge: when enabling the trigger-mode gate (idx8),
    // schedule a bar+offset restart (resync). This should happen when the
    // gate is switched ON; turning it OFF does not schedule a resync.
    {
        const bool tmNow = triggerModeEnabled.load(std::memory_order_relaxed);
        if (tmNow && ! lastTriggerModeEnabled)
        {
            // Show NEXT indicator and request a bar restart
            pendingBarRestart.store(true, std::memory_order_relaxed);
            uiNextRestartPending.store(true, std::memory_order_relaxed);

            // Compute current bar and step to schedule a resync target.
            const int offsetStep = resyncOffsetParamObj ? juce::jlimit(1, 16, resyncOffsetParamObj->get()) : 1;
            const double barLenQNow = (pos.timeSigNumerator > 0 && pos.timeSigDenominator > 0)
                ? (4.0 * (double) pos.timeSigNumerator / (double) pos.timeSigDenominator)
                : 4.0;
            long long curBar = computeCurrentBar(ppqStart, barLenQNow);
            int currentStepApprox = juce::jlimit(1, 16, uiStep16.load(std::memory_order_relaxed));

            // Attempt to schedule a resync target (non-force; obey replacement rules).
            attemptScheduleResync(curBar, currentStepApprox, offsetStep, false);
        }
        lastTriggerModeEnabled = tmNow;
    }

    // Handle explicit offset change request (clicking a wedge)
    if (resyncFromOffsetPending.exchange(false, std::memory_order_relaxed))
    {
        if (triggerModeEnabled.load(std::memory_order_relaxed))
        {
            const int offsetStep = resyncOffsetParamObj ? juce::jlimit(1, 16, resyncOffsetParamObj->get()) : 1;
            
            const double barLenQNow = (pos.timeSigNumerator > 0 && pos.timeSigDenominator > 0)
                ? (4.0 * (double) pos.timeSigNumerator / (double) pos.timeSigDenominator)
                : 4.0;
            long long curBar = computeCurrentBar(ppqStart, barLenQNow);
            int currentStepApprox = juce::jlimit(1, 16, uiStep16.load(std::memory_order_relaxed));

            attemptScheduleResync(curBar, currentStepApprox, offsetStep, false);
            uiNextRestartPending.store(true, std::memory_order_relaxed);
        }
    }

    // If Sync Latch (GATE mode) is enabled, ignore host transport transitions.
    const bool ignoreHostTransport = syncLatchEnabled.load(std::memory_order_relaxed);

    if (isPlaying && ! lastWasPlaying)
    {
        // Always reset accumulators and tick tracking on Host Start to handle loops/jumps
        resetAccumulators();
        lastTickIndex = std::numeric_limits<long long>::min();
        // Treat host START as a new session: reset duplicate suppression so the first
        // scheduled Start (including offset-based starts) is never dropped.
        lastStartBar.store(-1, std::memory_order_relaxed);
        lastStartStep.store(-1, std::memory_order_relaxed);

        if (!ignoreHostTransport)
        {
            // Distinguish plugin load mid-transport vs genuine transport start edge.
            const double lastBarLocal = pos.ppqPositionOfLastBarStart;
            const double barLenQLocal = (pos.timeSigNumerator > 0 && pos.timeSigDenominator > 0)
                ? (4.0 * (double) pos.timeSigNumerator / (double) pos.timeSigDenominator)
                : 4.0;
            const double posInBar = juce::jlimit(0.0, barLenQLocal, ppqStart - lastBarLocal);
            int initialStep = (int) std::floor((barLenQLocal > 0.0 ? (posInBar / barLenQLocal) : 0.0) * 16.0) + 1;
            if (initialStep < 1) initialStep = 1; if (initialStep > 16) initialStep = 16;
            // If the plugin was created while the host was already mid-bar (firstBlock),
            // suppress emitting an immediate Start/Continue. Instead arm a pending start
            // so the engine will align at the configured offset without producing an
            // extra Start message that can desync multi-instance setups.
            const bool suppressStartOnLoad = firstBlock && initialStep > 1;
            if (suppressStartOnLoad)
            {
                // Arm restart at next configured offset step and suppress clocks until applied.
                pendingStart = true;
                runActive = false;
                uiIsRunning.store(false, std::memory_order_relaxed);
                uiPendingStart.store(true, std::memory_order_relaxed);
                uiNextRestartPending.store(true, std::memory_order_relaxed);
                suppressUntilRestart = true;
                firstBlock = false;
            }
            else
            {
                // Only restart if the plugin is set to RUN. If Stopped, DAW start should not override.
                if (runParamCached)
                {
                    // On DAW restart (STOP -> START), we want a clean re-sync.
                    // If the transport starts mid-bar, don't continue clocks from the previous phase;
                    // instead, arm a pending start and wait for the NEXT bar boundary before emitting Start + clocks.
                    // If the transport starts at a bar boundary, we still honour the user-selected OFFSET step.
                    const bool nearBarStart = (posInBar <= 1.0e-6);
                    const bool startedMidBar = ! nearBarStart;

                    pendingStart = true;
                    runActive = false;
                    uiIsRunning.store(false, std::memory_order_relaxed);
                    uiPendingStart.store(true, std::memory_order_relaxed);
                    uiNextRestartPending.store(true, std::memory_order_relaxed);
                    // Suppress clocks/clicks until the scheduled Start is emitted.
                    suppressUntilRestart = true;
                    audioPulsesEnabled.store(false, std::memory_order_relaxed);
                    firstBlock = false;
                    uiHostStartPending.store(true, std::memory_order_relaxed);

                    // If we started mid-bar, force the pending start to wrap to the next bar (not the next offset occurrence in this bar).
                    // If we started at a bar boundary, allow the offset step inside the current bar (including immediate when offset==1).
                    forceNextBarStart = startedMidBar;

                    // Reset internal pattern scheduling state so intervals realign with host bars
                    lastPatternFiredBar = -1;
                    nextRandomPatternTargetBar = -1;
                    lastPatternRestartScheduledBar = -1;
                    pendingPatternRestartTargetBar.store(-1, std::memory_order_relaxed);
                    // Defer auto bar restarts until first pattern interval fires
                    const int modeAtStart = patternBarsMode.load(std::memory_order_relaxed);
                    deferBarRestartUntilPattern.store(modeAtStart > 0, std::memory_order_relaxed);

                    // If we started at an exact bar boundary but the OFFSET is not step-1, we still wait until that offset.
                    // (No special-case needed here; handleBarAlignedChanges will schedule it.)
                }
                else
                {
                    // Plugin is Stopped. Ensure state reflects that.
                    runActive = false;
                    pendingStart = false;
                    uiIsRunning.store(false, std::memory_order_relaxed);
                    uiPendingStart.store(false, std::memory_order_relaxed);
                    // If IDLE is ON, clocks will continue via allowMidiOut logic (clockWhileStoppedParam).
                }
            }
        }
        else
        {
            // Gate Mode: Just reset tracking
            hostStartSampleThisBlock = 0;
        }
    }
    else if (! isPlaying && lastWasPlaying)
    {
        if (!ignoreHostTransport)
        {
            const auto stopMsg = juce::MidiMessage::midiStop();
            if (allowMidiOut || hostForcedAllowMidiOut)
            {
                midi.addEvent(stopMsg, 0);
                // queue external stop for one-shot flush at end
                extClock.addEvent(stopMsg, 0);
                
                // Send SPP if enabled
                if (sppMode.load(std::memory_order_relaxed))
                {
                    int spp = (int)(pos.ppqPosition * 4.0);
                    auto sppMsg = juce::MidiMessage::songPositionPointer(spp);
                    midi.addEvent(sppMsg, 0);
                    extClock.addEvent(sppMsg, 0);
                }
            }
            // When host stops, also clear run-active state
            runActive = false;
            pendingStart = false;
            uiIsRunning.store(false, std::memory_order_relaxed);
            uiPendingStart.store(false, std::memory_order_relaxed);
            
            // Reset accumulators on host STOP
            resetAccumulators();
            // Gate audio pulses until next Start
            audioPulsesEnabled.store(false, std::memory_order_relaxed);
        }
    }

    // Generate clock ticks aligned to transport when playing
    if (isPlaying && std::isfinite(ppqStart))
    {
        const double ppqPerSample = 1.0 / samplesPerQuarter; // quarter-notes per sample
        const auto clockMsg = juce::MidiMessage::midiClock();
        // Update UI step (1..16) based on position within current bar.
        // If the next bar boundary falls within this block, force the UI to show step 1
        // so the visual starts at 12 o'clock on bar transitions that occur mid-block.
        const double lastBar = pos.ppqPositionOfLastBarStart;
        const double barLenQ = (pos.timeSigNumerator > 0 && pos.timeSigDenominator > 0)
            ? (4.0 * (double) pos.timeSigNumerator / (double) pos.timeSigDenominator)
            : 4.0;
        const double nextBar = lastBar + barLenQ;
        const double uiPpqEnd = ppqStart + (double) (numSamples - 1) * ppqPerSample;
        if (ppqStart < nextBar && nextBar <= uiPpqEnd)
        {
            uiStep16.store(1, std::memory_order_relaxed);
        }
        else
        {
            const double withinBar = juce::jlimit(0.0, barLenQ, ppqStart - lastBar);
            const double frac = (barLenQ > 0.0 ? withinBar / barLenQ : 0.0);
            int step = (int) std::floor(frac * 16.0) + 1;
            if (step < 1) step = 1; if (step > 16) step = 16;
            uiStep16.store(step, std::memory_order_relaxed);
        }

        // Helpers for computing tick indices under a given resolution
        auto tickAt = [](double ppq, int res) -> long long { return (long long) std::floor(ppq * (double) res); };

        // Handle pending shuffle (swing) parameter change: schedule at next unswung 1/8 (0.5 QN) boundary.
        // This preserves clock continuity: we switch swing only at 8th boundaries which are always "straight".
        if (shuffleStepParamObj != nullptr)
        {
            const int desiredShuffle = juce::jlimit(1, 7, shuffleStepParamObj->get());
            if (desiredShuffle != currentShuffleStep && pendingShuffleStep != desiredShuffle)
            {
                pendingShuffleStep = desiredShuffle;
                // Compute next unswung 1/8 boundary (0.5 quarter-note increments)
                const double nextUnswingPair = std::floor(ppqStart / 0.5 + 1.0) * 0.5;
                applyShuffleAtPPQ = nextUnswingPair;
                uiNextRestartPending.store(true, std::memory_order_relaxed); // show NEXT while waiting
            }
        }

        // Handle pending linear shuffle (continuous) changes: apply at next unswung 1/8 boundary
        if (isLinearShuffleEnabled())
        {
            const float desiredLinear = getLinearShuffleValue();
            if (std::fabs(desiredLinear - currentLinearShuffleNorm) > 1e-6f && pendingLinearShuffleNorm < 0.0f)
            {
                pendingLinearShuffleNorm = juce::jlimit(0.0f, 1.0f, desiredLinear);
                const double nextUnswingPair = std::floor(ppqStart / 0.5 + 1.0) * 0.5;
                applyLinearShuffleAtPPQ = nextUnswingPair;
                uiNextRestartPending.store(true, std::memory_order_relaxed);
            }
        }

        // Handle immediate retrigger (1/16) and bar-aligned changes (swing applied when computing sample offsets)
        // extClock is declared at function scope to collect external messages from all branches.
        int gapStart = -1, gapEnd = -1, startSampleAtBoundary = -1;
        int forbiddenClockSample = -1; // do not emit clock on this exact sample (Start sample)

        // Latent 1/16 grid Start: we always know next grid boundary; allow manual trigger even when stopped
        if (runActive || triggerArmedForNextSixteenth.load(std::memory_order_relaxed))
        {
            // Swing-aware 16th boundaries: if shuffle active (>1) and bar length is 4/4, use shifted boundary set.
            const double eps = 1.0e-6;
                const double lastBar = pos.ppqPositionOfLastBarStart;
                const double barLenQ = (pos.timeSigNumerator > 0 && pos.timeSigDenominator > 0)
                    ? (4.0 * (double) pos.timeSigNumerator / (double) pos.timeSigDenominator)
                    : 4.0;
                double withinBar = juce::jlimit(0.0, barLenQ, ppqStart - lastBar);
                double nextBoundaryQ = -1.0;
                {
                    // scope close for variables
                }
                const double shiftQActive = getCurrentShiftQ(barLenQ);
                if (shiftQActive > 0.0)
                {
                    // Build 16 boundary positions inside a 4/4 bar considering swing pair lengths (pairLen remains 0.5 QN):
                    // Even index (0,2,4,...) => pairStart; odd => pairStart + 0.25 + shiftQ.
                    double found = -1.0;
                    for (int pair = 0; pair < 8; ++pair)
                    {
                        double pairStart = pair * 0.5; // cumulative (two 16ths total 0.5 QN)
                        double firstBoundary = pairStart;            // step 2*pair+1 (1-based)
                        double secondBoundary = pairStart + 0.25 + shiftQActive; // step 2*pair+2 (1-based)
                        if (firstBoundary >= withinBar - eps && found < 0.0) found = firstBoundary;
                        if (secondBoundary >= withinBar - eps && found < 0.0) found = secondBoundary;
                    }
                    if (found < 0.0)
                        found = 4.0; // wrap to bar end (next bar start)
                    nextBoundaryQ = lastBar + found;
                }
                else
                {
                    // Unswing grid
                    const double gridQ = kSixteenthQ; // 0.25
                    // Compute previous and next 16th boundaries
                    const double prevGrid = std::floor(ppqStart / gridQ) * gridQ;
                    const double mod = ppqStart - prevGrid; // fraction into the 16th (in Q)
                    const bool onGrid = std::fabs(mod) <= eps || std::fabs(mod - gridQ) <= eps;
                    const double nextGrid = onGrid ? ppqStart : (prevGrid + gridQ);

                    // Tolerance: if we're only a few samples past the previous boundary, treat it as
                    // an immediate boundary (fire now) instead of waiting for the next 16th. This
                    // prevents borderline late triggers caused by tiny timing skews.
                    const int sampleTolerance = 8; // ~0.17ms at 48kHz, tweakable
                    const double samplesSincePrev = mod * samplesPerQuarter;
                    // Only treat a just-past previous 16th as the immediate boundary when a
                    // manual retrigger is armed. Otherwise prefer the next 16th to avoid
                    // perturbing normal clock alignment.
                    const bool armed = triggerArmedForNextSixteenth.load(std::memory_order_relaxed);
                    if (armed && samplesSincePrev <= (double) sampleTolerance)
                        nextBoundaryQ = prevGrid;
                    else
                        nextBoundaryQ = nextGrid;


                }
            const int offsetSamples = triggerOffsetSamples.load(std::memory_order_relaxed);
            const double triggerOffsetQ = (samplesPerQuarter > 0.0) ? ((double) offsetSamples / samplesPerQuarter) : 0.0;
            const double targetTriggerQ = nextBoundaryQ + triggerOffsetQ;
            const double deltaQ = targetTriggerQ - ppqStart;
            int nextGridOffset = fastRoundPositive(deltaQ * samplesPerQuarter);
            if (triggerArmedForNextSixteenth.load(std::memory_order_relaxed))
            {
                // If targetTriggerQ has already passed (due to negative offset or late click), fire immediately
                if (targetTriggerQ <= ppqStart)
                    nextGridOffset = 0;

                if (nextGridOffset >= 0 && nextGridOffset < numSamples)
                {
                    const auto startMsg = juce::MidiMessage::midiStart();
                    long long barForStart = computeCurrentBar(nextBoundaryQ, barLenQ);
                    int stepForStart = computeLogicalStepFromPPQ(nextBoundaryQ, barLenQ, shiftQActive);

                    const int offsetStep = resyncOffsetParamObj ? juce::jlimit(1, 16, resyncOffsetParamObj->get()) : 1;

                    const bool deferToResync = triggerModeEnabled.load(std::memory_order_relaxed)
                                            && !syncLatchEnabled.load(std::memory_order_relaxed)
                                            && offsetStep != 1
                                            && !runActive;

                    if (! deferToResync)
                    {
                        // Manual trigger: user explicitly pressed trigger, do not suppress unless duplicate at exact same sample
                        if (nextGridOffset != lastPatternStartSampleInBlock)
                        {
                            // Legacy mode: send Stop before Start for manual triggers too
                            if (legacyModeEnabled.load(std::memory_order_relaxed))
                            {
                                const auto stopMsg = juce::MidiMessage::midiStop();
                                addBoth(midi, extClock, stopMsg, nextGridOffset);
                            }

                            addBoth(midi, extClock, startMsg, nextGridOffset);
                            startSampleForRunSignal = nextGridOffset;
                            lastStartBar.store(barForStart, std::memory_order_relaxed);
                            lastStartStep.store(stepForStart, std::memory_order_relaxed);
                        }
                        // record that we emitted a Start from a manual trigger at this sample
                        lastPatternStartSampleInBlock = nextGridOffset;
                    }

                    // triggerArmed diagnostic message removed per user request
                    // Consume arm
                    triggerArmedForNextSixteenth.store(false, std::memory_order_relaxed);

                    if (deferToResync)
                    {
                        // No immediate Start; schedule resync at the offset step only.
                        pendingBarRestart.store(true, std::memory_order_relaxed);
                        uiNextRestartPending.store(true, std::memory_order_relaxed);
                        int currentStep = stepForStart;
                        attemptScheduleResync(barForStart, currentStep, offsetStep, false);

                        if (! runActive)
                        {
                            // Engine wasn't running: arm pending start and suppress clocks until resync fires
                            pendingStart = true;
                            uiPendingStart.store(true, std::memory_order_relaxed);
                            suppressUntilRestart = true;
                        }
                        // If engine was already running, clocks continue flowing; only the resync Start fires
                    }
                    else
                    {
                        // Manual trigger should clear a stopped state: mark engine running
                        runActive = true;
                        pendingStart = false;
                        uiIsRunning.store(true, std::memory_order_relaxed);
                        uiPendingStart.store(false, std::memory_order_relaxed);
                        // Enable audio pulses on manual trigger (Gate Note On)
                        audioPulsesEnabled.store(true, std::memory_order_relaxed);

                        // FIX: Ensure clocks flow immediately
                        suppressUntilRestart = false;

                        // Schedule resync if idx8 (gate) is enabled, per workflow.md rule 1
                        if (triggerModeEnabled.load(std::memory_order_relaxed))
                        {
                            if (! syncLatchEnabled.load(std::memory_order_relaxed))
                            {
                                pendingBarRestart.store(true, std::memory_order_relaxed);
                                uiNextRestartPending.store(true, std::memory_order_relaxed);
                                int currentStep = stepForStart;
                                attemptScheduleResync(barForStart, currentStep, offsetStep, false);
                            }
                        }
                    }
                    // Allow clock at same frame (no suppression)
                }
                // If offset beyond block, keep armed until next block (no change)
            }
        }

        // Pattern scheduling: first pass (full diagnostics). At bar boundaries decide if pattern fires; then emit Start events.
        tryFirePattern(ppqStart, barLenQ, lastBar, numSamples, midi, extClock, false, /*preview=*/false);

        auto barWindow = handleBarAlignedChanges(pos, numSamples, midi, extClock);
        gapStart = barWindow.gapStart;
        gapEnd = barWindow.gapEnd;
        startSampleAtBoundary = barWindow.startSample;
        const double boundaryPPQExact = barWindow.boundaryPPQ;
        // Second idempotent pass: suppress duplicate diagnostics unless new pattern targets appeared.
        tryFirePattern(ppqStart, barLenQ, lastBar, numSamples, midi, extClock, true, /*preview=*/false);

        // If host just started this block (sample 0), mark the boundary at sample 0
        if (startSampleAtBoundary < 0 && hostStartSampleThisBlock >= 0)
            startSampleAtBoundary = hostStartSampleThisBlock;

        // Also merge host Start/Continue into extClock at sample 0 so the following clock can share the same frame
        if (haveHostStartMsg)
            extClock.addEvent(hostStartMsg, 0);

        // If a plugin-driven boundary Start requires suppression, set forbidden sample (not set for bar-restart/change-rate)
        if (suppressClockAtBoundaryOnce && startSampleAtBoundary >= 0)
            forbiddenClockSample = startSampleAtBoundary;

        const bool boundaryInBlock = (startSampleAtBoundary >= 0);
        const int resolutionAfter = getClockResolution(); // may change if rate applied at boundary

        // Precompute several constants used inside the tick loops to reduce per-tick overhead.
        const double ppqBlockEnd = ppqStart + (double) (numSamples - 1) * ppqPerSample;
        
        // FIX: Detect transport loop/jump-back and reset tick tracking
        long long startTickIdx = tickAt(ppqStart, resolutionBefore);
        if (lastTickIndex == std::numeric_limits<long long>::min() || startTickIdx < lastTickIndex)
        {
            if (lastTickIndex != std::numeric_limits<long long>::min())
                resetAccumulators();
            lastTickIndex = startTickIdx - 1;
        }

        // Precompute swing/pulse mapping constants for the "before" resolution
        const double pairLenQBefore = 0.5;
        const double shiftQBefore = getCurrentShiftQ(barLenQ);
        const long long pulsesPerPairBefore = (long long) std::llround(pairLenQBefore * (double) resolutionBefore);
        const int pulsesPer16thBefore = (int) (pulsesPerPairBefore / 2);
        const double firstLenBefore = 0.25 + shiftQBefore;
        const double secondLenBefore = 0.25 - shiftQBefore;
        const double spacingFirstBefore = pulsesPer16thBefore > 0 ? (firstLenBefore / (double) pulsesPer16thBefore) : 0.0;
        const double spacingSecondBefore = pulsesPer16thBefore > 0 ? (secondLenBefore / (double) pulsesPer16thBefore) : 0.0;

        // Use persistent per-primary fractional accumulator (declared in header).
        // Note: conservative persistence (carries across blocks) but reset on transport/bpm/sample-rate/rate changes.

        // Audio Click dedicated generator for all PPQN rates (1, 2, 4, 24, 48, 96 ppq)
        auto generateAudioClicks = [&](int regionStartSample, int regionEndSample, double shiftQ)
        {
            if (!clickEnabled || !runActive || suppressUntilRestart || !audioPulsesEnabled.load(std::memory_order_acquire))
                return;
            if (regionEndSample < regionStartSample || regionEndSample < 0 || regionStartSample >= numSamples)
                return;

            int clickResolution = 24;
            switch (clickRateCached)
            {
                case 1: clickResolution = 1; break;  // beat (4th)
                case 2: clickResolution = 2; break;  // 8th
                case 3: clickResolution = 4; break;  // 16th
                case 4: clickResolution = 24; break; // 24ppq
                case 5: clickResolution = 48; break; // 48ppq
                case 6: clickResolution = 96; break; // 96ppq
                default: clickResolution = 4; break;
            }

            const double ppqRegionStart = ppqStart + (double) regionStartSample * ppqPerSample;
            const double ppqRegionEnd   = ppqStart + (double) regionEndSample * ppqPerSample;

            long long startClickIdx = (long long) std::floor(ppqRegionStart * (double) clickResolution);
            if (lastClickIndex == std::numeric_limits<long long>::min() || startClickIdx < lastClickIndex || clickRateCached != lastClickRateCached)
            {
                lastClickRateCached = clickRateCached;
                lastClickIndex = startClickIdx - 1;
            }

            const long long endClickIdx = (long long) std::floor(ppqRegionEnd * (double) clickResolution) + 2;

            // Swing constants for click resolution
            const double pairLenQ = 0.5;
            const int pulsesPerPair = (clickResolution >= 4) ? (clickResolution / 2) : 0;
            const int pulsesPer16th = pulsesPerPair / 2;
            const double firstLen = 0.25 + shiftQ;
            const double secondLen = 0.25 - shiftQ;
            const double spacingFirst = (pulsesPer16th > 0) ? (firstLen / (double) pulsesPer16th) : 0.0;
            const double spacingSecond = (pulsesPer16th > 0) ? (secondLen / (double) pulsesPer16th) : 0.0;

            const float outLevel = 1.0f; // Always fixed 0 dB, all beats even
            double pulseMs = (double) pulseWidthMs;
            if (clickResolution >= 96)      pulseMs = std::min(pulseMs, 1.2);
            else if (clickResolution >= 48) pulseMs = std::min(pulseMs, 3.0);
            else if (clickResolution >= 24) pulseMs = std::min(pulseMs, 8.0);

            const int pulseSamples = (int) juce::jmax(1, (int) std::round((pulseMs / 1000.0) * currentSampleRate));
            const int leftCh = 0;
            const bool haveLeft = (leftCh < buffer.getNumChannels());

            long long lastProcessedClick = lastClickIndex;

            for (long long ct = lastClickIndex + 1; ct <= endClickIdx; ++ct)
            {
                double ctPPQ;
                if (shiftQ <= 1e-12 || clickResolution < 4)
                {
                    ctPPQ = (double) ct / (double) clickResolution;
                }
                else
                {
                    long long pairIndex = (long long) std::floor((double) ct / (double) pulsesPerPair);
                    int indexInPair = (int) (ct - pairIndex * (long long) pulsesPerPair);
                    double pairStart = (double) pairIndex * pairLenQ;
                    if (indexInPair < pulsesPer16th)
                        ctPPQ = pairStart + (double) indexInPair * spacingFirst;
                    else
                    {
                        int idxSecond = indexInPair - pulsesPer16th;
                        ctPPQ = pairStart + firstLen + (double) idxSecond * spacingSecond;
                    }
                }

                const double deltaQuarter = ctPPQ - ppqStart;
                const double exactSample = deltaQuarter * samplesPerQuarter;
                const int mappedOffset = (exactSample >= 0.0) ? fastRoundPositive(exactSample) : (int) std::floor(exactSample + 0.5);

                if (mappedOffset > regionEndSample)
                {
                    break; // carry to next block or next region
                }

                if (mappedOffset < regionStartSample)
                {
                    lastProcessedClick = ct;
                    continue;
                }

                if (mappedOffset >= 0 && mappedOffset < numSamples)
                {
                    if (! clickPulseCached)
                    {
                        // 1-sample spike
                        if (haveLeft)
                            buffer.addSample(leftCh, mappedOffset, outLevel);
                    }
                    else
                    {
                        // Pulse: hold high for pulseSamples
                        for (int s = 0; s < pulseSamples; ++s)
                        {
                            const int idx = mappedOffset + s;
                            if (haveLeft && idx >= 0 && idx < numSamples)
                                buffer.addSample(leftCh, idx, outLevel);
                        }
                        const int tailBeyond = (mappedOffset + pulseSamples) - numSamples;
                        if (tailBeyond > 0)
                            clickHoldRemainingSamples = juce::jmax(clickHoldRemainingSamples, tailBeyond);
                    }
                }

                lastProcessedClick = ct;
            }

            lastClickIndex = lastProcessedClick;
        };

        // Phase 1: ticks before a restart boundary (or whole block if no restart boundary). May include a mid-block shuffle activation.
        const int preEndSample = boundaryInBlock ? (startSampleAtBoundary - 1) : (numSamples - 1);
        if (preEndSample >= 0)
        {
            const double ppqPreEnd = ppqStart + (double) preEndSample * ppqPerSample;
            const long long tickIndexEndPre   = tickAt(ppqPreEnd, resolutionBefore);
            long long lastProcessedT = lastTickIndex;
            // Always continue from lastTickIndex+1 to preserve carried ticks across blocks
            for (long long t = lastTickIndex + 1; t <= tickIndexEndPre; ++t)
            {
                double rawTickPPQ = (double) t / (double) resolutionBefore;

                // Activate pending shuffle at unswung 1/8 boundary
                if (pendingShuffleStep > 0 && applyShuffleAtPPQ >= 0.0 && rawTickPPQ >= applyShuffleAtPPQ)
                {
                    currentShuffleStep = pendingShuffleStep;
                    pendingShuffleStep = -1;
                    applyShuffleAtPPQ = -1.0;
                    uiNextRestartPending.store(false, std::memory_order_relaxed);
                    // Reschedule remaining pattern targets in this bar using new shift
                    const double lastBarLocal = pos.ppqPositionOfLastBarStart;
                    const double barLenQLocal = (pos.timeSigNumerator > 0 && pos.timeSigDenominator > 0)
                        ? (4.0 * (double) pos.timeSigNumerator / (double) pos.timeSigDenominator)
                        : 4.0;
                    rescheduleRemainingPatternTargets(lastBarLocal, barLenQLocal, rawTickPPQ);
                }
                // Activate pending linear shuffle change at boundary
                if (pendingLinearShuffleNorm >= 0.0f && applyLinearShuffleAtPPQ >= 0.0 && rawTickPPQ >= applyLinearShuffleAtPPQ)
                {
                    currentLinearShuffleNorm = pendingLinearShuffleNorm;
                    pendingLinearShuffleNorm = -1.0f;
                    applyLinearShuffleAtPPQ = -1.0;
                    uiNextRestartPending.store(false, std::memory_order_relaxed);
                    const double lastBarLocal = pos.ppqPositionOfLastBarStart;
                    const double barLenQLocal = (pos.timeSigNumerator > 0 && pos.timeSigDenominator > 0)
                        ? (4.0 * (double) pos.timeSigNumerator / (double) pos.timeSigDenominator)
                        : 4.0;
                    rescheduleRemainingPatternTargets(lastBarLocal, barLenQLocal, rawTickPPQ);
                }

                // Optimised swing mapping using precomputed constants (reduces per-tick overhead)
                long long pairIndex = 0;
                int indexInPair = 0;
                if (pulsesPerPairBefore > 0)
                {
                    pairIndex = t / pulsesPerPairBefore;
                    indexInPair = (int) (t % pulsesPerPairBefore);
                }
                double tickPPQ;
                if (shiftQBefore <= 1e-12)
                {
                    tickPPQ = (double) t / (double) resolutionBefore;
                }
                else
                {
                    double pairStart = (double) pairIndex * pairLenQBefore;
                    if (indexInPair < pulsesPer16thBefore)
                        tickPPQ = pairStart + (double) indexInPair * spacingFirstBefore;
                    else
                    {
                        const int idxSecond = indexInPair - pulsesPer16thBefore;
                        tickPPQ = pairStart + firstLenBefore + (double) idxSecond * spacingSecondBefore;
                    }
                }
                const double deltaQuarter = tickPPQ - ppqStart;
                const double exactSample = deltaQuarter * samplesPerQuarter;

                // Treat the first pulse inside each pair as a primary and apply a tiny per-block fractional accumulator
                // to stabilise rounding on these primaries (clamped per-block, no carry across blocks).
                int mappedOffset;
                if (indexInPair == 0)
                {
                    double adj = exactSample + fracAccPrimaryBefore;
                    mappedOffset = fastRoundPositive(adj);
                    fracAccPrimaryBefore = adj - (double) mappedOffset;
                    // clamp carry to avoid large spikes
                    if (fracAccPrimaryBefore > 0.5) fracAccPrimaryBefore = 0.5;
                    if (fracAccPrimaryBefore < -0.5) fracAccPrimaryBefore = -0.5;
                }
                else
                {
                    mappedOffset = fastRoundPositive(exactSample);
                }
                // If mapped clock lands beyond this pre-boundary region, carry it to next block (do not clamp/emit)
                if (mappedOffset > preEndSample)
                {
                    break; // leave lastProcessedT unchanged so t will be retried next block
                }
                const int sampleOffset = juce::jlimit(0, numSamples - 1, mappedOffset);
                const bool inGapBar = (gapStart >= 0 && sampleOffset >= gapStart && sampleOffset < gapEnd);
                const bool onForbidden = (forbiddenClockSample >= 0 && sampleOffset == forbiddenClockSample);
                const bool inGap = inGapBar;
                if (! suppressUntilRestart && ! inGap && ! onForbidden)
                {
                    if (allowMidiOut || hostForcedAllowMidiOut)
                    {
                        midi.addEvent(clockMsg, sampleOffset);
                        uiClockCounter.fetch_add(1, std::memory_order_relaxed);
                        if (haveExternalOut)
                            extClock.addEvent(clockMsg, sampleOffset);
                        // pre-phase diag logging removed per user request
                    }
                }

                    if (clickEnabled && runActive && audioPulsesEnabled.load(std::memory_order_acquire))
                    {
                        int targetPPQN = 24;
                        switch (clickRateCached)
                        {
                            case 1: targetPPQN = 1; break;  // beat (4th)
                            case 2: targetPPQN = 2; break;  // 8th
                            case 3: targetPPQN = 4; break;  // 16th
                            case 4: targetPPQN = 24; break; // 24ppq
                            case 5: targetPPQN = 48; break; // 48ppq
                            case 6: targetPPQN = 96; break; // 96ppq
                            default: targetPPQN = 4; break;
                        }

                        auto emitClickSample = [&](int clickOffset) {
                            if (clickOffset < 0 || clickOffset >= numSamples)
                                return;
                            const float outLevel = 1.0f; // Always fixed 0 dB, all beats even
                            double pulseMs = (double) pulseWidthMs;
                            if (targetPPQN >= 96)      pulseMs = std::min(pulseMs, 2.0);
                            else if (targetPPQN >= 48) pulseMs = std::min(pulseMs, 4.0);
                            else if (targetPPQN >= 24)
                            {
                                if (resolutionBefore == 48) pulseMs = std::min(pulseMs, 5.0);
                                else if (resolutionBefore == 24) pulseMs = std::min(pulseMs, 10.0);
                            }

                            const int pulseSamples = (int) juce::jmax(1, (int) std::round((pulseMs / 1000.0) * currentSampleRate));
                            const int leftCh = 0;
                            const bool haveLeft = (leftCh < buffer.getNumChannels());
                            if (! clickPulseCached)
                            {
                                // 1-sample spike
                                if (haveLeft)
                                    buffer.addSample(leftCh, clickOffset, outLevel);
                            }
                            else
                            {
                                // Pulse: hold high for pulseWidthMs, no decay
                                for (int s = 0; s < pulseSamples; ++s)
                                {
                                    const int idx = clickOffset + s;
                                    if (haveLeft && idx >= 0 && idx < numSamples)
                                        buffer.addSample(leftCh, idx, outLevel);
                                }
                                const int tailBeyond = (clickOffset + pulseSamples) - numSamples;
                                if (tailBeyond > 0)
                                    clickHoldRemainingSamples = juce::jmax(clickHoldRemainingSamples, tailBeyond);
                            }
                        };

                        if (targetPPQN <= resolutionBefore)
                        {
                            const int stepSpan = juce::jmax(1, resolutionBefore / targetPPQN);
                            if ((t % stepSpan) == 0)
                                emitClickSample(sampleOffset);
                        }
                        else
                        {
                            const int subClicks = targetPPQN / resolutionBefore;
                            const double tickSamples = samplesPerQuarter / (double) resolutionBefore;
                            for (int k = 0; k < subClicks; ++k)
                            {
                                const int clickOffset = sampleOffset + (int) std::round((double) k * (tickSamples / (double) subClicks));
                                emitClickSample(clickOffset);
                            }
                        }
                    }
                lastProcessedT = t;
            }
            lastTickIndex = lastProcessedT;
            generateAudioClicks(0, preEndSample, shiftQBefore);
        }

        // Precompute 'after' resolution swing/pulse mapping constants for use in the post-boundary loop
        const double pairLenQAfter = 0.5;
        const double shiftQAfter = getCurrentShiftQ(barLenQ);
        const long long pulsesPerPairAfter = (long long) std::llround(pairLenQAfter * (double) resolutionAfter);
        const int pulsesPer16thAfter = (int) (pulsesPerPairAfter > 0 ? (pulsesPerPairAfter / 2) : 0);
        const double firstLenAfter = 0.25 + shiftQAfter;
        const double secondLenAfter = 0.25 - shiftQAfter;
        const double spacingFirstAfter = pulsesPer16thAfter > 0 ? (firstLenAfter / (double) pulsesPer16thAfter) : 0.0;
        const double spacingSecondAfter = pulsesPer16thAfter > 0 ? (secondLenAfter / (double) pulsesPer16thAfter) : 0.0;

        // Phase 2: ticks after a restart boundary using the (possibly) new resolution.
        if (boundaryInBlock)
        {
            // Reset tick baseline to the boundary in "after" resolution so first tick lands exactly 1/res after Start
            const double boundaryPPQ = (boundaryPPQExact >= 0.0)
                ? boundaryPPQExact
                : (ppqStart + (double) startSampleAtBoundary * ppqPerSample);
            const long long baseIdx = computeTickIndexFromPPQ(boundaryPPQ, resolutionAfter, shiftQAfter);
            if (suppressClockAtBoundaryOnce)
            {
                // Skip the clock at the exact Start frame; next tick will be at baseIdx + 1
                lastTickIndex = baseIdx;
            }
            else
            {
                lastTickIndex = baseIdx - 1;
            }

            int clickResAfter = 24;
            switch (clickRateCached)
            {
                case 1: clickResAfter = 1; break;
                case 2: clickResAfter = 2; break;
                case 3: clickResAfter = 4; break;
                case 4: clickResAfter = 24; break;
                case 5: clickResAfter = 48; break;
                case 6: clickResAfter = 96; break;
                default: clickResAfter = 4; break;
            }
            const long long baseClickIdx = (long long) std::floor(boundaryPPQ * (double) clickResAfter);
            lastClickIndex = baseClickIdx - 1;

            const long long tickIndexEndPost   = tickAt(ppqBlockEnd,  resolutionAfter);
            long long lastProcessedT = lastTickIndex;
            for (long long t = lastTickIndex + 1; t <= tickIndexEndPost; ++t)
            {
                double rawTickPPQ = (double) t / (double) resolutionAfter;
                if (pendingShuffleStep > 0 && applyShuffleAtPPQ >= 0.0 && rawTickPPQ >= applyShuffleAtPPQ)
                {
                    currentShuffleStep = pendingShuffleStep;
                    pendingShuffleStep = -1;
                    applyShuffleAtPPQ = -1.0;
                    uiNextRestartPending.store(false, std::memory_order_relaxed);
                    const double lastBarLocal = pos.ppqPositionOfLastBarStart;
                    const double barLenQLocal = (pos.timeSigNumerator > 0 && pos.timeSigDenominator > 0)
                        ? (4.0 * (double) pos.timeSigNumerator / (double) pos.timeSigDenominator)
                        : 4.0;
                    rescheduleRemainingPatternTargets(lastBarLocal, barLenQLocal, rawTickPPQ);
                }
                if (pendingLinearShuffleNorm >= 0.0f && applyLinearShuffleAtPPQ >= 0.0 && rawTickPPQ >= applyLinearShuffleAtPPQ)
                {
                    currentLinearShuffleNorm = pendingLinearShuffleNorm;
                    pendingLinearShuffleNorm = -1.0f;
                    applyLinearShuffleAtPPQ = -1.0;
                    uiNextRestartPending.store(false, std::memory_order_relaxed);
                    const double lastBarLocal = pos.ppqPositionOfLastBarStart;
                    const double barLenQLocal = (pos.timeSigNumerator > 0 && pos.timeSigDenominator > 0)
                        ? (4.0 * (double) pos.timeSigNumerator / (double) pos.timeSigDenominator)
                        : 4.0;
                    rescheduleRemainingPatternTargets(lastBarLocal, barLenQLocal, rawTickPPQ);
                }
                double tickPPQ = 0.0;
                int indexInPair = 0;
                if (shiftQAfter <= 1e-12 || pulsesPerPairAfter <= 0)
                {
                    tickPPQ = (double) t / (double) resolutionAfter;
                }
                else
                {
                    const long long pairIndex = t / pulsesPerPairAfter;
                    indexInPair = (int) (t % pulsesPerPairAfter);
                    double pairStart = (double) pairIndex * pairLenQAfter;
                    if (indexInPair < pulsesPer16thAfter)
                        tickPPQ = pairStart + (double) indexInPair * spacingFirstAfter;
                    else
                    {
                        const int idxSecond = indexInPair - pulsesPer16thAfter;
                        tickPPQ = pairStart + firstLenAfter + (double) idxSecond * spacingSecondAfter;
                    }
                }
                const double deltaQuarter = tickPPQ - ppqStart;
                const double exactSample = deltaQuarter * samplesPerQuarter;
                int mappedOffset;
                if (currentShuffleStep > 1 && pulsesPerPairAfter > 0 && indexInPair == 0)
                {
                    double adj = exactSample + fracAccPrimaryAfter;
                    mappedOffset = fastRoundPositive(adj);
                    fracAccPrimaryAfter = adj - (double) mappedOffset;
                    if (fracAccPrimaryAfter > 0.5) fracAccPrimaryAfter = 0.5;
                    if (fracAccPrimaryAfter < -0.5) fracAccPrimaryAfter = -0.5;
                }
                else
                {
                    mappedOffset = fastRoundPositive(exactSample);
                }
                // Fix for duplicate clocks on rate change: if accumulator reset causes
                // mappedOffset to fall before the boundary sample (already covered by Phase 1), skip it.
                if (boundaryInBlock && mappedOffset < startSampleAtBoundary)
                {
                    continue;
                }
                if (mappedOffset > numSamples - 1)
                {
                    break; // carry into next block; do not advance lastTickIndex
                }
                const int sampleOffset = juce::jlimit(0, numSamples - 1, mappedOffset);
                // Past boundary; optionally suppress clock on the exact Start frame to avoid double clocks with Start
                if (! suppressUntilRestart && !(forbiddenClockSample >= 0 && sampleOffset == forbiddenClockSample))
                {
                    if (allowMidiOut || hostForcedAllowMidiOut)
                    {
                        midi.addEvent(clockMsg, sampleOffset);
                        uiClockCounter.fetch_add(1, std::memory_order_relaxed);
                        if (haveExternalOut)
                            extClock.addEvent(clockMsg, sampleOffset);
                        // post-phase diag logging removed per user request
                    }
                }

                    if (clickEnabled && runActive && audioPulsesEnabled.load(std::memory_order_acquire))
                    {
                        int targetPPQN = 24;
                        switch (clickRateCached)
                        {
                            case 1: targetPPQN = 1; break;  // beat (4th)
                            case 2: targetPPQN = 2; break;  // 8th
                            case 3: targetPPQN = 4; break;  // 16th
                            case 4: targetPPQN = 24; break; // 24ppq
                            case 5: targetPPQN = 48; break; // 48ppq
                            case 6: targetPPQN = 96; break; // 96ppq
                            default: targetPPQN = 4; break;
                        }

                        auto emitClickSample = [&](int clickOffset) {
                            if (clickOffset < 0 || clickOffset >= numSamples)
                                return;
                            const float outLevel = 1.0f; // Always fixed 0 dB, all beats even
                            double pulseMs = (double) pulseWidthMs;
                            if (targetPPQN >= 96)      pulseMs = std::min(pulseMs, 2.0);
                            else if (targetPPQN >= 48) pulseMs = std::min(pulseMs, 4.0);
                            else if (targetPPQN >= 24)
                            {
                                if (resolutionAfter == 48) pulseMs = std::min(pulseMs, 5.0);
                                else if (resolutionAfter == 24) pulseMs = std::min(pulseMs, 10.0);
                            }

                            const int pulseSamples = (int) juce::jmax(1, (int) std::round((pulseMs / 1000.0) * currentSampleRate));
                            const int leftCh = 0;
                            const bool haveLeft = (leftCh < buffer.getNumChannels());
                            if (! clickPulseCached)
                            {
                                // 1-sample spike
                                if (haveLeft)
                                    buffer.addSample(leftCh, clickOffset, outLevel);
                            }
                            else
                            {
                                // Pulse: hold high for pulseWidthMs, no decay
                                for (int s = 0; s < pulseSamples; ++s)
                                {
                                    const int idx = clickOffset + s;
                                    if (haveLeft && idx >= 0 && idx < numSamples)
                                        buffer.addSample(leftCh, idx, outLevel);
                                }
                                const int tailBeyond = (clickOffset + pulseSamples) - numSamples;
                                if (tailBeyond > 0)
                                    clickHoldRemainingSamples = juce::jmax(clickHoldRemainingSamples, tailBeyond);
                            }
                        };

                        if (targetPPQN <= resolutionAfter)
                        {
                            const int stepSpan = juce::jmax(1, resolutionAfter / targetPPQN);
                            if ((t % stepSpan) == 0)
                                emitClickSample(mappedOffset);
                        }
                        else
                        {
                            const int subClicks = targetPPQN / resolutionAfter;
                            const double tickSamples = samplesPerQuarter / (double) resolutionAfter;
                            for (int k = 0; k < subClicks; ++k)
                            {
                                const int clickOffset = mappedOffset + (int) std::round((double) k * (tickSamples / (double) subClicks));
                                emitClickSample(clickOffset);
                            }
                        }
                    }
                lastProcessedT = t;
            }
            lastTickIndex = lastProcessedT;
            // Consume the one-shot suppression
            suppressClockAtBoundaryOnce = false;
            generateAudioClicks(startSampleAtBoundary, numSamples - 1, shiftQAfter);
        }

        // extClock will be flushed once after scheduling (below)
    }

    // Generate Right Channel (Run Signal) with 2ms drop on Start
    if (buffer.getNumChannels() > 1)
    {
        const int rightCh = 1;
        buffer.clear(rightCh, 0, numSamples);
        
        const int dropDuration = (int)(0.002 * currentSampleRate);
        bool currentlyHigh = wasRunningAtStart; 
        
        for (int s = 0; s < numSamples; ++s)
        {
            if (s == startSampleForRunSignal)
            {
                runSignalDropSamples = dropDuration;
                currentlyHigh = true; // We are definitely running after a Start
            }
            
            bool outputHigh = currentlyHigh;
            
            if (runSignalDropSamples > 0)
            {
                outputHigh = false; // Force Low during drop
                runSignalDropSamples--;
            }
            
            if (outputHigh && clickEnabled) // Only output if click enabled (as per original logic)
            {
                buffer.addSample(rightCh, s, 1.0f);
            }
        }
    }

    // Flush any queued external MIDI messages gathered while scheduling.
    //
    // IMPORTANT (macOS): Avoid sendBlockOfMessagesNow() for clock streams.
    // That API sends all events immediately (no per-sample timing), which
    // collapses an entire audio block's worth of MIDI Clock into a burst.
    // Switching screens/apps (foreground changes) or brief CPU spikes can
    // otherwise trigger that path and knock external devices out of sync.
    flushExternalClock();

    // Per-tick clicks are written directly into the audio buffer in the scheduling loops above.

    lastWasPlaying = isPlaying;
    // Offset step change: schedule restart at the next occurrence of the selected step (modulo bar)
    if (resyncOffsetParamObj != nullptr)
    {
        const int cur = juce::jlimit(1, 16, resyncOffsetParamObj->get());
        if (cur != lastOffsetStep)
        {
            lastOffsetStep = cur;
            // Treat OFFSET changes as an explicit resync request. This is important
            // when pattern interval mode is active, because `pendingBarRestart` can
            // be deferred until the first pattern firing; the resync path is not.
            notifyResyncOffsetChanged();

            if (triggerModeEnabled.load(std::memory_order_relaxed))
            {
                pendingBarRestart.store(true, std::memory_order_relaxed);
                // no force to next bar; modulo handling in handleBarAlignedChanges
                uiNextRestartPending.store(true, std::memory_order_relaxed);
            }
        }
    }
}

// ---- Pattern helpers ----
void ClockSyncAudioProcessor::updatePatternParams()
{
    if (patternBarsParamObj != nullptr)
    {
        int newMode = patternBarsParamObj->getIndex();
        int oldMode = patternBarsMode.load(std::memory_order_relaxed);
        if (newMode != oldMode)
        {
            patternBarsMode.store(newMode, std::memory_order_relaxed);
            // Cancel any pending resync on mode change (stale targets invalid)
            resyncPending.store(false, std::memory_order_relaxed);
            resyncTargetBar.store(-1, std::memory_order_relaxed);
            resyncTargetStep.store(-1, std::memory_order_relaxed);
            patternModeJustChanged.store(true, std::memory_order_relaxed);
                // Changing interval should clear any pending pattern scheduling so
                // the new interval takes effect immediately (avoid firing on the
                // previously computed bar for the old interval).
                pendingPatternCount = 0;
                pendingPatternRestartTargetBar.store(-1, std::memory_order_relaxed);
                lastPatternFiredBar = -1;
                nextRandomPatternTargetBar = -1;

        }
    }
    if (patternStepsParamObj != nullptr)
    {
        const uint16_t oldMask = patternStepsMask.load(std::memory_order_relaxed);
        const uint16_t newMask = (uint16_t) juce::jlimit(0, 65535, patternStepsParamObj->get());
        if (oldMask == 0 && newMask != 0)
            patternMaskJustActivated.store(true, std::memory_order_relaxed);
        patternStepsMask.store(newMask, std::memory_order_relaxed);
    }
    if (triggerOffsetSamplesParamObj != nullptr)
    {
        triggerOffsetSamples.store(triggerOffsetSamplesParamObj->get(), std::memory_order_relaxed);
    }
}

int ClockSyncAudioProcessor::getPatternBarIntervalFromMode(int mode) const
{
    switch (mode)
    {
        case 1: return 1; // index 1 => bar interval 1
        case 2: return 2;
        case 3: return 4;
        case 4: return 8;
        case 5: return 16;
        case 6: return 32;
        case 7: return 64;
        default: return -1; // 0=OFF or RND (8) handled separately
    }
}

void ClockSyncAudioProcessor::preparePatternForBar(double barStartPPQ, double barLenQ)
{
    pendingPatternCount = 0;
    const uint16_t mask = patternStepsMask.load(std::memory_order_relaxed);
    if (mask != 0) {
        // Visual UI starts at 12 o'clock (top right, wedge 0 = step 1).
        // Stored bitmask indices directly represent logicalIndex (0..15).
        constexpr int visualRotation = 0; // identity mapping (0 offset)
        const double shiftQ = getCurrentShiftQ(barLenQ); // quantized or linear depending on ui.linearShuffleMode
        for (int logicalIndex = 0; logicalIndex < 16; ++logicalIndex)
        {
            const int storedIndex = (logicalIndex + visualRotation) & 15; // how it's stored in the mask
            if ((mask & (uint16_t(1) << storedIndex)) == 0) continue;
            const int triggerLogical = logicalIndex; // exact logical index
            double stepPosQ;
            if (shiftQ > 1e-12)
            {
                int pairIndex = triggerLogical / 2; // 0..7
                bool secondInPair = (triggerLogical & 1) == 1;
                double pairStart = pairIndex * 0.5; // each pair spans 0.5 QN
                stepPosQ = secondInPair ? (pairStart + 0.25 + shiftQ) : pairStart;
            }
            else
            {
                stepPosQ = (barLenQ / 16.0) * (double) triggerLogical;
            }
            // Snap exact bar-start triggers to the precise barStartPPQ to avoid
            // tiny FP rounding moving step 1 off the quantized boundary, unless offset is active.
            const int offsetSamples = triggerOffsetSamples.load(std::memory_order_relaxed);
            const double offsetQ = (samplesPerQuarter > 0.0) ? ((double) offsetSamples / samplesPerQuarter) : 0.0;
            double scheduledPPQ = barStartPPQ + stepPosQ + offsetQ;
            if (std::fabs(stepPosQ) < 1e-9 && offsetSamples == 0)
                scheduledPPQ = barStartPPQ;
            scheduledPPQ = juce::jmax(0.0, scheduledPPQ);
            if (pendingPatternCount < 16)
                pendingPatternPPQ[(size_t) pendingPatternCount++] = scheduledPPQ;
        }
    }
    std::sort(pendingPatternPPQ.begin(), pendingPatternPPQ.begin() + pendingPatternCount);
}

void ClockSyncAudioProcessor::rescheduleRemainingPatternTargets(double barStartPPQ, double barLenQ, double fromPPQ)
{
    if (pendingPatternCount == 0)
        return;
    const uint16_t mask = patternStepsMask.load(std::memory_order_relaxed);
    if (mask == 0) { pendingPatternCount = 0; return; }
    constexpr int visualRotation = 0;
    const double shiftQ = getCurrentShiftQ(barLenQ);
    int newCount = 0;
    for (int logicalIndex = 0; logicalIndex < 16; ++logicalIndex)
    {
        const int storedIndex = (logicalIndex + visualRotation) & 15;
        if ((mask & (uint16_t(1) << storedIndex)) == 0) continue;
        double stepPosQ;
        if (shiftQ > 1e-12 && std::fabs(barLenQ - 4.0) <= 1e-6)
        {
            int pairIndex = logicalIndex / 2;
            bool secondInPair = (logicalIndex & 1) == 1;
            double pairStart = pairIndex * 0.5;
            stepPosQ = secondInPair ? (pairStart + 0.25 + shiftQ) : pairStart;
        }
        else
        {
            stepPosQ = (barLenQ / 16.0) * (double) logicalIndex;
        }
        const int offsetSamples = triggerOffsetSamples.load(std::memory_order_relaxed);
        const double offsetQ = (samplesPerQuarter > 0.0) ? ((double) offsetSamples / samplesPerQuarter) : 0.0;
        double scheduledPPQ = barStartPPQ + stepPosQ + offsetQ;
        if (std::fabs(stepPosQ) < 1e-9 && offsetSamples == 0) scheduledPPQ = barStartPPQ;
        scheduledPPQ = juce::jmax(0.0, scheduledPPQ);
        if (scheduledPPQ >= fromPPQ - 1e-9 && newCount < 16)
            pendingPatternPPQ[(size_t) newCount++] = scheduledPPQ;
    }
    std::sort(pendingPatternPPQ.begin(), pendingPatternPPQ.begin() + newCount);
    pendingPatternCount = newCount;
}

// Attempt to schedule (or replace) resync target according to precedence rules.
// currentBar: 1-based bar where scheduling request occurs (first bar == 1).
// currentStep: 1..16 current step (approx) when request occurs.
// offsetStep: 1..16 target offset step (user selection).
// force: true for rate-change (sets regardless of gate idx8), false for Start-driven (only called when gate already TRUE).
void ClockSyncAudioProcessor::attemptScheduleResync(long long currentBar, int currentStep, int offsetStep, bool force)
{
    if (offsetStep < 1 || offsetStep > 16) return;
    // Honour the trigger-mode gate: if this is not a forced scheduling (rate change)
    // then require `triggerModeEnabled` (idx8) to be true. This centralises the
    // master resync gate so UI/parameter timing cannot accidentally override it.
    if (! force && ! triggerModeEnabled.load(std::memory_order_relaxed))
        return;
    // Determine target bar relative to current position
    long long targetBar = currentBar;
    if (offsetStep <= currentStep)
        targetBar = currentBar + 1; // wrap to next bar if offset already passed
    const bool already = resyncPending.load(std::memory_order_relaxed);
    long long existingBar = resyncTargetBar.load(std::memory_order_relaxed);
    int existingStep = resyncTargetStep.load(std::memory_order_relaxed);
    bool replace = false;
    if (!already)
        replace = true;
    else
    {
        // Shortest precedence: replace only if new target earlier than existing
        if (targetBar < existingBar || (targetBar == existingBar && offsetStep < existingStep))
            replace = true;
    }
    if (replace)
    {
        // If a pattern has already requested a deferred restart for this bar,
        // do not schedule an independent resync here — let the pattern's
        // deferred restart take precedence to avoid overlapping semantics
        // that can clear or shift the pattern stop/restart scheduling.
        const long long pendingTarget = pendingPatternRestartTargetBar.load(std::memory_order_relaxed);
        if (pendingTarget >= 0 && pendingTarget == targetBar)
        {
            return;
        }
        resyncTargetBar.store(targetBar, std::memory_order_relaxed);
        resyncTargetStep.store(offsetStep, std::memory_order_relaxed);
        resyncPending.store(true, std::memory_order_relaxed);

    }

}

void ClockSyncAudioProcessor::tryFirePattern(double ppqStart, double barLenQ, double barStartPPQ, int numSamples, juce::MidiBuffer& midi, juce::MidiBuffer& extClock, bool suppressDiag, bool preview)
{
    // In Sync Latch (GATE) mode, pattern firing is disabled.
    if (syncLatchEnabled.load(std::memory_order_relaxed))
        return;

    // When we are intentionally suppressing clocks/clicks until a scheduled restart boundary
    // (e.g. DAW restart mid-bar), do not emit pattern-driven Starts mid-bar.
    if (suppressUntilRestart)
        return;

    // Allow pattern-driven Starts to fire even when the internal `runActive` flag
    // is false. This makes pattern behaviour consistent with manual triggers: when
    // a pattern step issues a Start it should put the engine into the running
    // state immediately so any scheduled bar-aligned restarts take effect in
    // the same block. We still honour the Pattern Bars mode being OFF below.
    const int mode = patternBarsMode.load(std::memory_order_relaxed);
    if (mode == 0)
    {
        uiPatternBarActive.store(false, std::memory_order_relaxed);
        return; // OFF
    }
    const bool rnd = (mode == 8);
    // Determine whether the current or upcoming bar should trigger the pattern.
    // Previously pattern preparation only ran when the block began exactly at a bar start.
    // Prepare the pattern for the current bar if it should fire and hasn't been prepared yet
    // (this allows scheduling of steps that fall later in the same bar even if the block
    // started after the bar start).
    // Prefer host-provided bar count when available to sync across multiple plugin instances.
    // `uiExternalBarNumber` (1-based) is written in processBlock when the playhead provides barCount.
    // Previous logic used extBarNum-1 to convert to 0-based; now we use 1-based internal bar numbering.
    const int extBarNum = uiExternalBarNumber.load(std::memory_order_relaxed);
    // Prefer the host-provided bar number when available. Some hosts report
    // 0-based bar counts while our internal computation is 1-based. To avoid
    // a persistent off-by-one across hosts, compare the computed 1-based bar
    // and the host value; if they differ by exactly 1 assume the host is
    // 0-based and use the computed 1-based result instead.
    const long long computedBar = computeCurrentBar(ppqStart, barLenQ);
    long long currentBar = computedBar;
    if (extBarNum > 0)
    {
        if (computedBar - (long long) extBarNum == 1)
            currentBar = computedBar; // host appears 0-based, prefer computed 1-based
        else
            currentBar = (long long) extBarNum;
    }
    auto decideFireForBar = [&](long long candidateBar)->bool {
        if (rnd)
        {
            if (nextRandomPatternTargetBar < 0 || candidateBar >= nextRandomPatternTargetBar)
                return true;
            return false;
        }
        else
        {
            int interval = getPatternBarIntervalFromMode(mode);
            // Reverted semantics: fire ON every Nth DAW bar (interval), i.e. DAW bars N,2N,3N,...
            // candidateBar is 1-based internally, so check modulo directly.
            // Example: interval=4 -> fires when (candidateBar%4)==0 => candidateBar=4,8,12,...
            const bool res = (interval > 0 && ((candidateBar % interval) == 0));

            return res;
        }
    };

    if (decideFireForBar(currentBar) && lastPatternFiredBar != currentBar)
    {
        // Use the bar-start PPQ provided to this function (barStartPPQ) as authoritative
        // for scheduling. Update lastPatternFiredBar using the chosen bar index.
        lastPatternFiredBar = currentBar;
        preparePatternForBar(barStartPPQ, barLenQ);

    }

    // Determine whether a bar boundary (one or more) falls within this audio block.
    // The original implementation only prepared patterns when the block started exactly at a bar start
    // (ppqStart == barStartPPQ), which misses cases where the bar start is inside the block. We detect
    // candidate bar-start PPQ positions inside the block and prepare the pattern for the exact bar that fires.
    const double eps = 1e-6;
    const double ppqPerSample = 1.0 / samplesPerQuarter;
    const double blockEndPPQ = ppqStart + (double)(numSamples - 1) * ppqPerSample;

    // Use an explicit candidateBar index (when available from the host) so
    // that pattern firing decisions align across plugin instances that
    // receive the same DAW-provided bar count. The outer loop increments
    // the candidateBar value and passes it here rather than recomputing
    // from PPQ which could diverge from host bar numbering.
    auto considerBarStart = [&](double candidateBarStartPPQ, long long candidateBar)
    {
        // Guard: if we've already prepared/fired for this candidateBar in this block, skip duplicate preparation.
        if (candidateBar == lastPatternFiredBar)
            return;
        bool fire = false;
        if (rnd)
        {
            // Random mode: pick a future target bar; fire when reached then choose a new random interval.
            if (nextRandomPatternTargetBar < 0 || candidateBar >= nextRandomPatternTargetBar)
            {
                int intervalRnd = 1 + (int) (random.nextInt(16));
                nextRandomPatternTargetBar = candidateBar + intervalRnd;
                fire = true;
            }
        }
        else
        {
            // Recurring modulo gating: fire only on bars that are multiples of the selected interval.
            int interval = getPatternBarIntervalFromMode(mode); // returns -1 for OFF or RND
            if (interval < 0)
            {
                // OFF: do not auto-fire pattern.
            }
            else
            {
                if ((candidateBar % interval) == 0)
                {
                    // Suppress firing in the same block where mode changed (avoid immediate unintended Start)
                    if (patternModeJustChanged.load(std::memory_order_relaxed))
                    {
                        fire = false;
                    }
                    else
                    {
                        fire = true;
                    }
                }
            }
        }
        if (fire)
        {

            lastPatternFiredBar = candidateBar;
            // First pattern firing observed: allow bar restarts again.
            deferBarRestartUntilPattern.store(false, std::memory_order_relaxed);
            preparePatternForBar(candidateBarStartPPQ, barLenQ);
        }
    };

    // If the block begins exactly at a bar start, consider that bar.
    const int offsetSamples = triggerOffsetSamples.load(std::memory_order_relaxed);
    const double offsetQ = (samplesPerQuarter > 0.0) ? ((double) offsetSamples / samplesPerQuarter) : 0.0;
    const double lookaheadQ = (offsetQ < 0.0) ? -offsetQ : 0.0;

    // If the block begins near bar start (or with negative offset, if early target falls in this block), consider current bar:
    if (std::fabs(ppqStart - barStartPPQ) < eps || (offsetQ < 0.0 && barStartPPQ + offsetQ <= blockEndPPQ + eps && barStartPPQ + offsetQ >= ppqStart - eps))
        considerBarStart(barStartPPQ, currentBar);

    // Consider any future bar starts that fall within this block (taking into account negative offset lookahead):
    double nextBar = barStartPPQ + barLenQ;
    long long candidateBar = currentBar + 1;
    while ((nextBar - lookaheadQ) <= blockEndPPQ + eps)
    {
        if ((nextBar - lookaheadQ) >= ppqStart - eps)
            considerBarStart(nextBar, candidateBar);
        nextBar += barLenQ;
        ++candidateBar;
    }
    // Clear mode-change suppression flag after evaluating all candidate bars in this block.
    if (patternModeJustChanged.load(std::memory_order_relaxed))
        patternModeJustChanged.store(false, std::memory_order_relaxed);
    uiPatternBarActive.store(lastPatternFiredBar == currentBar || (mode == 1), std::memory_order_relaxed);
    if (pendingPatternCount == 0) return;
    // Emit Start messages for pattern PPQ times that fall within this block; remove them after emission.
    auto clockResolution = getClockResolution(); // not strictly needed
    std::array<double, 16> remaining {};
    int remainingCount = 0;
    int firedCount = 0;
    long long lastBarEmitted = std::numeric_limits<long long>::min();
    for (int pi = 0; pi < pendingPatternCount; ++pi)
    {
        const double targetPPQ = pendingPatternPPQ[(size_t) pi];
        if (targetPPQ < ppqStart - 1e-9)
        {

            continue; // already passed (late)
        }
        if (targetPPQ > blockEndPPQ + 1e-9)
        {

            if (remainingCount < 16) remaining[(size_t) remainingCount++] = targetPPQ;
            continue;
        }
        // Note: pendingPatternPPQ entries are already stored including any ms fine-tune
        // at prepare time. Use the stored targetPPQ directly as the emit point.
        double deltaQ = targetPPQ - ppqStart;
        int sampleOffset = fastRoundPositive(deltaQ * samplesPerQuarter);
        if (sampleOffset < 0 || sampleOffset >= numSamples)
        {
            if (remainingCount < 16) remaining[(size_t) remainingCount++] = targetPPQ;
            continue;
        }
        const auto startMsg = juce::MidiMessage::midiStart();
        // If the bar-restart path already emitted/consumed a Start at this sample,
        // avoid sending a duplicate and consume the pending indicators so UI/state
        // reflect the applied restart.
        if (sampleOffset == lastBarRestartStartSampleInBlock)
        {

            pendingBarRestart.store(false, std::memory_order_relaxed);
            uiNextRestartPending.store(false, std::memory_order_relaxed);
            // Clear any deferred pattern restart target to avoid re-scheduling.
            pendingPatternRestartTargetBar.store(-1, std::memory_order_relaxed);
            continue;
        }
        // Additional duplicate guard: if this pattern target already emitted earlier in this block (e.g. second tryFirePattern invocation), skip.
        if (sampleOffset == lastPatternStartSampleInBlock)
        {

            continue;
        }
        // Compute logical step for duplicate suppression before emission
        long long barForStart = computeCurrentBar(targetPPQ + lookaheadQ + 1e-4, barLenQ);
        // Compute logical step using current swing mapping (quantized or linear)
        int stepForStart;
        {
            const double posInBar = std::fmod(targetPPQ - barStartPPQ, barLenQ);
            const double shiftQ = getCurrentShiftQ(barLenQ);
            if (shiftQ <= 1e-12 || std::fabs(barLenQ - 4.0) > 1e-6)
            {
                int idx = (int) std::floor((posInBar / barLenQ) * 16.0);
                stepForStart = juce::jlimit(1, 16, idx + 1);
            }
            else
            {
                double bestDiff = 1e9; int bestIdx = 0;
                for (int logical = 0; logical < 16; ++logical)
                {
                    int pair = logical / 2;
                    bool second = (logical & 1) == 1;
                    double pairStart = pair * 0.5;
                    double stepPos = second ? (pairStart + 0.25 + shiftQ) : pairStart;
                    double d = std::fabs(posInBar - stepPos);
                    if (d < bestDiff)
                    {
                        bestDiff = d; bestIdx = logical;
                    }
                }
                stepForStart = bestIdx + 1;
            }
        }
        bool duplicate = (sampleOffset == lastPatternStartSampleInBlock) || (sampleOffset == lastBarRestartStartSampleInBlock);
        bool allowPatternStart = shouldAllowGeneratedStart();
        if (!duplicate && allowPatternStart)
        {
            midi.addEvent(startMsg, sampleOffset);
            extClock.addEvent(startMsg, sampleOffset);
            startSampleForRunSignal = sampleOffset;
            lastPatternStartSampleInBlock = sampleOffset;
            lastStartBar.store(barForStart, std::memory_order_relaxed);
            lastStartStep.store(stepForStart, std::memory_order_relaxed);
            lastBarEmitted = std::max(lastBarEmitted, barForStart);

            // If a deferred pattern-requested restart targets this same bar,
            // consume it now to avoid the bar-restart codepath re-emitting
            // the same Start later in this or the next block. This prevents
            // duplicate restart emissions when both scheduling paths coexist.
            {
                const long long pendingTarget = pendingPatternRestartTargetBar.load(std::memory_order_relaxed);
                if (pendingTarget >= 0 && pendingTarget == barForStart)
                {
                    pendingPatternRestartTargetBar.store(-1, std::memory_order_relaxed);
                    uiNextRestartPending.store(false, std::memory_order_relaxed);
                }
            }

        }

        ++firedCount; // count successful pattern Start emission for this block
        // record this sample so the bar-restart handler can avoid emitting a duplicate Start
        lastPatternStartSampleInBlock = sampleOffset;
        // When a pattern issues a Start we want the processor/UI internal
        // state to reflect that Start immediately so the visual step and
        // subsequent scheduling align with the emitted Start (matches manual trigger).
        // Compute the logical step (1..16) inside the bar for this targetPPQ and
        // update UI/run state accordingly.
        if (barLenQ > 0.0)
        {
            // Use centralized helper so shuffle/quantization match emission and duplicate suppression.
            int stepNumber = computeLogicalStepFromPPQ(targetPPQ, barLenQ, getCurrentShiftQ(barLenQ));
            uiStep16.store(stepNumber, std::memory_order_relaxed);
        }
        // Make the processor consider itself running so any restart/modifiers take effect.
        // For preview emissions, do not change run state.
        if (! preview)
        {
            runActive = true;
            pendingStart = false;
            uiIsRunning.store(true, std::memory_order_relaxed);
            uiPendingStart.store(false, std::memory_order_relaxed);
            // Enable audio pulses after pattern Start
            audioPulsesEnabled.store(true, std::memory_order_release);
        }
        // tryFirePattern diagnostic logging removed per user request
        // Respect trigger mode: schedule a bar+offset restart (same semantics as manual trigger idx1)
        const long long patternBar = computeCurrentBar(targetPPQ, barLenQ);
        if (!preview && triggerModeEnabled.load(std::memory_order_relaxed) && lastPatternRestartScheduledBar != patternBar)
        {
            // NOTE: previously we scheduled a deferred bar-aligned restart here
            // (pendingPatternRestartTargetBar) so that a pattern Start would also
            // request a restart at the next selected step (same semantics as an
            // explicit manual trigger). However this caused duplicate/late
            // restarts in some host/device setups because the pattern's own
            // step-trigger path already schedules retriggers. To avoid that
            // double-scheduling and the observed "one 16th late" behaviour,
            // disable the automatic deferred restart here. The explicit
            // trigger/retrigger path remains unchanged and will still schedule
            // any required restarts.
            // (Disabled: pendingPatternRestartTargetBar.store(...); uiNextRestartPending.store(...);)
        }
    }
    // Pattern completion resync scheduling (non-loop mode only).
    if (lastBarEmitted != std::numeric_limits<long long>::min())
    {
        const int modeNow = patternBarsMode.load(std::memory_order_relaxed);
        const bool loopMode = (modeNow == 1);
        const bool gate = triggerModeEnabled.load(std::memory_order_relaxed);
        if (!loopMode && gate)
        {
            const int offsetStep = resyncOffsetParamObj ? juce::jlimit(1, 16, resyncOffsetParamObj->get()) : 1;
            // Prefer the exact last emitted step when available to avoid
            // off-by-one scheduling caused by UI/storage index conversions.
            int lastEmitted = lastStartStep.load(std::memory_order_relaxed);
            int currentStepApprox = (lastEmitted >= 1 && lastEmitted <= 16)
                ? lastEmitted
                : juce::jlimit(1,16, uiStep16.load(std::memory_order_relaxed));
            // Do not schedule a completion resync while a shuffle change is pending.
            // Shuffle shifts second-16th PPQ positions and is applied at the next
            // 1/8 boundary; scheduling a resync while pendingShuffleStep is set
            // can cause duplicate restart emissions. Defer resync until shuffle
            // has been applied in `handleBarAlignedChanges`.
            if (pendingShuffleStep <= 0)
                attemptScheduleResync(lastBarEmitted, currentStepApprox, offsetStep, false);
        }
    }

    for (int i = 0; i < remainingCount; ++i)
        pendingPatternPPQ[(size_t) i] = remaining[(size_t) i];
    pendingPatternCount = remainingCount;
}

ClockSyncAudioProcessor::BarRestartWindow ClockSyncAudioProcessor::handleBarAlignedChanges(const juce::AudioPlayHead::CurrentPositionInfo& pos,
                                                                                           int numSamples,
                                                                                           juce::MidiBuffer& midi,
                                                                                           juce::MidiBuffer& extBuffer)
{
    BarRestartWindow window;
    const bool runWasActive = runActive;

    // Local snapshot: only send MIDI messages for restarts if Run is enabled or ClockWhileStopped is enabled
    const bool sendMidi = (parameters.getRawParameterValue(paramRun)->load() > 0.5f)
                          || (parameters.getRawParameterValue(paramClockWhileStopped)->load() > 0.5f);

    const double lastBar = pos.ppqPositionOfLastBarStart;
    const double barLenQ = (pos.timeSigNumerator > 0 && pos.timeSigDenominator > 0)
        ? (4.0 * (double) pos.timeSigNumerator / (double) pos.timeSigDenominator)
        : 4.0;
    const double ppqStart = pos.ppqPosition;
    const double posInBar = juce::jlimit(0.0, barLenQ, ppqStart - lastBar);
    double remainQ = barLenQ - posInBar;
    if (remainQ < 0.0) remainQ += barLenQ;
    // Default boundary at next bar start
    int sampleOffset = fastRoundPositive(remainQ * samplesPerQuarter);
    if (sampleOffset < 0) sampleOffset = 0;

    const bool haveRateChange = (pendingRateIndex >= 0);
    const bool haveShuffleChange = (pendingShuffleStep > 0); // shuffle by itself should NOT force a restart now
    const bool haveStart = pendingStart;
    bool haveBarRestart = pendingBarRestart.load(std::memory_order_relaxed);
    // Suppress automatic bar restart until the first pattern firing after host START when interval mode active
    if (haveBarRestart && deferBarRestartUntilPattern.load(std::memory_order_relaxed))
    {
        haveBarRestart = false;
    }
    const bool havePatternDeferred = (pendingPatternRestartTargetBar.load(std::memory_order_relaxed) >= 0);
    const bool applyShuffleWithBoundary = haveShuffleChange && (haveRateChange || haveStart || haveBarRestart || havePatternDeferred);

    // Compute the next occurrence of the selected step (1..16) at or after 'now'.
    // If the selected step lies ahead in the current bar, use it; otherwise target the same step in the next bar.
    const int offsetStep = resyncOffsetParamObj ? juce::jlimit(1, 16, resyncOffsetParamObj->get()) : 1;
    const int stepIndex0 = offsetStep - 1; // 0..15
    // Swing-aware mapping: if shuffle active (>1) and 4/4, recompute target boundary using shifted second 16th lengths.
    // Use master grid to compute target step position so all scheduling
    // paths (pattern, resync) agree exactly on swung/unswung positions.
    double stepQWithinBar = 0.0;
    {
        // Use linear/quantized shift via helper for target step position
        const double shiftQ = getCurrentShiftQ(barLenQ);
        const int idx = juce::jlimit(1, 16, offsetStep) - 1;
        double posInBar;
        if (shiftQ <= 1e-12 || std::fabs(barLenQ - 4.0) > 1e-6)
            posInBar = (barLenQ / 16.0) * (double) idx;
        else
        {
            int pair = idx / 2;
            bool second = (idx & 1) == 1;
            double pairStart = pair * 0.5;
            posInBar = second ? (pairStart + 0.25 + shiftQ) : pairStart;
        }
        stepQWithinBar = posInBar;
    }

    // Local flag to record whether a deferred pattern-requested restart matches
    // the scheduled boundary calculated below.
    bool deferredMatches = false;
    long long scheduledRestartBar = -1;

    if (haveStart || haveBarRestart || haveRateChange || havePatternDeferred) // exclude pure shuffle change
    {
        // Schedule at the next occurrence of the selected step within the bar (modulo behavior)
        double deltaQ = stepQWithinBar - posInBar;

        // Some operations (e.g. DAW restart mid-bar, rate-change realign) must always wrap
        // to the NEXT bar before applying the offset step. This prevents the clock from
        // "continuing" mid-bar and gives external gear a clean re-sync point.
        if (forceNextBarStart && (haveStart || haveRateChange))
        {
            deltaQ = (barLenQ - posInBar) + stepQWithinBar;
            // Consume the one-shot force flag so subsequent scheduling returns to modulo behaviour.
            forceNextBarStart = false;
        }
        else
        {
            if (deltaQ < -kBarEps)
                deltaQ += barLenQ; // wrap into the next bar if passed
            if (posInBar <= kBarEps && stepIndex0 == 0)
                deltaQ = 0.0; // allow immediate restart at bar start when offset=1
        }

        sampleOffset = fastRoundPositive(deltaQ * samplesPerQuarter);
        window.boundaryPPQ = ppqStart + deltaQ;
        // Determine which bar this scheduled restart will fall into. If a pattern
        // previously requested a deferred restart for that bar, mark the deferred
        // match so the restart is applied (and consume the deferred request).
        scheduledRestartBar = computeCurrentBar(ppqStart + deltaQ + 1e-9, barLenQ);
        const long long pendingTarget = pendingPatternRestartTargetBar.load(std::memory_order_relaxed);
        deferredMatches = (pendingTarget >= 0 && pendingTarget == scheduledRestartBar);
        if (deferredMatches)
            pendingPatternRestartTargetBar.store(-1, std::memory_order_relaxed);
    }

    // Resync firing: check whether pending target falls within this block.
    bool resyncMatches = false;
    int resyncStepForStart = -1;
    if (resyncPending.load(std::memory_order_relaxed))
    {
        const long long curBarForOffset = computeCurrentBar(ppqStart + (double) sampleOffset / samplesPerQuarter + 1e-9, barLenQ);
        const long long targetBar = resyncTargetBar.load(std::memory_order_relaxed);
        const int targetStep = resyncTargetStep.load(std::memory_order_relaxed);
        if (targetBar >= 0 && targetStep >= 1)
        {
            const double targetBarStartPPQ = (double) (targetBar - 1) * barLenQ;
            // Compute targetPPQ using unified shiftQ
            double targetPPQ = targetBarStartPPQ;
            {
                const double shiftQ = getCurrentShiftQ(barLenQ);
                const int idx = juce::jlimit(1, 16, targetStep) - 1;
                double posInBar;
                if (shiftQ <= 1e-12 || std::fabs(barLenQ - 4.0) > 1e-6)
                    posInBar = (barLenQ / 16.0) * (double) idx;
                else
                {
                    int pair = idx / 2;
                    bool second = (idx & 1) == 1;
                    double pairStart = pair * 0.5;
                    posInBar = second ? (pairStart + 0.25 + shiftQ) : pairStart;
                }
                targetPPQ += posInBar;
            }
            if (targetPPQ >= ppqStart - 1e-9)
            {
                double deltaQ = targetPPQ - ppqStart;
                int candidateOffset = fastRoundPositive(deltaQ * samplesPerQuarter);
                if (candidateOffset >= 0 && candidateOffset <= numSamples - 1)
                {
                    sampleOffset = candidateOffset;
                    window.boundaryPPQ = targetPPQ;
                    resyncMatches = true;
                    resyncStepForStart = targetStep;
                }
            }
            // Late consumption: target already passed before this block end; clear.
            const double blockEndPPQ = ppqStart + (double)(numSamples - 1) / samplesPerQuarter;
            if (targetPPQ < ppqStart - 1e-9 || targetPPQ < blockEndPPQ - 1e-9 && targetPPQ < ppqStart)
            {
                resyncPending.store(false, std::memory_order_relaxed);
                resyncTargetBar.store(-1, std::memory_order_relaxed);
                resyncTargetStep.store(-1, std::memory_order_relaxed);
            }
        }
    }

    // If Run is OFF, suppress resync/auto starts. Manual triggers re-enable runActive
    if (sampleOffset <= numSamples - 1 && (haveRateChange || haveStart || haveBarRestart || deferredMatches || resyncMatches))
    {
        jassert(sampleOffset >= 0); // debug-only: ensure non-negative scheduling
        // Gap length: use actual preceding 16th length under swing if possible (approx samples spanning previous half).
        double prevSixteenthLenQ = kSixteenthQ;
        if (getCurrentShiftQ(barLenQ) > 0.0 && std::fabs(barLenQ - 4.0) < 1e-6)
        {
            // Determine which 16th we restart at; previous length depends on whether this is second or first in pair.
            bool secondHalf = (stepIndex0 % 2) == 1;
            const double shiftQ = getCurrentShiftQ(barLenQ);
            prevSixteenthLenQ = secondHalf ? (0.25 + shiftQ) : (0.25 - shiftQ); // length of the preceding 16th segment
        }
        // Decide whether a Start/Stop should be emitted for this boundary.
        // We do NOT want to emit a Start when the only pending change is a
        // rate or shuffle parameter change (that would schedule an audible
        // restart and break bar-sync in hosts). Only emit Start when a real
        // start/restart was requested (haveStart/haveBarRestart/deferredMatches).
        const bool legacy = legacyModeEnabled.load(std::memory_order_relaxed);

        // In Sync Latch (GATE) mode, suppress all scheduled restarts/starts.
        // Only immediate Gate triggers (handled in generateClockAndClick via triggerArmedForNextSixteenth) are allowed.
        if (syncLatchEnabled.load(std::memory_order_relaxed))
        {
             if (resyncMatches) {
                 resyncPending.store(false, std::memory_order_relaxed);
                 resyncTargetBar.store(-1, std::memory_order_relaxed);
                 resyncTargetStep.store(-1, std::memory_order_relaxed);
                 resyncMatches = false;
             }
             haveBarRestart = false;
             deferredMatches = false;
        }

        // Emit Start for explicit reasons and realign only (rate change no longer directly emits a Start).
        bool shouldEmitStart = (haveStart || haveBarRestart || deferredMatches || resyncMatches || haveRateChange);

        if (syncLatchEnabled.load(std::memory_order_relaxed))
            shouldEmitStart = false;

        // Additional suppression: if this boundary is reached due purely to a pending initial start
        // (haveStart without barRestart/deferred) AND pattern interval mode is active but the pattern
        // has no active steps (mask==0), skip emitting a Start at the bar boundary. This prevents a
        // spurious Start at the first interval bar when the pattern is empty (user expectation: empty
        // pattern yields no Start events).
        // Suppress ANY bar-aligned Start emission (pendingStart, barRestart, deferred) when interval mode active and pattern empty.
        // Interval should only schedule potential pattern step evaluation; it must never itself cause a Start.
        // Empty pattern suppression applies only to pure pattern-related starts; allow rateChange or realign even if mask empty.
        if (shouldEmitStart && !resyncMatches && !haveRateChange)
        {
            int patternModeNow = patternBarsMode.load(std::memory_order_relaxed);
            if (patternModeNow > 0)
            {
                const uint16_t maskNow = patternStepsMask.load(std::memory_order_relaxed);
                if (maskNow == 0)
                {
                    shouldEmitStart = false;
   
                }
            }
        }
        int stopOffset = -1;

        // Only allow resyncMatches to bypass generated-start gating when the engine is running OR if we are explicitly starting now.
        const bool allowResyncBypass = resyncMatches && (runActive || haveStart);
        if (shouldEmitStart && (haveStart || shouldAllowGeneratedStart() || allowResyncBypass || haveRateChange))
        {
            const auto startMsg = juce::MidiMessage::midiStart();
            // If a pattern already emitted a Start at this exact sample earlier in
            // the block, avoid sending a duplicate and consume the pending
            // restart flags so the UI/state reflects the applied restart.
            if (sampleOffset == lastPatternStartSampleInBlock)
            {
                lastBarRestartStartSampleInBlock = sampleOffset;
                pendingBarRestart.store(false, std::memory_order_relaxed);
                uiNextRestartPending.store(false, std::memory_order_relaxed);
                pendingPatternRestartTargetBar.store(-1, std::memory_order_relaxed);
            }
            else
            {
                // Additional guard: if interval mode active and pattern empty, Start already suppressed above; no extra conditions.
                long long barForStart = computeCurrentBar(ppqStart + (double) sampleOffset / samplesPerQuarter, barLenQ);
                int stepForStart = resyncMatches ? resyncStepForStart : offsetStep;
                bool duplicate = isDuplicateStart(barForStart, stepForStart, sampleOffset);
                if (!duplicate && sendMidi)
                {
                    // Legacy Mode: Emit Stop before Start
                    if (legacy)
                    {
                        const int gapSamples = juce::jmax(1, fastRoundPositive(prevSixteenthLenQ * samplesPerQuarter) - 1);
                        stopOffset = juce::jmax(0, sampleOffset - gapSamples);
                        if (stopOffset >= sampleOffset)
                            stopOffset = juce::jmax(0, sampleOffset - 1);
                        
                        if (stopOffset < sampleOffset)
                        {
                            const auto stopMsg = juce::MidiMessage::midiStop();
                            midi.addEvent(stopMsg, stopOffset);
                            extBuffer.addEvent(stopMsg, stopOffset);
                            
                            // Send SPP if enabled
                            if (sppMode.load(std::memory_order_relaxed))
                            {
                                double stopPPQ = ppqStart + (double)stopOffset / samplesPerQuarter;
                                int spp = (int)(stopPPQ * 4.0);
                                auto sppMsg = juce::MidiMessage::songPositionPointer(spp);
                                midi.addEvent(sppMsg, stopOffset);
                                extBuffer.addEvent(sppMsg, stopOffset);
                            }
                        }
                    }

                    midi.addEvent(startMsg, sampleOffset);
                    extBuffer.addEvent(startMsg, sampleOffset);
                    startSampleForRunSignal = sampleOffset;
                    lastStartBar.store(barForStart, std::memory_order_relaxed);
                    lastStartStep.store(stepForStart, std::memory_order_relaxed);
                }

                lastBarRestartStartSampleInBlock = sampleOffset;
                if (resyncMatches)
                {
                    resyncPending.store(false, std::memory_order_relaxed);
                    resyncTargetBar.store(-1, std::memory_order_relaxed);
                    resyncTargetStep.store(-1, std::memory_order_relaxed);
                }
            }
            // Enable audio pulses after bar-aligned Start/resync
            audioPulsesEnabled.store(true, std::memory_order_relaxed);
        }

        else
        {
            // No Start/Stop emitted for rate-only or shuffle-only parameter changes.
            // Clear the marker so pattern path can still detect duplicates if needed.
            lastBarRestartStartSampleInBlock = -1;
        }
AfterBarRestartEmit:
        // Reflect selected step in UI at restart
        uiStep16.store(juce::jlimit(1, 16, offsetStep), std::memory_order_relaxed);

        // Schedule resync only for explicit restart requests (bar restart / deferred), not for a plain
        // "start from stopped" (pendingStart). Otherwise clicking the Run toggle (idx0) while the host
        // is playing can cause a second Start one bar later (pendingStart -> Start now, then resync).
        if ((haveBarRestart || deferredMatches) && triggerModeEnabled.load(std::memory_order_relaxed) && !resyncMatches)
        {
            long long curBar = computeCurrentBar(ppqStart, barLenQ);
            int currentStepApprox = juce::jlimit(1,16, uiStep16.load(std::memory_order_relaxed));
            attemptScheduleResync(curBar, currentStepApprox, offsetStep, false);
        }

        if (haveRateChange)
        {
            currentRateIndex = pendingRateIndex;
            uiActiveRateIndex.store(currentRateIndex, std::memory_order_relaxed);
            pendingRateIndex = -1;
            // changing resolution invalidates prior fractional carries -> reset
            resetAccumulators();
        }
        if (applyShuffleWithBoundary)
        {
            currentShuffleStep = pendingShuffleStep;
            pendingShuffleStep = -1;
            // shuffle applied as part of boundary: clear NEXT indicator if no other pending restart remain
        }
        if (haveStart || haveBarRestart || deferredMatches || haveRateChange || resyncMatches)
        {
            runActive = true;
            pendingStart = false;
            if (haveBarRestart || deferredMatches)
                pendingBarRestart.store(false, std::memory_order_relaxed);
            
            uiIsRunning.store(true, std::memory_order_relaxed);
            uiPendingStart.store(false, std::memory_order_relaxed);
            uiNextRestartPending.store(false, std::memory_order_relaxed);
            suppressUntilRestart = false; // allow clocks after scheduled restart
        }
        else if (applyShuffleWithBoundary)
        {
            // Clear NEXT when changes were applied without a separate start request
            uiNextRestartPending.store(false, std::memory_order_relaxed);
        }

        if (legacy && stopOffset >= 0)
        {
            window.gapStart = stopOffset;
            window.gapEnd = sampleOffset;
        }
        else
        {
            window.gapStart = -1;
            window.gapEnd = -1;
        }
        window.startSample = sampleOffset;

        if (runWasActive && haveStart && ! haveBarRestart && ! haveRateChange)
            suppressClockAtBoundaryOnce = true;

    }
    return window;
}

//==============================================================================
juce::AudioProcessorEditor* ClockSyncAudioProcessor::createEditor()
{
    return new ClockSyncAudioProcessorEditor(*this);
}

//==============================================================================
void ClockSyncAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
   #if CLOCKV3_DEMO
    // Demo build: do not persist settings into project state
    return;
   #endif
    auto state = parameters.copyState();
    // Persist the selected external device id
    if (externalDeviceId.isNotEmpty())
    {
        state.setProperty("externalDeviceId", externalDeviceId, nullptr);
        if (externalDeviceName.isNotEmpty())
            state.setProperty("externalDeviceName", externalDeviceName, nullptr);
    }

    // Persist MIDI Remote settings
    state.setProperty("midiRemoteStart", midiRemoteStart.load(), nullptr);
    state.setProperty("midiRemoteStop", midiRemoteStop.load(), nullptr);
    state.setProperty("midiRemoteTrigger", midiRemoteTrigger.load(), nullptr);
    state.setProperty("midiRemoteOffset", midiRemoteOffset.load(), nullptr);
    state.setProperty("midiRemoteShuffle", midiRemoteShuffle.load(), nullptr);
    state.setProperty("midiRemoteClockDiv", midiRemoteClockDiv.load(), nullptr);
    state.setProperty("midiRemoteResync", midiRemoteResync.load(), nullptr);
    state.setProperty("midiRemoteGatedSync", midiRemoteGatedSync.load(), nullptr);
    state.setProperty("midiRemoteAutoFill", midiRemoteAutoFill.load(), nullptr);

    // Persist NOTE/CC mode for remotes that can be either
    state.setProperty("midiRemoteStartIsCC", midiRemoteStartIsCC.load(std::memory_order_relaxed), nullptr);
    state.setProperty("midiRemoteStopIsCC", midiRemoteStopIsCC.load(std::memory_order_relaxed), nullptr);
    state.setProperty("midiRemoteTriggerIsCC", midiRemoteTriggerIsCC.load(std::memory_order_relaxed), nullptr);
    state.setProperty("midiRemoteResyncIsCC", midiRemoteResyncIsCC.load(std::memory_order_relaxed), nullptr);
    state.setProperty("midiRemoteGatedSyncIsCC", midiRemoteGatedSyncIsCC.load(std::memory_order_relaxed), nullptr);

    // Ensure editor-only UI properties are included in the saved state.
    // Some hosts may only persist parameter children — merge a small set of
    // known UI properties from the live APVTS state so they are always stored
    // with the plugin state (pattern-edit toggle, name/midi toggle, submenu,
    // selected instrument and instrumentNames list).
    {
        auto& live = parameters.state;
        if (live.hasProperty("ui.patternEditMode"))
            state.setProperty("ui.patternEditMode", live.getProperty("ui.patternEditMode"), nullptr);
        if (live.hasProperty("ui.showNameMode"))
            state.setProperty("ui.showNameMode", live.getProperty("ui.showNameMode"), nullptr);
        if (live.hasProperty("ui.setupSubmenuOn"))
            state.setProperty("ui.setupSubmenuOn", live.getProperty("ui.setupSubmenuOn"), nullptr);
        if (live.hasProperty("ui.selectedInstrument"))
            state.setProperty("ui.selectedInstrument", live.getProperty("ui.selectedInstrument"), nullptr);
        if (live.hasProperty("instrumentNames"))
            state.setProperty("instrumentNames", live.getProperty("instrumentNames"), nullptr);
        if (live.hasProperty("ui.linearShuffleMode"))
            state.setProperty("ui.linearShuffleMode", live.getProperty("ui.linearShuffleMode"), nullptr);
        if (live.hasProperty("ui.sppMode"))
            state.setProperty("ui.sppMode", live.getProperty("ui.sppMode"), nullptr);
        // Persist legacy mode (inverse of MODERN button)
        state.setProperty("ui.legacyMode", legacyModeEnabled.load(), nullptr);
    }

    // Save UI Colors
    auto uiState = state.getOrCreateChildWithName("UiTheme", nullptr);
    uiState.setProperty("accent", theme.accent().toString(), nullptr);
    uiState.setProperty("base", theme.base().toString(), nullptr);
    uiState.setProperty("cyan", theme.cyan().toString(), nullptr);

    if (auto xml = state.createXml()) {
        // pulseWidthMs is now stored in APVTS, no need to save manually
        copyXmlToBinary(*xml, destData);
    }
}

void ClockSyncAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
   #if CLOCKV3_DEMO
    // Demo build: do not restore saved settings from project state
    juce::ignoreUnused(data, sizeInBytes);
    return;
   #endif
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (xml->hasTagName(parameters.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml(*xml);
            parameters.replaceState(vt);
            // Restore external device id if present
            externalDeviceId = vt.getProperty("externalDeviceId").toString();
            externalDeviceName = vt.getProperty("externalDeviceName").toString();

            // Restore MIDI Remote settings
            midiRemoteStart.store(vt.getProperty("midiRemoteStart", 1), std::memory_order_relaxed);
            midiRemoteStop.store(vt.getProperty("midiRemoteStop", 3), std::memory_order_relaxed);
            midiRemoteTrigger.store(vt.getProperty("midiRemoteTrigger", 6), std::memory_order_relaxed);
            midiRemoteOffset.store(vt.getProperty("midiRemoteOffset", -1), std::memory_order_relaxed);
            midiRemoteShuffle.store(vt.getProperty("midiRemoteShuffle", -1), std::memory_order_relaxed);
            midiRemoteClockDiv.store(vt.getProperty("midiRemoteClockDiv", -1), std::memory_order_relaxed);
            midiRemoteResync.store(vt.getProperty("midiRemoteResync", 8), std::memory_order_relaxed);
            midiRemoteGatedSync.store(vt.getProperty("midiRemoteGatedSync", 10), std::memory_order_relaxed);
            midiRemoteAutoFill.store(vt.getProperty("midiRemoteAutoFill", -1), std::memory_order_relaxed);

            // Restore NOTE/CC mode for remotes that can be either
            midiRemoteStartIsCC.store((bool) vt.getProperty("midiRemoteStartIsCC", false), std::memory_order_relaxed);
            midiRemoteStopIsCC.store((bool) vt.getProperty("midiRemoteStopIsCC", false), std::memory_order_relaxed);
            midiRemoteTriggerIsCC.store((bool) vt.getProperty("midiRemoteTriggerIsCC", false), std::memory_order_relaxed);
            midiRemoteResyncIsCC.store((bool) vt.getProperty("midiRemoteResyncIsCC", false), std::memory_order_relaxed);
            midiRemoteGatedSyncIsCC.store((bool) vt.getProperty("midiRemoteGatedSyncIsCC", false), std::memory_order_relaxed);

            // Restore legacy mode (default false -> Modern)
            if (vt.hasProperty("ui.legacyMode"))
                legacyModeEnabled.store((bool)vt.getProperty("ui.legacyMode"), std::memory_order_relaxed);

            // Load UI Colors
            auto uiState = vt.getChildWithName("UiTheme");
            if (uiState.isValid())
            {
                if (uiState.hasProperty("accent")) theme.setAccent(juce::Colour::fromString(uiState.getProperty("accent").toString()));
                if (uiState.hasProperty("base"))   theme.setBase(juce::Colour::fromString(uiState.getProperty("base").toString()));
                if (uiState.hasProperty("cyan"))   theme.setCyan(juce::Colour::fromString(uiState.getProperty("cyan").toString()));
                
                if (onThemeChanged) onThemeChanged();
            }

            updateDerivedParams();
            updateExternalOut();
        }
    }
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ClockSyncAudioProcessor();
}

int ClockSyncAudioProcessor::getPendingPatternCount() const
{
    return pendingPatternCount;
}

int ClockSyncAudioProcessor::getPendingShuffleStep() const
{
    return pendingShuffleStep;
}

