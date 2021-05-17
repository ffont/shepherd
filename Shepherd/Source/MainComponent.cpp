#include "MainComponent.h"

#define OSC_ADDRESS_TRANSPORT "/transport"
#define OSC_ADDRESS_TRANSPORT_PLAY_STOP "/transport/playStop"
#define OSC_ADDRESS_TRANSPORT_RECORD_ON_OFF "/transport/recordOnOff"
#define OSC_ADDRESS_TRANSPORT_SET_BPM "/transport/setBpm"

#define OSC_ADDRESS_CLIP "/clip"
#define OSC_ADDRESS_CLIP_PLAY "/clip/play"
#define OSC_ADDRESS_CLIP_STOP "/clip/stop"
#define OSC_ADDRESS_CLIP_PLAY_STOP "/clip/playStop"
#define OSC_ADDRESS_CLIP_RECORD_ON_OFF "/clip/recordOnOff"
#define OSC_ADDRESS_CLIP_CLEAR "/clip/clear"

#define OSC_ADDRESS_TRACK "/track"
#define OSC_ADDRESS_TRACK_SELECT "/track/select"

#define OSC_ADDRESS_METRONOME "/metronome"
#define OSC_ADDRESS_METRONOME_ON "/metronome/on"
#define OSC_ADDRESS_METRONOME_OFF "/metronome/off"
#define OSC_ADDRESS_METRONOME_ON_OFF "/metronome/onOff"

#define OSC_ADDRESS_STATE "/state"
#define OSC_ADDRESS_STATE_TRACKS "/state/tracks"
#define OSC_ADDRESS_STATE_TRANSPORT "/state/transport"



//==============================================================================
MainComponent::MainComponent()
{
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
    addAndMakeVisible (globalRecordButton);
    globalRecordButton.onClick = [this] {
        juce::OSCMessage message = juce::OSCMessage(OSC_ADDRESS_TRANSPORT_RECORD_ON_OFF);
        oscMessageReceived(message);
    };
    globalRecordButton.setButtonText("Record");
    
    addAndMakeVisible (selectedTrackLabel);
    addAndMakeVisible (selectTrackButton);
    selectTrackButton.setButtonText ("Track sel");
    selectTrackButton.onClick = [this] {
        int newSelectedTrack = (selectedTrack + 1) % tracks.size();
        juce::OSCMessage message = juce::OSCMessage(OSC_ADDRESS_TRACK_SELECT);
        message.addInt32(newSelectedTrack);
        oscMessageReceived(message);
    };
    
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
    
    // Set UI size and start timer to print playhead position
    setSize (665, 575);
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
    
    // Setup OSC rserver
    if (! connect (oscReceivePort)){
        DBG("ERROR starting OSC server");
    } else {
        DBG("Started OSC server, listening at 0.0.0.0:" << oscReceivePort);
        addListener (this);
    }
    
    // Setup MIDI devices
    // TODO: read device names from config file instead of hardcoding them
    #if RPI_BUILD
    const juce::String outDeviceName = "MIDIFACE 2X2 MIDI 1";
    #else
    const juce::String outDeviceName = "IAC Driver Bus 1";
    #endif
    juce::String outDeviceIdentifier = "";
    auto midiOutputs = juce::MidiOutput::getAvailableDevices();
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
    } else {
        std::cout << "Could not connect to " << outDeviceName << std::endl;
    }
        
    #if RPI_BUILD
    const juce::String inDeviceName = "MIDIFACE 2X2 MIDI 1";
    #else
    const juce::String inDeviceName = "iCON iKEY V1.02";
    #endif
    juce::String inDeviceIdentifier = "";
    auto midiInputs = juce::MidiInput::getAvailableDevices();
    std::cout << "Available MIDI IN devices:" << std::endl;
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
    } else {
        std::cout << "Could not connect to " << inDeviceName << std::endl;
    }
    
    // Init sine synth with 16 voices (used for testig purposes only)
    #if !RPI_BUILD
    for (auto i = 0; i < nSynthVoices; ++i)
        sineSynth.addVoice (new SineWaveVoice());
    sineSynth.addSound (new SineWaveSound());
    #endif
}

