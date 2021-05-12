#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
{
    // Main transport controls
    addAndMakeVisible (tempoSlider);
    tempoSlider.setRange (40, 300);
    tempoSlider.setValue(bpm);
    tempoSlider.setTextValueSuffix (" bpm");
    tempoSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 160, tempoSlider.getTextBoxHeight());
    tempoSlider.onValueChange = [this] { nextBpm = tempoSlider.getValue(); };
    addAndMakeVisible (tempoSliderLabel);
    tempoSliderLabel.setText ("Tempo", juce::dontSendNotification);
    tempoSliderLabel.attachToComponent (&tempoSlider, true);
    addAndMakeVisible (playheadLabel);
    addAndMakeVisible (globalStartStopButton);
    globalStartStopButton.onClick = [this] { shouldToggleIsPlaying = true; };
    globalStartStopButton.setButtonText("Start/Stop");
    addAndMakeVisible (midiOutChannelLabel);
    addAndMakeVisible (midiOutChannelSetButton);
    midiOutChannelSetButton.setButtonText ("MIDI ch");
    midiOutChannelSetButton.onClick = [this] { midiOutChannel = midiOutChannel % 16 + 1; };
        
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
    const juce::String outDeviceName = "IAC Driver Bus 1";
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
    
    const juce::String inDeviceName = "iCON iKEY V1.02";
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
        std::cout << "Starting MIDI in callback" << std::endl;
        midiIn.get()->start();
    }
    
    // Init sine synth with 16 voices (used for testig purposes only)
    #if JUCE_DEBUG
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
    
    for (auto track: tracks){
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
    
    // Do global start/stop if requested and change of tempo
    if (nextBpm > 0.0){
        bpm = nextBpm;
        nextBpm = 0.0;
    }
    
    if (shouldToggleIsPlaying){
        if (isPlaying){
            for (auto track: tracks){
                track->clipsRenderRemainingNoteOffsIntoMidiBuffer(generatedMidi);
            }
            isPlaying = false;
        } else {
            playheadPositionInBeats = 0.0;
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
    if (metronomeOn) {
        double previousBeat = playheadPositionInBeats;
        double beatsPerSample = 1 / (60.0 * sampleRate / bpm);
        for (int i=0; i<bufferToFill.numSamples; i++){
            double nextBeat = previousBeat + beatsPerSample;
            if (previousBeat == 0.0) {
                previousBeat = -0.1;  // Edge case for when global playhead has just started, otherwise we miss tick at time 0.0
            }
            if ((std::floor(nextBeat)) != std::floor(previousBeat)) {
                juce::MidiMessage msgOn = juce::MidiMessage::noteOn(metronomeMidiChannel, metronomeMidiNote, metronomeMidiVelocity);
                juce::MidiMessage msgOff = juce::MidiMessage::noteOff(metronomeMidiChannel, metronomeMidiNote, 0.0f);
                generatedMidi.addEvent(msgOn, i);
                generatedMidi.addEvent(msgOff, i + metronomeNoteLengthInSamples);
            }
            previousBeat = nextBeat;
        }
    }
     
    // Send the generated MIDI buffer to the output
    if (midiOutA != nullptr)
        midiOutA.get()->sendBlockOfMessagesNow(generatedMidi);
    
    #if JUCE_DEBUG
    // Render the generated MIDI buffer with the sine synth
    sineSynth.renderNextBlock (*bufferToFill.buffer, generatedMidi, bufferToFill.startSample, bufferToFill.numSamples);
    #endif
    
    // Update playhead positions
    if (isPlaying){
        playheadPositionInBeats += bufferToFill.numSamples / (60.0 * sampleRate / bpm);
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
        for (auto track: tracks){
            for (int i=0; i<track->getNumberOfClips(); i++){
                auto clipPlayheadLabel = new juce::Label();
                auto clipRecordButton = new juce::TextButton();
                auto clipClearButton = new juce::TextButton();
                auto clipStartStopButton = new juce::TextButton();
                
                clipRecordButton->onClick = [track, i] { track->getClipAt(i)->toggleRecord(); };
                clipRecordButton->setButtonText("Record");
                clipClearButton->onClick = [track, i] { track->getClipAt(i)->clearSequence(); };
                clipClearButton->setButtonText("Clear");
                clipStartStopButton->onClick = [track, i] { track->getClipAt(i)->togglePlayStop(); };
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
    
    playheadLabel.setBounds(16, 20, 200, 20);
    globalStartStopButton.setBounds(16 + 210, 20, 100, 20);
    midiOutChannelLabel.setBounds(16 + 210 + 110, 20, 50, 20);
    midiOutChannelSetButton.setBounds(16 + 210 + 110 + 60, 20, 50, 20);
    tempoSlider.setBounds (sliderLeft, 45, getWidth() - sliderLeft - 10, 20);
 
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
    
    playheadLabel.setText ((juce::String)playheadPositionInBeats, juce::dontSendNotification);
    midiOutChannelLabel.setText ((juce::String)midiOutChannel, juce::dontSendNotification);
    
    if (clipControlElementsCreated){
        for (int i=0; i<tracks.size(); i++){
            for (int j=0; j<tracks[i]->getNumberOfClips(); j++){
                auto midiClip = tracks[i]->getClipAt(j);
                int componentIndex = tracks[i]->getNumberOfClips() * i + j;  // Note this can fail if tracks don't have same number of clips... just for quick testing...
                midiClipsPlayheadLabels[componentIndex]->setText ((juce::String)midiClip->getPlayheadPosition() + " (" + (juce::String)midiClip->getLengthInBeats() + ")", juce::dontSendNotification);
                if (midiClip->isRecording() && !midiClip->isCuedToStopRecording()) {
                    midiClipsPlayheadLabels[componentIndex]->setColour(juce::Label::textColourId, juce::Colours::red);
                } else if (midiClip->isRecording() && midiClip->isCuedToStopRecording()) {
                    midiClipsPlayheadLabels[componentIndex]->setColour(juce::Label::textColourId, juce::Colours::yellow);
                } else if (!midiClip->isRecording() && midiClip->isCuedToStartRecording()) {
                    midiClipsPlayheadLabels[componentIndex]->setColour(juce::Label::textColourId, juce::Colours::orange);
                } else {
                    midiClipsPlayheadLabels[componentIndex]->setColour(juce::Label::textColourId, juce::Colours::white);
                }
                
                if (midiClip->isPlaying() && !midiClip->isCuedToStop()){
                    midiClipsStartStopButtons[componentIndex]->setButtonText("Stop");
                    midiClipsStartStopButtons[componentIndex]->setColour(juce::TextButton::buttonColourId, juce::Colours::green);
                } else if (midiClip->isCuedToStop() || midiClip->isCuedToPlay()){
                    midiClipsStartStopButtons[componentIndex]->setColour(juce::TextButton::buttonColourId, juce::Colours::orange);
                } else {
                    midiClipsStartStopButtons[componentIndex]->setButtonText("Start");
                    midiClipsStartStopButtons[componentIndex]->setColour(juce::TextButton::buttonColourId, juce::Colours::black);
                }
            }
        }
    }
}

void MainComponent::oscMessageReceived (const juce::OSCMessage& message)
{
    std::cout << message.getAddressPattern().toString() << std::endl;
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
