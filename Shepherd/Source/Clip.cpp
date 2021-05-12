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
: playhead(playheadParentSliceGetter)
{
    getGlobalBpm = globalBpmGetter;
    getSampleRate = sampleRateGetter;
    getSamplesPerBlock = samplesPerBlockGetter;
    getMidiOutChannel = midiOutChannelGetter;
    
    #if JUCE_DEBUG
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
        int midiNote = juce::Random::getSystemRandom().nextInt (juce::Range<int> (64, 85));
        juce::MidiMessage msgNoteOn = juce::MidiMessage::noteOn(1, midiNote, 1.0f);
        msgNoteOn.setTimeStamp(note.first + note.second);
        midiSequence.addEvent(msgNoteOn);
        juce::MidiMessage msgNoteOff = juce::MidiMessage::noteOff(1, midiNote, 0.0f);
        msgNoteOff.setTimeStamp(note.first + note.second + 0.25);
        midiSequence.addEvent(msgNoteOff);
    }
    clipLengthInBeats = std::ceil(midiSequence.getEndTime()); // Quantize it to next beat integer
    #endif
    
    clipLengthInBeats = 8.0;
}

void Clip::playNow()
{
    playhead.playNow();
}

void Clip::playNow(double sliceOffset)
{
    playhead.playNow(sliceOffset);
}

void Clip::playAt(double positionInGlobalPlayhead)
{
    playhead.playAt(positionInGlobalPlayhead);
}

void Clip::stopNow()
{
    if (isRecording()){
        stopRecordingNow();
    }
    playhead.stopNow();
    resetPlayheadPosition();
}

void Clip::stopAt(double positionInGlobalPlayhead)
{
    playhead.stopAt(positionInGlobalPlayhead);
}

void Clip::togglePlayStop()
{
    
    double globalPlayheadPosition = playhead.getParentSlice().getEnd();
    double beatsRemainingForNextBar = 4.0 - std::fmod(globalPlayheadPosition, 4.0);
    double positionInGlobalPlayhead = std::round(globalPlayheadPosition + beatsRemainingForNextBar);
    
    if (isPlaying()){
        stopAt(positionInGlobalPlayhead);
    } else {
        playAt(positionInGlobalPlayhead);
    }
}

void Clip::startRecordingNow()
{
    willStartRecordingAt = -1.0;
    recording = true;
    hasJustStoppedRecordingFlag = false;
}

void Clip::stopRecordingNow()
{
    willStopRecordingAt = -1.0;
    recording = false;
    hasJustStoppedRecordingFlag = true;
}

void Clip::startRecordingAt(double positionInClipPlayhead)
{
    willStartRecordingAt = positionInClipPlayhead;
}

void Clip::stopRecordingAt(double positionInClipPlayhead)
{
    willStopRecordingAt = positionInClipPlayhead;
}

void Clip::toggleRecord()
{
    if (isRecording()){
        if (clipLengthInBeats > 0.0){
            stopRecordingAt(clipLengthInBeats);  // Record until next clip length
        } else {
            stopRecordingNow();
        }
    } else {
        if (clipLengthInBeats > 0.0){
            startRecordingAt(0.0);  // Start recording when clip loops
        } else {
            startRecordingNow();
        }
    }
}

bool Clip::isPlaying()
{
    return playhead.isPlaying();
}

bool Clip::isCuedToPlay()
{
    return playhead.isCuedToPlay();
}

bool Clip::isCuedToStop()
{
    return playhead.isCuedToStop();
}

bool Clip::isRecording()
{
    return recording;
}

bool Clip::isCuedToStartRecording()
{
    return willStartRecordingAt >= 0.0;
}

bool Clip::isCuedToStopRecording()
{
    return willStopRecordingAt >= 0.0;
}

void Clip::clearSequence()
{
    shouldClearSequence = true;
}

double Clip::getPlayheadPosition()
{
    return playhead.getCurrentSlice().getEnd();
}

void Clip::resetPlayheadPosition()
{
    playhead.resetSlice();
}

double Clip::getLengthInBeats()
{
    return clipLengthInBeats;
}

void Clip::renderRemainingNoteOffsIntoMidiBuffer(juce::MidiBuffer& bufferToFill)
{
    for (int i=0; i<notesCurrentlyPlayed.size(); i++){
        juce::MidiMessage msg = juce::MidiMessage::noteOff(getMidiOutChannel(), notesCurrentlyPlayed[i], 0.0f);
        bufferToFill.addEvent(msg, 0);
    }
}

