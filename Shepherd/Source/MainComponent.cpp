#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
{
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
    
    addAndMakeVisible (recordButton);
    recordButton.onClick = [this] { willStartRecordingMidi = true; };
    recordButton.setButtonText("Record");
    
    addAndMakeVisible (clearSequenceButton);
    clearSequenceButton.onClick = [this] { shouldClearSequence = true; };
    clearSequenceButton.setButtonText("Clear");
    
    setSize (800, 600);
    
    // Start timer to print playhead position
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
    
    
    // Initialize recordedSequence some notes
    noteOnTimes = {
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
        juce::MidiMessage msgNoteOn = juce::MidiMessage::noteOn(1, 64, 1.0f);
        msgNoteOn.setTimeStamp(note.first + note.second);
        recordedSequence.addEvent(msgNoteOn);
        juce::MidiMessage msgNoteOff = juce::MidiMessage::noteOff(1, 64, 1.0f);
        msgNoteOff.setTimeStamp(note.first + note.second + 0.25);
        recordedSequence.addEvent(msgNoteOff);
    }
}

MainComponent::~MainComponent()
{
    // This shuts down the audio device and clears the audio source.
    shutdownAudio();
}

//==============================================================================
void MainComponent::prepareToPlay (int samplesPerBlockExpected, double _sampleRate)
{
    // This function will be called when the audio device is started, or when
    // its settings (i.e. sample rate, block size, etc) are changed.

    // You can use this function to initialise any resources you might need,
    // but be careful - it will be called on the audio thread, not the GUI thread.

    // For more details, see the help for AudioProcessor::prepareToPlay()
    sampleRate = _sampleRate;
    
    midiInCollector.reset(_sampleRate);
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Clear audio buffers
    bufferToFill.clearActiveBufferRegion();
    
    // Porcess MIDI INPUT
    juce::MidiBuffer incomingMidi;
    midiInCollector.removeNextBlockOfMessages (incomingMidi, bufferToFill.numSamples);
    
    // Keep track of notes being currently played
    for (const auto metadata : incomingMidi)
    {
        const auto msg = metadata.getMessage();
        if      (msg.isNoteOn())  currentNotesMidiIn.add (msg.getNoteNumber());
        else if (msg.isNoteOff()) currentNotesMidiIn.removeValue (msg.getNoteNumber());
    }
         
    // Generate MIDI OUTPUT
    
    if (shouldClearSequence) {
        recordedSequence.clear();
        isPlayingRecordedSequence = false;
        willPlayRecordedSequence = true;
        shouldClearSequence = false;
    }
    
    int samplesPerBeat = (int)std::round(60.0 * sampleRate / bpm);
    
    juce::MidiBuffer generatedMidi;  // TODO: Is this thread safe?
    
    // Copy all incoming MIDI to the output buffer and, if recording, to the recording message collection
    for (const auto metadata : incomingMidi)
    {
        const auto msg = metadata.getMessage();
        generatedMidi.addEvent(msg, metadata.samplePosition);
        
        if (isRecordingMidi){
            juce::MidiMessage newMessage = juce::MidiMessage(msg);
            newMessage.setTimeStamp(beatCount - beatStartedRecordingMidi + currentBeatFraction + metadata.samplePosition/samplesPerBeat);
            recordingSequence.addEvent(newMessage);
        }
    }
    
    for (int i=0; i<bufferToFill.numSamples; i++){
        
        // Check if MIDI recording should start (at start of bar)
        if ((willStartRecordingMidi) && (currentBeat == 0)){
            beatStartedRecordingMidi = beatCount;
            isRecordingMidi = true;
            willStartRecordingMidi = false;
        }
        
        // Check if MIDI recording should stop
        if ((isRecordingMidi) && (beatCount - beatStartedRecordingMidi >= midiRecorderTime)){
            isRecordingMidi = false;
            beatStartedRecordingMidi = 0;
            // Use .addSequence to add too previous notes, use .swapWith to erase previous sequence
            //recordedSequence.swapWith(recordingSequence);
            recordedSequence.addSequence(recordingSequence, 0);
            recordingSequence.clear();
            isPlayingRecordedSequence = false;
            willPlayRecordedSequence = true;
        }
        
        // Check if recorded sequence should start playing (at start of bar)
        if ((willPlayRecordedSequence) && (recordedSequence.getNumEvents() > 0) && (currentBeat == 0)){
            isPlayingRecordedSequence = true;
            willPlayRecordedSequence = false;
            beatRecordedSequenceLastTriggered = beatCount;
        }
        
        // Update playhead
        double bufferStartBeatWithFraction = beatCount + currentBeatFraction;
        if (samplesSinceLastBeat == samplesPerBeat){
            samplesSinceLastBeat = 0;
            beatCount += 1;
            currentBeat = beatCount % beatsPerBar;
            currentBar = beatCount / beatsPerBar;
        } else if (samplesSinceLastBeat > samplesPerBeat){
            samplesSinceLastBeat = samplesSinceLastBeat - samplesPerBeat;
            beatCount += 1;
            currentBeat = beatCount % beatsPerBar;
            currentBar = beatCount / beatsPerBar;
        } else {
            samplesSinceLastBeat += 1;
        }
        currentBeatFraction = (double)samplesSinceLastBeat / (double)samplesPerBeat;
        double bufferEndBeatWithFraction = beatCount + currentBeatFraction;
        
        // See if notes from the recorded sequence should be triggered
        for (int j=0; j < recordedSequence.getNumEvents(); j++){
            juce::MidiMessage msg = recordedSequence.getEventPointer(j)->message;
            if ((msg.getTimeStamp() + beatRecordedSequenceLastTriggered >= bufferStartBeatWithFraction) && (msg.getTimeStamp() + beatRecordedSequenceLastTriggered < bufferEndBeatWithFraction)){
                generatedMidi.addEvent(msg, i);
                
                if (j == recordedSequence.getNumEvents() - 1){
                    // If last note has been added, we have reached end of clip
                    // Set it to "not playing" and trigger loop at next bar
                    isPlayingRecordedSequence = false;
                    willPlayRecordedSequence = true;
                }
            }
        }
    }
    
    // Send the generated MIDI buffer to the output
    midiOutA.get()->sendBlockOfMessagesNow(generatedMidi);
}

