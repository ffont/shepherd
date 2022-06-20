/*
  ==============================================================================

    Sequencer.cpp
    Created: 10 Jun 2022 12:07:14pm
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#include "Sequencer.h"


//==============================================================================
Sequencer::Sequencer()
{
    // Start timer for recurring tasks
    startTimer (50);
    
    // Set some defaults
    if (juce::String(DEFAULT_MIDI_CLOCK_OUT_DEVICE_NAME).length() > 0){
        sendMidiClockMidiDeviceNames = {DEFAULT_MIDI_CLOCK_OUT_DEVICE_NAME};
    }
    if (juce::String(DEFAULT_MIDI_OUT_DEVICE_NAME).length() > 0){
        sendMetronomeMidiDeviceNames = {DEFAULT_MIDI_OUT_DEVICE_NAME};
    }
    
    // Init hardware devices
    initializeHardwareDevices();
    
    // Init MIDI
    initializeMIDIInputs();
    initializeMIDIOutputs();  // Better to do it after hardware devices so we init devices needed in hardware devices as well
    notesMonitoringMidiOutput = juce::MidiOutput::createNewDevice(SHEPHERD_NOTES_MONITORING_MIDI_DEVICE_NAME);
    
    // Init OSC
    initializeOSC();
    
    // Init sine synth with 16 voices (used for testig purposes only)
    #if !RPI_BUILD
    for (auto i = 0; i < nSynthVoices; ++i)
        sineSynth.addVoice (new SineWaveVoice());
    sineSynth.addSound (new SineWaveSound());
    #endif
    
    // Load empty session to state
    DBG("Creating default session state");
    #if !RPI_BUILD
    int numEnabledTracks = juce::Random::getSystemRandom().nextInt (juce::Range<int> (2, MAX_NUM_TRACKS - 1));
    #else
    int numEnabledTracks = 0;
    #endif
    state = Helpers::createDefaultSession(availableHardwareDeviceNames, numEnabledTracks);
    
    // Add state change listener and bind cached properties to state properties
    bindState();
    
    // Initialize musical context
    musicalContext = std::make_unique<MusicalContext>([this]{return getGlobalSettings();}, state);
    
    // Create tracks
    initializeTracks();
    
    // Send OSC message to frontend indiating that Shepherd is ready to rock
    juce::OSCMessage message = juce::OSCMessage(OSC_ADDRESS_SHEPHERD_READY);
    sendOscMessage(message);
    sequencerInitialized = true;
    
    #if !RPI_BUILD
    // Randomly create clips so that we have testing material
    randomizeClipsNotes();
    #endif
}

Sequencer::~Sequencer()
{
}

void Sequencer::bindState()
{
    state.addListener(this);
    
    name.referTo(state, IDs::name, nullptr, Defaults::emptyString);
    fixedLengthRecordingBars.referTo(state, IDs::fixedLengthRecordingBars, nullptr, Defaults::fixedLengthRecordingBars);
    recordAutomationEnabled.referTo(state, IDs::recordAutomationEnabled, nullptr, Defaults::recordAutomationEnabled);
    fixedVelocity.referTo(state, IDs::fixedVelocity, nullptr, Defaults::fixedVelocity);
    
    if (musicalContext != nullptr){
        musicalContext->bindState();
    }
}

void Sequencer::saveCurrentSessionToFile()
{
    juce::File saveOutputFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Shepherd/" + state.getProperty(IDs::name).toString()).withFileExtension("xml");
    if (auto xml = std::unique_ptr<juce::XmlElement> (state.createXml()))
        xml->writeTo(saveOutputFile);
}

void Sequencer::loadSessionFromFile(juce::String sessionName)
{
    // TODO: This should be run when the RT thread is not trying to access state or objects, we should use some sort of flag to prevent that
    juce::File filePath = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Shepherd/" + sessionName).withFileExtension("xml");
    if (auto xml = std::unique_ptr<juce::XmlElement> (juce::XmlDocument::parse (filePath))){
        juce::ValueTree loadedState = juce::ValueTree::fromXml (*xml);
        // TODO: remove things like playhead positions, play/recording state and other things which are "voaltile"
        state = loadedState;
        bindState();
    }
    initializeTracks();
}

void Sequencer::initializeOSC()
{
    // Setup OSC server
    // Note that OSC sender is not set up here because it is done lazily when trying to send a message
    std::cout << "Initializing OSC server" << std::endl;
    if (! connect (oscReceivePort)){
        std::cout << "- ERROR starting OSC server" << std::endl;
    } else {
        std::cout << "- Started OSC server, listening at 0.0.0.0:" << oscReceivePort << std::endl;
        addListener (this);
    }
}


bool Sequencer::midiDeviceAlreadyInitialized(const juce::String& deviceName)
{
    for (auto deviceData: midiOutDevices){
        if (deviceData->name == deviceName){
            // If device already initialized, early return
            return true;
        }
    }
    return false;
}

void Sequencer::initializeMIDIInputs()
{
    JUCE_ASSERT_MESSAGE_THREAD
    
    std::cout << "Initializing MIDI input devices" << std::endl;
    
    lastTimeMidiInputInitializationAttempted = juce::Time::getCurrentTime().toMilliseconds();
    
    // Setup MIDI devices
    auto midiInputs = juce::MidiInput::getAvailableDevices();

    if (!midiInIsConnected){ // Keyboard MIDI in
        const juce::String inDeviceName = DEFAULT_KEYBOARD_MIDI_IN_DEVICE_NAME;
        juce::String inDeviceIdentifier = "";
        for (int i=0; i<midiInputs.size(); i++){
            if (midiInputs[i].name == inDeviceName){
                inDeviceIdentifier = midiInputs[i].identifier;
            }
        }
        midiIn = juce::MidiInput::openDevice(inDeviceIdentifier, &midiInCollector);
        if (midiIn != nullptr){
            std::cout << "- " << midiIn->getName() << std::endl;
            midiIn->start();
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
        const juce::String pushInDeviceName = PUSH_MIDI_IN_DEVICE_NAME;
        juce::String pushInDeviceIdentifier = "";
        for (int i=0; i<midiInputs.size(); i++){
            if (midiInputs[i].name == pushInDeviceName){
                pushInDeviceIdentifier = midiInputs[i].identifier;
            }
        }
        midiInPush = juce::MidiInput::openDevice(pushInDeviceIdentifier, &pushMidiInCollector);
        if (midiInPush != nullptr){
            std::cout << "- " << midiInPush->getName() << std::endl;
            midiInPush->start();
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

void Sequencer::initializeMIDIOutputs()
{
    JUCE_ASSERT_MESSAGE_THREAD
    
    std::cout << "Initializing MIDI output devices" << std::endl;
    
    lastTimeMidiOutputInitializationAttempted = juce::Time::getCurrentTime().toMilliseconds();
    
    bool someFailedInitialization = false;
    
    // Initialize all MIDI devices required by available hardware devices
    for (auto hwDevice: hardwareDevices){
        if (!midiDeviceAlreadyInitialized(hwDevice->getMidiOutputDeviceName())){
            auto midiDevice = initializeMidiOutputDevice(hwDevice->getMidiOutputDeviceName());
            if (midiDevice == nullptr) {
                DBG("Failed to initialize midi device for hardware device: " << hwDevice->getMidiOutputDeviceName());
                someFailedInitialization = true;
            }
        }
    }
    
    // Initialize midi output devices used for clock and metronome
    for (auto midiDeviceName: sendMidiClockMidiDeviceNames){
        if (!midiDeviceAlreadyInitialized(midiDeviceName)){
            auto midiDevice = initializeMidiOutputDevice(midiDeviceName);
            if (midiDevice == nullptr) {
                DBG("Failed to initialize midi device for clock: " << midiDeviceName);
                someFailedInitialization = true;
            }
        }
    }
    for (auto midiDeviceName: sendMetronomeMidiDeviceNames){
        if (!midiDeviceAlreadyInitialized(midiDeviceName)){
            auto midiDevice = initializeMidiOutputDevice(midiDeviceName);
            if (midiDevice == nullptr) {
                DBG("Failed to initialize midi device for metronome: " << midiDeviceName);
                someFailedInitialization = true;
            }
        }
    }
    
    // Initialize midi output to Push MIDI input (used for sending clock messages to push and sync animations with Shepherd tempo)
    if (juce::String(PUSH_MIDI_OUT_DEVICE_NAME).length() > 0){
        if (!midiDeviceAlreadyInitialized(PUSH_MIDI_OUT_DEVICE_NAME)){
            auto pushMidiDevice = initializeMidiOutputDevice(PUSH_MIDI_OUT_DEVICE_NAME);
            if (pushMidiDevice == nullptr) {
                DBG("Failed to initialize push midi device: " << PUSH_MIDI_OUT_DEVICE_NAME);
                someFailedInitialization = true;
            }
        }
    }
    
    if (!someFailedInitialization) shouldTryInitializeMidiOutputs = false;
}

MidiOutputDeviceData* Sequencer::initializeMidiOutputDevice(juce::String deviceName)
{
    JUCE_ASSERT_MESSAGE_THREAD
    
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
        std::cout << "- " << deviceData->device->getName() << std::endl;
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

juce::MidiOutput* Sequencer::getMidiOutputDevice(juce::String deviceName)
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

juce::MidiBuffer* Sequencer::getMidiOutputDeviceBuffer(juce::String deviceName)
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

void Sequencer::clearMidiDeviceOutputBuffers()
{
    for (auto deviceData: midiOutDevices){
        deviceData->buffer.clear();
    }
}

void Sequencer::clearMidiTrackBuffers()
{
    for (auto track: tracks->objects){
        track->clearLastSliceMidiBuffer();
    }
}

void Sequencer::sendMidiDeviceOutputBuffers()
{
    for (auto deviceData: midiOutDevices){
        deviceData->device->sendBlockOfMessagesNow(deviceData->buffer);
    }
}

void Sequencer::writeMidiToDevicesMidiBuffer(juce::MidiBuffer& buffer, std::vector<juce::String> midiOutDeviceNames)
{
    for (auto deviceName: midiOutDeviceNames){
        auto bufferToWrite = getMidiOutputDeviceBuffer(deviceName);
        if (bufferToWrite != nullptr){
            if (buffer.getNumEvents() > 0){
                bufferToWrite->addEvents(buffer, 0, samplesPerSlice, 0);
            }
        }
    }
}

void Sequencer::initializeHardwareDevices()
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
                    availableHardwareDeviceNames.add(shortName);
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
        const juce::String synthsMidiOut = DEFAULT_MIDI_OUT_DEVICE_NAME;
        for (int i=0; i<8; i++){
            juce::String name = "Synth " + juce::String(i + 1);
            juce::String shortName = "S" + juce::String(i + 1);
            HardwareDevice* device = new HardwareDevice(name,
                                                        shortName,
                                                        [this](juce::String deviceName){return getMidiOutputDevice(deviceName);},
                                                        [this](const juce::OSCMessage &message){sendOscMessage(message);}
                                                        );
            device->configureMidiOutput(synthsMidiOut, i + 1);
            hardwareDevices.add(device);
            availableHardwareDeviceNames.add(shortName);
            std::cout << "- " << name << std::endl;
        }
    }
}

HardwareDevice* Sequencer::getHardwareDeviceByName(juce::String name)
{
    for (auto device: hardwareDevices){
        if (device->getShortName() == name || device->getName() == name){
            return device;
        }
    }
    // If no hardware device is available with that name, simply return null pointer
    return nullptr;
}

void Sequencer::initializeTracks()
{
    // Create some tracks according to state
    tracks = std::make_unique<TrackList>(state,
                                         [this]{
                                             return juce::Range<double>{musicalContext->getPlayheadPositionInBeats(), musicalContext->getPlayheadPositionInBeats() + musicalContext->getSliceLengthInBeats()};
                                         },
                                         [this]{
                                             return getGlobalSettings();
                                         },
                                         [this]{
                                             return musicalContext.get();
                                         },
                                         [this](juce::String deviceName){
                                             return getHardwareDeviceByName(deviceName);
                                         },
                                         [this](juce::String deviceName){
                                             return getMidiOutputDeviceBuffer(deviceName);
                                         });
}

//==============================================================================
void Sequencer::prepareSequencer (int samplesPerBlockExpected, double _sampleRate)
{
    sampleRate = _sampleRate;
    samplesPerSlice = samplesPerBlockExpected; // We store samplesPerBlockExpected calling it samplesPerSlice as in our MIDI sequencer context we call our processig blocks "slices"
    
    midiInCollector.reset(_sampleRate);
    pushMidiInCollector.reset(_sampleRate);
    sineSynth.setCurrentPlaybackSampleRate (_sampleRate);
}

/** Process each audio block (in our case, we call it "slice" and only process MIDI data), ask each track to provide notes to be triggered during that slice, handle MIDI input and global playhead transport.
    @param bufferToFill                  JUCE's AudioSourceChannelInfo reference of audio buffer to fill (we don't use that as we don't genrate audio)
 
 The implementation of this method is
 struecutred as follows:
 
 1) Clear audio buffers (out app does not deal with audio...) but get buffer length (which should be same as stored samplesPerSlice)
     
 2) Check if main component has been fully initialized, if not do not proceed with getNextMIDISlice as we might be referencing some objects which have not yet been fully initialized (Tracks, HardwareDevices...)
    
 3) Clear all MIDI buffers so we can re-fill them with events corresponding to the current slice. These includes hardware device buffers, track buggers and other auxiliary buffers.
     
 4) Check if tempo or meter should be updated and, in case we're doing a count in, if count in finishes in this slice
     
 5) Update musical context bar counter
    
 6) Get MIDI messages from MIDI inputs (external MIDI controller and Push's pads/encoders) and merge them into a single stream. Send that stream to the different tracks for input monitoring. Also, keep track of the last N played notes as this will be used to quantize events at the start of a recording.

 7) Check if global playhead should be start/stopped and act accordingly

 8) Process the current slice in each track: trigger playing clips' notes and, if needed, record incoming MIDI in clip(s)
    
 9) Add generated MIDI buffers per track to the corresponding hardware device MIDI output buffer. Note that several tracks might be using the same hardware device (albeit using different MIDI channels) so at this point MIDI from several tracks might be merged in the hardware device MIDI buffers.
          
 10) Render metronome and clock MIDI messages into MIDI clock and metronome auxiliary buffers. Also render MIDI clock messages in Push's MIDI buffer, used to synchronize Push colour animations with Shepherd session tempo. Copy metronome and clock messages to the corresponding hardware device buffers according to Shepherd settings.
     
 11) Send the actual messages added to each hardware device's MIDI buffer
     
 12) Send monitored track notes to the notes MIDI output (if any selected). This is used by the Shepherd Controller to show feedback about notes being currently played.

 13) Render the generated MIDI buffers with the sine synth for debugging purposes

 14) Update playhead position if global playhead is playing
 
 
 See comments in the implementation for more details about each step.
 
*/
void Sequencer::getNextMIDISlice (const juce::AudioSourceChannelInfo& bufferToFill)
{
    // 1) -------------------------------------------------------------------------------------------------
    
    bufferToFill.clearActiveBufferRegion();
    int sliceNumSamples = bufferToFill.numSamples;
    
    // 2) -------------------------------------------------------------------------------------------------
    
    if (!sequencerInitialized){
        return;
    }
    
    // 3) -------------------------------------------------------------------------------------------------
    
    clearMidiDeviceOutputBuffers();
    clearMidiTrackBuffers();
    midiClockMessages.clear();
    midiMetronomeMessages.clear();
    pushMidiClockMessages.clear();
    incomingMidi.clear();
    incomingMidiKeys.clear();
    incomingMidiPush.clear();
    monitoringNotesMidiBuffer.clear();
    
    // 4) -------------------------------------------------------------------------------------------------
    
    // Check if tempo/meter should be updated
    if (nextBpm > 0.0){
        musicalContext->setBpm(nextBpm);
        shouldStartSendingPushMidiClockBurst = true;
        nextBpm = 0.0;
    }
    if (nextMeter > 0){
        musicalContext->setMeter(nextMeter);
        nextMeter = 0;
    }
    double sliceLengthInBeats = musicalContext->getSliceLengthInBeats();
    
    // Check if count-in finished and global's playhead "is playing" state should be toggled
    if (!musicalContext->playheadIsPlaying() && musicalContext->playheadIsDoingCountIn()){
        if (musicalContext->getMeter() >= musicalContext->getCountInPlayheadPositionInBeats() && musicalContext->getMeter() < musicalContext->getCountInPlayheadPositionInBeats() + sliceLengthInBeats){
            // Count in finishes in the current slice (getNextMIDISlice)
            // Align global playhead position with coutin buffer offset so that it starts at correct offset
            musicalContext->setPlayheadPosition(-(musicalContext->getMeter() - musicalContext->getCountInPlayheadPositionInBeats()));
            shouldToggleIsPlaying = true;
            musicalContext->setPlayheadIsDoingCountIn(false);
            musicalContext->setCountInPlayheadPosition(0.0);
        }
    }
    
    // 5) -------------------------------------------------------------------------------------------------
    
    // This must be called before musicalContext.renderMetronomeInSlice to make sure metronome "high tone" is played when bar changes
    musicalContext->updateBarsCounter(juce::Range<double>{musicalContext->getPlayheadPositionInBeats(), musicalContext->getPlayheadPositionInBeats() + sliceLengthInBeats});
    
    // 6) -------------------------------------------------------------------------------------------------
    
    // Collect messages from MIDI input (keys and push)
    midiInCollector.removeNextBlockOfMessages (incomingMidiKeys, sliceNumSamples);
    pushMidiInCollector.removeNextBlockOfMessages (incomingMidiPush, sliceNumSamples);

    // Process keys MIDI input and add it to combined incomming buffer
    for (const auto metadata : incomingMidiKeys)
    {
        auto msg = metadata.getMessage();
        if (msg.isNoteOnOrOff() && fixedVelocity > -1){
            msg.setVelocity((float)fixedVelocity/127.0f);
        }
        incomingMidi.addEvent(msg, metadata.samplePosition);
        
        if (msg.isNoteOn()){
            // Store message in the "list of last played notes" and set its timestamp to the global playhead position
            juce::MidiMessage msgToStoreInQueue = juce::MidiMessage(msg);
            if (musicalContext->playheadIsDoingCountIn()){
                msgToStoreInQueue.setTimeStamp(musicalContext->getCountInPlayheadPositionInBeats() - musicalContext->getMeter() + metadata.samplePosition/sliceNumSamples * sliceLengthInBeats);
            } else {
                msgToStoreInQueue.setTimeStamp(musicalContext->getPlayheadPositionInBeats() + metadata.samplePosition/sliceNumSamples * sliceLengthInBeats);
            }
            lastMidiNoteOnMessages.insert(lastMidiNoteOnMessages.begin(), msgToStoreInQueue);
        }
    }

    // Process push MIDI input and add it to combined incomming buffer transforming input message using MIDI message mappings if required
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
                // and use it (or discard the message). This is because push always sends the same MIDI note numbers for the pads, but we want
                // to interpret these as different notes depending on Shepherd settings like current octave, MIDi root note or even note layout.
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
                        if (musicalContext->playheadIsDoingCountIn()){
                            msgToStoreInQueue.setTimeStamp(musicalContext->getCountInPlayheadPositionInBeats() - musicalContext->getMeter() + metadata.samplePosition/sliceNumSamples * sliceLengthInBeats);
                        } else {
                            msgToStoreInQueue.setTimeStamp(musicalContext->getPlayheadPositionInBeats() + metadata.samplePosition/sliceNumSamples * sliceLengthInBeats);
                        }
                        lastMidiNoteOnMessages.insert(lastMidiNoteOnMessages.begin(), msgToStoreInQueue);
                    }
                }
            }
            
        } else if (msg.isController()){
            if (msg.getControllerNumber() >= 71 && msg.getControllerNumber() <= 78){
                // If MIDI message is a control change from one of the 8 encoders above the display, check if there's any mapping active
                // and use it (or discard the message). This is because push always sends the same MIDI CC numbers for the encoders above
                // the display, but we might want to interpret them as different CCs depending on Shepherd settings like current selected
                // page of MIDI parameters.
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
                            
                            // Store value and notify the controller about the CC change so it can show updated value information
                            device->setMidiCCParameterValue(mappedCCNumber, newValue, true);
                        }
                    }
                }
            }
        }
    }
    
    // Remove old messages from lastMidiNoteOnMessages if the capacity of the buffer is exceeded
    if (lastMidiNoteOnMessages.size() > lastMidiNoteOnMessagesToStore){
        for (int i=0; i<lastMidiNoteOnMessagesToStore-lastMidiNoteOnMessages.size(); i++){
            lastMidiNoteOnMessages.pop_back();
        }
    }
    
    // Send incoming MIDI buffer to each track so notes are forwarded to corresponding devices if input monitoring is enabled
    for (auto track: tracks->objects){
        track->processInputMonitoring(incomingMidi);
    }
    
    // 7) -------------------------------------------------------------------------------------------------
    
    if (shouldToggleIsPlaying){
        if (musicalContext->playheadIsPlaying()){
            // If global playhead is playing but it should be toggled, stop all tracks/clips and reset playhead and musical context
            for (auto track: tracks->objects){
                track->clipsRenderRemainingNoteOffsIntoMidiBuffer();
                track->stopAllPlayingClips(true, true, true);
            }
            musicalContext->setPlayheadIsPlaying(false);
            musicalContext->setPlayheadPosition(0.0);
            musicalContext->resetCounters();
            musicalContext->renderMidiStopInSlice(midiClockMessages);
        } else {
            // If global playhead is stopped but it should be toggled, set all tracks/clips to the start position and toggle to play
            // Also send MIDI start message for devices syncing to MIDI clock
            for (auto track: tracks->objects){
                track->clipsResetPlayheadPosition();
            }
            musicalContext->setPlayheadIsPlaying(true);
            musicalContext->renderMidiStartInSlice(midiClockMessages);
        }
        shouldToggleIsPlaying = false;
    }
    
    // 8) -------------------------------------------------------------------------------------------------
    
    for (auto track: tracks->objects){
        track->clipsPrepareSliceSlice();  // Pull sequences form the clip fifo
    }
    
    if (musicalContext->playheadIsPlaying()){
        for (auto track: tracks->objects){
            track->clipsProcessSlice(incomingMidi, lastMidiNoteOnMessages);
        }
    }
    
    // 9) -------------------------------------------------------------------------------------------------
    
    for (auto track: tracks->objects){
        track->writeLastSliceMidiBufferToHardwareDeviceMidiBuffer();
    }
    
    // 10) -------------------------------------------------------------------------------------------------
    
    musicalContext->renderMetronomeInSlice(midiMetronomeMessages);
    if (sendMidiClock){
        musicalContext->renderMidiClockInSlice(midiClockMessages);
    }
    
    // To sync Shepherd tempo with Push's button/pad animation tempo, a number of MIDI clock messages wrapped by a start and a stop
    // message should be sent to Push.
    if ((shouldStartSendingPushMidiClockBurst) && (musicalContext->playheadIsPlaying())){
        lastTimePushMidiClockBurstStarted = juce::Time::getCurrentTime().toMilliseconds();
        shouldStartSendingPushMidiClockBurst = false;
        musicalContext->renderMidiStartInSlice(pushMidiClockMessages);
    }
    if (lastTimePushMidiClockBurstStarted > -1.0){
        double timeNow = juce::Time::getCurrentTime().toMilliseconds();
        if (timeNow - lastTimePushMidiClockBurstStarted < PUSH_MIDI_CLOCK_BURST_DURATION_MILLISECONDS){
            pushMidiClockMessages.addEvents(midiClockMessages, 0, sliceNumSamples, 0);
        } else if (timeNow - lastTimePushMidiClockBurstStarted > PUSH_MIDI_CLOCK_BURST_DURATION_MILLISECONDS){
            musicalContext->renderMidiStopInSlice(pushMidiClockMessages);
            lastTimePushMidiClockBurstStarted = -1.0;
        }
    }
    
    // Add metronome and MIDI clock messages to the corresponding hardware device buffers according to settings
    // Also send MIDI clock message to Push
    writeMidiToDevicesMidiBuffer(midiClockMessages, sendMidiClockMidiDeviceNames);
    writeMidiToDevicesMidiBuffer(midiMetronomeMessages, sendMetronomeMidiDeviceNames);
    writeMidiToDevicesMidiBuffer(pushMidiClockMessages, std::vector<juce::String>{PUSH_MIDI_OUT_DEVICE_NAME});
    
    // 11) -------------------------------------------------------------------------------------------------
    
    sendMidiDeviceOutputBuffers();
    
    // 12) -------------------------------------------------------------------------------------------------
    
    if ((notesMonitoringMidiOutput != nullptr) && (activeUiNotesMonitoringTrack >= 0) && (activeUiNotesMonitoringTrack < tracks->objects.size())){
        auto track = tracks->objects[activeUiNotesMonitoringTrack];
        auto buffer = track->getLastSliceMidiBuffer();
        if (buffer != nullptr){
            for (auto event: *buffer){
                auto msg = event.getMessage();
                if (msg.isNoteOnOrOff() && msg.getChannel() == track->getMidiOutputChannel()){
                    monitoringNotesMidiBuffer.addEvent(msg, event.samplePosition);
                }
            }
            notesMonitoringMidiOutput->sendBlockOfMessagesNow(monitoringNotesMidiBuffer);
        }
    }
    
    // 13) -------------------------------------------------------------------------------------------------
    
    #if !RPI_BUILD
    if (renderWithInternalSynth){
        // All buffers are combined into a single buffer which is then sent to the synth
        juce::MidiBuffer combinedBuffer;
        for (auto deviceData: midiOutDevices){
            combinedBuffer.addEvents(deviceData->buffer, 0, sliceNumSamples, 0);
        }
        sineSynth.renderNextBlock (*bufferToFill.buffer, combinedBuffer, bufferToFill.startSample, sliceNumSamples);
    }
    #endif
    
    // 14) -------------------------------------------------------------------------------------------------
    
    if (musicalContext->playheadIsPlaying()){
        musicalContext->setPlayheadPosition(musicalContext->getPlayheadPositionInBeats() + sliceLengthInBeats);
    } else {
        if (musicalContext->playheadIsDoingCountIn()) {
            musicalContext->setCountInPlayheadPosition(musicalContext->getCountInPlayheadPositionInBeats() + sliceLengthInBeats);
        }
    }
}

