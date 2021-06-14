#include "MainComponent.h"


//==============================================================================
MainComponent::MainComponent()
: musicalContext([this]{return getGlobalSettings();})
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
    
    // Init hardware devices
    initializeHardwareDevices();
    
    // Init MIDI
    initializeMIDIInputs();
    initializeMIDIOutputs();  // Better to do it after hardware devices so we init devices needed in hardware devices as well
    notesMonitoringMidiOutput = juce::MidiOutput::createNewDevice("ShepherdBackendNotesMonitoring");
    
    // Init OSC
    initializeOSC();
    
    // Create tracks
    initializeTracks();
    
    // Init sine synth with 16 voices (used for testig purposes only)
    #if !RPI_BUILD
    for (auto i = 0; i < nSynthVoices; ++i)
        sineSynth.addVoice (new SineWaveVoice());
    sineSynth.addSound (new SineWaveSound());
    #endif
    
    // Send OSC message to frontend indiating that Shepherd is ready
    juce::OSCMessage returnMessage = juce::OSCMessage("/shepherdReady");
    sendOscMessage(returnMessage);
    
    mainComponentInitialized = true;
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
        std::cout << "- ERROR starting OSC server" << std::endl;
    } else {
        std::cout << "- Started OSC server, listening at 0.0.0.0:" << oscReceivePort << std::endl;
        addListener (this);
    }
    // OSC sender is not set up here because it is done lazily when trying to send a message
}

void MainComponent::initializeMIDIInputs()
{
    JUCE_ASSERT_MESSAGE_THREAD
    
    std::cout << "Initializing MIDI input devices" << std::endl;
    
    lastTimeMidiInputInitializationAttempted = juce::Time::getCurrentTime().toMilliseconds();
    
    // Setup MIDI devices
    auto midiInputs = juce::MidiInput::getAvailableDevices();

    if (!midiInIsConnected){ // Keyboard MIDI in
        #if RPI_BUILD
        const juce::String inDeviceName = "LUMI Keys BLOCK MIDI 1";
        #else
        const juce::String inDeviceName = "iCON iKEY V1.02";
        #endif
        juce::String inDeviceIdentifier = "";
        for (int i=0; i<midiInputs.size(); i++){
            if (midiInputs[i].name == inDeviceName){
                inDeviceIdentifier = midiInputs[i].identifier;
            }
        }
        midiIn = juce::MidiInput::openDevice(inDeviceIdentifier, &midiInCollector);
        if (midiIn != nullptr){
            std::cout << "- " << midiIn.get()->getName() << std::endl;
            midiIn.get()->start();
            midiInIsConnected = true;
        } else {
            std::cout << "- ERROR " << inDeviceName << ". Available MIDI IN devices: ";
            for (int i=0; i<midiInputs.size(); i++){
                std::cout << midiInputs[i].name << ((i != (midiInputs.size() - 1)) ? ", ": "");
            }
            std::cout << std::endl;
        }
    }

    if (!midiInPushIsConnected){ // Push messages MIDI in (used for triggering notes and encoders if mode is active)
        #if RPI_BUILD
        const juce::String pushInDeviceName = "Ableton Push 2 MIDI 1";
        #else
        const juce::String pushInDeviceName = "Push2Simulator";
        #endif
        juce::String pushInDeviceIdentifier = "";
        for (int i=0; i<midiInputs.size(); i++){
            if (midiInputs[i].name == pushInDeviceName){
                pushInDeviceIdentifier = midiInputs[i].identifier;
            }
        }
        midiInPush = juce::MidiInput::openDevice(pushInDeviceIdentifier, &pushMidiInCollector);
        if (midiInPush != nullptr){
            std::cout << "- " << midiInPush.get()->getName() << std::endl;
            midiInPush.get()->start();
            midiInPushIsConnected = true;
        } else {
            std::cout << "- ERROR " << pushInDeviceName << ". Available MIDI IN devices: ";
            for (int i=0; i<midiInputs.size(); i++){
                std::cout << midiInputs[i].name << ((i != (midiInputs.size() - 1)) ? ", ": "");
            }
            std::cout << std::endl;
        }
    }
}

