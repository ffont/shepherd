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
    
    #if !RPI_BUILD
    // Certain chance to initialize midiSequence with some notes
    // This makes testing quicker
    if (juce::Random::getSystemRandom().nextInt (juce::Range<int> (0, 10)) > 5){
        clipLengthInBeats = (double)juce::Random::getSystemRandom().nextInt (juce::Range<int> (5, 13));
        std::vector<std::pair<int, float>> noteOnTimes = {};
        for (int i=0; i<clipLengthInBeats; i++){
            noteOnTimes.push_back({i, 0.0});
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
    }
    #endif
}

/** Return a pointer to a "cloned" version of the current clip which has the same MIDI sequence
 Note that the playhead status is not copied therefore the new clip will have same MIDI sequence and length, but it will be stopped, etc.
    @param clipN         position where to insert the new clip
    @param clip           clip to insert at clipN
*/
Clip* Clip::clone() const
{
    auto newClip = new Clip(
        this->playhead.getParentSlice,
        this->getGlobalBpm,
        this->getSampleRate,
        this->getSamplesPerBlock,
        this->getMidiOutChannel
    );
    newClip->replaceSequence(this->midiSequence, this->clipLengthInBeats);
    return newClip;
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
    double globalPlayheadPosition = playhead.getParentSlice().getStart();
    double beatsRemainingForNextBar;
    if (globalPlayheadPosition == 0.0){
        // Edge case in which global playhead is stopped
        beatsRemainingForNextBar = 0.0;
    } else {
        beatsRemainingForNextBar = 4.0 - std::fmod(globalPlayheadPosition, 4.0);
    }
    double positionInGlobalPlayhead = std::round(globalPlayheadPosition + beatsRemainingForNextBar);
    
    if (isPlaying()){
        // If clip is playing, cue it to stop
        stopAt(positionInGlobalPlayhead);
    } else {
        if (playhead.isCuedToPlay()){
            // If clip is not playing but it is already cued to start, cancel the cue
            playhead.clearPlayCue();
        } else {
            if (midiSequence.getNumEvents() > 0 || isCuedToStartRecording()){
                // If not already cued and clip has recorded notes or it is cued to record, cue to play as well
                playAt(positionInGlobalPlayhead);
            } else {
                // If clip has no notes and it is not cued to record, don't cue to play either
                // Do nothing...
            }
        }
    }
}

void Clip::clearPlayCue()
{
    playhead.clearPlayCue();
}

void Clip::clearStopCue()
{
    playhead.clearStopCue();
}

void Clip::startRecordingNow()
{
    clearStartRecordingCue();
    recording = true;
    hasJustStoppedRecordingFlag = false;
}

void Clip::stopRecordingNow()
{
    clearStopRecordingCue();
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
    double globalPlayheadPosition = playhead.getParentSlice().getStart();
    double nextBeatPosition;
    if (globalPlayheadPosition == 0.0){
        nextBeatPosition = 0.0;  // Edge case in which global playhead has not yet started and it is exactly 0.0 (happens when arming clip to record with global playhead stopped)
    } else {
        if (clipLengthInBeats > 0.0){
            // If clip has length, it could loop, therefore make sure that next beat position will also "loop"
            nextBeatPosition = std::fmod(std::floor(playhead.getCurrentSlice().getStart()) + 1, clipLengthInBeats);
        } else {
            // If clip has no length, no need to account for potential "loop" of nextBeatPosition
            nextBeatPosition = std::floor(playhead.getCurrentSlice().getStart()) + 1;
        }
    }
    if (isRecording()){
        stopRecordingNow();
        //stopRecordingAt(nextBeatPosition);  // Record until next integer beat
    } else {
        startRecordingAt(nextBeatPosition);  // Start recording at next beat integer
        if (!isPlaying()){
            // If clip is not playing, toggle play
            togglePlayStop();
        }
    }
}

void Clip::clearStartRecordingCue()
{
    willStartRecordingAt = -1.0;
}

