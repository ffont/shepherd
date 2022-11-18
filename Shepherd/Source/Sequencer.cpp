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
    
    // Pre allocate memory for MIDI buffers
    // My rough tests indicate that 1 midi message takes 9 int8 array positions in the midibuffer.data structure
    // (therefore 9 bytes?). These buffers are cleared at every slice, so some milliseconds only, no need to make them super
    // big but make them considerably big to be on the safe side.
    midiClockMessages.ensureSize(MIDI_BUFFER_MIN_BYTES);
    midiMetronomeMessages.ensureSize(MIDI_BUFFER_MIN_BYTES);
    pushMidiClockMessages.ensureSize(MIDI_BUFFER_MIN_BYTES);
    incomingMidi.ensureSize(MIDI_BUFFER_MIN_BYTES);
    incomingMidiKeys.ensureSize(MIDI_BUFFER_MIN_BYTES);
    incomingMidiPush.ensureSize(MIDI_BUFFER_MIN_BYTES);
    monitoringNotesMidiBuffer.ensureSize(MIDI_BUFFER_MIN_BYTES);
    internalSynthCombinedBuffer.ensureSize(MIDI_BUFFER_MIN_BYTES);
    
    // Pre-allocate memory for lastMidiNoteOnMessages array
    lastMidiNoteOnMessages.ensureStorageAllocated(MIDI_BUFFER_MIN_BYTES);
    
    // Init hardware devices
    initializeHardwareDevices();
    
    // Init MIDI
    initializeMIDIInputs();
    initializeMIDIOutputs();  // Better to do it after hardware devices so we init devices needed in hardware devices as well
    notesMonitoringMidiOutput = juce::MidiOutput::createNewDevice(SHEPHERD_NOTES_MONITORING_MIDI_DEVICE_NAME);
    
    // Init OSC and WebSockets
#if ENABLE_SYNC_STATE_WITH_OSC
    initializeOSC();
#endif
#if ENABLE_SYNC_STATE_WITH_WS
    initializeWS();
#endif
    
    // Init sine synth with 16 voices (used for testig purposes only)
    #if !RPI_BUILD
    for (auto i = 0; i < nSynthVoices; ++i)
        sineSynth.addVoice (new SineWaveVoice());
    sineSynth.addSound (new SineWaveSound());
    #endif
    
    // Load empty session to state
    DBG("Creating default session state");
    #if !RPI_BUILD
    //int numEnabledTracks = juce::Random::getSystemRandom().nextInt (juce::Range<int> (2, MAX_NUM_TRACKS - 1));
    int numEnabledTracks = 8;
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
    sendMessageToController(juce::OSCMessage(ACTION_ADDRESS_STARTED_MESSAGE));  // For new state synchroniser
    sequencerInitialized = true;
    
    #if !RPI_BUILD
    // Randomly create clips so that we have testing material
    randomizeClipsNotes();
    #endif
}

Sequencer::~Sequencer()
{
    if (wsServer.serverPtr != nullptr){
        wsServer.serverPtr->stop();
    }
    wsServer.stopThread(5000);  // Give it enough time to stop the websockets server...
}