void MainComponent::initializeMIDIOutputs()
{
    JUCE_ASSERT_MESSAGE_THREAD
    
    std::cout << "Initializing MIDI output devices" << std::endl;
    
    lastTimeMidiOutputInitializationAttempted = juce::Time::getCurrentTime().toMilliseconds();
    
    bool someFailedInitialization = false;
    
    // Initialize all MIDI devices required by available hardware devices
    for (auto hwDevice: hardwareDevices){
        auto midiDevice = initializeMidiOutputDevice(hwDevice->getMidiOutputDeviceName());
        if (midiDevice == nullptr) someFailedInitialization = true;
    }
    
    // Initialize midi output devices used for clock and metronome
    for (auto midiDeviceName: sendMidiClockMidiDeviceNames){
        auto midiDevice = initializeMidiOutputDevice(midiDeviceName);
        if (midiDevice == nullptr) someFailedInitialization = true;
    }
    for (auto midiDeviceName: sendMetronomeMidiDeviceNames){
        auto midiDevice = initializeMidiOutputDevice(midiDeviceName);
        if (midiDevice == nullptr) someFailedInitialization = true;
    }
    
    // Initialize midi output to Push MIDI input (used for sending clock messages)
    auto pushMidiDevice = initializeMidiOutputDevice(PUSH_MIDI_IN_DEVICE_NAME);
    if (pushMidiDevice == nullptr) someFailedInitialization = true;
    
    if (!someFailedInitialization) shouldTryInitializeMidiOutputs = false;
}

MidiOutputDeviceData* MainComponent::initializeMidiOutputDevice(juce::String deviceName)
{
    JUCE_ASSERT_MESSAGE_THREAD
        
    for (auto deviceData: midiOutDevices){
        if (deviceData->name == deviceName){
            // If device already initialized, early return
            return nullptr;
        }
    }
    
    auto midiOutputs = juce::MidiOutput::getAvailableDevices();
    juce::String outDeviceIdentifier = "";
    for (int i=0; i<midiOutputs.size(); i++){
        if (midiOutputs[i].name == deviceName){
            outDeviceIdentifier = midiOutputs[i].identifier;
        }
    }
    
    MidiOutputDeviceData* deviceData = new MidiOutputDeviceData();
    deviceData->identifier = outDeviceIdentifier;
    deviceData->name = deviceName;
    deviceData->device = juce::MidiOutput::openDevice(outDeviceIdentifier);
    if (deviceData->device != nullptr){
        std::cout << "- " << deviceData->device.get()->getName() << std::endl;
        midiOutDevices.add(deviceData);
        return deviceData;
    } else {
        delete deviceData; // Delete created MidiOutputDeviceData to avoid memory leaks with created buffer
        std::cout << "- ERROR " << deviceName << ". Available MIDI OUT devices: ";
        for (int i=0; i<midiOutputs.size(); i++){
            std::cout << midiOutputs[i].name << ((i != (midiOutputs.size() - 1)) ? ", ": "");
        }
        std::cout << std::endl;
        return nullptr;
    }
}

juce::MidiOutput* MainComponent::getMidiOutputDevice(juce::String deviceName)
{
    for (auto deviceData: midiOutDevices){
        if (deviceData->name == deviceName){
            return deviceData->device.get();
        }
    }
    // If function did not yet return, it means the requested output device has not yet been initialized
    // Set a flag so the device gets initialized in the message thread and return null pointer.
    // NOTE: we could check if we're in the message thread and, if this is the case, initialize the device
    // instead of setting a flag, but this optimization is probably not necessary.
    shouldTryInitializeMidiOutputs = true;
    return nullptr;
}

juce::MidiBuffer* MainComponent::getMidiOutputDeviceBuffer(juce::String deviceName)
{
    for (auto deviceData: midiOutDevices){
        if (deviceData->name == deviceName){
            return &deviceData->buffer;
        }
    }
    // If the above code does not return, this means we're trying to access a buffer of a MIDI device that has
    // not yet been initialized. Set a flag so the device gets initialized in the message thread and return null pointer.
    // NOTE: we could check if we're in the message thread and, if this is the case, initialize the device
    // instead of setting a flag, but this optimization is probably not necessary.
    shouldTryInitializeMidiOutputs = true;
    return nullptr;
}

void MainComponent::clearMidiDeviceOutputBuffers()
{
    for (auto deviceData: midiOutDevices){
        deviceData->buffer.clear();
    }
}