void Clip::clearStopRecordingCue()
{
    willStopRecordingAt = -1.0;
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

bool Clip::isEmpty()
{
    return midiSequence.getNumEvents() == 0;
}

juce::String Clip::getStatus()
{
    juce::String playStatus = "";
    juce::String recordStatus = "";
    juce::String emptyStatus = "";
    
    if (isCuedToStartRecording()) {
        recordStatus = CLIP_STATUS_CUED_TO_RECORD;
    } else if (isCuedToStopRecording()) {
        recordStatus = CLIP_STATUS_CUED_TO_STOP_RECORDING;
    } else if (isRecording()) {
        recordStatus = CLIP_STATUS_RECORDING;
    } else {
        recordStatus = CLIP_STATUS_NO_RECORDING;
    }
    
    if (isCuedToPlay()) {
        playStatus = CLIP_STATUS_CUED_TO_PLAY;
    } else if (isCuedToStop()) {
        playStatus = CLIP_STATUS_CUED_TO_STOP;
    } else if (isPlaying()) {
        playStatus = CLIP_STATUS_PLAYING;
    } else {
        playStatus = CLIP_STATUS_STOPPED;
    }
    
    if (isEmpty()){
        emptyStatus = CLIP_STATUS_IS_EMPTY;
    } else {
        emptyStatus = CLIP_STATUS_IS_NOT_EMPTY;
    }
    
    return playStatus + recordStatus + emptyStatus;
}

void Clip::clearSequence()
{
    // Removes all midi events from the midi sequence (but clip length remains the same)
    if (isPlaying()){
        // If is playing, clear sequence at the start of next process block
        shouldClearSequence = true;
    } else {
        // If not playing, clear sequence immediately
        // If clip has no events, reset clip length
        if (midiSequence.getNumEvents() > 0){
            midiSequence.clear();
        } else {
            clipLengthInBeats = 0.0;
        }
    }
}

void Clip::doubleSequenceHelper()
{
    juce::MidiMessageSequence doubledSequence;
    for (int i=0; i < midiSequence.getNumEvents(); i++){
        juce::MidiMessage msg = midiSequence.getEventPointer(i)->message;
        // Add original event
        doubledSequence.addEvent(msg);
        // Add event delayed by length of clip
        msg.setTimeStamp(msg.getTimeStamp() + clipLengthInBeats);
        doubledSequence.addEvent(msg);
    }
    midiSequence = doubledSequence;
    clipLengthInBeats = clipLengthInBeats * 2;
}

void Clip::doubleSequence()
{
    // Makes the midi sequence twice as long and duplicates existing events in the second repetition of it
    if (isPlaying()){
        // If is playing, double sequence at the start of next process block
        shouldDoubleSequence = true;
    } else {
        // If not playing, double sequence immediately
        doubleSequenceHelper();
    }
}

void Clip::replaceSequence(juce::MidiMessageSequence newSequence, double newLength)
{
    // TODO: update this in the process slice block instead of here using "shouldReplaceSequence"
    midiSequence = newSequence;
    clipLengthInBeats = newLength;
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
        if (midiSequence.getNumEvents() > 0){
            midiSequence.clear();
        } else {
            clipLengthInBeats = 0.0;
            stopNow();
        }
        shouldClearSequence = false;
        renderRemainingNoteOffsIntoMidiBuffer(bufferToFill);
    }
    
    if (shouldDoubleSequence){
        doubleSequenceHelper();
        shouldDoubleSequence = false;
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
                double eventPositionInGlobalPlayheadInBeats = eventPositionInSliceInBeats + playhead.getParentSlice().getStart();
                if (isCuedToStop() && eventPositionInGlobalPlayheadInBeats >= playhead.getStopAtCueBeats()){
                    // Edge case in which the current event of the sequence falls inside the current slice, but the clip is cued to stop
                    // at some point in the middle of the slice therefore some notes of the sequence should not be triggered if they are after the stop point
                } else if (isCuedToPlay() && eventPositionInGlobalPlayheadInBeats < playhead.getPlayAtCueBeats()) {
                    // Edge case in which the current event of the sequence falls inside the current slice, but the clip is cued to start
                    // at some point in the middle of the slice therefore some notes of the sequence should not be triggered if they are before the start point
                } else {
                    // Normal case in which notes should be triggered
                    
                    // Calculate note position for the midi buffer (in samples)
                    int eventPositionInSliceInSamples = eventPositionInSliceInBeats * (int)std::round(60.0 * getSampleRate() / getGlobalBpm());
                    jassert(juce::isPositiveAndBelow(eventPositionInSliceInSamples, bufferSize));
                    
                    // Re-write MIDI channel (in might have changed...) and add note to the buffer
                    msg.setChannel(getMidiOutChannel());
                    bufferToFill.addEvent(msg, eventPositionInSliceInSamples);
                    
                    // Keep track of notes currently played so later we can send note offs if needed
                    if      (msg.isNoteOn())  notesCurrentlyPlayed.add (msg.getNoteNumber());
                    else if (msg.isNoteOff()) notesCurrentlyPlayed.removeValue (msg.getNoteNumber());
                }
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
            addRecordedSequenceToSequence();
            playhead.resetSlice(clipLengthInBeats - sliceInBeats.getEnd());
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
        // If it has just stopped recording, add the notes to the sequence and re-set playhead to loop
        addRecordedSequenceToSequence();
        playhead.resetSlice(clipLengthInBeats - playhead.getCurrentSlice().getEnd());
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
