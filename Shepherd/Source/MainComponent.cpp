#include "MainComponent.h"

#define OSC_ADDRESS_TRANSPORT "/transport"
#define OSC_ADDRESS_TRANSPORT_PLAY_STOP "/transport/playStop"
#define OSC_ADDRESS_TRANSPORT_SET_BPM "/transport/setBpm"

#define OSC_ADDRESS_CLIP "/clip"
#define OSC_ADDRESS_CLIP_PLAY "/clip/play"
#define OSC_ADDRESS_CLIP_STOP "/clip/stop"
#define OSC_ADDRESS_CLIP_PLAY_STOP "/clip/playStop"
#define OSC_ADDRESS_CLIP_RECORD_ON_OFF "/clip/recordOnOff"
#define OSC_ADDRESS_CLIP_CLEAR "/clip/clear"
#define OSC_ADDRESS_CLIP_DOUBLE "/clip/double"
#define OSC_ADDRESS_CLIP_QUANTIZE "/clip/quantize"
#define OSC_ADDRESS_CLIP_UNDO "/clip/undo"

#define OSC_ADDRESS_TRACK "/track"
#define OSC_ADDRESS_TRACK_SET_INPUT_MONITORING "/track/setInputMonitoring"

#define OSC_ADDRESS_SCENE "/scene"
#define OSC_ADDRESS_SCENE_DUPLICATE "/scene/duplicate"
#define OSC_ADDRESS_SCENE_PLAY "/scene/play"

#define OSC_ADDRESS_METRONOME "/metronome"
#define OSC_ADDRESS_METRONOME_ON "/metronome/on"
#define OSC_ADDRESS_METRONOME_OFF "/metronome/off"
#define OSC_ADDRESS_METRONOME_ON_OFF "/metronome/onOff"

#define OSC_ADDRESS_SETTINGS "/settings"
#define OSC_ADDRESS_SETTINGS_PUSH_NOTES_MAPPING "/settings/pushNotesMapping"
#define OSC_ADDRESS_SETTINGS_PUSH_ENCODERS_MAPPING "/settings/pushEncodersMapping"
#define OSC_ADDRESS_SETTINGS_FIXED_VELOCITY "/settings/fixedVelocity"

#define OSC_ADDRESS_STATE "/state"
#define OSC_ADDRESS_STATE_TRACKS "/state/tracks"
#define OSC_ADDRESS_STATE_TRANSPORT "/state/transport"