void MainComponent::sendMidiDeviceOutputBuffers()
{
    for (auto deviceData: midiOutDevices){
        deviceData->device.get()->sendBlockOfMessagesNow(deviceData->buffer);
    }
}

void MainComponent::writeMidiToDevicesMidiBuffer(juce::MidiBuffer& buffer, int bufferSize, std::vector<juce::String> midiOutDeviceNames)
{
    for (auto deviceName: midiOutDeviceNames){
        auto bufferToWrite = getMidiOutputDeviceBuffer(deviceName);
        if (bufferToWrite != nullptr){
            bufferToWrite->addEvents(buffer, 0, bufferSize, 0);
        }
    }
}

void MainComponent::initializeHardwareDevices()
{
    
    bool shouldLoadDefaults = false;
    
    juce::File hardwareDeviceDefinitionsLocation = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Shepherd/hardwareDevices").withFileExtension("json");
    if (hardwareDeviceDefinitionsLocation.existsAsFile()){
        std::cout << "Initializing Hardware Devices from JSON file" << std::endl;
        juce::var parsedJson;
        auto result = juce::JSON::parse(hardwareDeviceDefinitionsLocation.loadFileAsString(), parsedJson);
        if (!result.wasOk())
        {
            std::cout << "Error parsing JSON: " + result.getErrorMessage() << std::endl;
            std::cout << "Will load default hardware devices" << std::endl;
            shouldLoadDefaults = true;
        } else {
            // At the top level, the JSON file should be an array
            if (!parsedJson.isArray()){
                std::cout << "Devices configuration file has wrong contents or can't be read. Are permissions granted to access the file?" << std::endl;
                shouldLoadDefaults = true;
            } else {
                for (int i=0; i<parsedJson.size(); i++){
                    // Each element in the array should be an object element with the properties needed to create the hardware device
                    juce::var deviceInfo = parsedJson[i];
                    if (!parsedJson.isObject()){
                        std::cout << "Devices configuration file has wrong contents or can't be read." << std::endl;
                        shouldLoadDefaults = true;
                    }
                    juce::String name = deviceInfo.getProperty("name", "NoName").toString();
                    juce::String shortName = deviceInfo.getProperty("short_name", "NoShortName").toString();
                    juce::String midiDeviceName = deviceInfo.getProperty("midi_out_device", "NoMIDIOutDevice").toString();
                    int midiChannel = (int)deviceInfo.getProperty("midi_out_channel", "NoMIDIOutDevice");
                    HardwareDevice* device = new HardwareDevice(name,
                                                                shortName,
                                                                [this](juce::String deviceName){return getMidiOutputDevice(deviceName);},
                                                                [this](const juce::OSCMessage &message){sendOscMessage(message);}
                                                                );
                    device->configureMidiOutput(midiDeviceName, midiChannel);
                    hardwareDevices.add(device);
                    std::cout << "- " << name << std::endl;
                }
            }
        }
    } else {
        std::cout << "No hardware devices configuration file found at " << hardwareDeviceDefinitionsLocation.getFullPathName() << std::endl;
        shouldLoadDefaults = true;
    }
    
    if (shouldLoadDefaults){
        std::cout << "Initializing default Hardware Devices" << std::endl;
        
        #if RPI_BUILD
        const juce::String synthsMidiOut = "ESI M4U eX MIDI 5";
        #else
        const juce::String synthsMidiOut = "IAC Driver Bus 1";
        #endif

        for (int i=0; i<8; i++){
            juce::String name = "Synth " + juce::String(i + 1);
            HardwareDevice* device = new HardwareDevice(name,
                                                        "S" + juce::String(i + 1),
                                                        [this](juce::String deviceName){return getMidiOutputDevice(deviceName);},
                                                        [this](const juce::OSCMessage &message){sendOscMessage(message);}
                                                        );
            device->configureMidiOutput(synthsMidiOut, i + 1);
            hardwareDevices.add(device);
            std::cout << "- " << name << std::endl;
        }
    }
}

HardwareDevice* MainComponent::getHardwareDeviceByName(juce::String name)
{
    for (auto device: hardwareDevices){
        if (device->getShortName() == name || device->getName() == name){
            return device;
        }
    }
    // If no hardware device is available with that name, simply return null pointer
    return nullptr;
}