MainComponent::~MainComponent()
{
    shutdownAudio();
}

//==============================================================================
void MainComponent::prepareToPlay (int samplesPerBlockExpected, double _sampleRate)
{
    std::cout << "Prepare to play called with samples per block " << samplesPerBlockExpected << " and sample rate " << _sampleRate << std::endl;

    sampleRate = _sampleRate;
    samplesPerBlock = samplesPerBlockExpected;
    
    midiInCollector.reset(_sampleRate);
    sineSynth.setCurrentPlaybackSampleRate (_sampleRate);
    
    // Create some tracks
    for (int i=0; i<nTestTracks; i++){
        tracks.add(
          new Track(
               [this]{ return juce::Range<double>{playheadPositionInBeats, playheadPositionInBeats + (double)samplesPerBlock / (60.0 * sampleRate / bpm)}; },
               [this]{ return bpm; },
               [this]{ return sampleRate; },
               [this]{ return samplesPerBlock; }
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
    
    // Collect messages from MIDI input
    juce::MidiBuffer incomingMidi;
    midiInCollector.removeNextBlockOfMessages (incomingMidi, bufferToFill.numSamples);
    
    // Generate MIDI output buffer
    juce::MidiBuffer generatedMidi;  // TODO: Is this thread safe?
    
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
            playheadPositionInBeats = 0.0; // Align global playhead position with coutin buffer offset so that it starts at correct offset
            shouldToggleIsPlaying = true;
            doingCountIn = false;
            countInplayheadPositionInBeats = 0.0;
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
    
    // Generate notes and/or record notes
    if (isPlaying){
        for (auto track: tracks){
            track->clipsProcessSlice(incomingMidi, generatedMidi, bufferToFill.numSamples);
        }
    }
    
    // Copy all incoming MIDI notes to the output buffer for direct monitoring of the currently selected channel
    for (const auto metadata : incomingMidi)
    {
        auto msg = metadata.getMessage();
        int channel = tracks[selectedTrack]->getMidiOutChannel();
        msg.setChannel(channel);
        generatedMidi.addEvent(msg, metadata.samplePosition);
    }
    
    // Add metronome ticks to the buffer
    if (metronomePendingNoteOffSamplePosition){
        // If there was a noteOff metronome message pending from previous block, add it now to the buffer
        juce::MidiMessage msgOff = juce::MidiMessage::noteOff(metronomeMidiChannel, metronomePendingNoteOffIsHigh ? metronomeHighMidiNote: metronomeLowMidiNote, 0.0f);
        generatedMidi.addEvent(msgOff, metronomePendingNoteOffSamplePosition);
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
                    generatedMidi.addEvent(msgOff, i + metronomeTickLengthInSamples);
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
                auto clipClearButton = new juce::TextButton();
                auto clipStartStopButton = new juce::TextButton();
                
                clipRecordButton->onClick = [this, j, i] {
                    juce::OSCMessage message = juce::OSCMessage(OSC_ADDRESS_CLIP_RECORD_ON_OFF);
                    message.addInt32(j);
                    message.addInt32(i);
                    oscMessageReceived(message);
                };
                clipRecordButton->setButtonText("Record");
                clipClearButton->onClick = [this, j, i] {
                    juce::OSCMessage message = juce::OSCMessage(OSC_ADDRESS_CLIP_CLEAR);
                    message.addInt32(j);
                    message.addInt32(i);
                    oscMessageReceived(message);
                };
                clipClearButton->setButtonText("Clear");
                clipStartStopButton->onClick = [this, j, i] {
                    juce::OSCMessage message = juce::OSCMessage(OSC_ADDRESS_CLIP_PLAY_STOP);
                    message.addInt32(j);
                    message.addInt32(i);
                    oscMessageReceived(message);
                };
                clipStartStopButton->setButtonText("Start");
                
                addAndMakeVisible (clipPlayheadLabel);
                addAndMakeVisible (clipRecordButton);
                addAndMakeVisible (clipClearButton);
                addAndMakeVisible (clipStartStopButton);
                
                midiClipsClearButtons.add(clipClearButton);
                midiClipsRecordButtons.add(clipRecordButton);
                midiClipsPlayheadLabels.add(clipPlayheadLabel);
                midiClipsStartStopButtons.add(clipStartStopButton);
            }
        }
    }
    
    auto sliderLeft = 70;
    
    playheadLabel.setBounds(16, 20, 90, 20);
    globalStartStopButton.setBounds(16 + 100, 20, 100, 20);
    globalRecordButton.setBounds(16 + 210, 20, 100, 20);
    selectedTrackLabel.setBounds(16 + 210 + 110, 20, 50, 20);
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
                midiClipsClearButtons[componentIndex]->setBounds(16 + xoffset, 100 + yoffset + 75, 70, 20);
            }
        }
    }
}

