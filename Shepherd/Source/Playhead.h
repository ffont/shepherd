/*
  ==============================================================================

    Playhead.h
    Created: 9 May 2021 9:50:05am
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>


class Playhead
{
public:
    Playhead(std::function<juce::Range<double>()> parentSliceGetter);

    void playNow();
    void playNow(double sliceOffset);
    void playAt(double positionInParent);

    void stopNow();
    void stopAt(double positionInParent);

    bool isPlaying() const;
    bool isCuedToPlay() const;
    bool isCuedToStop() const;
    bool hasJustStopped();
    double getPlayAtCueBeats() const;
    double getStopAtCueBeats() const;

    void captureSlice();
    void releaseSlice();
    void resetSlice();
    void resetSlice(double sliceOffset);

    juce::Range<double> getCurrentSlice() const noexcept;
    std::function<juce::Range<double>()> getParentSlice;

private:
    juce::Range<double> currentSlice { 0.0, 0.0 };
    bool playing = false;
    bool hasJustStoppedFlag = false;
    double willPlayAt = -1.0;
    double willStopAt = -1.0;
};
