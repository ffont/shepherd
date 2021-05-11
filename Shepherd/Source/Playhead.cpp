/*
  ==============================================================================

    Playhead.cpp
    Created: 9 May 2021 9:50:05am
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#include "Playhead.h"

Playhead::Playhead(std::function<juce::Range<double>()> parentSliceGetter)
{
    getParentSlice = parentSliceGetter;
}

void Playhead::playNow()
{
    playNow(0.0);
}

void Playhead::playNow(double sliceOffset)
{
    currentSlice = { -sliceOffset, -sliceOffset }; // Reset position to the indicated offset so that play event is triggered sample accurate
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
    jassert(isCuedToPlay() == true);  // Getting play at beats without being cued to play
    return willPlayAt;
}

double Playhead::getStopAtCueBeats() const
{
    jassert(isCuedToStop() == true);  // Getting stop at beats without being cued to stop
    return willStopAt;
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
}

juce::Range<double> Playhead::getCurrentSlice() const noexcept
{
    return currentSlice;
}

void Playhead::resetSlice()
{
    currentSlice = {0.0, 0.0};
}
