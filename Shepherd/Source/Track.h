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
    Track(std::function<juce::Range<double>()> playheadParentSliceGetter,
          std::function<GlobalSettingsStruct()> globalSettingsGetter,
          std::function<MusicalContext*()> musicalContextGetter,
          std::function<juce::MidiBuffer*(juce::String deviceName)> midiOutputDeviceBufferGetter
          );
    
    void setHardwareDevice(HardwareDevice* device);
    HardwareDevice* getHardwareDevice();
    
    juce::String getMidiOutputDeviceName();
    int getMidiOutputChannel();
    
    void prepareClips();
    int getNumberOfClips();
    
    void processInputMonitoring(juce::MidiBuffer& incommingBuffer);
    
    void clipsProcessSlice(juce::MidiBuffer& incommingBuffer, std::vector<juce::MidiMessage>& lastMidiNoteOnMessages);
    void clipsRenderRemainingNoteOffsIntoMidiBuffer();
    void clipsResetPlayheadPosition();
    
    Clip* getClipAt(int clipN);
    void stopAllPlayingClips(bool now, bool deCue, bool reCue);
    void stopAllPlayingClipsExceptFor(int clipN, bool now, bool deCue, bool reCue);
    std::vector<int> getCurrentlyPlayingClipsIndex();
    void insertClipAt(int clipN, Clip* clip);
    
    bool hasClipsCuedToRecord();
    bool inputMonitoringEnabled();
    
    void setInputMonitoring(bool enabled);

private:
    
    HardwareDevice* device;
    
    std::function<juce::Range<double>()> getPlayheadParentSlice;
    std::function<GlobalSettingsStruct()> getGlobalSettings;
    std::function<MusicalContext*()> getMusicalContext;
    std::function<juce::MidiBuffer*(juce::String deviceName)> getMidiOutputDeviceBuffer;
    juce::MidiBuffer* getMidiOutputDeviceBufferIfDevice();
    
    int nClips = 0;
    juce::OwnedArray<Clip> midiClips;
    
    bool inputMonitoring = false;
    
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Track)
};
