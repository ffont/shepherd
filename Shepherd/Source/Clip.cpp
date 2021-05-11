/*
  ==============================================================================

    Clip.cpp
    Created: 9 May 2021 9:50:14am
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#include "Clip.h"


Clip::Clip(std::function<juce::Range<double>()> playheadParentSliceGetter,
           std::function<double()> globalBpmGetter,
           std::function<double()> sampleRateGetter,
           std::function<int()> samplesPerBlockGetter,
           std::function<int()> midiOutChannelGetter
           )
: playerPlayhead(playheadParentSliceGetter), recorderPlayhead([this]{return juce::Range<double>{playerPlayhead.getCurrentSlice().getStart(), playerPlayhead.getCurrentSlice().getStart() + (double)getSamplesPerBlock() / (60.0 * getSampleRate() / getGlobalBpm())};})
//: playerPlayhead(playheadParentSliceGetter), recorderPlayhead([this]{ return playerPlayhead.getCurrentSlice();})
{
    getGlobalBpm = globalBpmGetter;
    getSampleRate = sampleRateGetter;
    getSamplesPerBlock = samplesPerBlockGetter;
    getMidiOutChannel = midiOutChannelGetter;
    
    // Initialize midiSequence with some notes
    std::vector<std::pair<int, float>> noteOnTimes = {
        {0, 0.0},
        {1, 0.0},
        {2, 0.0},
        {3, 0.0},
        {4, 0.0},
        {5, 0.0},
        {6, 0.0},
        {7, 0.0}
    };
    for (auto note: noteOnTimes) {
        // NOTE: don't care about the channel here because it is re-written when filling midi buffer
        juce::MidiMessage msgNoteOn = juce::MidiMessage::noteOn(1, 64, 1.0f);
        msgNoteOn.setTimeStamp(note.first + note.second);
        midiSequence.addEvent(msgNoteOn);
        juce::MidiMessage msgNoteOff = juce::MidiMessage::noteOff(1, 64, 0.0f);
        msgNoteOff.setTimeStamp(note.first + note.second + 0.25);
        midiSequence.addEvent(msgNoteOff);
    }
}

void Clip::playNow()
{
    playerPlayhead.playNow();
}

void Clip::playAt(double positionInParent)
{
    playerPlayhead.playAt(positionInParent);
}

void Clip::stopNow()
{
    playerPlayhead.stopNow();
}

void Clip::stopAt(double positionInParent)
{
    playerPlayhead.stopAt(positionInParent);
}

void Clip::togglePlayStopNow()
{
    if (playerPlayhead.isPlaying()){
        playerPlayhead.stopNow();
    } else {
        playerPlayhead.playNow();
    }
}

void Clip::recordNow()
{
    recorderPlayhead.playNow();
}

void Clip::recordAt(double positionInParent)
{
    recorderPlayhead.playAt(positionInParent);
}

void Clip::toggleRecord()
{
    if (recorderPlayhead.isPlaying()){
        recorderPlayhead.stopAt(0.0);  // Record until next beat
    } else {
        recorderPlayhead.playAt(0.0);  // Start recording at next beat
    }
}

void Clip::clearSequence()
{
    shouldClearSequence = true;
}

Playhead* Clip::getPlayerPlayhead()
{
    return &playerPlayhead;
}

Playhead* Clip::getRecorderPlayhead()
{
    return &recorderPlayhead;
}

void Clip::renderSliceIntoMidiBuffer(juce::MidiBuffer& bufferToFill, int bufferSize)
{
    
    if (shouldClearSequence){
        midiSequence.clear();
        shouldClearSequence = false;
    }
    
    // If the clip is playing, check if any notes should be added to the current slice
    if (playerPlayhead.isPlaying()){
        playerPlayhead.captureSlice();
        const auto sliceInBeats = playerPlayhead.getCurrentSlice();
        for (int i=0; i < midiSequence.getNumEvents(); i++){
            juce::MidiMessage msg = midiSequence.getEventPointer(i)->message;
            double eventPositionInBeats = msg.getTimeStamp();
            if (sliceInBeats.contains(eventPositionInBeats))
            {
                double eventPositionInSliceInBeats = eventPositionInBeats - sliceInBeats.getStart();
                int eventPositionInSliceInSamples = eventPositionInSliceInBeats * (int)std::round(60.0 * getSampleRate() / getGlobalBpm());
                jassert(juce::isPositiveAndBelow(eventPositionInSliceInSamples, bufferSize));
                
                msg.setChannel(getMidiOutChannel()); // Re-write MIDI channel (in might have changed...)
                bufferToFill.addEvent(msg, eventPositionInSliceInSamples);
                
                // Store notes currently played
                if      (msg.isNoteOn())  notesCurrentlyPlayed.add (msg.getNoteNumber());
                else if (msg.isNoteOff()) notesCurrentlyPlayed.removeValue (msg.getNoteNumber());
                
                if (i == midiSequence.getNumEvents() - 1){
                    // If last note has been added, we have reached end of clip, stop now and cue play at next beat integer
                    playerPlayhead.stopNow();
                    playerPlayhead.playAt(std::round(playerPlayhead.getParentSlice().getEnd() + 1));
                }
            }
        }
        playerPlayhead.releaseSlice();
    } else {
        if (playerPlayhead.hasJustStopped()){
            renderRemainingNoteOffsIntoMidiBuffer(bufferToFill);
        }
    }
    
    // Check if Clip's player is cued to play and call playNow if needed (accounting for potential offset in samples)
    if (playerPlayhead.isCuedToPlay()){
        const auto parentSliceInBeats = playerPlayhead.getParentSlice();
        if (parentSliceInBeats.contains(playerPlayhead.getPlayAtCueBeats())){
            playerPlayhead.playNow(playerPlayhead.getPlayAtCueBeats() - parentSliceInBeats.getStart());
        }
    }
    
    // Check if Clip's player is cued to stop and call stopNow if needed
    if (playerPlayhead.isCuedToStop()){
        const auto parentSliceInBeats = playerPlayhead.getParentSlice();
        if (parentSliceInBeats.contains(playerPlayhead.getStopAtCueBeats())){
            playerPlayhead.stopNow();
        }
    }
}

void Clip::renderRemainingNoteOffsIntoMidiBuffer(juce::MidiBuffer& bufferToFill)
{
    for (int i=0; i<notesCurrentlyPlayed.size(); i++){
        juce::MidiMessage msg = juce::MidiMessage::noteOff(getMidiOutChannel(), notesCurrentlyPlayed[i], 0.0f);
        bufferToFill.addEvent(msg, 0);
    }
}

void Clip::recordFromBuffer(juce::MidiBuffer& incommingBuffer, int bufferSize)
{
    // If the clip is recording, check if any notes should be added to the recording buffer
    if (recorderPlayhead.isPlaying()){
        
        recorderPlayhead.captureSlice();
        const auto sliceInBeats = recorderPlayhead.getCurrentSlice();
        
        for (const auto metadata : incommingBuffer)
        {
            auto msg = metadata.getMessage();
            double eventPositionInBeats = sliceInBeats.getStart() + sliceInBeats.getLength() * metadata.samplePosition / bufferSize;
            
            if (sliceInBeats.contains(eventPositionInBeats)) {
                // This condition should always be true except maybe when recorder was just started or just stopped
                msg.setTimeStamp(eventPositionInBeats);
                recordedMidiSequence.addEvent(msg);
                
            } else {
                //jassert() ?
            }
        }
        recorderPlayhead.releaseSlice();
    } else {
        if (recorderPlayhead.hasJustStopped()){
            // Add new events to the sequence buffer and clear recording buffer
            midiSequence.addSequence(recordedMidiSequence, 0);
            recordedMidiSequence.clear();
        }
    }
    
    // Check if Clip's recorder is cued to play and call playNow if needed (accounting for potential offset in samples)
    if (recorderPlayhead.isCuedToPlay()){
        const auto parentSliceInBeats = recorderPlayhead.getParentSlice();
        if (parentSliceInBeats.contains(recorderPlayhead.getPlayAtCueBeats())){
            recorderPlayhead.playNow(recorderPlayhead.getPlayAtCueBeats() - parentSliceInBeats.getStart());
        }
    }
    
    // Check if Clip's recorder is cued to stop and call stopNow if needed
    if (recorderPlayhead.isCuedToStop()){
        const auto parentSliceInBeats = recorderPlayhead.getParentSlice();
        if (parentSliceInBeats.contains(recorderPlayhead.getStopAtCueBeats())){
            recorderPlayhead.stopNow();
        }
    }
}
