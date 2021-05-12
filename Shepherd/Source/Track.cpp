/*
  ==============================================================================

    Track.cpp
    Created: 12 May 2021 4:14:25pm
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#include "Track.h"

Track::Track(std::function<juce::Range<double>()> playheadParentSliceGetter,
                  std::function<double()> globalBpmGetter,
                  std::function<double()> sampleRateGetter,
                  std::function<int()> samplesPerBlockGetter
                  )
{
    getPlayheadParentSlice = playheadParentSliceGetter;
    getGlobalBpm = globalBpmGetter;
    getSampleRate = sampleRateGetter;
    getSamplesPerBlock = samplesPerBlockGetter;
}


void Track::prepareClips()
{
    for (int i=0; i<nTestClips; i++){
        midiClips.add(
          new Clip(
                getPlayheadParentSlice,
                getGlobalBpm,
                getSampleRate,
                getSamplesPerBlock,
                [this]{ return midiOutChannel; }
        ));
    }
}

int Track::getNumberOfClips()
{
    return midiClips.size();
}

int Track::getMidiOutChannel()
{
    return midiOutChannel;
}

void Track::clipsProcessSlice(juce::MidiBuffer& incommingBuffer, juce::MidiBuffer& bufferToFill, int bufferSize)
{
    for (auto clip: midiClips){
        clip->processSlice(incommingBuffer, bufferToFill, bufferSize);
    }
}

void Track::clipsRenderRemainingNoteOffsIntoMidiBuffer(juce::MidiBuffer& bufferToFill)
{
    for (auto clip: midiClips){
        clip->renderRemainingNoteOffsIntoMidiBuffer(bufferToFill);
    }
}

void Track::clipsResetPlayheadPosition()
{
    for (auto clip: midiClips){
        clip->resetPlayheadPosition();
    }
}

Clip* Track::getClipAt(int clipN)
{
    jassert(clipN < midiClips.size());
    return midiClips[clipN];
}