void Sequencer::bindState()
{
    state.addListener(this);
    
    name.referTo(state, IDs::name, nullptr, Defaults::emptyString);
    fixedLengthRecordingBars.referTo(state, IDs::fixedLengthRecordingBars, nullptr, Defaults::fixedLengthRecordingBars);
    recordAutomationEnabled.referTo(state, IDs::recordAutomationEnabled, nullptr, Defaults::recordAutomationEnabled);
    fixedVelocity.referTo(state, IDs::fixedVelocity, nullptr, Defaults::fixedVelocity);
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

juce::String Sequencer::serliaizeOSCMessage(const juce::OSCMessage& message)
{
    juce::String actionName = message.getAddressPattern().toString();
    juce::StringArray actionParameters = {};
    for (int i=0; i<message.size(); i++){
        if (message[i].isString()){
            actionParameters.add(message[i].getString());
        } else if (message[i].isInt32()){
            actionParameters.add((juce::String)message[i].getInt32());
        } else if (message[i].isFloat32()){
            actionParameters.add((juce::String)message[i].getFloat32());
        }
    }
    juce::String serializedParameters = actionParameters.joinIntoString(SERIALIZATION_SEPARATOR);
    juce::String actionMessage = actionName + ":" + serializedParameters;
    return actionMessage;
}

void Sequencer::sendWSMessage(const juce::OSCMessage& message) {
    if (wsServer.serverPtr == nullptr){
        // If ws server is not yet running, don't try to send any message
        return;
    }
    // Takes a OSC message object and serializes in a way that can be sent to WebSockets conencted clients
    for(auto &a_connection : wsServer.serverPtr->get_connections()){
        juce::String serializedMessage = serliaizeOSCMessage(message);
        a_connection->send(serializedMessage.toStdString());
    }
}

void Sequencer::sendOscMessage (const juce::OSCMessage& message)
{
    if (!oscSenderIsConnected){
        if (oscSender.connect (oscSendHost, oscSendPort)){
            oscSenderIsConnected = true;
        }
    }
    if (oscSenderIsConnected){
        bool success = oscSender.send (message);
    }
}

void Sequencer::sendMessageToController(const juce::OSCMessage& message) {
#if ENABLE_SYNC_STATE_WITH_OSC
    sendOscMessage(message);
#endif
    
#if ENABLE_SYNC_STATE_WITH_WS
    sendWSMessage(message);
#endif
}

void Sequencer::oscMessageReceived (const juce::OSCMessage& message)
{
    juce::String action = message.getAddressPattern().toString();
    juce::StringArray actionParameters = {};
    for (int i=0; i<message.size(); i++){
        if (message[i].isString()){
            actionParameters.add(message[i].getString());
        } else if (message[i].isInt32()){
            actionParameters.add((juce::String)message[i].getInt32());
        } else if (message[i].isFloat32()){
            actionParameters.add((juce::String)message[i].getFloat32());
        }
    }
    processMessageFromController(action, actionParameters);
}

void Sequencer::wsMessageReceived (const juce::String& serializedMessage)
{
    juce::String action = serializedMessage.substring(0, serializedMessage.indexOf(":"));
    juce::String serializedParameters = serializedMessage.substring(serializedMessage.indexOf(":") + 1);
    juce::StringArray actionParameters;
    actionParameters.addTokens (serializedParameters, (juce::String)SERIALIZATION_SEPARATOR, "");
    processMessageFromController(action, actionParameters);
}

void Sequencer::initializeWS() {
    wsServer.setSequencerPointer(this);
    wsServer.startThread(0);
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
    
    lastTimeMidiInputInitializationAttempted = juce::Time::getMillisecondCounter();
    
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
    
    lastTimeMidiOutputInitializationAttempted = juce::Time::getMillisecondCounter();
    
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
    deviceData->buffer.ensureSize(MIDI_BUFFER_MIN_BYTES);
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
                                                                [this](const juce::OSCMessage &message){sendMessageToController(message);},
                                                                [this](juce::String deviceName){ return getMidiOutputDeviceBuffer(deviceName);}
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
                                                        [this](const juce::OSCMessage &message){sendMessageToController(message);},
                                                        [this](juce::String deviceName){ return getMidiOutputDeviceBuffer(deviceName);}
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

Track* Sequencer::getTrackWithUUID(juce::String trackUUID)
{
    return tracks->getObjectWithUUID(trackUUID);
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
    
 3) Clear all MIDI buffers so we can re-fill them with events corresponding to the current slice. These includes hardware device buffers, track buffers and other auxiliary buffers. Clearing the buffers does not free their pre-allocated memory, so this is fine in the RT thread.
     
 4) Check if tempo or meter should be updated and, in case we're doing a count in, check if count in finishes in this slice
     
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
            lastMidiNoteOnMessages.insert(0, msgToStoreInQueue);
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
                        lastMidiNoteOnMessages.insert(0, msgToStoreInQueue);
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
    
    // Remove old messages from lastMidiNoteOnMessages if we are already storing lastMidiNoteOnMessagesToStore
    if (lastMidiNoteOnMessages.size() > lastMidiNoteOnMessagesToStore){
        lastMidiNoteOnMessages.removeLast(lastMidiNoteOnMessages.size() - lastMidiNoteOnMessagesToStore);
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
    
    for (auto device: hardwareDevices){
        // Send "arbitrary" messages pending to be sent in every hardware device
        // NOTE: iterating hardwareDevices could be problematic without a lock if devices are
        // added/removed. However this not something that will be happening as hw devices should
        // not be created or removed...
        device->renderPendingMidiMessagesToRenderInBuffer();
    }
    
    // 10) -------------------------------------------------------------------------------------------------
    
    musicalContext->renderMetronomeInSlice(midiMetronomeMessages);
    if (sendMidiClock){
        musicalContext->renderMidiClockInSlice(midiClockMessages);
    }
    
    // To sync Shepherd tempo with Push's button/pad animation tempo, a number of MIDI clock messages wrapped by a start and a stop
    // message should be sent to Push.
    if ((shouldStartSendingPushMidiClockBurst) && (musicalContext->playheadIsPlaying())){
        lastTimePushMidiClockBurstStarted = juce::Time::getMillisecondCounter();
        shouldStartSendingPushMidiClockBurst = false;
        musicalContext->renderMidiStartInSlice(pushMidiClockMessages);
    }
    if (lastTimePushMidiClockBurstStarted > -1.0){
        double timeNow = juce::Time::getMillisecondCounter();
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
    writeMidiToDevicesMidiBuffer(pushMidiClockMessages, sendPushMidiClockDeviceNames);
    
    // 11) -------------------------------------------------------------------------------------------------
    
    sendMidiDeviceOutputBuffers();
    
    // 12) -------------------------------------------------------------------------------------------------
    if ((notesMonitoringMidiOutput != nullptr) && (activeUiNotesMonitoringTrack != "")){
        auto track = getTrackWithUUID(activeUiNotesMonitoringTrack);
        if (track != nullptr){
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
    }
    
    // 13) -------------------------------------------------------------------------------------------------
    
    #if !RPI_BUILD
    if (renderWithInternalSynth){
        // All buffers are combined into a single buffer which is then sent to the synth
        internalSynthCombinedBuffer.clear();
        for (auto deviceData: midiOutDevices){
            internalSynthCombinedBuffer.addEvents(deviceData->buffer, 0, sliceNumSamples, 0);
        }
        sineSynth.renderNextBlock (*bufferToFill.buffer, internalSynthCombinedBuffer, bufferToFill.startSample, sliceNumSamples);
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
        if (juce::Time::getMillisecondCounter() - lastTimeMidiInputInitializationAttempted > 2000){
            // If at least one of the MIDI devices is not properly connected and 2 seconds have passed since last
            // time we tried to initialize them, try to initialize again
            initializeMIDIInputs();
        }
    }
    
    if (shouldTryInitializeMidiOutputs){
        if (juce::Time::getMillisecondCounter() - lastTimeMidiOutputInitializationAttempted > 2000){
            // If at least one of the MIDI devices is not properly connected and 2 seconds have passed since last
            // time we tried to initialize them, try to initialize again
            initializeMIDIOutputs();
        }
    }
    
    // Update musical context stateX members
    musicalContext->updateStateMemberVersions();
    
#if ENABLE_SYNC_STATE_WITH_OSC
    // If syncing the state wia OSC, we send "/alive" messages as these are used to determine if the app is up and running
    if ((juce::Time::getMillisecondCounterHiRes() - lastTimeIsAliveWasSent) > 1000.0){
        // Every second send "alive" message
        juce::OSCMessage message = juce::OSCMessage(ACTION_ADDRESS_ALIVE_MESSAGE);
        sendOscMessage(message);
        lastTimeIsAliveWasSent = juce::Time::getMillisecondCounterHiRes();
    }
#endif
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

void Sequencer::processMessageFromController (const juce::String action, juce::StringArray parameters)
{
    if (action == ACTION_ADDRESS_GENERIC) {
        juce::var parsedJson = juce::JSON::parse(parameters[0]);  // parameters[0] is expected to be a JSON object
        juce::String action = parsedJson["action"].toString();
        juce::var parameters = parsedJson["parameters"];
        
        if (action == ACTION_ADDRESS_CLIP_SET_SEQUENCE) {
            juce::String trackUUID = parameters["trackUUID"].toString();
            juce::String clipUUID = parameters["clipUUID"].toString();
            auto* track = getTrackWithUUID(trackUUID);
            if (track != nullptr){
                auto* clip = track->getClipWithUUID(clipUUID);
                if (clip != nullptr){
                    // Remove all existing notes from sequence and set length to new clip length
                    clip->clearClipSequence();
                    clip->setClipLength((double)parameters["clipLength"]);
                    
                    // Add new sequence events to the sequence
                    juce::Array<juce::var>* sequenceEvents = parameters["sequenceEvents"].getArray();
                    for (juce::var eventData: *sequenceEvents){
                        if ((int)eventData["type"] == SequenceEventType::note){
                            double timestamp = (double)eventData["timestamp"];
                            int midiNote = (int)eventData["midiNote"];
                            float midiVelocity = (float)eventData["midiVelocity"];
                            double duration = (double)eventData["duration"];
                            clip->state.addChild(Helpers::createSequenceEventOfTypeNote(timestamp, midiNote, midiVelocity, duration), -1, nullptr);
                        }
                    }
                }
            }
        }
        
    } else if (action.startsWith(ACTION_ADDRESS_CLIP)) {
        jassert(parameters.size() >= 2);
        juce::String trackUUID = parameters[0];
        juce::String clipUUID = parameters[1];
        auto* track = getTrackWithUUID(trackUUID);
        if (track != nullptr){
            auto* clip = track->getClipWithUUID(clipUUID);
            if (clip != nullptr){
                if (action == ACTION_ADDRESS_CLIP_PLAY){
                    if (!clip->isPlaying()){
                        track->stopAllPlayingClipsExceptFor(clipUUID, false, true, false);
                        clip->togglePlayStop();
                    }
                } else if (action == ACTION_ADDRESS_CLIP_STOP){
                    if (clip->isPlaying()){
                        clip->togglePlayStop();
                    }
                } else if (action == ACTION_ADDRESS_CLIP_PLAY_STOP){
                    if (!clip->isPlaying()){
                        track->stopAllPlayingClipsExceptFor(clipUUID, false, true, false);
                    }
                    clip->togglePlayStop();
                } else if (action == ACTION_ADDRESS_CLIP_RECORD_ON_OFF){
                    if (!clip->isPlaying()){
                        track->stopAllPlayingClipsExceptFor(clipUUID, false, true, false);
                    }
                    clip->toggleRecord();
                } else if (action == ACTION_ADDRESS_CLIP_CLEAR){
                    clip->clearClip();
                } else if (action == ACTION_ADDRESS_CLIP_DOUBLE){
                    clip->doubleSequence();
                } else if (action == ACTION_ADDRESS_CLIP_UNDO){
                    clip->undo();
                } else if (action == ACTION_ADDRESS_CLIP_QUANTIZE){
                    jassert(parameters.size() == 3);
                    double quantizationStep = (double)parameters[2].getFloatValue();
                    clip->quantizeSequence(quantizationStep);
                } else if (action == ACTION_ADDRESS_CLIP_SET_LENGTH){
                    jassert(parameters.size() == 3);
                    double newLength = (double)parameters[2].getFloatValue();
                    clip->setClipLength(newLength);
                }
            }
        }
        
    } else if (action.startsWith(ACTION_ADDRESS_TRACK)) {
        jassert(parameters.size() >= 1);
        juce::String trackUUID = parameters[0];
        auto* track = getTrackWithUUID(trackUUID);
        if (track != nullptr){
            if (action == ACTION_ADDRESS_TRACK_SET_INPUT_MONITORING){
                jassert(parameters.size() == 2);
                bool trueFalse = parameters[1].getIntValue() == 1;
                track->setInputMonitoring(trueFalse);
            } else if (action == ACTION_ADDRESS_TRACK_SET_ACTIVE_UI_NOTES_MONITORING_TRACK){
                activeUiNotesMonitoringTrack = trackUUID;
            }
        }
               
    } else if (action.startsWith(ACTION_ADDRESS_DEVICE)) {
        jassert(parameters.size() >= 1);
        juce::String deviceName = parameters[0];
        auto device = getHardwareDeviceByName(deviceName);
        if (device == nullptr) return;
        if (action == ACTION_ADDRESS_DEVICE_SEND_ALL_NOTES_OFF_TO_DEVICE){
             device->sendAllNotesOff();
        } else if (action == ACTION_ADDRESS_DEVICE_LOAD_DEVICE_PRESET){
            jassert(parameters.size() == 3);
            int bank = parameters[1].getIntValue();
            int preset = parameters[2].getIntValue();
            device->loadPreset(bank, preset);
        } else if (action == ACTION_ADDRESS_DEVICE_SEND_MIDI){
            jassert(parameters.size() == 4);
            juce::MidiMessage msg = juce::MidiMessage(parameters[1].getIntValue(), parameters[2].getIntValue(), parameters[3].getIntValue());
            device->sendMidi(msg);
        } else if (action ==  ACTION_ADDRESS_DEVICE_SET_MIDI_CC_PARAMETERS){
            jassert(parameters.size() > 1);
            for (int i=1; i<parameters.size(); i=i+2){
                int index = parameters[i].getIntValue();
                int value = parameters[i + 1].getIntValue();
                device->setMidiCCParameterValue(index, value, false);
                // Don't notify controller about the cc value change as the change is most probably comming from the controller itself
            }
        } else if (action ==  ACTION_ADDRESS_DEVICE_GET_MIDI_CC_PARAMETERS){
            jassert(parameters.size() > 1);
            juce::OSCMessage returnMessage = juce::OSCMessage(ACTION_ADDRESS_MIDI_CC_PARAMETER_VALUES_FOR_DEVICE);
            returnMessage.addString(device->getShortName());
            for (int i=1; i<parameters.size(); i++){
                int index = parameters[i].getIntValue();
                int value = device->getMidiCCParameterValue(index);
                returnMessage.addInt32(index);
                returnMessage.addInt32(value);
            }
            sendMessageToController(returnMessage);
        }
    
    } else if (action.startsWith(ACTION_ADDRESS_SCENE)) {
        jassert(parameters.size() == 1);
        int sceneNum = parameters[0].getIntValue();
        if (action == ACTION_ADDRESS_SCENE_PLAY){
            playScene(sceneNum);
        } else if (action == ACTION_ADDRESS_SCENE_DUPLICATE){
            duplicateScene(sceneNum);
        }
         
    } else if (action.startsWith(ACTION_ADDRESS_TRANSPORT)) {
        if (action == ACTION_ADDRESS_TRANSPORT_PLAY_STOP){
            jassert(parameters.size() == 0);
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
        } else if (action == ACTION_ADDRESS_TRANSPORT_SET_BPM){
            jassert(parameters.size() == 1);
            float newBpm = parameters[0].getFloatValue();
            if (newBpm > 0.0 && newBpm < 400.0){
                nextBpm = (double)newBpm;
            }
        } else if (action == ACTION_ADDRESS_TRANSPORT_SET_METER){
            jassert(parameters.size() == 1);
            int newMeter = parameters[0].getIntValue();
            if (newMeter > 0 && !musicalContext->playheadIsDoingCountIn()){
                // Don't allow chaning meter while doing count in, this could lead to severe disaster
                nextMeter = newMeter;
            }
        }
        
    } else if (action.startsWith(ACTION_ADDRESS_METRONOME)) {
        if (action == ACTION_ADDRESS_METRONOME_ON){
            jassert(parameters.size() == 0);
            musicalContext->setMetronome(true);
        } else if (action == ACTION_ADDRESS_METRONOME_OFF){
            jassert(parameters.size() == 0);
            musicalContext->setMetronome(false);
        } else if (action == ACTION_ADDRESS_METRONOME_ON_OFF){
            jassert(parameters.size() == 0);
            musicalContext->toggleMetronome();
        }
        
    } else if (action.startsWith(ACTION_ADDRESS_SETTINGS)) {
        if (action == ACTION_ADDRESS_SETTINGS_PUSH_NOTES_MAPPING){
            jassert(parameters.size() == 64);
            for (int i=0; i<64; i++){
                pushPadsNoteMapping[i] = parameters[i].getIntValue();
            }
        } else if (action == ACTION_ADDRESS_SETTINGS_PUSH_ENCODERS_MAPPING){
            jassert(parameters.size() == 9);
            juce::String deviceName = parameters[0];
            pushEncodersCCMappingHardwareDeviceShortName = deviceName;
            for (int i=1; i<9; i++){
                pushEncodersCCMapping[i - 1] = parameters[i].getIntValue();
            }
            
            // Send the currently stored values for these controls to the controller
            // The Controller needs these values to display current cc parameter values on the display
            auto device = getHardwareDeviceByName(deviceName);
            if (device != nullptr){
                juce::OSCMessage returnMessage = juce::OSCMessage(ACTION_ADDRESS_MIDI_CC_PARAMETER_VALUES_FOR_DEVICE);
                returnMessage.addString(device->getShortName());
                for (int i=0; i<8; i++){
                    int index = pushEncodersCCMapping[i];
                    if (index > -1){
                        int value = device->getMidiCCParameterValue(index);
                        returnMessage.addInt32(index);
                        returnMessage.addInt32(value);
                    }
                }
                sendMessageToController(returnMessage);
            }

        } else if (action == ACTION_ADDRESS_SETTINGS_FIXED_VELOCITY){
            jassert(parameters.size() == 1);
            fixedVelocity = parameters[0].getIntValue();
        } else if (action == ACTION_ADDRESS_SETTINGS_FIXED_LENGTH){
            jassert(parameters.size() == 1);
            fixedLengthRecordingBars = parameters[0].getIntValue();
            
            // If there are empty clips cued to record and playhead is stopped, also update their length
            for (int track_num=0; track_num<tracks->objects.size(); track_num++){
                auto track = tracks->objects[track_num];
                for (int clip_num=0; clip_num<track->getNumberOfClips(); clip_num++){
                    auto clip = track->getClipAt(clip_num);
                    if (!clip->hasSequenceEvents() && clip->isCuedToStartRecording() && !clip->isRecording() && !clip->isPlaying()){
                        clip->setClipLengthToGlobalFixedLength();
                    }
                }
            }
            
            
        } else if (action == ACTION_ADDRESS_TRANSPORT_RECORD_AUTOMATION){
            jassert(parameters.size() == 0);
            recordAutomationEnabled = !recordAutomationEnabled;
        }
        
    } else if (action == ACTION_ADDRESS_GET_STATE) {
        jassert(parameters.size() == 1);
        juce::String stateType = parameters[0];
        if (stateType == "full"){
            juce::OSCMessage returnMessage = juce::OSCMessage(ACTION_ADDRESS_FULL_STATE);
            returnMessage.addInt32(stateUpdateID);
            returnMessage.addString(state.toXmlString(juce::XmlElement::TextFormat().singleLine()));
            // Note that sending full state does not work well with OSC as the state is too big, so if
            // OSC communication is to be used, this here we should somehow split the state in several
            // messages or find a solution
            sendMessageToController(returnMessage);
        }
    } else if (action == ACTION_ADDRESS_SHEPHERD_CONTROLLER_READY) {
        jassert(parameters.size() == 0);
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
    
    // Send state update to UI
    juce::OSCMessage message = juce::OSCMessage(ACTION_ADDRESS_STATE_UPDATE);
    message.addString("propertyChanged");
    message.addInt32(stateUpdateID);
    message.addString(treeWhosePropertyHasChanged[IDs::uuid].toString());
    message.addString(treeWhosePropertyHasChanged.getType().toString());
    message.addString(property.toString());
    message.addString(treeWhosePropertyHasChanged[property].toString());
    sendMessageToController(message);
    stateUpdateID += 1;
}

void Sequencer::valueTreeChildAdded (juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenAdded)
{
    // We should never call this function from the realtime thread because editing VT might not be RT safe...
    // jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    
    // Send state update to UI
    juce::OSCMessage message = juce::OSCMessage(ACTION_ADDRESS_STATE_UPDATE);
    message.addString("addedChild");
    message.addInt32(stateUpdateID);
    message.addString(parentTree[IDs::uuid].toString());
    message.addString(parentTree.getType().toString());
    message.addInt32(parentTree.indexOf(childWhichHasBeenAdded));
    message.addString(childWhichHasBeenAdded.toXmlString(juce::XmlElement::TextFormat().singleLine()));
    sendMessageToController(message);
    stateUpdateID += 1;
}

void Sequencer::valueTreeChildRemoved (juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenRemoved, int indexFromWhichChildWasRemoved)
{
    // We should never call this function from the realtime thread because editing VT might not be RT safe...
    // jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    
    // Send state update to UI
    juce::OSCMessage message = juce::OSCMessage(ACTION_ADDRESS_STATE_UPDATE);
    message.addString("removedChild");
    message.addInt32(stateUpdateID);
    message.addString(childWhichHasBeenRemoved[IDs::uuid].toString());
    message.addString(childWhichHasBeenRemoved.getType().toString());
    sendMessageToController(message);
    stateUpdateID += 1;
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
                    if (child.hasType (IDs::SEQUENCE_EVENT)){
                        clip->state.removeChild(child, nullptr);
                    }
                    clip->setClipLength(0.0); // This will trigger stopping the clip
                }
                
                // Then for 50% of the clips, add new random content
                if (juce::Random::getSystemRandom().nextInt (juce::Range<int> (0, 10)) > 5){
                    double clipLengthInBeats = (double)juce::Random::getSystemRandom().nextInt (juce::Range<int> (5, 13));
                    clip->setClipLength(clipLengthInBeats);
                    std::vector<std::pair<int, float>> noteOnTimes = {};
                    for (int j=0; j<clipLengthInBeats - 0.5; j++){
                        noteOnTimes.push_back({j, juce::Random::getSystemRandom().nextFloat() * 0.5});
                    };
                    for (auto note: noteOnTimes) {
                        // NOTE: don't care about the channel here because it is re-written when filling midi buffer
                        int midiNote = juce::Random::getSystemRandom().nextInt (juce::Range<int> (64, 85));
                        float midiVelocity = 1.0f;
                        double timestamp = note.first + note.second;
                        double duration = juce::Random::getSystemRandom().nextFloat() * 1.5;
                        clip->state.addChild(Helpers::createSequenceEventOfTypeNote(timestamp, midiNote, midiVelocity, duration), -1, nullptr);
                    }
                }
            }
        }
    }
}
