#include "MainComponent.h"


//==============================================================================
MainComponent::MainComponent()
{
    #if !RPI_BUILD
    addAndMakeVisible(devUiComponent);
    setSize (devUiComponent.getWidth(), devUiComponent.getHeight());
    #else
    setSize(10, 10); // I think this needs to be called anyway...
    #endif
    
    // Start timer for recurring tasks
    startTimer (50);

    // Some platforms require permissions to open input channels so request that here
    if (juce::RuntimePermissions::isRequired (juce::RuntimePermissions::recordAudio)
        && ! juce::RuntimePermissions::isGranted (juce::RuntimePermissions::recordAudio))
    {
        juce::RuntimePermissions::request (juce::RuntimePermissions::recordAudio,
                                           [&] (bool granted) { setAudioChannels (granted ? 2 : 0, 2); });
    }
    else
    {
        // Specify the number of input and output channels that we want to open
        setAudioChannels (2, 2);
    }
    
    // Init OSC and MIDI
    initializeOSC();
    initializeMIDI();
    
    // Init sine synth with 16 voices (used for testig purposes only)
    #if !RPI_BUILD
    for (auto i = 0; i < nSynthVoices; ++i)
        sineSynth.addVoice (new SineWaveVoice());
    sineSynth.addSound (new SineWaveSound());
    #endif
    
    // Send OSC message to frontend indiating that Shepherd is ready
    juce::OSCMessage returnMessage = juce::OSCMessage("/shepherdReady");
    sendOscMessage(returnMessage);
}

MainComponent::~MainComponent()
{
    shutdownAudio();
}

void MainComponent::initializeOSC()
{
    std::cout << "Initializing OSC server" << std::endl;
    
    // Setup OSC server
    if (! connect (oscReceivePort)){
        std::cout << "ERROR starting OSC server" << std::endl;
    } else {
        std::cout << "Started OSC server, listening at 0.0.0.0:" << oscReceivePort << std::endl;
        addListener (this);
    }
    // OSC sender is not set up here because it is done lazily when trying to send a message
}

void MainComponent::initializeMIDI()
{
    std::cout << "Initializing MIDI devices" << std::endl;
    
    lastTimeMidiInitializationAttempted = juce::Time::getCurrentTime().toMilliseconds();
    
    // TODO: read device names from config file instead of hardcoding them
    
    // Setup MIDI devices
    auto midiInputs = juce::MidiInput::getAvailableDevices();
    auto midiOutputs = juce::MidiOutput::getAvailableDevices();
    
    if (!midiOutAIsConnected){
        #if RPI_BUILD
        const juce::String outDeviceName = "MIDIFACE 2X2 MIDI 1";
        #else
        const juce::String outDeviceName = "IAC Driver Bus 1";
        #endif
        juce::String outDeviceIdentifier = "";
        std::cout << "Available MIDI OUT devices:" << std::endl;
        for (int i=0; i<midiOutputs.size(); i++){
            std::cout << " - " << midiOutputs[i].name << std::endl;
            if (midiOutputs[i].name == outDeviceName){
                outDeviceIdentifier = midiOutputs[i].identifier;
            }
        }
        midiOutA = juce::MidiOutput::openDevice(outDeviceIdentifier);
        if (midiOutA != nullptr){
            std::cout << "Connected to:" << midiOutA.get()->getName() << std::endl;
            midiOutAIsConnected = true;
        } else {
            std::cout << "Could not connect to " << outDeviceName << std::endl;
        }
    }
    
    if (!midiInIsConnected){ // Keyboard MIDI in
        #if RPI_BUILD
        const juce::String inDeviceName = "LUMI Keys BLOCK MIDI 1";
        #else
        const juce::String inDeviceName = "iCON iKEY V1.02";
        #endif
        juce::String inDeviceIdentifier = "";
        std::cout << "Available MIDI IN devices for Keys input:" << std::endl;
        for (int i=0; i<midiInputs.size(); i++){
            std::cout << " - " << midiInputs[i].name << std::endl;
            if (midiInputs[i].name == inDeviceName){
                inDeviceIdentifier = midiInputs[i].identifier;
            }
        }
        midiIn = juce::MidiInput::openDevice(inDeviceIdentifier, &midiInCollector);
        if (midiIn != nullptr){
            std::cout << "Connected to:" << midiIn.get()->getName() << std::endl;
            std::cout << "Starting MIDI in callback" << std::endl;
            midiIn.get()->start();
            midiInIsConnected = true;
        } else {
            std::cout << "Could not connect to " << inDeviceName << std::endl;
        }
    }

    if (!midiInPushIsConnected){ // Push messages MIDI in (used for triggering notes and encoders if mode is active)
        #if RPI_BUILD
        const juce::String pushInDeviceName = "Ableton Push 2 MIDI 1";
        #else
        const juce::String pushInDeviceName = "Push2Simulator";
        #endif
        juce::String pushInDeviceIdentifier = "";
        std::cout << "Available MIDI IN devices for Push input:" << std::endl;
        for (int i=0; i<midiInputs.size(); i++){
            std::cout << " - " << midiInputs[i].name << std::endl;
            if (midiInputs[i].name == pushInDeviceName){
                pushInDeviceIdentifier = midiInputs[i].identifier;
            }
        }
        midiInPush = juce::MidiInput::openDevice(pushInDeviceIdentifier, &pushMidiInCollector);
        if (midiInPush != nullptr){
            std::cout << "Connected to:" << midiInPush.get()->getName() << std::endl;
            std::cout << "Starting MIDI in callback" << std::endl;
            midiInPush.get()->start();
            midiInPushIsConnected = true;
        } else {
            std::cout << "Could not connect to " << pushInDeviceName << std::endl;
        }
    }
}

