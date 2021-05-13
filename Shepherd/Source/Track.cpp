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

void Track::setMidiOutChannel(int newMidiOutChannel)
{
    midiOutChannel = newMidiOutChannel;
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

void Track::stopAllPlayingClips(bool now)
{
    stopAllPlayingClipsExceptFor(-1, now);
}

void Track::stopAllPlayingClipsExceptFor(int clipN, bool now)
{
    jassert(clipN < midiClips.size());
    for (int i=0; i<midiClips.size(); i++){
        if (i != clipN){
            auto clip = midiClips[i];
            if (clip->isPlaying()){
                if (!now){
                    clip->togglePlayStop();
                } else {
                    clip->stopNow();
                }
            }
        }
    }
}

std::vector<int> Track::getCurrentlyPlayingClipsIndex()
{
    std::vector<int> currentlyPlayingClips = {};
    for (int i=0; i<midiClips.size(); i++){
        auto clip = midiClips[i];
        if (clip->isPlaying()){
            currentlyPlayingClips.push_back(i);
        }
    }
    return currentlyPlayingClips;
}