void Clip::processSlice(juce::MidiBuffer& incommingBuffer, juce::MidiBuffer& bufferToFill, int bufferSize)
{
    if (shouldClearSequence){
        midiSequence.clear();
        shouldClearSequence = false;
        renderRemainingNoteOffsIntoMidiBuffer(bufferToFill);
        //clipLengthInBeats = 0.0;
    }
    
    // Check if Clip's player is cued to play and call playNow if needed (accounting for potential offset in samples)
    if (playhead.isCuedToPlay()){
        const auto parentSliceInBeats = playhead.getParentSlice();
        if (parentSliceInBeats.contains(playhead.getPlayAtCueBeats())){
            playhead.playNow(playhead.getPlayAtCueBeats() - parentSliceInBeats.getStart());
        }
    }
    
    // If the clip is playing, check if any notes should be added to the current slice
    if (playhead.isPlaying()){
        playhead.captureSlice();
        const auto sliceInBeats = playhead.getCurrentSlice();
        
        // Read clip's sequence and play notes if needed
        for (int i=0; i < midiSequence.getNumEvents(); i++){
            juce::MidiMessage msg = midiSequence.getEventPointer(i)->message;
            double eventPositionInBeats = msg.getTimeStamp();
            bool sliceContainsEvent = sliceInBeats.contains(eventPositionInBeats);
            bool sliceContainsLoopedEvent = sliceInBeats.contains(eventPositionInBeats + clipLengthInBeats);
            
            if (sliceContainsEvent || sliceContainsLoopedEvent)
            {
                double eventPositionInSliceInBeats;
                if (sliceContainsEvent) {
                    eventPositionInSliceInBeats = eventPositionInBeats - sliceInBeats.getStart();
                } else {
                    eventPositionInSliceInBeats = eventPositionInBeats + clipLengthInBeats - sliceInBeats.getStart();
                }
                    
                int eventPositionInSliceInSamples = eventPositionInSliceInBeats * (int)std::round(60.0 * getSampleRate() / getGlobalBpm());
                jassert(juce::isPositiveAndBelow(eventPositionInSliceInSamples, bufferSize));
                
                msg.setChannel(getMidiOutChannel()); // Re-write MIDI channel (in might have changed...)
                bufferToFill.addEvent(msg, eventPositionInSliceInSamples);
                
                // Store notes currently played
                if      (msg.isNoteOn())  notesCurrentlyPlayed.add (msg.getNoteNumber());
                else if (msg.isNoteOff()) notesCurrentlyPlayed.removeValue (msg.getNoteNumber());
            }
        }
        
        // Record midi events (and check if we should start/stop recording)
        if (isCuedToStartRecording()){
            if (sliceInBeats.contains(willStartRecordingAt) || sliceInBeats.contains(willStartRecordingAt + clipLengthInBeats)){ // Edge case: start recording at end of clip
                startRecordingNow();
            }
        }
    
        if (recording){
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
        }
        
        if (isCuedToStopRecording()){
            if (sliceInBeats.contains(willStopRecordingAt) || sliceInBeats.contains(willStopRecordingAt + clipLengthInBeats)){ // Edge case: stop recording at end of clip
                stopRecordingNow();
            }
        }
        
        // Loop the clip if needed
        // Check if Clip length and, if current slice contains it, reset slice so the clip loops.
        // Note that in the next slice clip will start with an offset of some samples (positive) because the
        // loop point falls before the end of the current slice and we need to compensate for that.
        if ((clipLengthInBeats > 0.0) && (sliceInBeats.contains(clipLengthInBeats))){
            playhead.resetSlice(clipLengthInBeats - sliceInBeats.getEnd());
            addRecordedSequenceToSequence();
        }
        
        // Release slice as we finished processing it
        // Note that releaseSlice sets the end of the playhead slice to be the same as the start. This won't have
        // any negative effect if we've restarted playhead because clip is looping, but it will me meaningless.
        playhead.releaseSlice();
        
    }
    if (playhead.hasJustStopped()){
        renderRemainingNoteOffsIntoMidiBuffer(bufferToFill);
    }
    
    if (hasJustStoppedRecording()){
        addRecordedSequenceToSequence();
    }
    
    // Check if Clip's player is cued to stop and call stopNow if needed
    if (playhead.isCuedToStop()){
        const auto parentSliceInBeats = playhead.getParentSlice();
        if (parentSliceInBeats.contains(playhead.getStopAtCueBeats())){
            stopNow();
        }
    }
}


void Clip::addRecordedSequenceToSequence()
{
    // If there are events in the recordedMidiSequence, add them to the sequence buffer and
    // clear the recording buffer. Also set clip length if it was not set.
    if (recordedMidiSequence.getNumEvents() > 0){
        midiSequence.addSequence(recordedMidiSequence, 0);
        if (clipLengthInBeats == 0.0) {
            clipLengthInBeats = std::ceil(midiSequence.getEndTime()); // Quantize it to next beat integer
        }
        recordedMidiSequence.clear();
    }
}


bool Clip::hasJustStoppedRecording()
{
    // This funciton will return true the first time it is called after recording has been stopped
    // Starting recording resets the flag (even if this function was never called)
    if (hasJustStoppedRecordingFlag){
        hasJustStoppedRecordingFlag = false;
        return true;
    } else {
        return false;
    }
}
