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
    // Initialize state as root
    state = ShepherdHelpers::createDefaultStateRoot();
    
    // Make sure data location exists
    juce::File location = getDataLocation();
    if (!location.exists()){
        location.createDirectory();
    }
    
    // Start timer for recurring tasks
    startTimer (50);
    
    // Pre allocate memory for MIDI buffers
    // My rough tests indicate that 1 midi message takes 9 int8 array positions in the midibuffer.data structure
    // (therefore 9 bytes?). These buffers are cleared at every slice, so some milliseconds only, no need to make them super
    // big but make them considerably big to be on the safe side.
    midiClockMessages.ensureSize(MIDI_BUFFER_MIN_BYTES);
    midiMetronomeMessages.ensureSize(MIDI_BUFFER_MIN_BYTES);
    pushMidiClockMessages.ensureSize(MIDI_BUFFER_MIN_BYTES);
    monitoringNotesMidiBuffer.ensureSize(MIDI_BUFFER_MIN_BYTES);

    // Init hardware devices
    initializeHardwareDevices();

    // Load some settings from file
    sendMidiClockMidiDeviceNames = getListStringPropertyFromSettingsFile("midiDevicesToSendClockTo");
    sendMetronomeMidiDeviceName = getStringPropertyFromSettingsFile("metronomeMidiDevice");
    
    // Init MIDI
    // Better to do it after hardware devices so we init devices needed in hardware devices as well
    initializeMIDIInputs();
    initializeMIDIOutputs();
    notesMonitoringMidiOutput = juce::MidiOutput::createNewDevice(SHEPHERD_NOTES_MONITORING_MIDI_DEVICE_NAME);
    
    // Init OSC and WebSockets
#if ENABLE_SYNC_STATE_WITH_OSC
    initializeOSC();
#endif
#if ENABLE_SYNC_STATE_WITH_WS
    initializeWS();
#endif
    
    // Load first preset (if no presets saved, it will create an empty session)
    loadSessionFromFile("0");

    sequencerInitialized = true;
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
    
    juce::ValueTree sessionState = state.getChildWithName(ShepherdIDs::SESSION);
    name.referTo(sessionState, ShepherdIDs::name, nullptr, ShepherdDefaults::emptyString);
    fixedLengthRecordingBars.referTo(sessionState, ShepherdIDs::fixedLengthRecordingBars, nullptr, ShepherdDefaults::fixedLengthRecordingBars);
    recordAutomationEnabled.referTo(sessionState, ShepherdIDs::recordAutomationEnabled, nullptr, ShepherdDefaults::recordAutomationEnabled);
    fixedVelocity.referTo(state, ShepherdIDs::fixedVelocity, nullptr, ShepherdDefaults::fixedVelocity);
    
    state.setProperty(ShepherdIDs::dataLocation, getDataLocation().getFullPathName(), nullptr);
    renderWithInternalSynth.referTo(state, ShepherdIDs::renderWithInternalSynth, nullptr, ShepherdDefaults::renderWithInternalSynth);
}

juce::File Sequencer::getDataLocation() {
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Shepherd/");
}

void Sequencer::saveCurrentSessionToFile(juce::String filePath)
{
    juce::File outputFile;
    if (juce::File::isAbsolutePath(filePath)){
        // File path is an absolute path to a file where to save the session
        outputFile = juce::File(filePath);
    } else {
        // File path is the name of the file only, save it in the default location
        outputFile = getDataLocation().getChildFile(filePath).withFileExtension("xml");
    }
    
    // Get the part of the state that corresponds to the session, remove things from it like playhead positions,
    // play/recording state and other things which are "voaltile"
    juce::ValueTree savedState = state.getChildWithName(ShepherdIDs::SESSION).createCopy();
    savedState.setProperty (ShepherdIDs::playheadPositionInBeats, ShepherdDefaults::playheadPosition, nullptr);
    savedState.setProperty (ShepherdIDs::playing, ShepherdDefaults::playing, nullptr);
    savedState.setProperty (ShepherdIDs::doingCountIn, ShepherdDefaults::doingCountIn, nullptr);
    savedState.setProperty (ShepherdIDs::countInPlayheadPositionInBeats, ShepherdDefaults::playheadPosition, nullptr);
    savedState.setProperty (ShepherdIDs::barCount, ShepherdDefaults::barCount, nullptr);
    for (auto t: savedState) {
        if (t.hasType(ShepherdIDs::TRACK)) {
            for (auto c: t) {
                if (c.hasType(ShepherdIDs::CLIP)) {
                    c.setProperty (ShepherdIDs::recording, ShepherdDefaults::recording, nullptr);
                    c.setProperty (ShepherdIDs::willStartRecordingAt, ShepherdDefaults::willStartRecordingAt, nullptr);
                    c.setProperty (ShepherdIDs::willStopRecordingAt, ShepherdDefaults::willStopRecordingAt, nullptr);
                    c.setProperty (ShepherdIDs::playing, ShepherdDefaults::playing, nullptr);
                    c.setProperty (ShepherdIDs::willPlayAt, ShepherdDefaults::willPlayAt, nullptr);
                    c.setProperty (ShepherdIDs::willStopAt, ShepherdDefaults::willStopAt, nullptr);
                    c.setProperty (ShepherdIDs::playheadPositionInBeats, ShepherdDefaults::playheadPosition, nullptr);
                    for (auto se: c) {
                        if (se.hasType(ShepherdIDs::SEQUENCE_EVENT)) {
                            // Do nothing...
                        }
                    }
                }
            }
        }
    }
    
    // Update version
    savedState.setProperty (ShepherdIDs::version, ProjectInfo::versionString , nullptr);
    
    if (auto xml = std::unique_ptr<juce::XmlElement> (savedState.createXml())) {
        xml->writeTo(outputFile);
    }
}