void MainComponent::releaseResources()
{
    // This will be called when the audio device stops, or when it is being
    // restarted due to a setting change.

    // For more details, see the help for AudioProcessor::releaseResources()
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    // You can add your drawing code here!
}

void MainComponent::resized()
{
    // This is called when the MainContentComponent is resized.
    // If you add any child components, this is where you should
    // update their positions.
    auto sliderLeft = 70;
    playheadLabel.setBounds(16, 20, 200, 20);
    recordButton.setBounds(16 + 210, 20, 50, 20);
    clearSequenceButton.setBounds(16 + 210 + 60, 20, 50, 20);
    tempoSlider.setBounds (sliderLeft, 45, getWidth() - sliderLeft - 10, 20);
}

//==============================================================================
void MainComponent::timerCallback()
{
    const juce::String text = juce::String::formatted("Playhead: %i|%i|%03i", currentBar + 1, currentBeat + 1, (int)(round(currentBeatFraction * 1000)));
    playheadLabel.setText (text, juce::dontSendNotification);
    
    if (isRecordingMidi){
        playheadLabel.setColour(juce::Label::textColourId, juce::Colours::red);
    } else {
        if (willStartRecordingMidi) {
            playheadLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
        } else {
            playheadLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        }
    }
    
    if ((!isRecordingMidi) && (!recordButton.isEnabled())){
        recordButton.setEnabled(true);
    }
    
    if (((willStartRecordingMidi) || (isRecordingMidi)) && (recordButton.isEnabled())){
        recordButton.setEnabled(false);
    }
}
