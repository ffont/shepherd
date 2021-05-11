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
    tempoSlider.onValueChange = [this] { bpm = tempoSlider.getValue(); };
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
    
    // Clip controls
    addAndMakeVisible (clipPlayheadLabel);
    addAndMakeVisible (clipRecorderPlayheadLabel);
    addAndMakeVisible (clipRecordButton);
    clipRecordButton.onClick = [this] { midiClip->toggleRecord(); };
    clipRecordButton.setButtonText("Record");
    addAndMakeVisible (clipClearButton);
    clipClearButton.onClick = [this] { midiClip->clearSequence(); };
    clipClearButton.setButtonText("Clear");
    addAndMakeVisible (clipStartStopButton);
    clipStartStopButton.onClick = [this] { midiClip->togglePlayStopNow(); };
    clipStartStopButton.setButtonText("Start/Stop");
        
    // Set UI size and start timer to print playhead position
    setSize (800, 600);
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
    for (auto i = 0; i < 16; ++i)
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
    
    midiClip.reset(new Clip([this]{ return juce::Range<double>{playheadPositionInBeats, playheadPositionInBeats + (double)samplesPerBlock / (60.0 * sampleRate / bpm)}; },
                            [this]{ return bpm; },
                            [this]{ return sampleRate; },
                            [this]{ return samplesPerBlock; },
                            [this]{ return midiOutChannel; }
                            ));
    midiClip->playNow();
    
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
    
    // Do global start/stop if requested
    if (shouldToggleIsPlaying){
        if (isPlaying){
            midiClip->renderRemainingNoteOffsIntoMidiBuffer(generatedMidi);
            isPlaying = false;
        } else {
            playheadPositionInBeats = 0.0;
            midiClip->getPlayerPlayhead()->resetSlice();
            isPlaying = true;
        }
        shouldToggleIsPlaying = false;
    }
    
    // Generate notes and/or record notes
    if (isPlaying){
        midiClip->recordFromBuffer(incomingMidi, bufferToFill.numSamples);
        midiClip->renderSliceIntoMidiBuffer(generatedMidi, bufferToFill.numSamples);
    }
    
    // Copy all incoming MIDI notes to the output buffer for direct monitoring
    for (const auto metadata : incomingMidi)
    {
        auto msg = metadata.getMessage();
        msg.setChannel(midiOutChannel);
        generatedMidi.addEvent(msg, metadata.samplePosition);
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
    auto sliderLeft = 70;
    
    playheadLabel.setBounds(16, 20, 200, 20);
    globalStartStopButton.setBounds(16 + 210, 20, 100, 20);
    midiOutChannelLabel.setBounds(16 + 210 + 110, 20, 50, 20);
    midiOutChannelSetButton.setBounds(16 + 210 + 110 + 60, 20, 50, 20);
    tempoSlider.setBounds (sliderLeft, 45, getWidth() - sliderLeft - 10, 20);
 
    clipPlayheadLabel.setBounds(16, 70, 200, 20);
    clipRecorderPlayheadLabel.setBounds(16 + 110, 70, 200, 20);
    clipStartStopButton.setBounds(16 + 210, 70, 100, 20);
    clipRecordButton.setBounds(16 + 210 + 110, 70, 50, 20);
    clipClearButton.setBounds(16 + 210 + 60 + 110, 70, 50, 20);
}

//==============================================================================
void MainComponent::timerCallback()
{
    playheadLabel.setText ((juce::String)playheadPositionInBeats, juce::dontSendNotification);
    midiOutChannelLabel.setText ((juce::String)midiOutChannel, juce::dontSendNotification);
    
    clipPlayheadLabel.setText ((juce::String)midiClip->getPlayerPlayhead()->getCurrentSlice().getStart(), juce::dontSendNotification);
    clipRecorderPlayheadLabel.setText ((juce::String)midiClip->getRecorderPlayhead()->getCurrentSlice().getStart(), juce::dontSendNotification);
    
    if ((midiClip->getRecorderPlayhead()->isPlaying()) && (!midiClip->getRecorderPlayhead()->isCuedToPlay()) && (!midiClip->getRecorderPlayhead()->isCuedToStop())){
        clipPlayheadLabel.setColour(juce::Label::textColourId, juce::Colours::red);
    } else if ((midiClip->getRecorderPlayhead()->isPlaying()) && (midiClip->getRecorderPlayhead()->isCuedToStop())){
        clipPlayheadLabel.setColour(juce::Label::textColourId, juce::Colours::yellow);
    } else if ((!midiClip->getRecorderPlayhead()->isPlaying()) && (midiClip->getRecorderPlayhead()->isCuedToPlay())){
        clipPlayheadLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    } else {
        clipPlayheadLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    }
}