bool Sequencer::validateAndUpdateStateToLoad(juce::ValueTree& stateToCheck)
{
    // Makes sure that the state VT can be loaded by doing some checks. Also makes changes to it
    // which could be necessary for bakcwards compatiblity (e.g., adding missing properties which were
    // not available in older versions of shepherd, etc
    
    // Check root element type
    if (!stateToCheck.hasType(ShepherdIDs::SESSION)){
        DBG("Root element is not of type SESSION");
        DBG(stateToCheck.toXmlString());
        return false;
    }
    
    // Make sure structure has correct child types
    std::vector<int> numClipsPerTrack = {};
    for (int i=0; i<stateToCheck.getNumChildren(); i++){
        auto firstLevelChild = stateToCheck.getChild(i);
        if (!firstLevelChild.hasType(ShepherdIDs::TRACK)){
            DBG("Session element contains child elements of type other than TRACK");
            return false;
        }
        
        if (firstLevelChild.hasType(ShepherdIDs::TRACK)){
            int nClips = 0;
            for (int j=0; j<firstLevelChild.getNumChildren(); j++){
                auto secondLevelChild = firstLevelChild.getChild(j);
                if (!secondLevelChild.hasType(ShepherdIDs::CLIP)){
                    DBG("Track element contains child elements of type other than CLIP");
                    return false;
                }
                
                if (secondLevelChild.hasType(ShepherdIDs::CLIP)){
                    nClips += 1;
                    for (int k=0; k<secondLevelChild.getNumChildren(); k++){
                        auto thirdLevelChild = secondLevelChild.getChild(k);
                        if (!thirdLevelChild.hasType(ShepherdIDs::SEQUENCE_EVENT)){
                            DBG("Clip element contains child elements of type other than SEQUENCE_EVENT");
                            return false;
                        }
                    }
                }
            }
            numClipsPerTrack.push_back(nClips);
        }
    }
    
    // Check that numClipsPerTrack is a vector in which all values are the same number
    for (int i=0; i<numClipsPerTrack.size()-1; i++){
        if (numClipsPerTrack[i] != numClipsPerTrack[i+1]){
            DBG("Inconsistent number of clips per track");
            return false;
        }
    }
    
    return true;
}

void Sequencer::loadSession(juce::ValueTree& stateToLoad)
{
    // Make some checks about the state which is about to be loaded, and if all is fine, proceed loading state
    if (validateAndUpdateStateToLoad(stateToLoad)){
        
        // If sequencer is initialized remove event listeners, etc
        if (sequencerInitialized){
            // If sequencer is playing, first stop playback (give some sleep time so
            // the RT thread has time to be executed and clips set to stop (with note offs sent)
            if (musicalContext->playheadIsPlaying()){ shouldToggleIsPlaying = true; }
            juce::Time::waitForMillisecondCounter(juce::Time::getMillisecondCounter() + 50);

            // Remove current state VT listener
            state.removeListener(this);
        }
        
        // Remove current session state and assign new one
        if (state.getChildWithName(ShepherdIDs::SESSION).isValid()){
            state.removeChild(state.getChildWithName(ShepherdIDs::SESSION), nullptr);
        }
        state.addChild(stateToLoad, 0, nullptr);
        
        // Add state change listener and bind cached properties to state properties
        bindState();
        
        // Initialize musical context
        musicalContext = std::make_unique<MusicalContext>([this]{return getGlobalSettings();}, state.getChildWithName(ShepherdIDs::SESSION));
        const int metronomeMidiChannel = getIntPropertyFromSettingsFile("metronomeMidiChannel");
        if (metronomeMidiChannel != -1){
            musicalContext->setMetronomeMidiChannel(metronomeMidiChannel);
        }
        
        // Initialize tracks
        tracks = std::make_unique<TrackList>(state.getChildWithName(ShepherdIDs::SESSION),
                                             [this]{
                                                 return juce::Range<double>{musicalContext->getPlayheadPositionInBeats(), musicalContext->getPlayheadPositionInBeats() + musicalContext->getSliceLengthInBeats()};
                                             },
                                             [this]{
                                                 return getGlobalSettings();
                                             },
                                             [this]{
                                                 return musicalContext.get();
                                             },
                                             [this](juce::String deviceName, HardwareDeviceType type){
                                                 return getHardwareDeviceByName(deviceName, type);
                                             },
                                             [this](juce::String deviceName){
                                                 return getMidiOutputDeviceData(deviceName);
                                             });
        
        // Send OSC message to frontend indiating that Shepherd is ready to rock
        sendMessageToController(juce::OSCMessage(ACTION_ADDRESS_STARTED_MESSAGE));  // For new state synchroniser
    } else {
        DBG("ERROR: Could not load session data as it is incompatible or it has inconsistencies...");
        loadNewEmptySession(DEFAULT_NUM_TRACKS, DEFAULT_NUM_SCENES);
    }
}

void Sequencer::loadNewEmptySession(int numTracks, int numScenes)
{
    DBG("Loading new empty state with " << numTracks << " tracks and " << numScenes << " scenes");
    juce::ValueTree stateToLoad = ShepherdHelpers::createDefaultSession(hardwareDevices->getAvailableOutputHardwareDeviceNames(), numTracks, numScenes);
    loadSession(stateToLoad);
}

void Sequencer::loadSessionFromFile(juce::String filePath)
{
    bool stateLoadedFromFileSuccessfully = false;
    juce::File sessionFile;
    if (juce::File::isAbsolutePath(filePath)){
        // File path is an absolute path to a file where to save the session
        sessionFile = juce::File(filePath);
    } else {
        // File path is the name of the file only, save it in the default location
        sessionFile = getDataLocation().getChildFile(filePath).withFileExtension("xml");
    }
    juce::ValueTree stateToLoad;
    if (sessionFile.existsAsFile()){
        if (auto xml = std::unique_ptr<juce::XmlElement> (juce::XmlDocument::parse (sessionFile))){
            DBG("Loading session from: " << sessionFile.getFullPathName());
            juce::ValueTree loadedState = juce::ValueTree::fromXml (*xml);  // Load new state into VT
            stateToLoad = loadedState; // Then set the new state
            stateLoadedFromFileSuccessfully = true;
        }
    }
    loadSession(stateToLoad);
}

juce::String Sequencer::getStringPropertyFromSettingsFile(juce::String propertyName)
{
    juce::String returnValue = "";
    juce::File backendSettingsLocation = getDataLocation().getChildFile("backendSettings").withFileExtension("json");
    if (backendSettingsLocation.existsAsFile()){
        juce::var parsedJson;
        auto result = juce::JSON::parse(backendSettingsLocation.loadFileAsString(), parsedJson);
        if (result.wasOk())
        {
            if (parsedJson.isObject()){
                returnValue = parsedJson.getProperty(propertyName, "").toString();
            }
        }
    }
    return returnValue;
}

