/*
  ==============================================================================

    Track.h
    Created: 12 May 2021 4:14:25pm
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "Clip.h"

class Track
{
public:
    Track(std::function<juce::Range<double>()> playheadParentSliceGetter,
          std::function<double()> globalBpmGetter,
          std::function<double()> sampleRateGetter,
          std::function<int()> samplesPerBlockGetter
          );
    
    void prepareClips();
    int getNumberOfClips();
    int getMidiOutChannel();
    
    void clipsProcessSlice(juce::MidiBuffer& incommingBuffer, juce::MidiBuffer& bufferToFill, int bufferSize);
    void clipsRenderRemainingNoteOffsIntoMidiBuffer(juce::MidiBuffer& bufferToFill);
    void clipsResetPlayheadPosition();
    
    Clip* getClipAt(int clipN);

private:
    
    std::function<juce::Range<double>()> getPlayheadParentSlice;
    std::function<double()> getGlobalBpm;
    std::function<double()> getSampleRate;
    std::function<int()> getSamplesPerBlock;
    
    int nTestClips = 4;
    juce::OwnedArray<Clip> midiClips;
    
    int midiOutChannel = 1;
    
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Track)
};
