/*
  ==============================================================================

    Playhead.h
    Created: 9 May 2021 9:50:05am
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "helpers_shepherd.h"

class Playhead
{
public:
    Playhead(const juce::ValueTree& state,
             std::function<juce::Range<double>()> parentSliceGetter,
             std::function<double()> localSliceLengthGetter);
    void bindState();
    void updateStateMemberVersions();
    juce::ValueTree state;

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
    void clearPlayCue();
    void clearStopCue();

    void captureSlice();
    void releaseSlice();
    void resetSlice();
    void resetSlice(double sliceOffset);

    juce::Range<double> getCurrentSlice() const noexcept;
    std::function<juce::Range<double>()> getParentSlice;
    std::function<double()> getLocalSliceLength;

private:
    juce::Range<double> currentSlice { 0.0, 0.0 };
    double playheadPositionInBeats = ShepherdDefaults::playheadPosition;
    bool playing = ShepherdDefaults::playing;
    double willPlayAt = ShepherdDefaults::willPlayAt;
    double willStopAt = ShepherdDefaults::willStopAt;
    
    juce::CachedValue<double> statePlayheadPositionInBeats;  // Used only so that current position is somehow stored in the state
    juce::CachedValue<bool> statePlaying;
    juce::CachedValue<double> stateWillPlayAt;
    juce::CachedValue<double> stateWillStopAt;
    bool hasJustStoppedFlag = false;
};
