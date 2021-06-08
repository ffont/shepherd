/*
  ==============================================================================

    Track.h
    Created: 12 May 2021 4:14:25pm
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "defines.h"
#include "Clip.h"
#include "MusicalContext.h"
#include "HardwareDevice.h"

class Track
{
public:
    Track(std::function<juce::Range<double>()> playheadParentSliceGetter,
          std::function<GlobalSettingsStruct()> globalSettingsGetter,
          std::function<MusicalContext()> musicalContextGetter
          );
    
    void setHardwareDevice(HardwareDevice* device);
    juce::String getMidiOutputDeviceName();
    int getMidiOutputChannel();
    
    void prepareClips();
    int getNumberOfClips();
    
    void processInputMonitoring(juce::MidiBuffer& incommingBuffer, juce::MidiBuffer& bufferToFill);
    
    void clipsProcessSlice(juce::MidiBuffer& incommingBuffer, juce::MidiBuffer& bufferToFill, int bufferSize, std::vector<juce::MidiMessage>& lastMidiNoteOnMessages);
    void clipsRenderRemainingNoteOffsIntoMidiBuffer(juce::MidiBuffer& bufferToFill);
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
    std::function<MusicalContext()> getMusicalContext;
    
    int nClips = 0;
    juce::OwnedArray<Clip> midiClips;
    
    bool inputMonitoring = false;
    
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Track)
};