//==============================================================================

GlobalSettingsStruct Sequencer::getGlobalSettings()
{
    GlobalSettingsStruct settings;
    settings.fixedLengthRecordingBars = fixedLengthRecordingBars;
    settings.maxScenes = MAX_NUM_SCENES;
    settings.maxTracks = MAX_NUM_TRACKS;
    settings.sampleRate = sampleRate;
    settings.samplesPerSlice = samplesPerSlice;
    settings.recordAutomationEnabled = recordAutomationEnabled;
    return settings;
}

//==============================================================================
void Sequencer::timerCallback()
{
    // Carry out actions that should be done periodically
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
    
    // Update musical context stateX members
    musicalContext->updateStateMemberVersions();
}

//==============================================================================
void Sequencer::playScene(int sceneN)
{
    jassert(sceneN < MAX_NUM_SCENES);
    for (auto track: tracks->objects){
        auto clip = track->getClipAt(sceneN);
        track->stopAllPlayingClipsExceptFor(sceneN, false, true, false);
        clip->clearStopCue();
        if (!clip->isPlaying() && !clip->isCuedToPlay()){
            clip->togglePlayStop();
        }
    }
}

void Sequencer::duplicateScene(int sceneN)
{
    // Assert we're not attempting to duplicate if the selected scene is the very last as there's no more space to accomodate new clips
    jassert(sceneN < MAX_NUM_SCENES - 1);
    
    // Make a copy of the sceneN and insert it to the current position of sceneN. This will shift position of current
    // sceneN.
    for (auto track: tracks->objects){
        track->duplicateClipAt(sceneN);
    }
}