//==============================================================================
void MainComponent::prepareToPlay (int samplesPerBlockExpected, double _sampleRate)
{
    std::cout << "Prepare to play called with samples per block " << samplesPerBlockExpected << " and sample rate " << _sampleRate << std::endl;

    sampleRate = _sampleRate;
    samplesPerBlock = samplesPerBlockExpected;
    
    midiInCollector.reset(_sampleRate);
    pushMidiInCollector.reset(_sampleRate);
    sineSynth.setCurrentPlaybackSampleRate (_sampleRate);
    
    // Create some tracks
    for (int i=0; i<nTestTracks; i++){
        tracks.add(
          new Track(
               [this]{ return juce::Range<double>{playheadPositionInBeats, playheadPositionInBeats + (double)samplesPerBlock / (60.0 * sampleRate / bpm)}; },
               [this]{
                    GlobalSettingsStruct settings;
                    settings.bpm = bpm;
                    settings.fixedLengthRecordingAmount = fixedLengthRecordingAmount;
                    settings.nScenes = nScenes;
                    settings.sampleRate = sampleRate;
                    settings.samplesPerBlock = samplesPerBlock;
                    return settings;
                }
        ));
    }
    
    for (int i=0; i<tracks.size(); i++){
        auto track = tracks[i];
        track->setMidiOutChannel(i + 1);
        track->prepareClips();
    }
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Clear audio buffers
    bufferToFill.clearActiveBufferRegion();
    
    // Check if tempo should be updated
    if (nextBpm > 0.0){
        bpm = nextBpm;
        nextBpm = 0.0;
    }
    double bufferLengthInBeats = bufferToFill.numSamples / (60.0 * sampleRate / bpm);
    
    // Check if count-in finished and global playhead should be toggled
    if (!isPlaying && doingCountIn){
        if (countInLengthInBeats >= countInplayheadPositionInBeats && countInLengthInBeats < countInplayheadPositionInBeats + bufferLengthInBeats){
            // Count in finishes in the current getNextAudioBlock execution
            playheadPositionInBeats = -(countInLengthInBeats - countInplayheadPositionInBeats); // Align global playhead position with coutin buffer offset so that it starts at correct offset
            shouldToggleIsPlaying = true;
            doingCountIn = false;
            countInplayheadPositionInBeats = 0.0;
        }
    }

    // Generate MIDI output buffer
    juce::MidiBuffer generatedMidi;
    
    // Collect messages from MIDI input (keys and push)
    juce::MidiBuffer incomingMidi;

    juce::MidiBuffer incomingMidiKeys;
    midiInCollector.removeNextBlockOfMessages (incomingMidiKeys, bufferToFill.numSamples);

    juce::MidiBuffer incomingMidiPush;
    pushMidiInCollector.removeNextBlockOfMessages (incomingMidiPush, bufferToFill.numSamples);

    // Process keys MIDI input and add it to combined incomming buffer
    for (const auto metadata : incomingMidiKeys)
    {
        auto msg = metadata.getMessage();
        if (msg.isNoteOnOrOff() && fixedVelocity > -1){
            msg.setVelocity((float)fixedVelocity/127.0f);
        }
        incomingMidi.addEvent(msg, metadata.samplePosition);
        
        if (msg.isNoteOn()){
            // Store message in the list of last note on messages and set its timestamp to the global playhead position
            juce::MidiMessage msgToStoreInQueue = juce::MidiMessage(msg);
            if (doingCountIn){
                msgToStoreInQueue.setTimeStamp(countInplayheadPositionInBeats - countInLengthInBeats + metadata.samplePosition/bufferToFill.numSamples * bufferLengthInBeats);
            } else {
                msgToStoreInQueue.setTimeStamp(playheadPositionInBeats + metadata.samplePosition/bufferToFill.numSamples * bufferLengthInBeats);
            }
            lastMidiNoteOnMessages.insert(lastMidiNoteOnMessages.begin(), msgToStoreInQueue);
        }
    }

    // Process push MIDI input and add it to combined incomming buffer
    for (const auto metadata : incomingMidiPush)
    {
        auto msg = metadata.getMessage();
        
        if (msg.isController() && msg.getControllerNumber() == 64){
            // If sustain pedal, we always pass it to the output as is
            incomingMidi.addEvent(msg, metadata.samplePosition);
        } else if (msg.isController() && msg.getControllerNumber() == 1){
            // If modulation wheel, we always pass it to the output as is
            incomingMidi.addEvent(msg, metadata.samplePosition);
        } else if (msg.isPitchWheel()){
            // If pitch wheel, we always pass it to the output as is
            incomingMidi.addEvent(msg, metadata.samplePosition);
        } else if (msg.isNoteOnOrOff() || msg.isAftertouch() || msg.isChannelPressure()){
            if (msg.getNoteNumber() >= 36 && msg.getNoteNumber() <= 99){
                // If midi message is a note on/off, aftertouch or channel pressure from one of the 64 pads, check if there's any mapping active
                // and use it (or discard the message).
                int mappedNote = pushPadsNoteMapping[msg.getNoteNumber()-36];
                if (mappedNote > -1){
                    msg.setNoteNumber(mappedNote);
                    if (fixedVelocity > -1){
                        msg.setVelocity((float)fixedVelocity/127.0f);
                    }
                    incomingMidi.addEvent(msg, metadata.samplePosition);
                    
                    if (msg.isNoteOn()){
                        // Store message in the list of last note on messages and set its timestamp to the global playhead position
                        juce::MidiMessage msgToStoreInQueue = juce::MidiMessage(msg);
                        if (doingCountIn){
                            msgToStoreInQueue.setTimeStamp(countInplayheadPositionInBeats - countInLengthInBeats + metadata.samplePosition/bufferToFill.numSamples * bufferLengthInBeats);
                        } else {
                            msgToStoreInQueue.setTimeStamp(playheadPositionInBeats + metadata.samplePosition/bufferToFill.numSamples * bufferLengthInBeats);
                        }
                        lastMidiNoteOnMessages.insert(lastMidiNoteOnMessages.begin(), msgToStoreInQueue);
                    }
                }
            }
            
        } else if (msg.isController()){
            if (msg.getControllerNumber() >= 71 && msg.getControllerNumber() <= 78){
                // If midi message is a control change from one of the 8 encoders above the display, check if there's any mapping active
                // and use it (or discard the message).
                int mappedCCNumber = pushEncodersCCMapping[msg.getControllerNumber() - 71];
                if (mappedCCNumber > -1){
                    auto newMsg = juce::MidiMessage::controllerEvent (msg.getChannel(), mappedCCNumber, msg.getControllerValue());
                    newMsg.setTimeStamp (msg.getTimeStamp());
                    incomingMidi.addEvent(newMsg, metadata.samplePosition);
                }
            }
        }
    }
    
    // Remove old messages from lastMidiNoteOnMessages if capacity is exceeded
    if (lastMidiNoteOnMessages.size() > lastMidiNoteOnMessagesToStore){
        for (int i=0; i<lastMidiNoteOnMessagesToStore-lastMidiNoteOnMessages.size(); i++){
            lastMidiNoteOnMessages.pop_back();
        }
    }
    
    // Check if global playhead should be start/stopped
    if (shouldToggleIsPlaying){
        if (isPlaying){
            for (auto track: tracks){
                track->clipsRenderRemainingNoteOffsIntoMidiBuffer(generatedMidi);
                track->stopAllPlayingClips(true, true, true);
            }
            isPlaying = false;
            playheadPositionInBeats = 0.0;
        } else {
            for (auto track: tracks){
                track->clipsResetPlayheadPosition();
            }
            isPlaying = true;
        }
        shouldToggleIsPlaying = false;
    }
    
    // Copy incoming midi to generated midi buffer for tracks that have input monitoring enabled
    for (auto track: tracks){
        track->processInputMonitoring(incomingMidi, generatedMidi);
    }
    
    // Generate notes and/or record notes
    if (isPlaying){
        for (auto track: tracks){
            track->clipsProcessSlice(incomingMidi, generatedMidi, bufferToFill.numSamples, lastMidiNoteOnMessages);
        }
    }
    
    // TODO: do MIDI FX processing of the generatedMidi buffer per track (or per clip if we decide effects are clip-based?)

    
    // Add metronome ticks to the buffer
    if (metronomePendingNoteOffSamplePosition > -1){
        // If there was a noteOff metronome message pending from previous block, add it now to the buffer
        juce::MidiMessage msgOff = juce::MidiMessage::noteOff(metronomeMidiChannel, metronomePendingNoteOffIsHigh ? metronomeHighMidiNote: metronomeLowMidiNote, 0.0f);
        #if !RPI_BUILD
        // Don't send note off messages in RPI_BUILD as it messed up external metronome
        // Should investigate why...
        generatedMidi.addEvent(msgOff, metronomePendingNoteOffSamplePosition);
        #endif
        metronomePendingNoteOffSamplePosition = -1;
    }
    if (metronomeOn && (isPlaying || doingCountIn)) {
        
        double previousBeat = isPlaying ? playheadPositionInBeats : countInplayheadPositionInBeats;
        double beatsPerSample = 1 / (60.0 * sampleRate / bpm);
        for (int i=0; i<bufferToFill.numSamples; i++){
            double nextBeat = previousBeat + beatsPerSample;
            if (previousBeat == 0.0) {
                previousBeat = -0.1;  // Edge case for when global playhead has just started, otherwise we miss tick at time 0.0
            }
            if ((std::floor(nextBeat)) != std::floor(previousBeat)) {
                bool tickIsHigh = int(std::floor(nextBeat)) % 4 == 0;
                juce::MidiMessage msgOn = juce::MidiMessage::noteOn(metronomeMidiChannel, tickIsHigh ? metronomeHighMidiNote: metronomeLowMidiNote, metronomeMidiVelocity);
                generatedMidi.addEvent(msgOn, i);
                if (i + metronomeTickLengthInSamples < bufferToFill.numSamples){
                    juce::MidiMessage msgOff = juce::MidiMessage::noteOff(metronomeMidiChannel, tickIsHigh ? metronomeHighMidiNote: metronomeLowMidiNote, 0.0f);
                    #if !RPI_BUILD
                    // Don't send note off messages in RPI_BUILD as it messed up external metronome
                    // Should investigate why...
                    generatedMidi.addEvent(msgOff, i + metronomeTickLengthInSamples);
                    #endif
                } else {
                    metronomePendingNoteOffSamplePosition = i + metronomeTickLengthInSamples - bufferToFill.numSamples;
                    metronomePendingNoteOffIsHigh = tickIsHigh;
                }
            }
            previousBeat = nextBeat;
        }
    }
     
    // Send the generated MIDI buffer to the output
    if (midiOutA != nullptr)
        midiOutA.get()->sendBlockOfMessagesNow(generatedMidi);
    
    #if !RPI_BUILD
    if (renderWithInternalSynth){
        // Render the generated MIDI buffer with the sine synth for quick testing
        sineSynth.renderNextBlock (*bufferToFill.buffer, generatedMidi, bufferToFill.startSample, bufferToFill.numSamples);
    }
    #endif
    
    // Update playhead positions
    if (isPlaying){
        playheadPositionInBeats += bufferLengthInBeats;
    } else {
        if (doingCountIn) {
            countInplayheadPositionInBeats += bufferLengthInBeats;
        }
    }
}