//==============================================================================
MainComponent::MainComponent()
{
    #if !RPI_BUILD
    
    // Main transport controls
    addAndMakeVisible (tempoSlider);
    tempoSlider.setRange (40, 300);
    tempoSlider.setValue(bpm);
    tempoSlider.setTextValueSuffix (" bpm");
    tempoSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 160, tempoSlider.getTextBoxHeight());
    tempoSlider.onValueChange = [this] {
        juce::OSCMessage message = juce::OSCMessage(OSC_ADDRESS_TRANSPORT_SET_BPM);
        message.addFloat32((float)tempoSlider.getValue());
        oscMessageReceived(message);
    };
    addAndMakeVisible (tempoSliderLabel);
    tempoSliderLabel.setText ("Tempo", juce::dontSendNotification);
    tempoSliderLabel.attachToComponent (&tempoSlider, true);
    addAndMakeVisible (playheadLabel);
    addAndMakeVisible (globalStartStopButton);
    globalStartStopButton.onClick = [this] {
        juce::OSCMessage message = juce::OSCMessage(OSC_ADDRESS_TRANSPORT_PLAY_STOP);
        oscMessageReceived(message);
    };
    globalStartStopButton.setButtonText("Start/Stop");
        
    addAndMakeVisible (metronomeToggleButton);
    metronomeToggleButton.setButtonText ("Metro on/off");
    metronomeToggleButton.onClick = [this] {
        juce::OSCMessage message = juce::OSCMessage(OSC_ADDRESS_METRONOME_ON_OFF);
        oscMessageReceived(message);
    };
    
    addAndMakeVisible (internalSynthButton);
    internalSynthButton.setButtonText ("Synth on/off");
    internalSynthButton.onClick = [this] {
        renderWithInternalSynth = !renderWithInternalSynth;
        // NOTE: we don't use OSC interface here because this setting is only meant for testing
        // purposes and is not included in "release" builds using OSC interface
    };
    #endif
    
    // Set UI size and start timer to print playhead position
    setSize (710, 575);
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
               [this]{ return bpm; },
               [this]{ return sampleRate; },
               [this]{ return samplesPerBlock; },
               nScenes
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
    // Recreate clip contorl objects if tracks array has been filled
    if (tracks.size() > 0 && !clipControlElementsCreated){
        clipControlElementsCreated = true;
        for (int j=0; j<tracks.size(); j++){
            auto track = tracks[j];
            for (int i=0; i<track->getNumberOfClips(); i++){
                auto clipPlayheadLabel = new juce::Label();
                auto clipRecordButton = new juce::TextButton();
                auto clipStartStopButton = new juce::TextButton();
                auto clipClearButton = new juce::TextButton();
                auto clipDoubleButton = new juce::TextButton();
                
                clipRecordButton->onClick = [this, j, i] {
                    juce::OSCMessage message = juce::OSCMessage(OSC_ADDRESS_CLIP_RECORD_ON_OFF);
                    message.addInt32(j);
                    message.addInt32(i);
                    oscMessageReceived(message);
                };
                clipRecordButton->setButtonText("Record");
                clipStartStopButton->onClick = [this, j, i] {
                    juce::OSCMessage message = juce::OSCMessage(OSC_ADDRESS_CLIP_PLAY_STOP);
                    message.addInt32(j);
                    message.addInt32(i);
                    oscMessageReceived(message);
                };
                clipStartStopButton->setButtonText("Start");
                clipClearButton->onClick = [this, j, i] {
                    juce::OSCMessage message = juce::OSCMessage(OSC_ADDRESS_CLIP_CLEAR);
                    message.addInt32(j);
                    message.addInt32(i);
                    oscMessageReceived(message);
                };
                clipClearButton->setButtonText("C");
                clipDoubleButton->onClick = [this, j, i] {
                    juce::OSCMessage message = juce::OSCMessage(OSC_ADDRESS_CLIP_DOUBLE);
                    message.addInt32(j);
                    message.addInt32(i);
                    oscMessageReceived(message);
                };
                clipDoubleButton->setButtonText("D");
                
                addAndMakeVisible (clipPlayheadLabel);
                addAndMakeVisible (clipRecordButton);
                addAndMakeVisible (clipClearButton);
                addAndMakeVisible (clipStartStopButton);
                addAndMakeVisible (clipDoubleButton);
                
                midiClipsClearButtons.add(clipClearButton);
                midiClipsDoubleButtons.add(clipDoubleButton);
                midiClipsRecordButtons.add(clipRecordButton);
                midiClipsPlayheadLabels.add(clipPlayheadLabel);
                midiClipsStartStopButtons.add(clipStartStopButton);
            }
        }
        for (int i=0; i<nScenes; i++){
            auto scenePlayButton = new juce::TextButton();
            auto sceneDuplicateButton = new juce::TextButton();
            
            scenePlayButton->onClick = [this, i] {
                juce::OSCMessage message = juce::OSCMessage(OSC_ADDRESS_SCENE_PLAY);
                message.addInt32(i);
                oscMessageReceived(message);
            };
            scenePlayButton->setButtonText("Tri");
            
            sceneDuplicateButton->onClick = [this, i] {
                juce::OSCMessage message = juce::OSCMessage(OSC_ADDRESS_SCENE_DUPLICATE);
                message.addInt32(i);
                oscMessageReceived(message);
            };
            sceneDuplicateButton->setButtonText("Dup");
            
            addAndMakeVisible (scenePlayButton);
            addAndMakeVisible (sceneDuplicateButton);
            
            scenePlayButtons.add(scenePlayButton);
            sceneDuplicateButtons.add(sceneDuplicateButton);
        }
    }
    
    auto sliderLeft = 70;
    
    playheadLabel.setBounds(16, 20, 90, 20);
    globalStartStopButton.setBounds(16 + 100, 20, 100, 20);
    selectTrackButton.setBounds(16 + 210 + 110 + 60, 20, 50, 20);
    tempoSlider.setBounds (sliderLeft, 45, getWidth() - sliderLeft - 10, 20);
    metronomeToggleButton.setBounds(16 + 210 + 110 + 60 + 60, 20, 50, 20);
    internalSynthButton.setBounds(16 + 210 + 110 + 60 + 60 + 60, 20, 50, 20);
 
    if (clipControlElementsCreated){
        for (int i=0; i<tracks.size(); i++){
            int xoffset = 80 * i;
            for (int j=0; j<tracks[i]->getNumberOfClips(); j++){
                int yoffset = j * 120;
                int componentIndex = tracks[i]->getNumberOfClips() * i + j;  // Note this can fail if tracks don't have same number of clips... just for quick testing...
                midiClipsPlayheadLabels[componentIndex]->setBounds(16 + xoffset, 100 + yoffset, 70, 20);
                midiClipsStartStopButtons[componentIndex]->setBounds(16 + xoffset, 100 + yoffset + 25, 70, 20);
                midiClipsRecordButtons[componentIndex]->setBounds(16 + xoffset, 100 + yoffset + 50, 70, 20);
                midiClipsClearButtons[componentIndex]->setBounds(16 + xoffset, 100 + yoffset + 75, 30, 20);
                midiClipsDoubleButtons[componentIndex]->setBounds(16 + xoffset + 40, 100 + yoffset + 75, 30, 20);
            }
        }
        
        for (int i=0; i<nScenes; i++){
            int yoffset = 120 * i;
            scenePlayButtons[i]->setBounds(16 + 80 * 8, 100 + yoffset, 40, 20);
            sceneDuplicateButtons[i]->setBounds(16 + 80 * 8, 100 + yoffset + 25, 40, 20);
        }
    }
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
    
    #if !RPI_BUILD
    // Backend test UI stuff (not to be run in RPI builds)

    if (!clipControlElementsCreated){
        // Call resized methods so all the clip control componenets are created and drawn
        resized();
    }
    
    if (doingCountIn){
        playheadLabel.setText ((juce::String)(-1 * (countInLengthInBeats - countInplayheadPositionInBeats)), juce::dontSendNotification);
    } else {
        playheadLabel.setText ((juce::String)playheadPositionInBeats, juce::dontSendNotification);
    }
    
    if (tempoSlider.getValue() != bpm){
        tempoSlider.setValue(bpm);
    }
    
    if (clipControlElementsCreated){
        for (int i=0; i<tracks.size(); i++){
            for (int j=0; j<tracks[i]->getNumberOfClips(); j++){
                auto midiClip = tracks[i]->getClipAt(j);
                int componentIndex = tracks[i]->getNumberOfClips() * i + j;  // Note this can fail if tracks don't have same number of clips... just for quick testing...
                midiClipsPlayheadLabels[componentIndex]->setText ((juce::String)midiClip->getPlayheadPosition() + " (" + (juce::String)midiClip->getLengthInBeats() + ")", juce::dontSendNotification);
                
                juce::String clipStatus = midiClip->getStatus();
                
                if (clipStatus.contains(CLIP_STATUS_RECORDING)){
                    midiClipsPlayheadLabels[componentIndex]->setColour(juce::Label::textColourId, juce::Colours::red);
                } else if (clipStatus.contains(CLIP_STATUS_CUED_TO_RECORD)){
                    midiClipsPlayheadLabels[componentIndex]->setColour(juce::Label::textColourId, juce::Colours::orange);
                } else if (clipStatus.contains(CLIP_STATUS_CUED_TO_STOP_RECORDING)){
                    midiClipsPlayheadLabels[componentIndex]->setColour(juce::Label::textColourId, juce::Colours::yellow);
                } else {
                    midiClipsPlayheadLabels[componentIndex]->setColour(juce::Label::textColourId, juce::Colours::white);
                }
                
                if (clipStatus.contains(CLIP_STATUS_PLAYING)){
                    midiClipsStartStopButtons[componentIndex]->setButtonText("Stop");
                    midiClipsStartStopButtons[componentIndex]->setColour(juce::TextButton::buttonColourId, juce::Colours::green);
                } else if ((clipStatus.contains(CLIP_STATUS_CUED_TO_PLAY)) || (clipStatus.contains(CLIP_STATUS_CUED_TO_STOP))){
                    midiClipsStartStopButtons[componentIndex]->setColour(juce::TextButton::buttonColourId, juce::Colours::orange);
                } else {
                    midiClipsStartStopButtons[componentIndex]->setButtonText("Start");
                    midiClipsStartStopButtons[componentIndex]->setColour(juce::TextButton::buttonColourId, juce::Colours::black);
                }
            }
        }
    }
    #endif
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
        jassert(message.size() == 2);
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
                    clip->clearSequence();
                } else if (address == OSC_ADDRESS_CLIP_DOUBLE){
                    clip->doubleSequence();
                } else if (address == OSC_ADDRESS_CLIP_QUANTIZE){
                    clip->cycleQuantization();
                } else if (address == OSC_ADDRESS_CLIP_UNDO){
                    clip->undo();
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
            std::cout << trackNum << " " << trueFalse << std::endl;
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
            
        } else if (address == OSC_ADDRESS_STATE_TRANSPORT){
            jassert(message.size() == 0);
            
            juce::StringArray stateAsStringParts = {};
            stateAsStringParts.add("transport");
            stateAsStringParts.add(isPlaying ? "p":"s");
            stateAsStringParts.add((juce::String)bpm);
            stateAsStringParts.add((juce::String)playheadPositionInBeats);
            stateAsStringParts.add(metronomeOn ? "p":"s");
            
            juce::OSCMessage returnMessage = juce::OSCMessage("/stateFromShepherd");
            returnMessage.addString(stateAsStringParts.joinIntoString(","));
            sendOscMessage(returnMessage);
            
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