//==============================================================================
void Sequencer::sendOscMessage (const juce::OSCMessage& message)
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

void Sequencer::oscMessageReceived (const juce::OSCMessage& message)
{
    const juce::String address = message.getAddressPattern().toString();
    
    if (address.startsWith(OSC_ADDRESS_CLIP)) {
        jassert(message.size() >= 2);
        int trackNum = message[0].getInt32();
        int clipNum = message[1].getInt32();
        if (trackNum < tracks->objects.size()){
            auto track = tracks->objects[trackNum];
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
                    clip->setNewClipLength(newLength, false);
                }
            }
        }
        
    } else if (address.startsWith(OSC_ADDRESS_TRACK)) {
        jassert(message.size() >= 1);
        int trackNum = message[0].getInt32();
        if (trackNum >= tracks->objects.size()) return;
        auto track = tracks->objects[trackNum];
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
            juce::OSCMessage returnMessage = juce::OSCMessage(OSC_ADDRESS_MIDI_CC_PARAMETER_VALUES_FOR_DEVICE);
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
            if (musicalContext->playheadIsPlaying()){
                // If it is playing, stop it
                shouldToggleIsPlaying = true;
            } else{
                // If it is not playing, check if there are record-armed clips and, if so, do count-in before playing
                bool recordCuedClipsFound = false;
                for (auto track: tracks->objects){
                    if (track->hasClipsCuedToRecord()){
                        recordCuedClipsFound = true;
                        break;
                    }
                }
                if (recordCuedClipsFound){
                    musicalContext->setPlayheadIsDoingCountIn(true);
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
            if (newMeter > 0 && !musicalContext->playheadIsDoingCountIn()){
                // Don't allow chaning meter while doing count in, this could lead to severe disaster
                nextMeter = newMeter;
            }
        }
        
    } else if (address.startsWith(OSC_ADDRESS_METRONOME)) {
        if (address == OSC_ADDRESS_METRONOME_ON){
            jassert(message.size() == 0);
            musicalContext->setMetronome(true);
        } else if (address == OSC_ADDRESS_METRONOME_OFF){
            jassert(message.size() == 0);
            musicalContext->setMetronome(false);
        } else if (address == OSC_ADDRESS_METRONOME_ON_OFF){
            jassert(message.size() == 0);
            musicalContext->toggleMetronome();
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
                juce::OSCMessage returnMessage = juce::OSCMessage(OSC_ADDRESS_MIDI_CC_PARAMETER_VALUES_FOR_DEVICE);
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
            stateAsStringParts.add((juce::String)tracks->objects.size());
            for (auto track: tracks->objects){
                stateAsStringParts.add("t");
                stateAsStringParts.add((juce::String)track->getNumberOfClips());
                stateAsStringParts.add(track->isEnabled() ? "1":"0");
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
            juce::OSCMessage returnMessage = juce::OSCMessage(OSC_ADDRESS_STATE_FROM_SHEPHERD);
            returnMessage.addString(stateAsStringParts.joinIntoString(","));
            sendOscMessage(returnMessage);
            
            #if !RPI_BUILD
            sendActionMessage(juce::String(ACTION_UPDATE_DEVUI_STATE_TRACKS) + ":" + stateAsStringParts.joinIntoString(","));
            #endif
            
        } else if (address == OSC_ADDRESS_STATE_TRANSPORT){
            jassert(message.size() == 0);
            
            juce::StringArray stateAsStringParts = {};
            stateAsStringParts.add("transport");
            stateAsStringParts.add(musicalContext->playheadIsPlaying() ? "p":"s");
            stateAsStringParts.add(juce::String(musicalContext->getBpm(), 2));
            if (musicalContext->playheadIsDoingCountIn()){
                stateAsStringParts.add(juce::String(-1 * (musicalContext->getMeter() - musicalContext->getCountInPlayheadPositionInBeats()), 3));
            } else {
                stateAsStringParts.add(juce::String(musicalContext->getPlayheadPositionInBeats(), 3));
            }
            stateAsStringParts.add(musicalContext->metronomeIsOn() ? "p":"s");
            juce::StringArray clipsPlayheadStateParts = {};
            for (int track_num=0; track_num<tracks->objects.size(); track_num++){
                auto track = tracks->objects[track_num];
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
            stateAsStringParts.add(juce::String(musicalContext->getMeter()));
            stateAsStringParts.add(juce::String(recordAutomationEnabled ? "1":"0"));
            stateAsStringParts.add(SHEPHERD_NOTES_MONITORING_MIDI_DEVICE_NAME);
            
            juce::OSCMessage returnMessage = juce::OSCMessage(OSC_ADDRESS_STATE_FROM_SHEPHERD);
            returnMessage.addString(stateAsStringParts.joinIntoString(","));
            sendOscMessage(returnMessage);
            
            #if !RPI_BUILD
            sendActionMessage(juce::String(ACTION_UPDATE_DEVUI_STATE_TRNSPORT) + ":" + stateAsStringParts.joinIntoString(","));
            #endif
            
        }
        
    } else if (address == OSC_ADDRESS_SHEPHERD_CONTROLLER_READY) {
        jassert(message.size() == 0);
        // Set midi in push connection to false so the method to initialize midi in is retrieggered at next timer call
        // This is to prevent issues caused by the order in which frontend and backend are started
        midiInPushIsConnected = false;
        
        // Also in dev mode trigger reload ui
        #if !RPI_BUILD
        juce::Time::waitForMillisecondCounter(juce::Time::getMillisecondCounter() + 2000);
        sendActionMessage(ACTION_UPDATE_DEVUI_RELOAD_BROWSER);
        #endif
    }
}

//==============================================================================

void Sequencer::valueTreePropertyChanged (juce::ValueTree& treeWhosePropertyHasChanged, const juce::Identifier& property)
{
    // We should never call this function from the realtime thread because editing VT might not be RT safe...
    // jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    std::cout << "Changed " << treeWhosePropertyHasChanged[IDs::name].toString() << " " << property.toString() << ": " << treeWhosePropertyHasChanged[property].toString() << std::endl;
}

void Sequencer::valueTreeChildAdded (juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenAdded)
{
    // We should never call this function from the realtime thread because editing VT might not be RT safe...
    // jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
}

void Sequencer::valueTreeChildRemoved (juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenRemoved, int indexFromWhichChildWasRemoved)
{
    // We should never call this function from the realtime thread because editing VT might not be RT safe...
    // jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
}

void Sequencer::valueTreeChildOrderChanged (juce::ValueTree& parentTree, int oldIndex, int newIndex)
{
    // We should never call this function from the realtime thread because editing VT might not be RT safe...
    // jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
}

void Sequencer::valueTreeParentChanged (juce::ValueTree& treeWhoseParentHasChanged)
{
    // We should never call this function from the realtime thread because editing VT might not be RT safe...
    // jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
}

//==============================================================================

void Sequencer::debugState() {
    DBG(state.toXmlString());
}

void Sequencer::randomizeClipsNotes() {
    for (auto track: tracks->objects){
        if (track->isEnabled()){
            for (int i=0; i<MAX_NUM_SCENES; i++){
                auto clip = track->getClipAt(i);
                
                // First remove all notes from clip
                for (int j=0; j<clip->state.getNumChildren(); j++){
                    auto child = clip->state.getChild(j);
                    clip->state.setProperty(IDs::enabled, false, nullptr);
                    clip->setNewClipLength(0.0, true);
                    //clip->state.setProperty(IDs::clipLengthInBeats, 0.0, nullptr);
                    if (child.hasType (IDs::SEQUENCE_EVENT)){
                        clip->state.removeChild(child, nullptr);
                    }
                    clip->stopNow();
                }
                
                // Then for 50% of the clips, add new random content
                if (juce::Random::getSystemRandom().nextInt (juce::Range<int> (0, 10)) > 5){
                    clip->state.setProperty(IDs::enabled, true, nullptr);
                    double clipLengthInBeats = (double)juce::Random::getSystemRandom().nextInt (juce::Range<int> (5, 13));
                    //clip->state.setProperty(IDs::clipLengthInBeats, clipLengthInBeats, nullptr);
                    clip->setNewClipLength(clipLengthInBeats, true);
                    std::vector<std::pair<int, float>> noteOnTimes = {};
                    for (int j=0; j<clipLengthInBeats - 0.5; j++){
                        noteOnTimes.push_back({j, juce::Random::getSystemRandom().nextFloat() * 0.5});
                    };
                    for (auto note: noteOnTimes) {
                        // NOTE: don't care about the channel here because it is re-written when filling midi buffer
                        int midiNote = juce::Random::getSystemRandom().nextInt (juce::Range<int> (64, 85));
                        juce::MidiMessage msgNoteOn = juce::MidiMessage::noteOn(1, midiNote, 1.0f);
                        msgNoteOn.setTimeStamp(note.first + note.second);
                        clip->state.addChild(Helpers::midiMessageToSequenceEventValueTree(msgNoteOn), -1, nullptr);
                        
                        juce::MidiMessage msgNoteOff = juce::MidiMessage::noteOff(1, midiNote, 0.0f);
                        msgNoteOff.setTimeStamp(note.first + note.second + 0.25);
                        clip->state.addChild(Helpers::midiMessageToSequenceEventValueTree(msgNoteOff), -1, nullptr);
                    }
                }
            }
        }
    }
}
