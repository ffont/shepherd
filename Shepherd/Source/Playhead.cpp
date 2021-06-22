/*
  ==============================================================================

    Playhead.cpp
    Created: 9 May 2021 9:50:05am
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#include "Playhead.h"

Playhead::Playhead(const juce::ValueTree& _state, std::function<juce::Range<double>()> parentSliceGetter): state(_state)
{
    getParentSlice = parentSliceGetter;
    bindState();
}

void Playhead::bindState()
{
    playing.referTo(state, IDs::playing, nullptr, Defaults::playing);
    willPlayAt.referTo(state, IDs::willPlayAt, nullptr, Defaults::willPlayAt);
    willStopAt.referTo(state, IDs::willStopAt, nullptr, Defaults::willStopAt);
    playheadPositionInBeats.referTo(state, IDs::playheadPositionInBeats, nullptr, Defaults::playheadPosition);
}

void Playhead::playNow()
{
    playNow(0.0);
}

void Playhead::playNow(double sliceOffset)
{
    resetSlice(sliceOffset);  // Reset position to the indicated offset so that play event is triggered sample accurate
    willPlayAt = -1.0;
    playing = true;
    hasJustStoppedFlag = false;
}

void Playhead::playAt(double positionInParent)
{
    willPlayAt = positionInParent;
}

void Playhead::stopNow()
{
    willStopAt = -1.0;
    playing = false;
    hasJustStoppedFlag = true;
}

void Playhead::stopAt(double positionInParent)
{
    willStopAt = positionInParent;
}

bool Playhead::isPlaying() const
{
    return playing;
}

bool Playhead::isCuedToPlay() const
{
    return willPlayAt >= 0.0;
}

bool Playhead::isCuedToStop() const
{
    return willStopAt >= 0.0;
}

bool Playhead::hasJustStopped()
{
    // This funciton will return true the first time it is called after the Playhead has been stopped
    // Starting the playhead resets the flag (even if this function was never called)
    if (hasJustStoppedFlag){
        hasJustStoppedFlag = false;
        return true;
    } else {
        return false;
    }
}

double Playhead::getPlayAtCueBeats() const
{
    return willPlayAt;
}

double Playhead::getStopAtCueBeats() const
{
    return willStopAt;
}

void Playhead::clearPlayCue()
{
    willPlayAt = -1.0;
}

void Playhead::clearStopCue()
{
    willStopAt = -1.0;
}

void Playhead::captureSlice()
{
    if (! playing)
        return;
    
    const auto parentSliceLength = getParentSlice().getLength();
    currentSlice.setEnd(currentSlice.getStart() + parentSliceLength);
}

void Playhead::releaseSlice()
{
    currentSlice.setStart(currentSlice.getEnd());
    playheadPositionInBeats = currentSlice.getStart();
}

juce::Range<double> Playhead::getCurrentSlice() const noexcept
{
    return currentSlice;
}

void Playhead::resetSlice()
{
    currentSlice = {0.0, 0.0};
    playheadPositionInBeats = currentSlice.getStart();
}

void Playhead::resetSlice(double sliceOffset)
{
    currentSlice = {-sliceOffset, -sliceOffset};
    playheadPositionInBeats = currentSlice.getStart();
}
