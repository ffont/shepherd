/*
  ==============================================================================

    Track.cpp
    Created: 12 May 2021 4:14:25pm
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#include "Track.h"

Track::Track(std::function<juce::Range<double>()> playheadParentSliceGetter,
             std::function<GlobalSettingsStruct()> globalSettingsGetter,
             std::function<MusicalContext()> musicalContextGetter)
{
    getPlayheadParentSlice = playheadParentSliceGetter;
    getGlobalSettings = globalSettingsGetter;
    getMusicalContext = musicalContextGetter;
    nClips = getGlobalSettings().nScenes;
}

void Track::setMidiOutChannel(int newMidiOutChannel)
{
    midiOutChannel = newMidiOutChannel;
}


void Track::prepareClips()
{
    for (int i=0; i<nClips; i++){
        midiClips.add(
          new Clip(
                getPlayheadParentSlice,
                getGlobalSettings,
                [this]{
                    TrackSettingsStruct settings;
                    settings.midiOutChannel = midiOutChannel;
                    return settings;
                },
                getMusicalContext
        ));
    }
}

int Track::getNumberOfClips()
{
    return midiClips.size();
}

void Track::processInputMonitoring(juce::MidiBuffer& incommingBuffer, juce::MidiBuffer& bufferToFill)
{
    if (inputMonitoringEnabled()){
        for (const auto metadata : incommingBuffer)
        {
            auto msg = metadata.getMessage();
            msg.setChannel(midiOutChannel);
            bufferToFill.addEvent(msg, metadata.samplePosition);
        }
    }
}

void Track::clipsProcessSlice(juce::MidiBuffer& incommingBuffer, juce::MidiBuffer& bufferToFill, int bufferSize, std::vector<juce::MidiMessage>& lastMidiNoteOnMessages)
{
    for (auto clip: midiClips){
        clip->processSlice(incommingBuffer, bufferToFill, bufferSize, lastMidiNoteOnMessages);
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

/** Stop all track clips that are currently playing
    @param now             stop clips immediately, otherwise wait until next quatized step
    @param deCue         de-cue all clips cued to play or record but that did not yet start playing or recording
    @param reCue         re-cue all non-empty clips that where stopped so that they start playing again at next 0.0 global beat position
*/
void Track::stopAllPlayingClips(bool now, bool deCue, bool reCue)
{
    // See stopAllPlayingClipsExceptFor for docs
    stopAllPlayingClipsExceptFor(-1, now, deCue, reCue);
}

/** Stop all track clips that are currently playing
    @param clipN         do not stop this clip
    @param now             stop clips immediately, otherwise wait until next quatized step
    @param deCue         de-cue all clips cued to play or record but that did not yet start playing or recording
    @param reCue         re-cue all non-empty clips that where stopped so that they start playing again at next 0.0 global beat position
*/
void Track::stopAllPlayingClipsExceptFor(int clipN, bool now, bool deCue, bool reCue)
{
    jassert(clipN < midiClips.size());
    for (int i=0; i<midiClips.size(); i++){
        if (i != clipN){
            auto clip = midiClips[i];
            bool wasPlaying = false;
            if (clip->isPlaying()){
                wasPlaying = true;
                if (!now){
                    if (!clip->isCuedToStop()){
                        // Only toggle if not already cued to stop
                        clip->togglePlayStop();
                    }
                } else {
                    clip->stopNow();
                }
            }
            if (deCue){
                if (clip->isCuedToPlay()){
                    clip->clearPlayCue();
                }
                if (clip->isCuedToStartRecording()){
                    clip->clearStartRecordingCue();
                }
            }
            if (reCue && wasPlaying && !clip->isEmpty()){
                clip->playAt(0.0);
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

/** Insert a copy of the passed clip at the position indicated. All clips below that position will be shifted downwards to accomodate the new clip. Last clip will be removed
    @param clipN         position where to insert the new clip
    @param clip           clip to insert at clipN
*/
void Track::insertClipAt(int clipN, Clip* clip)
{
    int currentMidiClipsLength = midiClips.size();
    midiClips.insert(clipN, clip);
    midiClips.removeLast();
    jassert(midiClips.size() == currentMidiClipsLength);
}

bool Track::hasClipsCuedToRecord()
{
    for (auto clip: midiClips){
        if (clip->isCuedToStartRecording()){
            return true;
        }
    }
    return false;
}

bool Track::inputMonitoringEnabled()
{
    return inputMonitoring == true;
}

void Track::setInputMonitoring(bool enabled)
{
    inputMonitoring = enabled;
}