int Sequencer::getIntPropertyFromSettingsFile(juce::String propertyName)
{
    int returnValue = -1;
    juce::File backendSettingsLocation = getDataLocation().getChildFile("backendSettings").withFileExtension("json");
    if (backendSettingsLocation.existsAsFile()){
        juce::var parsedJson;
        auto result = juce::JSON::parse(backendSettingsLocation.loadFileAsString(), parsedJson);
        if (result.wasOk())
        {
            if (parsedJson.isObject()){
                returnValue = (int)parsedJson.getProperty(propertyName, "");
            }
        }
    }
    return returnValue;
}

std::vector<juce::String> Sequencer::getListStringPropertyFromSettingsFile(juce::String propertyName)
{
    std::vector<juce::String> returnValue = {};
    juce::File backendSettingsLocation = getDataLocation().getChildFile("backendSettings").withFileExtension("json");
    if (backendSettingsLocation.existsAsFile()){
        juce::var parsedJson;
        auto result = juce::JSON::parse(backendSettingsLocation.loadFileAsString(), parsedJson);
        if (result.wasOk())
        {
            if (parsedJson.isObject()){
                juce::var rawElement = parsedJson.getProperty(propertyName, juce::var());
                if (rawElement.isArray()){
                
                    juce::Array<juce::var>* deviceNames = rawElement.getArray();
                    for (juce::var element: *deviceNames){
                        returnValue.push_back(element.toString());
                    }
                }
            }
        }
    }
    return returnValue;
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

bool Sequencer::midiOutputDeviceAlreadyInitialized(const juce::String& deviceName)
{
    for (auto deviceData: midiOutDevices){
        if (deviceData != nullptr && deviceData->name == deviceName){
            // If device already initialized, early return
            return true;
        }
    }
    return false;
}

bool Sequencer::midiInputDeviceAlreadyInitialized(const juce::String& deviceName)
{
    for (auto deviceData: midiInDevices){
        if (deviceData != nullptr && deviceData->name == deviceName){
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
    
    bool someFailedInitialization = false;
    
    // Initialize all MIDI devices required by available hardware devices (also re-initialize those that have been already initialized)
    for (auto hwDevice: hardwareDevices->objects){
        if (hwDevice->isTypeInput()){
            if (!midiInputDeviceAlreadyInitialized(hwDevice->getMidiInputDeviceName())){
                // If device has not been initialized, initialize it and add it to midiInDevices
                auto midiDeviceData = initializeMidiInputDevice(hwDevice->getMidiInputDeviceName());
                if (midiDeviceData == nullptr) {
                    DBG("Failed to initialize input MIDI device for hardware device: " << hwDevice->getMidiInputDeviceName());
                    someFailedInitialization = true;
                } else {
                    midiInDevices.add(midiDeviceData);
                }
            } else {
                // If device is already initialized, then re-initialize it and replace existing one
                auto initializedMidiDevice = getMidiInputDeviceData(hwDevice->getMidiInputDeviceName());
                
                for (int i=0; i<midiInDevices.size(); i++){
                    if (midiInDevices[i] != nullptr) {
                        if (midiInDevices[i]->identifier == initializedMidiDevice->identifier){
                            midiInDevices[i]->device->stop();
                            auto reinitializedMidiDeviceData = initializeMidiInputDevice(hwDevice->getMidiInputDeviceName());
                            midiInDevices.set(i, reinitializedMidiDeviceData);
                            break;
                        }
                    }                    
                }
            }
        }
    }
    
    // Remove elements from midiInDevices that could be remaining null pointers of previous sessions
    for (int i=midiInDevices.size() - 1; i>0; i--){
        if (midiInDevices[i] == nullptr){
            midiInDevices.remove(i);
        }
    }
    
    for (auto device: midiInDevices){
        std::cout << "- " << device->device->getName() << std::endl;
    }
    
    if (!someFailedInitialization) shouldTryInitializeMidiInputs = false;
}

void Sequencer::initializeMIDIOutputs()
{
    JUCE_ASSERT_MESSAGE_THREAD
    
    std::cout << "Initializing MIDI output devices" << std::endl;
    
    lastTimeMidiOutputInitializationAttempted = juce::Time::getMillisecondCounter();
    
    bool someFailedInitialization = false;
    
    // Initialize all MIDI devices required by available hardware devices
    for (auto hwDevice: hardwareDevices->objects){
        if (hwDevice->isTypeOutput()){
            if (!midiOutputDeviceAlreadyInitialized(hwDevice->getMidiOutputDeviceName())){
                auto midiDeviceData = initializeMidiOutputDevice(hwDevice->getMidiOutputDeviceName());
                if (midiDeviceData == nullptr) {
                    DBG("Failed to initialize output MIDI device for hardware device: " << hwDevice->getMidiOutputDeviceName());
                    someFailedInitialization = true;
                } else {
                    midiOutDevices.add(midiDeviceData);
                }
            }
        }
    }
    
    // Initialize midi output devices used for clock
    for (auto midiDeviceName: sendMidiClockMidiDeviceNames){
        if (!midiOutputDeviceAlreadyInitialized(midiDeviceName)){
            auto midiDeviceData = initializeMidiOutputDevice(midiDeviceName);
            if (midiDeviceData == nullptr) {
                DBG("Failed to initialize midi device for clock: " << midiDeviceName);
                someFailedInitialization = true;
            } else {
                midiOutDevices.add(midiDeviceData);
            }
        }
    }
    
    // Init metronome decice
    if (!midiOutputDeviceAlreadyInitialized(sendMetronomeMidiDeviceName)){
        auto midiDeviceData = initializeMidiOutputDevice(sendMetronomeMidiDeviceName);
        if (midiDeviceData == nullptr) {
            DBG("Failed to initialize midi device for metronome: " << sendMetronomeMidiDeviceName);
            someFailedInitialization = true;
        } else {
            midiOutDevices.add(midiDeviceData);
        }
    }
    
    // Initialize Push midi output (used for sending clock messages to push and sync animations with Shepherd tempo)
    juce::String pushMidiOutDeviceName = getStringPropertyFromSettingsFile("pushClockDeviceName");
    if (pushMidiOutDeviceName != ""){
        sendPushLikeMidiClockBursts = true;  // Enable sending of push-like midi clock bursts to device indicated
        sendPushMidiClockDeviceNames = {pushMidiOutDeviceName};
        if (!midiOutputDeviceAlreadyInitialized(pushMidiOutDeviceName)){
            auto pushMidiDevice = initializeMidiOutputDevice(pushMidiOutDeviceName);
            if (pushMidiDevice == nullptr) {
                DBG("Failed to initialize push midi device: " << pushMidiOutDeviceName);
                someFailedInitialization = true;
            } else {
                midiOutDevices.add(pushMidiDevice);
            }
        }
    }
    
    // Remove elements from midiOutDevices that could be remaining null pointers of previous sessions
    for (int i=midiOutDevices.size() - 1; i>0; i--){
        if (midiOutDevices[i] == nullptr){
            midiOutDevices.remove(i);
        }
    }
    
    for (auto device: midiOutDevices){
        std::cout << "- " << device->device->getName() << std::endl;
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

MidiOutputDeviceData* Sequencer::getMidiOutputDeviceData(juce::String deviceName)
{
    for (auto deviceData: midiOutDevices){
        if (deviceData != nullptr && deviceData->name == deviceName){
            return deviceData;
        }
    }
    // If function did not yet return, it means the requested output device has not yet been initialized
    // Set a flag so the device gets initialized in the message thread and return null pointer.
    // NOTE: we could check if we're in the message thread and, if this is the case, initialize the device
    // instead of setting a flag, but this optimization is probably not necessary.
    shouldTryInitializeMidiOutputs = true;
    return nullptr;
}

MidiInputDeviceData* Sequencer::initializeMidiInputDevice(juce::String deviceName)
{
    JUCE_ASSERT_MESSAGE_THREAD
    
    auto midiInputs = juce::MidiInput::getAvailableDevices();
    juce::String inDeviceIdentifier = "";
    for (int i=0; i<midiInputs.size(); i++){
        if (midiInputs[i].name == deviceName){
            inDeviceIdentifier = midiInputs[i].identifier;
        }
    }
    
    MidiInputDeviceData* deviceData = new MidiInputDeviceData();
    deviceData->buffer.ensureSize(MIDI_BUFFER_MIN_BYTES);
    deviceData->collector.ensureStorageAllocated(MIDI_BUFFER_MIN_BYTES);
    deviceData->identifier = inDeviceIdentifier;
    deviceData->name = deviceName;
    deviceData->device = juce::MidiInput::openDevice(inDeviceIdentifier, &deviceData->collector);
    if (sampleRate > 0){
        // If sample rate has already been set (this is a midi in device being initialized of late), then use if to reset collectpr
        deviceData->collector.reset(sampleRate);
    }
    
    if (deviceData->device != nullptr){
        deviceData->device->start();
        return deviceData;
    } else {
        delete deviceData; // Delete created MidiInputDeviceData to avoid memory leaks with created buffer
        std::cout << "- ERROR " << deviceName << ". Available MIDI IN devices: ";
        for (int i=0; i<midiInputs.size(); i++){
            std::cout << midiInputs[i].name << ((i != (midiInputs.size() - 1)) ? ", ": "");
        }
        std::cout << std::endl;
        return nullptr;
    }
}

MidiInputDeviceData* Sequencer::getMidiInputDeviceData(juce::String deviceName)
{
    for (auto deviceData: midiInDevices){
        if (deviceData != nullptr && deviceData->name == deviceName){
            return deviceData;
        }
    }
    // If function did not yet return, it means the requested input device has not yet been initialized
    // Set a flag so the device gets initialized in the message thread and return null pointer.
    // NOTE: we could check if we're in the message thread and, if this is the case, initialize the device
    // instead of setting a flag, but this optimization is probably not necessary.
    shouldTryInitializeMidiInputs = true;
    return nullptr;
}

void Sequencer::collectorsRetrieveLatestBlockOfMessages(int sliceNumSamples)
{
    for (auto deviceData: midiInDevices){
        if (deviceData != nullptr){
            deviceData->collector.removeNextBlockOfMessages (deviceData->buffer, sliceNumSamples);
        }
    }
}

void Sequencer::resetMidiInCollectors(double sampleRate)
{
    for (auto deviceData: midiInDevices){
        if (deviceData != nullptr){
            deviceData->collector.reset(sampleRate);
        }
    }
}

void Sequencer::clearMidiDeviceInputBuffers()
{
    for (auto deviceData: midiInDevices){
        if (deviceData != nullptr){
            deviceData->buffer.clear();
        }
    }
}


void Sequencer::clearMidiDeviceOutputBuffers()
{
    for (auto deviceData: midiOutDevices){
        if (deviceData != nullptr){
            deviceData->buffer.clear();
        }
    }
}

void Sequencer::clearMidiTrackBuffers()
{
    for (auto track: tracks->objects){
        track->clearMidiBuffers();
    }
}

void Sequencer::sendMidiDeviceOutputBuffers()
{
    for (auto deviceData: midiOutDevices){
        if (deviceData != nullptr){
            deviceData->device->sendBlockOfMessagesNow(deviceData->buffer);
        }
    }
}

void Sequencer::writeMidiToDevicesMidiBuffer(juce::MidiBuffer& buffer, std::vector<juce::String> midiOutDeviceNames)
{
    for (auto deviceName: midiOutDeviceNames){
        auto deviceData = getMidiOutputDeviceData(deviceName);
        if (deviceData != nullptr){
            auto bufferToWrite = &deviceData->buffer;
            if (bufferToWrite != nullptr){
                if (buffer.getNumEvents() > 0){
                    bufferToWrite->addEvents(buffer, 0, samplesPerSlice, 0);
                }
            }
        }
    }
}

void Sequencer::initializeHardwareDevices()
{
    juce::ValueTree hardwareDevicesState (ShepherdIDs::HARDWARE_DEVICES);
    
    // Initialize INPUT and OUTPUT hardware devices from the definitions file
    juce::File hardwareDeviceDefinitionsLocation = getDataLocation().getChildFile("hardwareDevices").withFileExtension("json");
    if (hardwareDeviceDefinitionsLocation.existsAsFile()){
        std::cout << "Initializing Hardware Devices from JSON file" << std::endl;
        juce::var parsedJson;
        auto result = juce::JSON::parse(hardwareDeviceDefinitionsLocation.loadFileAsString(), parsedJson);
        if (!result.wasOk())
        {
            std::cout << "Error parsing JSON: " + result.getErrorMessage() << std::endl;
        } else {
            // At the top level, the JSON file should be an array
            if (!parsedJson.isArray()){
                std::cout << "Devices configuration file has wrong contents or can't be read. Are permissions granted to access the file?" << std::endl;
            } else {
                for (int i=0; i<parsedJson.size(); i++){
                    // Each element in the array should be an object element with the properties needed to create the hardware device
                    juce::var deviceInfo = parsedJson[i];
                    if (!parsedJson.isObject()){
                        std::cout << "Devices configuration file has wrong contents or can't be read." << std::endl;
                        continue;
                    }
                    juce::String type = deviceInfo.getProperty("type", "output").toString();
                    juce::String name = deviceInfo.getProperty("name", "NoName").toString();
                    juce::String shortName = deviceInfo.getProperty("shortName", name).toString();
                    if (type == "output"){
                        juce::String midiOutDeviceName = deviceInfo.getProperty("midiOutputDeviceName", "NoMIDIOutDevice").toString();
                        int midiChannel = (int)deviceInfo.getProperty("midiChannel", "NoMIDIOutDevice");
                        hardwareDevicesState.addChild(ShepherdHelpers::createOutputHardwareDevice(name,
                                                                                          shortName,
                                                                                          midiOutDeviceName,
                                                                                          midiChannel), -1, nullptr);
                    } else if (type == "input"){
                        juce::String midiInDeviceName = deviceInfo.getProperty("midiInputDeviceName", "NoMIDIInDevice").toString();
                        bool controlChangeMessagesAreRelative = (bool)deviceInfo.getProperty("controlChangeMessagesAreRelative", ShepherdDefaults::controlChangeMessagesAreRelative);
                        int allowedMidiInputChannel = (int)deviceInfo.getProperty("allowedMidiInputChannel", ShepherdDefaults::allowedMidiInputChannel);
                        bool allowNoteMessages = (bool)deviceInfo.getProperty("allowNoteMessages", ShepherdDefaults::allowNoteMessages);
                        bool allowControllerMessages = (bool)deviceInfo.getProperty("allowControllerMessages", ShepherdDefaults::allowControllerMessages);
                        bool allowPitchBendMessages = (bool)deviceInfo.getProperty("allowPitchBendMessages", ShepherdDefaults::allowPitchBendMessages);
                        bool allowAftertouchMessages = (bool)deviceInfo.getProperty("allowAftertouchMessages", ShepherdDefaults::allowAftertouchMessages);
                        bool allowChannelPressureMessages = (bool)deviceInfo.getProperty("allowChannelPressureMessages", ShepherdDefaults::allowChannelPressureMessages);
                        juce::String notesMapping = deviceInfo.getProperty("notesMapping", "").toString(); // Empty string will be standard 1-128 mapping
                        juce::String controlChangeMapping = deviceInfo.getProperty("controlChangeMapping", "").toString(); // Empty string will be standard 1-128 mapping
                        hardwareDevicesState.addChild(ShepherdHelpers::createInputHardwareDevice(name,
                                                                                         shortName,
                                                                                         midiInDeviceName,
                                                                                         controlChangeMessagesAreRelative,
                                                                                         allowedMidiInputChannel,
                                                                                         allowNoteMessages,
                                                                                         allowControllerMessages,
                                                                                         allowPitchBendMessages,
                                                                                         allowAftertouchMessages,
                                                                                         allowChannelPressureMessages,
                                                                                         notesMapping,
                                                                                         controlChangeMapping), -1, nullptr);
                    }
                    
                    
                }
            }
        }
    } else {
        std::cout << "No hardware devices configuration file found at " << hardwareDeviceDefinitionsLocation.getFullPathName() << std::endl;
        std::cout << "There will be no MIDI going in and out if there are no hardware devices defined :) " << std::endl;
    }
    
    /*
    // Then initialize extra default OUTPUT hardware devices, one per available output midi port and midi channel
    std::cout << "Initializing default Output Hardware Devices" << std::endl;
    juce::Array<juce::MidiDeviceInfo> availableMidiOutDevices = juce::MidiOutput::getAvailableDevices();
    for (auto midiOutputDevice: availableMidiOutDevices) {
        for (int i=0; i<16; i++) {
            juce::String name = midiOutputDevice.name + " ch " + juce::String(i + 1);
            juce::String shortName = midiOutputDevice.name.substring(0, 10) + " ch" + juce::String(i + 1);
            hardwareDevicesState.addChild(ShepherdHelpers::createOutputHardwareDevice(name, shortName, midiOutputDevice.name, i + 1), -1, nullptr);
        }
    }*/
    
    // Now do create the actual HardwareDevice objects
    if (state.getChildWithName(ShepherdIDs::HARDWARE_DEVICES).isValid()){
        // Remove existing child (if any?)
        state.removeChild(state.getChildWithName(ShepherdIDs::HARDWARE_DEVICES), nullptr);
    }
    state.addChild(hardwareDevicesState, -1, nullptr);
    hardwareDevices = std::make_unique<HardwareDeviceList>(state.getChildWithName(ShepherdIDs::HARDWARE_DEVICES),
                                                           [this](juce::String deviceName){return getMidiOutputDeviceData(deviceName);},
                                                           [this](juce::String deviceName){return getMidiInputDeviceData(deviceName);}
                                                           );
    std::cout << "Output Hardware Devices initialized:" << std::endl;
    for (auto deviceName: hardwareDevices->getAvailableOutputHardwareDeviceNames()){
        std::cout << "- " << deviceName << std::endl;
    }
    /*std::cout << "Input Hardware Devices initialized:" << std::endl;
    for (auto deviceName: hardwareDevices->getAvailableInputHardwareDeviceNames()){
        std::cout << "- " << deviceName << std::endl;
    }*/
}

HardwareDevice* Sequencer::getHardwareDeviceByName(juce::String name, HardwareDeviceType type)
{
    for (auto device: hardwareDevices->objects){
        if ((device->getShortName() == name || device->getName() == name) && (type == device->getType())){
            return device;
        }
    }
    // If no hardware device is available with that name and for that type, simply return null pointer
    return nullptr;
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
    resetMidiInCollectors (_sampleRate);
}

/** Process each audio block (in our case, we call it "slice" and only process MIDI data), ask each track to provide notes to be triggered during that slice, handle MIDI input and global playhead transport.
    @param bufferToFill                  JUCE's AudioSourceChannelInfo reference of audio buffer to fill (we don't use that as we don't genrate audio)
 
 The implementation of this method is
 struecutred as follows:
 
 1) Check if main component has been fully initialized, if not do not proceed with getNextMIDISlice as we might be referencing some objects which have not yet been fully initialized (Tracks, HardwareDevices...)
    
 2) Clear all MIDI buffers so we can re-fill them with events corresponding to the current slice. These includes hardware device buffers, track buffers and other auxiliary buffers. Clearing the buffers does not free their pre-allocated memory, so this is fine in the RT thread.
     
 3) Check if tempo or meter should be updated and, in case we're doing a count in, check if count in finishes in this slice
     
 4) Update musical context bar counter
    
 5) Get MIDI messages from MIDI inputs (external MIDI controller and Push's pads/encoders) and merge them into a single stream. Send that stream to the different tracks for input monitoring. Also, keep track of the last N played notes as this will be used to quantize events at the start of a recording.

 6) Check if global playhead should be start/stopped and act accordingly

 7) Process the current slice in each track: trigger playing clips' notes and, if needed, record incoming MIDI in clip(s)
    
 8) Add generated MIDI buffers per track to the corresponding hardware device MIDI output buffer. Note that several tracks might be using the same hardware device (albeit using different MIDI channels) so at this point MIDI from several tracks might be merged in the hardware device MIDI buffers.
          
 9) Render metronome and clock MIDI messages into MIDI clock and metronome auxiliary buffers. Also render MIDI clock messages in Push's MIDI buffer, used to synchronize Push colour animations with Shepherd session tempo. Copy metronome and clock messages to the corresponding hardware device buffers according to Shepherd settings.
     
 10) Send the actual messages added to each hardware device's MIDI buffer
     
 11) Send monitored track notes to the notes MIDI output (if any selected). This is used by the Shepherd Controller to show feedback about notes being currently played.

 12) Update playhead position if global playhead is playing
 
 See comments in the implementation for more details about each step.
 
*/
void Sequencer::getNextMIDISlice (int sliceNumSamples)
{
    // 1) -------------------------------------------------------------------------------------------------
    if (!sequencerInitialized){
        return;
    }
    
    // 2) -------------------------------------------------------------------------------------------------
    
    clearMidiDeviceInputBuffers();
    clearMidiDeviceOutputBuffers();
    clearMidiTrackBuffers();
    midiClockMessages.clear();
    midiMetronomeMessages.clear();
    pushMidiClockMessages.clear();
    monitoringNotesMidiBuffer.clear();
    
    // 3) -------------------------------------------------------------------------------------------------
    
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
    
    // 4) -------------------------------------------------------------------------------------------------
    
    // This must be called before musicalContext.renderMetronomeInSlice to make sure metronome "high tone" is played when bar changes
    musicalContext->updateBarsCounter(juce::Range<double>{musicalContext->getPlayheadPositionInBeats(), musicalContext->getPlayheadPositionInBeats() + sliceLengthInBeats});
    
    // 5) -------------------------------------------------------------------------------------------------
    
    // Collect messages from different MIDI inputs and put them into a single buffer
    collectorsRetrieveLatestBlockOfMessages(sliceNumSamples);
    
    for (auto inputDevice: hardwareDevices->objects){
        // NOTE: iterating hardwareDevices could be problematic without a lock if devices are
        // added/removed. However this not something that will be happening as hw devices should
        // not be created or removed...

        if (inputDevice->isTypeInput() && inputDevice->isMidiInitialized()){
            auto inputDeviceData = getMidiInputDeviceData(inputDevice->getMidiInputDeviceName());
            if (inputDeviceData == nullptr) { continue; }
            juce::MidiBuffer& deviceLastBlockOfMessages = inputDeviceData->buffer;
            
            // Apply fixed velocity filter
            for (const auto metadata: deviceLastBlockOfMessages){
                auto msg = metadata.getMessage();
                if (msg.isNoteOnOrOff() && fixedVelocity > -1){
                    msg.setVelocity((float)fixedVelocity/127.0f);
                }
            }
            
            // Iterate through all tracks and pass them the current input device to see if they want to do anything with it (if they have input monitoring enabled)
            // and if they need to process it (e.g. update control change values from relative controllers or change midi notes). The processed messages will be stored
            // in track's incomingMidiBuffer, and this will later be used by clips being played from that track
            for (auto track: tracks->objects){
                track->processInputMessagesFromInputHardwareDevice(inputDevice,
                                                                   musicalContext->getSliceLengthInBeats(),
                                                                   sliceNumSamples,
                                                                   musicalContext->getCountInPlayheadPositionInBeats(),
                                                                   musicalContext->getPlayheadPositionInBeats(),
                                                                   musicalContext->getMeter(),
                                                                   musicalContext->playheadIsDoingCountIn());
            }
        }
    }
    
    // 6) -------------------------------------------------------------------------------------------------
    
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
    
    // 7) -------------------------------------------------------------------------------------------------
    
    for (auto track: tracks->objects){
        track->clipsPrepareSlice();  // Pull sequences form the clip fifo
    }
    
    if (musicalContext->playheadIsPlaying()){
        for (auto track: tracks->objects){
            track->clipsProcessSlice();  // No need to pass buffers here because Clip objects will retrieve them from its parent track object
        }
    }
    
    // 8) -------------------------------------------------------------------------------------------------
    
    for (auto track: tracks->objects){
        track->writeLastSliceMidiBufferToHardwareDeviceMidiBuffer();
    }
    
    for (auto outputDevice: hardwareDevices->objects){
        // Send "arbitrary" messages pending to be sent in every hardware device
        // NOTE: iterating hardwareDevices could be problematic without a lock if devices are
        // added/removed. However this not something that will be happening as hw devices should
        // not be created or removed...
        if (outputDevice->isTypeOutput() && outputDevice->isMidiInitialized()){
            outputDevice->renderPendingMidiMessagesToRenderInBuffer();
        }
    }
    
    // 9) -------------------------------------------------------------------------------------------------
    
    musicalContext->renderMetronomeInSlice(midiMetronomeMessages);
    if (sendMidiClock){
        musicalContext->renderMidiClockInSlice(midiClockMessages);
    }
    
    if (sendPushLikeMidiClockBursts){
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
    }
    
    // Add metronome and MIDI clock messages to the corresponding hardware device buffers according to settings
    // Also send MIDI clock message to Push
    writeMidiToDevicesMidiBuffer(midiClockMessages, sendMidiClockMidiDeviceNames);
    if (sendMetronomeMidiDeviceName != ""){
        writeMidiToDevicesMidiBuffer(midiMetronomeMessages, {sendMetronomeMidiDeviceName});
    }
    if (sendPushLikeMidiClockBursts){
        writeMidiToDevicesMidiBuffer(pushMidiClockMessages, sendPushMidiClockDeviceNames);
    }
    
    
    // 10) -------------------------------------------------------------------------------------------------
    
    sendMidiDeviceOutputBuffers();
    
    // 11) -------------------------------------------------------------------------------------------------
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
    
    // 12) -------------------------------------------------------------------------------------------------
    
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
    settings.sampleRate = sampleRate;
    settings.samplesPerSlice = samplesPerSlice;
    settings.recordAutomationEnabled = recordAutomationEnabled;
    return settings;
}

//==============================================================================
void Sequencer::timerCallback()
{
    if (shouldTryInitializeMidiOutputs){
        if (juce::Time::getMillisecondCounter() - lastTimeMidiOutputInitializationAttempted > 2000){
            // If at least one of the MIDI devices is not properly connected and 2 seconds have passed since last
            // time we tried to initialize them, try to initialize again
            initializeMIDIOutputs();
        }
    }
    
    if (shouldTryInitializeMidiInputs){
        if (juce::Time::getMillisecondCounter() - lastTimeMidiInputInitializationAttempted > 2000){
            // If at least one of the MIDI devices is not properly connected and 2 seconds have passed since last
            // time we tried to initialize them, try to initialize again
            initializeMIDIInputs();
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
    if ((tracks->objects.size() > 0)  && (sceneN < tracks->objects[0]->getNumberOfClips())){
        for (auto track: tracks->objects){
            auto clip = track->getClipAt(sceneN);
            track->stopAllPlayingClipsExceptFor(sceneN, false, true, false);
            clip->clearStopCue();
            if (!clip->isPlaying() && !clip->isCuedToPlay()){
                clip->togglePlayStop();
            }
        }
    }
}

void Sequencer::duplicateScene(int sceneN)
{
    // Make sure we're not attempting to duplicate if the selected scene is the very last as there's no more space to accomodate new clips
    if ((tracks->objects.size() > 0)  && (sceneN < tracks->objects[0]->getNumberOfClips() - 1)){
        // Make a copy of the sceneN and insert it to the current position of sceneN. This will shift position of current
        // sceneN.
        for (auto track: tracks->objects){
            track->duplicateClipAt(sceneN);
        }
    }
}

//==============================================================================

void Sequencer::processMessageFromController (const juce::String action, juce::StringArray parameters)
{
    if (action.startsWith(ACTION_ADDRESS_CLIP)) {
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
                } else if (action == ACTION_ADDRESS_CLIP_SET_BPM_MULTIPLIER){
                    jassert(parameters.size() == 3);
                    double newBpmMultiplier = (double)parameters[2].getFloatValue();
                    clip->setBpmMultiplier(newBpmMultiplier);
                } else if (action == ACTION_ADDRESS_CLIP_SET_SEQUENCE) {
                    // New sequence data is passed in JSON format, eg:
                    /*{
                       "clipLength": 6,
                       "sequenceEvents": [
                         {"type": 1, "midiNote": 79, "midiVelocity": 1.0, "timestamp": 0.29, "duration": 0.65},  // type 1 = note event
                         {"type": 1, "midiNote": 73, "midiVelocity": 1.0, "timestamp": 2.99, "duration": 1.42},
                         {"type": 0, "eventMidiBytes": "73,21,56", "timestamp": 2.99},  // type 0 = generic midi message
                         ...
                       ]
                    }*/
                    juce::var sequenceData = juce::JSON::parse(parameters[2]);
                    // Remove all existing notes from sequence and set length to new clip length
                    clip->clearClipSequence();
                    clip->setClipLength((double)sequenceData["clipLength"]);
                    // Add new sequence events to the sequence
                    juce::Array<juce::var>* sequenceEvents = sequenceData["sequenceEvents"].getArray();
                    for (juce::var eventData: *sequenceEvents){
                        if ((int)eventData["type"] == SequenceEventType::note){
                            double timestamp = (double)eventData["timestamp"];
                            int midiNote = (int)eventData["midiNote"];
                            float midiVelocity = (float)eventData["midiVelocity"];
                            double duration = (double)eventData["duration"];
                            clip->state.addChild(ShepherdHelpers::createSequenceEventOfTypeNote(timestamp, midiNote, midiVelocity, duration), -1, nullptr);
                        } else if ((int)eventData["type"] == SequenceEventType::midi){
                            double timestamp = (double)eventData["timestamp"];
                            juce::String eventMidiBytes = eventData["eventMidiBytes"].toString();
                            clip->state.addChild(ShepherdHelpers::createSequenceEventFromMidiBytesString(timestamp, eventMidiBytes), -1, nullptr);
                        }
                    }
                } else if (action == ACTION_ADDRESS_CLIP_EDIT_SEQUENCE) {
                    // New sequence data is passed in JSON format, eg:
                    /*{
                       "action": "removeEvent" | "editEvent" | "addEvent",  // One of these three options
                       "eventUUID":  "356cbbdjgf...", // Used by "removeEvent" and "editEvent" only
                       "eventData": {
                            "type": 1,
                            "midiNote": 79,
                            "midiVelocity": 1.0,
                            ... // All the event properties that should be updated or "added" (in case of a new event)
                        }
                    }*/
                    juce::var editSequenceData = juce::JSON::parse(parameters[2]);
                    juce::String editAction = editSequenceData["action"].toString();
                    if (editAction == "removeEvent"){
                        clip->removeSequenceEventWithUUID(editSequenceData["eventUUID"]);
                    } else if (editAction == "editEvent"){
                        juce::ValueTree sequenceEvent = clip->getSequenceEventWithUUID(editSequenceData["eventUUID"]);
                        if ((int)sequenceEvent.getProperty(ShepherdIDs::type) == SequenceEventType::note){
                            juce::var eventData = editSequenceData["eventData"];
                            if (eventData.hasProperty("midiNote")) {
                                sequenceEvent.setProperty(ShepherdIDs::midiNote, (int)eventData["midiNote"], nullptr);
                            }
                            if (eventData.hasProperty("midiVelocity")) {
                                sequenceEvent.setProperty(ShepherdIDs::midiVelocity, (float)eventData["midiVelocity"], nullptr);
                            }
                            if (eventData.hasProperty("chance")) {
                                sequenceEvent.setProperty(ShepherdIDs::chance, (float)eventData["chance"], nullptr);
                            }
                            if (eventData.hasProperty("timestamp")) {
                                sequenceEvent.setProperty(ShepherdIDs::timestamp, (double)eventData["timestamp"], nullptr);
                            }
                            if (eventData.hasProperty("utime")) {
                                sequenceEvent.setProperty(ShepherdIDs::uTime, (double)eventData["utime"], nullptr);
                            }
                            if (eventData.hasProperty("duration")) {
                                sequenceEvent.setProperty(ShepherdIDs::duration, (double)eventData["duration"], nullptr);
                            }
                        } else if ((int)sequenceEvent.getProperty(ShepherdIDs::type) == SequenceEventType::midi){
                            juce::var eventData = editSequenceData["eventData"];
                            if (eventData.hasProperty("timestamp")) {
                                sequenceEvent.setProperty(ShepherdIDs::timestamp, (double)eventData["timestamp"], nullptr);
                            }
                            if (eventData.hasProperty("utime")) {
                                sequenceEvent.setProperty(ShepherdIDs::uTime, (double)eventData["utime"], nullptr);
                            }
                            if (eventData.hasProperty("eventMidiBytes")) {
                                sequenceEvent.setProperty(ShepherdIDs::eventMidiBytes, (float)eventData["eventMidiBytes"], nullptr);
                            }
                        }
                    } else if (editAction == "addEvent") {
                        // Create new sequence event
                        juce::var eventData = editSequenceData["eventData"];
                        if ((int)eventData["type"] == SequenceEventType::note){
                            double timestamp = (double)eventData["timestamp"];
                            int midiNote = (int)eventData["midiNote"];
                            float midiVelocity = (float)eventData["midiVelocity"];
                            double duration = (double)eventData["duration"];
                            double utime = (double)eventData["utime"];
                            float chance = (float)eventData["chance"];
                            clip->state.addChild(ShepherdHelpers::createSequenceEventOfTypeNote(timestamp, midiNote, midiVelocity, duration, utime, chance), -1, nullptr);
                        } else if ((int)eventData["type"] == SequenceEventType::midi){
                            double timestamp = (double)eventData["timestamp"];
                            juce::String eventMidiBytes = eventData["eventMidiBytes"].toString();
                            double utime = (double)eventData["utime"];
                            clip->state.addChild(ShepherdHelpers::createSequenceEventFromMidiBytesString(timestamp, eventMidiBytes, utime), -1, nullptr);
                        }
                    }
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
            } else if (action == ACTION_ADDRESS_TRACK_SET_HARDWARE_DEVICE){
                jassert(parameters.size() == 2);
                juce::String deviceName = parameters[1];
                track->setOutputHardwareDeviceByName(deviceName); // if device can't be found, it won't set it 
            }
        }
               
    } else if (action.startsWith(ACTION_ADDRESS_DEVICE)) {
        jassert(parameters.size() >= 1);
        juce::String deviceName = parameters[0];
        if (action == ACTION_ADDRESS_DEVICE_SEND_ALL_NOTES_OFF_TO_DEVICE){
            auto device = getHardwareDeviceByName(deviceName, HardwareDeviceType::output);
            if (device == nullptr) return;
            device->sendAllNotesOff();
        } else if (action == ACTION_ADDRESS_DEVICE_LOAD_DEVICE_PRESET){
            jassert(parameters.size() == 3);
            auto device = getHardwareDeviceByName(deviceName, HardwareDeviceType::output);
            if (device == nullptr) return;
            int bank = parameters[1].getIntValue();
            int preset = parameters[2].getIntValue();
            device->loadPreset(bank, preset);
        } else if (action == ACTION_ADDRESS_DEVICE_SEND_MIDI){
            jassert(parameters.size() == 4);
            auto device = getHardwareDeviceByName(deviceName, HardwareDeviceType::output);
            if (device == nullptr) return;
            juce::MidiMessage msg = juce::MidiMessage(parameters[1].getIntValue(), parameters[2].getIntValue(), parameters[3].getIntValue());
            device->sendMidi(msg);
        } else if (action == ACTION_ADDRESS_DEVICE_SET_NOTES_MAPPING){
            jassert(parameters.size() == 2);
            auto device = getHardwareDeviceByName(deviceName, HardwareDeviceType::input);
            if (device == nullptr) return;
            juce::String serializedMapping = parameters[1];  // 128 ints serialized into string, separated by comas
            device->setNotesMapping(serializedMapping);
        } else if (action == ACTION_ADDRESS_DEVICE_SET_CC_MAPPING){
            jassert(parameters.size() == 2);
            auto device = getHardwareDeviceByName(deviceName, HardwareDeviceType::input);
            if (device == nullptr) return;
            juce::String serializedMapping = parameters[1];  // 128 ints serialized into string, separated by comas
            device->setControlChangeMapping(serializedMapping);
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
        } else if (action == ACTION_ADDRESS_TRANSPORT_PLAY){
            jassert(parameters.size() == 0);
            if (musicalContext->playheadIsPlaying()){
                // If it is playing, do nothing
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
        } else if (action == ACTION_ADDRESS_TRANSPORT_STOP){
            jassert(parameters.size() == 0);
            if (musicalContext->playheadIsPlaying()){
                // If it is playing, stop it
                shouldToggleIsPlaying = true;
            } else{
                // If it is not playing, do nothing
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
        if (action == ACTION_ADDRESS_SETTINGS_LOAD_SESSION) {
            jassert(parameters.size() == 1);
            juce::String filePath = parameters[0];
            loadSessionFromFile(filePath);
            
        } else if (action == ACTION_ADDRESS_SETTINGS_SAVE_SESSION){
            jassert(parameters.size() == 1);
            juce::String filePath = parameters[0];
            saveCurrentSessionToFile(filePath);
        
        } else if (action == ACTION_ADDRESS_SETTINGS_NEW_SESSION){
            jassert(parameters.size() == 2);
            int numTracks = parameters[0].getIntValue();
            int numScenes = parameters[1].getIntValue();
            loadNewEmptySession(numTracks, numScenes);
            
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
        
        } else if (action == ACTION_ADDRESS_SETTINGS_TOGGLE_DEBUG_SYNTH){
            renderWithInternalSynth = !renderWithInternalSynth;
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
        // Set midi in connection to false so the method to initialize midi in is retrieggered at next timer call
        // This is to prevent issues caused by the order in which frontend and backend are started (if using virtual midi
        // devices in the contorller app that should be connected here, these will need reconnection after controller
        // restarts)
        shouldTryInitializeMidiInputs = true;
        
        // Also in dev mode trigger reload ui
        #if JUCE_DEBUG
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
    message.addString(treeWhosePropertyHasChanged[ShepherdIDs::uuid].toString());
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
    message.addString(parentTree[ShepherdIDs::uuid].toString());
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
    message.addString(childWhichHasBeenRemoved[ShepherdIDs::uuid].toString());
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
