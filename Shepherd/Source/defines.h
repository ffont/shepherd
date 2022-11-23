/*
  ==============================================================================

    defines.h
    Created: 3 Jun 2021 5:49:48pm
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#pragma once

#define ENABLE_SYNC_STATE_WITH_OSC 0  // Using OSC does not seem to work well as full state is too big to bundle in an OSC message (?)
#define ENABLE_SYNC_STATE_WITH_WS 1

#define OSC_BACKEND_RECEIVE_PORT 9003
#define OSC_CONRTOLLER_RECEIVE_PORT 9004
#define WEBSOCKETS_SERVER_PORT 8125

#define ACTION_ADDRESS_GENERIC "/action"

#define ACTION_ADDRESS_TRANSPORT "/transport"
#define ACTION_ADDRESS_TRANSPORT_PLAY_STOP "/transport/playStop"
#define ACTION_ADDRESS_TRANSPORT_SET_BPM "/transport/setBpm"
#define ACTION_ADDRESS_TRANSPORT_SET_METER "/transport/setMeter"

#define ACTION_ADDRESS_CLIP "/clip"
#define ACTION_ADDRESS_CLIP_PLAY "/clip/play"
#define ACTION_ADDRESS_CLIP_STOP "/clip/stop"
#define ACTION_ADDRESS_CLIP_PLAY_STOP "/clip/playStop"
#define ACTION_ADDRESS_CLIP_RECORD_ON_OFF "/clip/recordOnOff"
#define ACTION_ADDRESS_CLIP_CLEAR "/clip/clear"
#define ACTION_ADDRESS_CLIP_DOUBLE "/clip/double"
#define ACTION_ADDRESS_CLIP_QUANTIZE "/clip/quantize"
#define ACTION_ADDRESS_CLIP_UNDO "/clip/undo"
#define ACTION_ADDRESS_CLIP_SET_LENGTH "/clip/setLength"
#define ACTION_ADDRESS_CLIP_SET_SEQUENCE "/clip/setSequence"
#define ACTION_ADDRESS_CLIP_EDIT_SEQUENCE "/clip/editSequence"

#define ACTION_ADDRESS_TRACK "/track"
#define ACTION_ADDRESS_TRACK_SET_INPUT_MONITORING "/track/setInputMonitoring"
#define ACTION_ADDRESS_TRACK_SET_ACTIVE_UI_NOTES_MONITORING_TRACK "/track/setActiveUiNotesMonitoringTrack"

#define ACTION_ADDRESS_DEVICE "/device"
#define ACTION_ADDRESS_DEVICE_SEND_ALL_NOTES_OFF_TO_DEVICE "/device/sendAllNotesOff"
#define ACTION_ADDRESS_DEVICE_LOAD_DEVICE_PRESET "/device/loadDevicePreset"
#define ACTION_ADDRESS_DEVICE_SEND_MIDI "/device/sendMidi"
#define ACTION_ADDRESS_DEVICE_SET_MIDI_CC_PARAMETERS "/device/setMidiCCParameterValues"
#define ACTION_ADDRESS_DEVICE_GET_MIDI_CC_PARAMETERS "/device/getMidiCCParameterValues"

#define ACTION_ADDRESS_SCENE "/scene"
#define ACTION_ADDRESS_SCENE_DUPLICATE "/scene/duplicate"
#define ACTION_ADDRESS_SCENE_PLAY "/scene/play"

#define ACTION_ADDRESS_METRONOME "/metronome"
#define ACTION_ADDRESS_METRONOME_ON "/metronome/on"
#define ACTION_ADDRESS_METRONOME_OFF "/metronome/off"
#define ACTION_ADDRESS_METRONOME_ON_OFF "/metronome/onOff"

#define ACTION_ADDRESS_SETTINGS "/settings"
#define ACTION_ADDRESS_SETTINGS_LOAD_SESSION "/settings/load"
#define ACTION_ADDRESS_SETTINGS_SAVE_SESSION "/settings/save"
#define ACTION_ADDRESS_SETTINGS_PUSH_NOTES_MAPPING "/settings/pushNotesMapping"
#define ACTION_ADDRESS_SETTINGS_PUSH_ENCODERS_MAPPING "/settings/pushEncodersMapping"
#define ACTION_ADDRESS_SETTINGS_FIXED_VELOCITY "/settings/fixedVelocity"
#define ACTION_ADDRESS_SETTINGS_FIXED_LENGTH "/settings/fixedLength"
#define ACTION_ADDRESS_TRANSPORT_RECORD_AUTOMATION "/settings/toggleRecordAutomation"

#define ACTION_ADDRESS_GET_STATE "/get_state"
#define ACTION_ADDRESS_FULL_STATE "/full_state"
#define ACTION_ADDRESS_STATE_UPDATE "/state_update"

#define ACTION_ADDRESS_SHEPHERD_CONTROLLER_READY "/shepherdControllerReady"
#define ACTION_ADDRESS_MIDI_CC_PARAMETER_VALUES_FOR_DEVICE "/midiCCParameterValuesForDevice"
#define ACTION_ADDRESS_ALIVE_MESSAGE "/alive"
#define ACTION_ADDRESS_STARTED_MESSAGE "/app_started"

#define SERIALIZATION_SEPARATOR ";"

#define ACTION_UPDATE_DEVUI_RELOAD_BROWSER "ACTION_UPDATE_DEVUI_RELOAD_BROWSER"

#define DEV_UI_SIMULATOR_URL "http://localhost:6128/"

#define MAX_NUM_SCENES 8  // In the future we want to support more scenes but then we need to implement some sort of scrolling in UI
#define MAX_NUM_TRACKS 8  // In the future we want to support more tracks but then we need to implement some sort of scrolling in UI

#define MIDI_SUSTAIN_PEDAL_CC 64
#define MIDI_BANK_CHANGE_CC 0

#define MIDI_BUFFER_MIN_BYTES 512

#define SHEPHERD_NOTES_MONITORING_MIDI_DEVICE_NAME "ShepherdBackendNotesMonitoring"

#define PUSH_MIDI_CLOCK_BURST_DURATION_MILLISECONDS 500


namespace Defaults
{
inline juce::String emptyString = "";
inline int order = -1;
inline double playheadPosition = 0.0;
inline bool isPlaying = false;
inline bool enabled = false;
inline bool doingCountIn = false;
inline int fixedLengthRecordingBars = 0;
inline bool recordAutomationEnabled = true;
inline int fixedVelocity = -1;
inline double bpm = 120.0;
inline int meter = 4;
inline int barCount = 0;
inline bool metronomeOn = true;
inline bool inputMonitoring = false;
inline double clipLengthInBeats = 0.0;
inline bool wrapEventsAcrossClipLoop = true;
inline double currentQuantizationStep = 0.0;
inline double willStartRecordingAt = -1.0;
inline double willStopRecordingAt = -1.0;
inline bool recording = false;
inline double willPlayAt = -1.0;
inline double willStopAt = -1.0;
inline bool playing = false;
inline double timestamp = 0.0;
inline juce::String eventType = "midi";
inline juce::String eventMidiBytes = "128,64,64";
}

namespace IDs
{
#define DECLARE_ID(name) const juce::Identifier name (#name);

DECLARE_ID (SESSION)
DECLARE_ID (DEVICE)
DECLARE_ID (TRACK)
DECLARE_ID (CLIP)
DECLARE_ID (SEQUENCE_EVENT)

DECLARE_ID (enabled)
DECLARE_ID (name)
DECLARE_ID (uuid)
DECLARE_ID (order)
DECLARE_ID (type)
DECLARE_ID (length)
DECLARE_ID (playheadPositionInBeats)
DECLARE_ID (isPlaying)
DECLARE_ID (shouldToggleIsPlaying)
DECLARE_ID (doingCountIn)
DECLARE_ID (countInPlayheadPositionInBeats)
DECLARE_ID (fixedLengthRecordingBars)
DECLARE_ID (recordAutomationEnabled)
DECLARE_ID (fixedVelocity)
DECLARE_ID (bpm)
DECLARE_ID (meter)
DECLARE_ID (barCount)
DECLARE_ID (metronomeOn)
DECLARE_ID (inputMonitoring)
DECLARE_ID (hardwareDeviceName)
DECLARE_ID (clipLengthInBeats)
DECLARE_ID (currentQuantizationStep)
DECLARE_ID (wrapEventsAcrossClipLoop)
DECLARE_ID (playing)
DECLARE_ID (willPlayAt)
DECLARE_ID (willStopAt)
DECLARE_ID (recording)
DECLARE_ID (willStartRecordingAt)
DECLARE_ID (willStopRecordingAt)
DECLARE_ID (timestamp)
DECLARE_ID (eventMidiBytes)
DECLARE_ID (midiNote)
DECLARE_ID (midiVelocity)
DECLARE_ID (duration)
DECLARE_ID (renderedStartTimestamp)
DECLARE_ID (renderedEndTimestamp)

#undef DECLARE_ID
}

enum SequenceEventType { midi, note };

struct MidiOutputDeviceData {
    juce::String identifier;
    juce::String name;
    std::unique_ptr<juce::MidiOutput> device;
    juce::MidiBuffer buffer;
};

struct GlobalSettingsStruct {
    double sampleRate;
    int samplesPerSlice;
    int maxScenes;  // This is the same of the maximum number of clips per track
    int maxTracks;
    int fixedLengthRecordingBars;
    double playheadPositionInBeats;
    double countInPlayheadPositionInBeats;
    bool isPlaying;
    bool doingCountIn;
    bool recordAutomationEnabled;
};


// NOTE: TrackSettingsStruct is defined in Clip.h to avoid circular import depedency issues as it requires HardwareDevice class
