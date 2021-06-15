/*
  ==============================================================================

    defines.h
    Created: 3 Jun 2021 5:49:48pm
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#pragma once

#define OSC_ADDRESS_TRANSPORT "/transport"
#define OSC_ADDRESS_TRANSPORT_PLAY_STOP "/transport/playStop"
#define OSC_ADDRESS_TRANSPORT_SET_BPM "/transport/setBpm"
#define OSC_ADDRESS_TRANSPORT_SET_METER "/transport/setMeter"

#define OSC_ADDRESS_CLIP "/clip"
#define OSC_ADDRESS_CLIP_PLAY "/clip/play"
#define OSC_ADDRESS_CLIP_STOP "/clip/stop"
#define OSC_ADDRESS_CLIP_PLAY_STOP "/clip/playStop"
#define OSC_ADDRESS_CLIP_RECORD_ON_OFF "/clip/recordOnOff"
#define OSC_ADDRESS_CLIP_CLEAR "/clip/clear"
#define OSC_ADDRESS_CLIP_DOUBLE "/clip/double"
#define OSC_ADDRESS_CLIP_QUANTIZE "/clip/quantize"
#define OSC_ADDRESS_CLIP_UNDO "/clip/undo"
#define OSC_ADDRESS_CLIP_SET_LENGTH "/clip/setLength"

#define OSC_ADDRESS_TRACK "/track"
#define OSC_ADDRESS_TRACK_SET_INPUT_MONITORING "/track/setInputMonitoring"
#define OSC_ADDRESS_TRACK_SET_ACTIVE_UI_NOTES_MONITORING_TRACK "/track/setActiveUiNotesMonitoringTrack"

#define OSC_ADDRESS_DEVICE "/device"
#define OSC_ADDRESS_DEVICE_SEND_ALL_NOTES_OFF_TO_DEVICE "/device/sendAllNotesOff"
#define OSC_ADDRESS_DEVICE_LOAD_DEVICE_PRESET "/device/loadDevicePreset"
#define OSC_ADDRESS_DEVICE_SEND_MIDI "/device/sendMidi"
#define OSC_ADDRESS_DEVICE_SET_MIDI_CC_PARAMETERS "/device/setMidiCCParameterValues"
#define OSC_ADDRESS_DEVICE_GET_MIDI_CC_PARAMETERS "/device/getMidiCCParameterValues"

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
#define OSC_ADDRESS_SETTINGS_FIXED_LENGTH "/settings/fixedLength"
#define OSC_ADDRESS_TRANSPORT_RECORD_AUTOMATION "/settings/toggleRecordAutomation"

#define OSC_ADDRESS_STATE "/state"
#define OSC_ADDRESS_STATE_TRACKS "/state/tracks"
#define OSC_ADDRESS_STATE_TRANSPORT "/state/transport"

#define OSC_ADDRESS_SHEPHERD_CONTROLLER_READY "/shepherdControllerReady"

#define OSC_ADDRESS_STATE_FROM_SHEPHERD "/stateFromShepherd"
#define OSC_ADDRESS_MIDI_CC_PARAMETER_VALUES_FOR_DEVICE "/midiCCParameterValuesForDevice"
#define OSC_ADDRESS_SHEPHERD_READY "/shepherdReady"

#define OSC_BACKEND_RECEIVE_PORT 9003
#define OSC_CONRTOLLER_RECEIVE_PORT 9004


#define DEV_UI_SIMULATOR_URL "http://localhost:6128/"


#define CLIP_STATUS_PLAYING "p"
#define CLIP_STATUS_STOPPED "s"
#define CLIP_STATUS_CUED_TO_PLAY "c"
#define CLIP_STATUS_CUED_TO_STOP "C"
#define CLIP_STATUS_RECORDING "r"
#define CLIP_STATUS_CUED_TO_RECORD "w"
#define CLIP_STATUS_CUED_TO_STOP_RECORDING "W"
#define CLIP_STATUS_NO_RECORDING "n"
#define CLIP_STATUS_IS_EMPTY "E"
#define CLIP_STATUS_IS_NOT_EMPTY "e"


#define MIDI_SUSTAIN_PEDAL_CC 64
#define MIDI_BANK_CHANGE_CC 0


#define SHEPHERD_NOTES_MONITORING_MIDI_DEVICE_NAME "ShepherdBackendNotesMonitoring"


#define PUSH_MIDI_CLOCK_BURST_DURATION_MILLISECONDS 500
#if RPI_BUILD
#define PUSH_MIDI_OUT_DEVICE_NAME "Ableton Push 2 MIDI 1"
#define PUSH_MIDI_IN_DEVICE_NAME "Ableton Push 2 MIDI 1"
#else
#define PUSH_MIDI_OUT_DEVICE_NAME ""
#define PUSH_MIDI_IN_DEVICE_NAME "Push2Simulator"
#endif


#if RPI_BUILD
#define DEFAULT_MIDI_OUT_DEVICE_NAME "ESI M4U eX MIDI 5"
#define DEFAULT_MIDI_CLOCK_OUT_DEVICE_NAME "ESI M4U eX MIDI 6"
#define DEFAULT_KEYBOARD_MIDI_IN_DEVICE_NAME "LUMI Keys BLOCK MIDI 1"
#else
#define DEFAULT_MIDI_OUT_DEVICE_NAME "IAC Driver Bus 1"
#define DEFAULT_MIDI_CLOCK_OUT_DEVICE_NAME ""
#define DEFAULT_KEYBOARD_MIDI_IN_DEVICE_NAME "iCON iKEY V1.02"
#endif


struct MidiOutputDeviceData {
    juce::String identifier;
    juce::String name;
    std::unique_ptr<juce::MidiOutput> device;
    juce::MidiBuffer buffer;
};

struct GlobalSettingsStruct {
    double sampleRate;
    int samplesPerBlock;
    int nScenes;
    int fixedLengthRecordingBars;
    double playheadPositionInBeats;
    double countInplayheadPositionInBeats;
    bool isPlaying;
    bool doingCountIn;
    bool recordAutomationEnabled;
};


// NOTE: TrackSettingsStruct is defined in Clip.h to avoid circular import depedency issues as it requires HardwareDevice class