void MainComponent::releaseResources()
{
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    #if !RPI_BUILD
    devUiComponent.setBounds(getLocalBounds());
    #else
    setSize(10, 10); // I think this needs to be called anyway...
    #endif
}

//==============================================================================
void MainComponent::timerCallback()
{
    // Things that need periodic checks
    if (!midiInIsConnected || !midiInPushIsConnected || !midiOutAIsConnected){
        if (juce::Time::getCurrentTime().toMilliseconds() - lastTimeMidiInitializationAttempted > 2000){
            // If at least one of the MIDI devices is not properly connected and 2 seconds have passed since last
            // time we tried to initialize them, try to initialize again
            initializeMIDI();
        }
    }
}

//==============================================================================
void MainComponent::sendOscMessage (const juce::OSCMessage& message)
{
    if (!oscSenderIsConnected){
        if (oscSender.connect (oscSendHost, oscSendPort)){
            oscSenderIsConnected = true;
        }
    }
    if (oscSenderIsConnected){
        oscSender.send (message);
    }
}

void MainComponent::oscMessageReceived (const juce::OSCMessage& message)
{
    const juce::String address = message.getAddressPattern().toString();
    
    if (address.startsWith(OSC_ADDRESS_CLIP)) {
        jassert(message.size() >= 2);
        int trackNum = message[0].getInt32();
        int clipNum = message[1].getInt32();
        if (trackNum < tracks.size()){
            auto track = tracks[trackNum];
            if (clipNum < track->getNumberOfClips()){
                auto clip = track->getClipAt(clipNum);
                if (address == OSC_ADDRESS_CLIP_PLAY){
                    if (!clip->isPlaying()){
                        track->stopAllPlayingClipsExceptFor(clipNum, false, true, false);
                        clip->togglePlayStop();
                    }
                } else if (address == OSC_ADDRESS_CLIP_STOP){
                    if (clip->isPlaying()){
                        clip->togglePlayStop();
                    }
                } else if (address == OSC_ADDRESS_CLIP_PLAY_STOP){
                    if (!clip->isPlaying()){
                        track->stopAllPlayingClipsExceptFor(clipNum, false, true, false);
                    }
                    clip->togglePlayStop();
                } else if (address == OSC_ADDRESS_CLIP_RECORD_ON_OFF){
                    if (!clip->isPlaying()){
                        track->stopAllPlayingClipsExceptFor(clipNum, false, true, false);
                    }
                    clip->toggleRecord();
                } else if (address == OSC_ADDRESS_CLIP_CLEAR){
                    clip->clearClip();
                } else if (address == OSC_ADDRESS_CLIP_DOUBLE){
                    clip->doubleSequence();
                } else if (address == OSC_ADDRESS_CLIP_UNDO){
                    clip->undo();
                } else if (address == OSC_ADDRESS_CLIP_QUANTIZE){
                    jassert(message.size() == 3);
                    double quantizationStep = (double)message[2].getFloat32();
                    clip->quantizeSequence(quantizationStep);
                } else if (address == OSC_ADDRESS_CLIP_SET_LENGTH){
                    jassert(message.size() == 3);
                    double newLength = (double)message[2].getFloat32();
                    clip->setNewClipLength(newLength);
                }
            }
        }
        
    } else if (address.startsWith(OSC_ADDRESS_TRACK)) {
        jassert(message.size() >= 1);
        int trackNum = message[0].getInt32();
        jassert(trackNum < tracks.size());
        if (address == OSC_ADDRESS_TRACK_SET_INPUT_MONITORING){
            jassert(message.size() == 2);
            bool trueFalse = message[1].getInt32() == 1;
            auto track = tracks[trackNum];
            track->setInputMonitoring(trueFalse);
        }
         
    } else if (address.startsWith(OSC_ADDRESS_SCENE)) {
        jassert(message.size() == 1);
        int sceneNum = message[0].getInt32();
        if (address == OSC_ADDRESS_SCENE_PLAY){
            playScene(sceneNum);
        } else if (address == OSC_ADDRESS_SCENE_DUPLICATE){
            duplicateScene(sceneNum);
        }
         
    } else if (address.startsWith(OSC_ADDRESS_TRANSPORT)) {
        if (address == OSC_ADDRESS_TRANSPORT_PLAY_STOP){
            jassert(message.size() == 0);
            if (isPlaying){
                // If it is playing, stop it
                shouldToggleIsPlaying = true;
            } else{
                // If it is not playing, check if there are record-armed clips and, if so, do count-in before playing
                bool recordCuedClipsFound = false;
                for (auto track: tracks){
                    if (track->hasClipsCuedToRecord()){
                        recordCuedClipsFound = true;
                        break;
                    }
                }
                if (recordCuedClipsFound){
                    doingCountIn = true;
                } else {
                    shouldToggleIsPlaying = true;
                }
            }
        } else if (address == OSC_ADDRESS_TRANSPORT_SET_BPM){
            jassert(message.size() == 1);
            float newBpm = message[0].getFloat32();
            if (newBpm > 0.0 && newBpm < 400.0){
                nextBpm = (double)newBpm;
            }
        }
        
    } else if (address.startsWith(OSC_ADDRESS_METRONOME)) {
        if (address == OSC_ADDRESS_METRONOME_ON){
            jassert(message.size() == 0);
            metronomeOn = true;
        } else if (address == OSC_ADDRESS_METRONOME_OFF){
            jassert(message.size() == 0);
            metronomeOn = false;
        } else if (address == OSC_ADDRESS_METRONOME_ON_OFF){
            jassert(message.size() == 0);
            metronomeOn = !metronomeOn;
        }
        
    } else if (address.startsWith(OSC_ADDRESS_SETTINGS)) {
        if (address == OSC_ADDRESS_SETTINGS_PUSH_NOTES_MAPPING){
            jassert(message.size() == 64);
            for (int i=0; i<64; i++){
                pushPadsNoteMapping[i] = message[i].getInt32();
            }
        } else if (address == OSC_ADDRESS_SETTINGS_PUSH_ENCODERS_MAPPING){
            jassert(message.size() == 8);
            for (int i=0; i<8; i++){
                pushEncodersCCMapping[i] = message[i].getInt32();
            }
        } else if (address == OSC_ADDRESS_SETTINGS_FIXED_VELOCITY){
            jassert(message.size() == 1);
            fixedVelocity = message[0].getInt32();
        } else if (address == OSC_ADDRESS_SETTINGS_FIXED_LENGTH){
            jassert(message.size() == 1);
            fixedLengthRecordingAmount = message[0].getFloat32();
        }
        
    } else if (address.startsWith(OSC_ADDRESS_STATE)) {
        if (address == OSC_ADDRESS_STATE_TRACKS){
            jassert(message.size() == 0);
            
            juce::StringArray stateAsStringParts = {};
            stateAsStringParts.add("tracks");
            stateAsStringParts.add((juce::String)tracks.size());
            for (auto track: tracks){
                stateAsStringParts.add("t");
                stateAsStringParts.add((juce::String)track->getNumberOfClips());
                stateAsStringParts.add(track->inputMonitoringEnabled() ? "1":"0");
                for (int i=0; i<track->getNumberOfClips(); i++){
                    stateAsStringParts.add(track->getClipAt(i)->getStatus());
                }
            }
            juce::OSCMessage returnMessage = juce::OSCMessage("/stateFromShepherd");
            returnMessage.addString(stateAsStringParts.joinIntoString(","));
            sendOscMessage(returnMessage);
            
            #if !RPI_BUILD
            devUiComponent.setStateTracks(stateAsStringParts.joinIntoString(","));
            #endif
            
        } else if (address == OSC_ADDRESS_STATE_TRANSPORT){
            jassert(message.size() == 0);
            
            juce::StringArray stateAsStringParts = {};
            stateAsStringParts.add("transport");
            stateAsStringParts.add(isPlaying ? "p":"s");
            stateAsStringParts.add(juce::String(bpm, 2));
            stateAsStringParts.add(juce::String(playheadPositionInBeats, 3));
            stateAsStringParts.add(metronomeOn ? "p":"s");
            juce::StringArray clipsPlayheadStateParts = {};
            for (int track_num=0; track_num<tracks.size(); track_num++){
                auto track = tracks[track_num];
                for (int clip_num=0; clip_num<track->getNumberOfClips(); clip_num++){
                    double clipPlayheadPosition = track->getClipAt(clip_num)->getPlayheadPosition();
                    if (clipPlayheadPosition > 0.0){
                        clipsPlayheadStateParts.add(juce::String(track_num));
                        clipsPlayheadStateParts.add(juce::String(clip_num));
                        clipsPlayheadStateParts.add(juce::String(clipPlayheadPosition, 3));
                    }
                }
            }
            stateAsStringParts.add(clipsPlayheadStateParts.joinIntoString(":"));
            stateAsStringParts.add(juce::String(fixedLengthRecordingAmount));
            
            juce::OSCMessage returnMessage = juce::OSCMessage("/stateFromShepherd");
            returnMessage.addString(stateAsStringParts.joinIntoString(","));
            sendOscMessage(returnMessage);
            
            #if !RPI_BUILD
            devUiComponent.setStateTransport(stateAsStringParts.joinIntoString(","));
            #endif
            
        }
    }
}

//==============================================================================
void MainComponent::playScene(int sceneN)
{
    jassert(sceneN < nScenes);
    for (auto track: tracks){
        auto clip = track->getClipAt(sceneN);
        track->stopAllPlayingClipsExceptFor(sceneN, false, true, false);
        clip->clearStopCue();
        if (!clip->isPlaying() && !clip->isCuedToPlay()){
            clip->togglePlayStop();
        }
    }
}

void MainComponent::duplicateScene(int sceneN)
{
    // Assert we're not attempting to duplicate if the selected scene is the very last as there's no more space to accomodate new clips
    jassert(sceneN < nScenes - 1);
    
    // Make a copy of the sceneN and insert it to the current position of sceneN. This will shift position of current
    // sceneN.
    for (auto track: tracks){
        auto clip = track->getClipAt(sceneN);
        track->insertClipAt(sceneN, clip->clone());
    }
}