void MainComponent::initializeTracks()
{    
    // Create some tracks
    // By default create one track per available hardware device and assign the device to it (with a maximum of 8)
    // TODO: In the future this should be done by reading from some Shepherd preset/project/session file
    for (int i=0; i<juce::jmin(hardwareDevices.size(), 8); i++){
        tracks.add(
          new Track(
               [this]{ return juce::Range<double>{playheadPositionInBeats, playheadPositionInBeats + (double)samplesPerBlock / (60.0 * sampleRate / musicalContext.getBpm())}; },
               [this]{
                   return getGlobalSettings();
               },
               [this]{
                   return musicalContext;
               },
               [this](juce::String deviceName){
            return getMidiOutputDeviceBuffer(deviceName);
               }
        ));
        auto track = tracks[i];
        track->setHardwareDevice(hardwareDevices[i]);
        track->prepareClips();
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
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Clear audio buffers
    bufferToFill.clearActiveBufferRegion();
    
    if (!mainComponentInitialized){
        // If main component is not yet fully initialized, don't process further
        return;
    }
    
    // Clear midi output buffers
    clearMidiDeviceOutputBuffers();
    
    // Prepare some buffers to add messages to them
    juce::MidiBuffer midiClockMessages;
    juce::MidiBuffer midiMetronomeMessages;
    juce::MidiBuffer pushMidiClockMessages;
    
    // Check if tempo/meter should be updated
    if (nextBpm > 0.0){
        musicalContext.setBpm(nextBpm);
        shouldStartSendingPushMidiClockBurst = true;
        nextBpm = 0.0;
    }
    if (nextMeter > 0){
        musicalContext.setMeter(nextMeter);
        nextMeter = 0;
    }
    double bufferLengthInBeats = bufferToFill.numSamples / (60.0 * sampleRate / musicalContext.getBpm());
    
    // Check if count-in finished and global playhead should be toggled
    if (!isPlaying && doingCountIn){
        if (musicalContext.getMeter() >= countInplayheadPositionInBeats && musicalContext.getMeter() < countInplayheadPositionInBeats + bufferLengthInBeats){
            // Count in finishes in the current getNextAudioBlock execution
            playheadPositionInBeats = -(musicalContext.getMeter() - countInplayheadPositionInBeats); // Align global playhead position with coutin buffer offset so that it starts at correct offset
            shouldToggleIsPlaying = true;
            doingCountIn = false;
            countInplayheadPositionInBeats = 0.0;
        }
    }
    
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
                msgToStoreInQueue.setTimeStamp(countInplayheadPositionInBeats - musicalContext.getMeter() + metadata.samplePosition/bufferToFill.numSamples * bufferLengthInBeats);
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
        
        if (msg.isController() && msg.getControllerNumber() == MIDI_SUSTAIN_PEDAL_CC){
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
                            msgToStoreInQueue.setTimeStamp(countInplayheadPositionInBeats - musicalContext.getMeter() + metadata.samplePosition/bufferToFill.numSamples * bufferLengthInBeats);
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
                if (pushEncodersCCMappingHardwareDeviceShortName != ""){
                    auto device = getHardwareDeviceByName(pushEncodersCCMappingHardwareDeviceShortName);
                    if (device != nullptr){
                        int mappedCCNumber = pushEncodersCCMapping[msg.getControllerNumber() - 71];
                        if (mappedCCNumber > -1){
                            int rawControllerValue = msg.getControllerValue();
                            int increment = 0;
                            if (rawControllerValue > 0 && rawControllerValue < 64){
                                increment = rawControllerValue;
                            } else {
                                increment = rawControllerValue - 128;
                            }
                            int currentValue = device->getMidiCCParameterValue(mappedCCNumber);
                            int newValue = currentValue + increment;
                            if (newValue > 127){
                                newValue = 127;
                            } else if (newValue < 0){
                                newValue = 0;
                            }
                            auto newMsg = juce::MidiMessage::controllerEvent (msg.getChannel(), mappedCCNumber, newValue);
                            newMsg.setTimeStamp (msg.getTimeStamp());
                            incomingMidi.addEvent(newMsg, metadata.samplePosition);
                            
                            // Store value and notify the controller about the cc change so it can show updated value information
                            device->setMidiCCParameterValue(mappedCCNumber, newValue, true);
                        }
                    }
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
                track->clipsRenderRemainingNoteOffsIntoMidiBuffer();
                track->stopAllPlayingClips(true, true, true);
            }
            isPlaying = false;
            playheadPositionInBeats = 0.0;
            musicalContext.resetCounters();
            musicalContext.renderMidiStopInSlice(midiClockMessages, bufferToFill.numSamples);
        } else {
            for (auto track: tracks){
                track->clipsResetPlayheadPosition();
            }
            isPlaying = true;
            musicalContext.renderMidiStartInSlice(midiClockMessages, bufferToFill.numSamples);
        }
        shouldToggleIsPlaying = false;
    }
    
    // Copy incoming midi to generated midi buffer for tracks that have input monitoring enabled
    for (auto track: tracks){
        track->processInputMonitoring(incomingMidi);
    }
    
    // Generate notes and/or record notes
    if (isPlaying){
        for (auto track: tracks){
            track->clipsProcessSlice(incomingMidi, bufferToFill.numSamples, lastMidiNoteOnMessages);
        }
    }
    
    // TODO: do MIDI FX processing of the generatedMidi buffer per track/clip
    
    // Update musical context bar counter
    // This must be called before musicalContext.renderMetronomeInSlice to make sure metronome high tone is played when bar changes
    musicalContext.updateBarsCounter(juce::Range<double>{playheadPositionInBeats, playheadPositionInBeats + bufferLengthInBeats});
    
    // Render metronome in generated midi
    musicalContext.renderMetronomeInSlice(midiMetronomeMessages, bufferToFill.numSamples);
    if (sendMidiClock){
        musicalContext.renderMidiClockInSlice(midiClockMessages, bufferToFill.numSamples);
    }
    
    // Render clock in push midi buffer if required (and playing)
    if ((shouldStartSendingPushMidiClockBurst) && (isPlaying)){
        lastTimePushMidiClockBurstStarted = juce::Time::getCurrentTime().toMilliseconds();
        shouldStartSendingPushMidiClockBurst = false;
        musicalContext.renderMidiStartInSlice(pushMidiClockMessages, bufferToFill.numSamples);
    }
    if (lastTimePushMidiClockBurstStarted > -1.0){
        double timeNow = juce::Time::getCurrentTime().toMilliseconds();
        if (timeNow - lastTimePushMidiClockBurstStarted < PUSH_MIDI_CLOCK_BURST_DURATION_MILLISECONDS){
            pushMidiClockMessages.addEvents(midiClockMessages, 0, bufferToFill.numSamples, 0);
        } else if (timeNow - lastTimePushMidiClockBurstStarted > PUSH_MIDI_CLOCK_BURST_DURATION_MILLISECONDS){
            musicalContext.renderMidiStopInSlice(pushMidiClockMessages, bufferToFill.numSamples);
            lastTimePushMidiClockBurstStarted = -1.0;
        }
    }
    
    // Add metronome and midi clock messages to the corresponding buffers (also push midi clock messages)
    writeMidiToDevicesMidiBuffer(midiClockMessages, bufferToFill.numSamples, sendMidiClockMidiDeviceNames);
    writeMidiToDevicesMidiBuffer(midiMetronomeMessages, bufferToFill.numSamples, sendMetronomeMidiDeviceNames);
    if (pushMidiClockMessages.getNumEvents() > 0){
        writeMidiToDevicesMidiBuffer(pushMidiClockMessages, bufferToFill.numSamples, std::vector<juce::String>{PUSH_MIDI_IN_DEVICE_NAME});
    }

    // Send the generated MIDI buffers to the outputs
    sendMidiDeviceOutputBuffers();
    
    // Send monitred track notes to the notes output (if any selected)
    if ((notesMonitoringMidiOutput != nullptr) && (activeUiNotesMonitoringTrack >= 0) && (activeUiNotesMonitoringTrack < tracks.size())){
        monitoringNotesMidiBuffer.clear();
        auto track = tracks[activeUiNotesMonitoringTrack];
        auto buffer = getMidiOutputDeviceBuffer(track->getMidiOutputDeviceName());
        // TODO: send only current track buffer, not all contents of the device...
        // Maybe always store last buffer computer for a track in the track object
        if (buffer != nullptr){
            for (auto event: *buffer){
                auto msg = event.getMessage();
                if (msg.isNoteOnOrOff() && msg.getChannel() == track->getMidiOutputChannel()){
                    monitoringNotesMidiBuffer.addEvent(msg, event.samplePosition);
                }
            }
            notesMonitoringMidiOutput.get()->sendBlockOfMessagesNow(monitoringNotesMidiBuffer);
        }
    }
    
    #if !RPI_BUILD
    if (renderWithInternalSynth){
        // Render the generated MIDI buffers with the sine synth for quick testing
        // First all buffers are combined into a single buffer which is then sent to the synth
        juce::MidiBuffer combinedBuffer;
        for (auto deviceData: midiOutDevices){
            combinedBuffer.addEvents(deviceData->buffer, 0, bufferToFill.numSamples, 0);
        }
        sineSynth.renderNextBlock (*bufferToFill.buffer, combinedBuffer, bufferToFill.startSample, bufferToFill.numSamples);
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

GlobalSettingsStruct MainComponent::getGlobalSettings()
{
    GlobalSettingsStruct settings;
    settings.fixedLengthRecordingBars = fixedLengthRecordingBars;
    settings.nScenes = nScenes;
    settings.sampleRate = sampleRate;
    settings.samplesPerBlock = samplesPerBlock;
    settings.isPlaying = isPlaying;
    settings.playheadPositionInBeats = playheadPositionInBeats;
    settings.countInplayheadPositionInBeats = countInplayheadPositionInBeats;
    settings.doingCountIn = doingCountIn;
    settings.recordAutomationEnabled = recordAutomationEnabled;
    return settings;
}

//==============================================================================
void MainComponent::timerCallback()
{
    
    // If prepareToPlay has been called, we can now initializeTracks
    
    //std::cout << musicalContext.getBarCount() << ":" << musicalContext.getBeatsInBarCount() << std::endl;
    
    // Things that need periodic checks
    if (!midiInIsConnected || !midiInPushIsConnected ){
        if (juce::Time::getCurrentTime().toMilliseconds() - lastTimeMidiInputInitializationAttempted > 2000){
            // If at least one of the MIDI devices is not properly connected and 2 seconds have passed since last
            // time we tried to initialize them, try to initialize again
            initializeMIDIInputs();
        }
    }
    
    if (shouldTryInitializeMidiOutputs){
        if (juce::Time::getCurrentTime().toMilliseconds() - lastTimeMidiOutputInitializationAttempted > 2000){
            // If at least one of the MIDI devices is not properly connected and 2 seconds have passed since last
            // time we tried to initialize them, try to initialize again
            initializeMIDIOutputs();
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
        if (trackNum >= tracks.size()) return;
        auto track = tracks[trackNum];
        if (address == OSC_ADDRESS_TRACK_SET_INPUT_MONITORING){
            jassert(message.size() == 2);
            bool trueFalse = message[1].getInt32() == 1;
            track->setInputMonitoring(trueFalse);
        } else if (address == OSC_ADDRESS_TRACK_SET_ACTIVE_UI_NOTES_MONITORING_TRACK){
            activeUiNotesMonitoringTrack = trackNum;
        }
        
    } else if (address.startsWith(OSC_ADDRESS_DEVICE)) {
        jassert(message.size() >= 1);
        juce::String deviceName = message[0].getString();
        auto device = getHardwareDeviceByName(deviceName);
        if (device == nullptr) return;
        if (address == OSC_ADDRESS_DEVICE_SEND_ALL_NOTES_OFF_TO_DEVICE){
             device->sendAllNotesOff();
        } else if (address == OSC_ADDRESS_DEVICE_LOAD_DEVICE_PRESET){
            jassert(message.size() == 3);
            int bank = message[1].getInt32();
            int preset = message[2].getInt32();
            device->loadPreset(bank, preset);
        } else if (address == OSC_ADDRESS_DEVICE_SEND_MIDI){
            jassert(message.size() == 4);
            juce::MidiMessage msg = juce::MidiMessage(message[1].getInt32(), message[2].getInt32(), message[3].getInt32());
            device->sendMidi(msg);
        } else if (address ==  OSC_ADDRESS_DEVICE_SET_MIDI_CC_PARAMETERS){
            jassert(message.size() > 1);
            for (int i=1; i<message.size(); i=i+2){
                int index = message[i].getInt32();
                int value = message[i + 1].getInt32();
                device->setMidiCCParameterValue(index, value, false);
                // Don't notify controller about the cc value change as the change is most probably comming from the controller itself
            }
        } else if (address ==  OSC_ADDRESS_DEVICE_GET_MIDI_CC_PARAMETERS){
            jassert(message.size() > 1);
            juce::OSCMessage returnMessage = juce::OSCMessage("/midiCCParameterValuesForDevice");
            returnMessage.addString(device->getShortName());
            for (int i=1; i<message.size(); i++){
                int index = message[i].getInt32();
                int value = device->getMidiCCParameterValue(index);
                returnMessage.addInt32(index);
                returnMessage.addInt32(value);
            }
            sendOscMessage(returnMessage);
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
        } else if (address == OSC_ADDRESS_TRANSPORT_SET_METER){
            jassert(message.size() == 1);
            int newMeter = message[0].getInt32();
            if (newMeter > 0 && !doingCountIn){
                // Don't allow chaning meter while doing count in, this could lead to severe disaster
                nextMeter = newMeter;
            }
        }
        
    } else if (address.startsWith(OSC_ADDRESS_METRONOME)) {
        if (address == OSC_ADDRESS_METRONOME_ON){
            jassert(message.size() == 0);
            musicalContext.setMetronome(true);
        } else if (address == OSC_ADDRESS_METRONOME_OFF){
            jassert(message.size() == 0);
            musicalContext.setMetronome(false);
        } else if (address == OSC_ADDRESS_METRONOME_ON_OFF){
            jassert(message.size() == 0);
            musicalContext.toggleMetronome();
        }
        
    } else if (address.startsWith(OSC_ADDRESS_SETTINGS)) {
        if (address == OSC_ADDRESS_SETTINGS_PUSH_NOTES_MAPPING){
            jassert(message.size() == 64);
            for (int i=0; i<64; i++){
                pushPadsNoteMapping[i] = message[i].getInt32();
            }
        } else if (address == OSC_ADDRESS_SETTINGS_PUSH_ENCODERS_MAPPING){
            jassert(message.size() == 9);
            juce::String deviceName = message[0].getString();
            pushEncodersCCMappingHardwareDeviceShortName = deviceName;
            for (int i=1; i<9; i++){
                pushEncodersCCMapping[i - 1] = message[i].getInt32();
            }
            
            // Send the currently stored values for these controls to the controller
            // The Controller needs these values to display current cc parameter values on the display
            auto device = getHardwareDeviceByName(deviceName);
            if (device != nullptr){
                juce::OSCMessage returnMessage = juce::OSCMessage("/midiCCParameterValuesForDevice");
                returnMessage.addString(device->getShortName());
                for (int i=0; i<8; i++){
                    int index = pushEncodersCCMapping[i];
                    if (index > -1){
                        int value = device->getMidiCCParameterValue(index);
                        returnMessage.addInt32(index);
                        returnMessage.addInt32(value);
                    }
                }
                sendOscMessage(returnMessage);
            }

        } else if (address == OSC_ADDRESS_SETTINGS_FIXED_VELOCITY){
            jassert(message.size() == 1);
            fixedVelocity = message[0].getInt32();
        } else if (address == OSC_ADDRESS_SETTINGS_FIXED_LENGTH){
            jassert(message.size() == 1);
            fixedLengthRecordingBars = message[0].getInt32();
        } else if (address == OSC_ADDRESS_TRANSPORT_RECORD_AUTOMATION){
            jassert(message.size() == 0);
            recordAutomationEnabled = !recordAutomationEnabled;
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
                if (track->getHardwareDevice() != nullptr){
                    stateAsStringParts.add(track->getHardwareDevice()->getShortName());
                } else {
                    stateAsStringParts.add("NoDevice");
                }
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
            stateAsStringParts.add(juce::String(musicalContext.getBpm(), 2));
            if (doingCountIn){
                stateAsStringParts.add(juce::String(-1 * (musicalContext.getMeter() - countInplayheadPositionInBeats), 3));
            } else {
                stateAsStringParts.add(juce::String(playheadPositionInBeats, 3));
            }
            stateAsStringParts.add(musicalContext.metronomeIsOn() ? "p":"s");
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
            stateAsStringParts.add(juce::String(fixedLengthRecordingBars));
            stateAsStringParts.add(juce::String(musicalContext.getMeter()));
            stateAsStringParts.add(juce::String(recordAutomationEnabled ? "1":"0"));
            
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