//==============================================================================
void MainComponent::timerCallback()
{
    if (!clipControlElementsCreated){
        // Call resized methods so all the clip control componenets are created and drawn
        resized();
    }
    
    if (doingCountIn){
        playheadLabel.setText ((juce::String)(-1 * (countInLengthInBeats - countInplayheadPositionInBeats)), juce::dontSendNotification);
    } else {
        playheadLabel.setText ((juce::String)playheadPositionInBeats, juce::dontSendNotification);
    }
    
    selectedTrackLabel.setText ((juce::String)selectedTrack, juce::dontSendNotification);
    
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
}

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
                    clip->toggleRecord();
                } else if (address == OSC_ADDRESS_CLIP_CLEAR){
                    clip->clearSequence();
                }
            }
        }
        
    } else if (address.startsWith(OSC_ADDRESS_TRACK)) {
        jassert(message.size() == 1);
        int trackNum = message[0].getInt32();
        if (trackNum < tracks.size() && trackNum >= 0){
            if (address == OSC_ADDRESS_TRACK_SELECT){
                selectedTrack = trackNum;
            }
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
        } else if (address == OSC_ADDRESS_TRANSPORT_RECORD_ON_OFF){
            auto track = tracks[selectedTrack];
            std::vector<int> currentlyPlayingClipIndexes = track->getCurrentlyPlayingClipsIndex();
            jassert(currentlyPlayingClipIndexes.size() <= 1);  // Only one clip per track should be playing at a time
            if (currentlyPlayingClipIndexes.size() == 1){
                // If currently selected track has a playing clip, toggle recording on that clip
                for (auto clipNum: currentlyPlayingClipIndexes){
                    track->getClipAt(clipNum)->toggleRecord();
                }
            } else {
                // If currently selected track has no playing clip, it also means no clip is recording. Toggle recording on the first non-empty clip
                for (int i=0; i<track->getNumberOfClips(); i++){
                    auto clip = track->getClipAt(i);
                    if (clip->isEmpty()){
                        clip->toggleRecord();
                        break;
                    }
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
        
    } else if (address.startsWith(OSC_ADDRESS_STATE)) {
        if (address == OSC_ADDRESS_STATE_TRACKS){
            jassert(message.size() == 0);
            
            juce::StringArray stateAsStringParts = {};
            stateAsStringParts.add("tracks");
            stateAsStringParts.add((juce::String)tracks.size());
            stateAsStringParts.add((juce::String)selectedTrack);
            for (auto track: tracks){
                stateAsStringParts.add("t");
                stateAsStringParts.add((juce::String)track->getNumberOfClips());
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
            stateAsStringParts.add((juce::String)selectedTrack);
            
            juce::OSCMessage returnMessage = juce::OSCMessage("/stateFromShepherd");
            returnMessage.addString(stateAsStringParts.joinIntoString(","));
            sendOscMessage(returnMessage);
            
        }
    }
}
