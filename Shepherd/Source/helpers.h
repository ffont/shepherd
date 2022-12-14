/*
  ==============================================================================

    helpers.h
    Created: 21 Jun 2021 6:48:10pm
    Author:  Frederic Font Corbera

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "defines.h"
#include "drow_ValueTreeObjectList.h"

namespace Helpers
{

    inline bool sameMidiMessageWithSameTimestamp(juce::MidiMessage& m1, juce::MidiMessage& m2)
    {
        // Check that two midi messages are exactly the same and have the same timestamp
        int sizeM1 = m1.getRawDataSize();
        int sizeM2 = m2.getRawDataSize();
        if (sizeM1 != sizeM2){
            return false;
        }
        return (memcmp(m1.getRawData(), m2.getRawData(), sizeM1) == 0 && m1.getTimeStamp() == m2.getTimeStamp());
        //return m1.getDescription() + (juce::String)m1.getTimeStamp() == m2.getDescription() + (juce::String)m2.getTimeStamp();
    }

    inline juce::ValueTree createUuidProperty (juce::ValueTree& v)
    {
        if (! v.hasProperty (IDs::uuid))
            v.setProperty (IDs::uuid, juce::Uuid().toString(), nullptr);
        return v;
    }

    inline juce::ValueTree createDefaultStateRoot()
    {
        juce::ValueTree state (IDs::STATE);
        Helpers::createUuidProperty (state);
        state.setProperty (IDs::renderWithInternalSynth, Defaults::renderWithInternalSynth, nullptr);
        state.setProperty (IDs::dataLocation, Defaults::emptyString, nullptr);
        return state;
    }

    inline juce::ValueTree createDefaultSession(juce::StringArray availableHardwareDeviceNames, int numTracks, int numScenes)
    {
        juce::ValueTree session (IDs::SESSION);
        Helpers::createUuidProperty (session);
        session.setProperty (IDs::version, ProjectInfo::versionString , nullptr);
        session.setProperty (IDs::name, juce::Time::getCurrentTime().formatted("%Y%m%d") + " unnamed", nullptr);
        session.setProperty (IDs::playheadPositionInBeats, Defaults::playheadPosition, nullptr);
        session.setProperty (IDs::isPlaying, Defaults::isPlaying, nullptr);
        session.setProperty (IDs::doingCountIn, Defaults::doingCountIn, nullptr);
        session.setProperty (IDs::countInPlayheadPositionInBeats, Defaults::playheadPosition, nullptr);
        session.setProperty (IDs::barCount, Defaults::barCount, nullptr);
        session.setProperty (IDs::bpm, Defaults::bpm, nullptr);
        session.setProperty (IDs::meter, Defaults::meter, nullptr);
        session.setProperty (IDs::metronomeOn, Defaults::metronomeOn, nullptr);
        session.setProperty (IDs::fixedVelocity, Defaults::fixedVelocity, nullptr);
        session.setProperty (IDs::fixedLengthRecordingBars, Defaults::fixedLengthRecordingBars, nullptr);
        session.setProperty (IDs::recordAutomationEnabled, Defaults::recordAutomationEnabled, nullptr);
        
        // Hardcode some needed variables
        session.setProperty ("notesMonitoringDeviceName", SHEPHERD_NOTES_MONITORING_MIDI_DEVICE_NAME, nullptr);
        
        for (int tn = 0; tn < numTracks; ++tn)
        {
            // Create track
            juce::ValueTree t (IDs::TRACK);
            Helpers::createUuidProperty (t);
            t.setProperty (IDs::order, tn, nullptr);
            t.setProperty (IDs::inputMonitoring, Defaults::inputMonitoring, nullptr);
            const juce::String trackName ("Track " + juce::String (tn + 1));
            t.setProperty (IDs::name, trackName, nullptr);
            t.setProperty (IDs::hardwareDeviceName, Defaults::emptyString, nullptr);

            // Add hardware device to track
            if (tn < availableHardwareDeviceNames.size()){
                t.setProperty (IDs::hardwareDeviceName, availableHardwareDeviceNames[tn], nullptr);
            } else {
                t.setProperty (IDs::hardwareDeviceName, availableHardwareDeviceNames[availableHardwareDeviceNames.size() - 1], nullptr);
            }
            
            // Now add clips to track (for now clips are still empty and disabled)
            for (int cn = 0; cn < numScenes; ++cn)
            {
                juce::ValueTree c (IDs::CLIP);
                Helpers::createUuidProperty (c);
                c.setProperty (IDs::name, t.getProperty(IDs::order).toString() + "-" + juce::String (cn), nullptr);
                c.setProperty (IDs::clipLengthInBeats, Defaults::clipLengthInBeats, nullptr);
                c.setProperty (IDs::currentQuantizationStep, Defaults::currentQuantizationStep, nullptr);
                c.setProperty (IDs::wrapEventsAcrossClipLoop, Defaults::wrapEventsAcrossClipLoop, nullptr);
                
                c.setProperty (IDs::recording, Defaults::recording, nullptr);
                c.setProperty (IDs::willStartRecordingAt, Defaults::willStartRecordingAt, nullptr);
                c.setProperty (IDs::willStopRecordingAt, Defaults::willStopRecordingAt, nullptr);
                c.setProperty (IDs::playing, Defaults::playing, nullptr);
                c.setProperty (IDs::willPlayAt, Defaults::willPlayAt, nullptr);
                c.setProperty (IDs::willStopAt, Defaults::willStopAt, nullptr);
                c.setProperty (IDs::playheadPositionInBeats, Defaults::playheadPosition, nullptr);

                t.addChild (c, -1, nullptr);
            }
            
            session.addChild (t, -1, nullptr);
        }

        return session;
    }

    inline juce::ValueTree createOutputHardwareDevice(juce::String name, juce::String shortName, juce::String midiDeviceName, int midiChannel)
    {
        juce::ValueTree device {IDs::HARDWARE_DEVICE};
        Helpers::createUuidProperty (device);
        device.setProperty(IDs::type, HardwareDeviceType::output, nullptr);
        device.setProperty(IDs::name, name, nullptr);
        device.setProperty(IDs::shortName, shortName, nullptr);
        device.setProperty(IDs::midiDeviceName, midiDeviceName, nullptr);
        device.setProperty(IDs::midiChannel, midiChannel, nullptr);
        device.setProperty(IDs::midiCCParameterValuesList, "", nullptr);
        return device;
    }

    inline juce::ValueTree createSequenceEventFromMidiMessage(juce::MidiMessage msg)
    {
        juce::ValueTree sequenceEvent {IDs::SEQUENCE_EVENT};
        Helpers::createUuidProperty (sequenceEvent);
        sequenceEvent.setProperty(IDs::type, SequenceEventType::midi, nullptr);
        sequenceEvent.setProperty(IDs::timestamp, msg.getTimeStamp(), nullptr);
        sequenceEvent.setProperty(IDs::uTime, Defaults::uTime, nullptr);
        sequenceEvent.setProperty(IDs::renderedStartTimestamp, -1.0, nullptr);
        sequenceEvent.setProperty(IDs::renderedEndTimestamp, -1.0, nullptr);
        juce::StringArray bytes = {};
        for (int i=0; i<msg.getRawDataSize(); i++){
            bytes.add(juce::String(msg.getRawData()[i]));
        }
        sequenceEvent.setProperty(IDs::eventMidiBytes, bytes.joinIntoString(","), nullptr);
        return sequenceEvent;
    }

    inline juce::ValueTree createSequenceEventFromMidiBytesString(double timestamp, const juce::String& eventMidiBytes, double utime)
    {
        // eventMidiBytes = comma separated byte values, eg: 127,75,12
        juce::ValueTree sequenceEvent {IDs::SEQUENCE_EVENT};
        Helpers::createUuidProperty (sequenceEvent);
        sequenceEvent.setProperty(IDs::type, SequenceEventType::midi, nullptr);
        sequenceEvent.setProperty(IDs::timestamp, timestamp, nullptr);
        sequenceEvent.setProperty(IDs::uTime, utime, nullptr);
        sequenceEvent.setProperty(IDs::renderedStartTimestamp, -1.0, nullptr);
        sequenceEvent.setProperty(IDs::renderedEndTimestamp, -1.0, nullptr);
        sequenceEvent.setProperty(IDs::eventMidiBytes, eventMidiBytes, nullptr);
        return sequenceEvent;
    }

    inline juce::ValueTree createSequenceEventFromMidiBytesString(double timestamp, const juce::String& eventMidiBytes)
    {
        return createSequenceEventFromMidiBytesString(timestamp, eventMidiBytes, Defaults::uTime);
    }

    inline juce::ValueTree createSequenceEventOfTypeNote(double timestamp, int note, float velocity, double duration, double utime, float chance)
    {
        juce::ValueTree sequenceEvent {IDs::SEQUENCE_EVENT};
        Helpers::createUuidProperty (sequenceEvent);
        sequenceEvent.setProperty(IDs::type, SequenceEventType::note, nullptr);
        sequenceEvent.setProperty(IDs::timestamp, timestamp, nullptr);
        sequenceEvent.setProperty(IDs::uTime, utime, nullptr);
        sequenceEvent.setProperty(IDs::renderedStartTimestamp, -1.0, nullptr);
        sequenceEvent.setProperty(IDs::renderedEndTimestamp, -1.0, nullptr);
        sequenceEvent.setProperty(IDs::midiNote, note, nullptr);
        sequenceEvent.setProperty(IDs::midiVelocity, velocity, nullptr);
        sequenceEvent.setProperty(IDs::duration, duration, nullptr);
        sequenceEvent.setProperty(IDs::chance, chance, nullptr);
        return sequenceEvent;
    }

    inline juce::ValueTree createSequenceEventOfTypeNote(double timestamp, int note, float velocity, double duration)
    {
        return createSequenceEventOfTypeNote(timestamp, note, velocity, duration, Defaults::uTime, Defaults::chance);
    }

    inline std::vector<juce::MidiMessage> eventValueTreeToMidiMessages(juce::ValueTree& sequenceEvent)
    {
        std::vector<juce::MidiMessage> messages = {};
        
        // NOTE: don't care about MIDI channel here as they will be replaced when sending the notes to the appropriate output device
        int midiChannel = 1;
        
        if ((int)sequenceEvent.getProperty(IDs::type) == SequenceEventType::midi) {
            juce::String bytesString = sequenceEvent.getProperty(IDs::eventMidiBytes, Defaults::eventMidiBytes);
            juce::StringArray bytes;
            bytes.addTokens(bytesString, ",", "");
            juce::MidiMessage msg;
            if (bytes.size() == 2){
                msg = juce::MidiMessage(bytes[0].getIntValue(), bytes[1].getIntValue());
            } else if (bytes.size() == 3){
                msg = juce::MidiMessage(bytes[0].getIntValue(), bytes[1].getIntValue(), bytes[2].getIntValue());
            }
            msg.setChannel(midiChannel);
            msg.setTimeStamp(sequenceEvent.getProperty(IDs::renderedStartTimestamp));
            messages.push_back(msg);
            
        } else if ((int)sequenceEvent.getProperty(IDs::type) == SequenceEventType::note) {
           
            int midiNote = (int)sequenceEvent.getProperty(IDs::midiNote);
            float midiVelocity = (float)sequenceEvent.getProperty(IDs::midiVelocity);
            double noteOnTimestamp = (double)sequenceEvent.getProperty(IDs::renderedStartTimestamp);
            double noteOffTimestamp = (double)sequenceEvent.getProperty(IDs::renderedEndTimestamp);
            
            juce::MidiMessage msgNoteOn = juce::MidiMessage::noteOn(midiChannel, midiNote, midiVelocity);
            msgNoteOn.setTimeStamp(noteOnTimestamp);
            messages.push_back(msgNoteOn);
            
            juce::MidiMessage msgNoteOff = juce::MidiMessage::noteOff(midiChannel, midiNote, 0.0f);
            msgNoteOff.setTimeStamp(noteOffTimestamp);
            messages.push_back(msgNoteOff);
        }
        return messages;
    }

    inline juce::String serialize128IntArray(std::array<int, 128> array)
    {
        juce::StringArray splittedValues;
        for (int i=0; i<array.size(); i++){
            splittedValues.add(juce::String(array[i]));
        }
        return splittedValues.joinIntoString(",");
    }

    inline std::array<int, 128> deserialize128IntArray(juce::String serializedArray)
    {
        std::array<int, 128> array = {0};
        if (serializedArray == ""){
            // Return array with default midi CC values
            for (int i=0; i<array.size(); i++){
                array[i] = 64;
            }
        } else {
            juce::StringArray splittedValues;
            splittedValues.addTokens (serializedArray, ",", "");
            int i=0;
            for (auto value: splittedValues) {
                array[i] = value.getIntValue();
                i+=1;
            }
        }
        jassert(array.size() == 128);
        return array;
    }
}
