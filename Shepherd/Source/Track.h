/*
  ==============================================================================

    Track.h
    Created: 12 May 2021 4:14:25pm
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "helpers.h"
#include "Clip.h"
#include "MusicalContext.h"
#include "HardwareDevice.h"


class Track
{
public:
    Track(const juce::ValueTree& state,
          std::function<juce::Range<double>()> playheadParentSliceGetter,
          std::function<GlobalSettingsStruct()> globalSettingsGetter,
          std::function<MusicalContext*()> musicalContextGetter,
          std::function<HardwareDevice*(juce::String deviceName)> hardwareDeviceGetter,
          std::function<MidiOutputDeviceData*(juce::String deviceName)> midiOutputDeviceDataGetter
          );
    void bindState();
    juce::ValueTree state;
    
    juce::String getUUID() { return uuid.get(); };
    juce::String getName() { return name.get(); };
    
    void setOutputHardwareDeviceByName(juce::String deviceName);
    HardwareDevice* getOutputHardwareDevice();
    
    juce::String getMidiOutputDeviceName();
    int getMidiOutputChannel();
    
    void prepareClips();
    int getNumberOfClips();
    
    void processInputMonitoring(juce::MidiBuffer& incommingBuffer);
    
    void clipsProcessSlice(juce::MidiBuffer& incommingBuffer, juce::Array<juce::MidiMessage>& lastMidiNoteOnMessages);
    void clipsPrepareSliceSlice();
    void clipsRenderRemainingNoteOffsIntoMidiBuffer();
    void clipsResetPlayheadPosition();
    
    Clip* getClipAt(int clipN);
    Clip* getClipWithUUID(juce::String clipUUID);
    void stopAllPlayingClips(bool now, bool deCue, bool reCue);
    void stopAllPlayingClipsExceptFor(int clipN, bool now, bool deCue, bool reCue);
    void stopAllPlayingClipsExceptFor(juce::String clipUUID, bool now, bool deCue, bool reCue);
    std::vector<int> getCurrentlyPlayingClipsIndex();
    void duplicateClipAt(int clipN);
    
    bool hasClipsCuedToRecord();
    bool inputMonitoringEnabled();
    
    void setInputMonitoring(bool enabled);
    
    void clearLastSliceMidiBuffer();
    juce::MidiBuffer* getLastSliceMidiBuffer();
    void writeLastSliceMidiBufferToHardwareDeviceMidiBuffer();

private:
    
    juce::CachedValue<juce::String> uuid;
    juce::CachedValue<juce::String> name;
    juce::CachedValue<int> order;
    
    juce::CachedValue<juce::String> hardwareDeviceName;
    juce::CachedValue<bool> inputMonitoring;
    
    HardwareDevice* outputHwDevice = nullptr;
    void setOutputHardwareDevice(HardwareDevice* device);
    
    juce::MidiBuffer lastSliceMidiBuffer;
    
    std::function<juce::Range<double>()> getPlayheadParentSlice;
    std::function<GlobalSettingsStruct()> getGlobalSettings;
    std::function<MusicalContext*()> getMusicalContext;
    std::function<HardwareDevice*(juce::String deviceName)> getHardwareDeviceByName;
    std::function<MidiOutputDeviceData*(juce::String deviceName)> getMidiOutputDeviceData;
    juce::MidiBuffer* getMidiOutputDeviceBufferIfDevice();
    
    std::unique_ptr<ClipList> clips;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Track)
};

struct TrackList: public drow::ValueTreeObjectList<Track>
{
    TrackList (const juce::ValueTree& v,
               std::function<juce::Range<double>()> playheadParentSliceGetter,
               std::function<GlobalSettingsStruct()> globalSettingsGetter,
               std::function<MusicalContext*()> musicalContextGetter,
               std::function<HardwareDevice*(juce::String deviceName)> hardwareDeviceGetter,
               std::function<MidiOutputDeviceData*(juce::String deviceName)> midiOutputDeviceDataGetter)
    : drow::ValueTreeObjectList<Track> (v)
    {
        getPlayheadParentSlice = playheadParentSliceGetter;
        getGlobalSettings = globalSettingsGetter;
        getMusicalContext = musicalContextGetter;
        getHardwareDeviceByName = hardwareDeviceGetter;
        getMidiOutputDeviceData = midiOutputDeviceDataGetter;
        rebuildObjects();
    }

    ~TrackList()
    {
        freeObjects();
    }

    bool isSuitableType (const juce::ValueTree& v) const override
    {
        return v.hasType (IDs::TRACK);
    }

    Track* createNewObject (const juce::ValueTree& v) override
    {
        return new Track (v,
                          getPlayheadParentSlice,
                          getGlobalSettings,
                          getMusicalContext,
                          getHardwareDeviceByName,
                          getMidiOutputDeviceData);
    }

    void deleteObject (Track* c) override
    {
        delete c;
    }

    void newObjectAdded (Track*) override    {}
    void objectRemoved (Track*) override     {}
    void objectOrderChanged() override       {}
    
    Track* getObjectWithUUID(const juce::String& uuid) {
        for (auto* object: objects){
            if (object->getUUID() == uuid){
                return object;
            }
        }
        return nullptr;
    }
    
    std::function<juce::Range<double>()> getPlayheadParentSlice;
    std::function<GlobalSettingsStruct()> getGlobalSettings;
    std::function<MusicalContext*()> getMusicalContext;
    std::function<HardwareDevice*(juce::String deviceName)> getHardwareDeviceByName;
    std::function<MidiOutputDeviceData*(juce::String deviceName)> getMidiOutputDeviceData;
};
