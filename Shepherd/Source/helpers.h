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

namespace Helpers
{

    inline juce::ValueTree createUuidProperty (juce::ValueTree& v)
    {
        if (! v.hasProperty (IDs::uuid))
            v.setProperty (IDs::uuid, juce::Uuid().toString(), nullptr);

        return v;
    }

    inline juce::ValueTree createDefaultSession()
    {
        juce::ValueTree session (IDs::SESSION);
        Helpers::createUuidProperty (session);
        session.setProperty (IDs::name, juce::Time::getCurrentTime().formatted("%Y%m%d") + " unnamed", nullptr);
        session.setProperty (IDs::playheadPositionInBeats, Defaults::playheadPosition, nullptr);
        session.setProperty (IDs::isPlaying, Defaults::isPlaying, nullptr);
        session.setProperty (IDs::doingCountIn, Defaults::doingCountIn, nullptr);
        session.setProperty (IDs::countInplayheadPositionInBeats, Defaults::playheadPosition, nullptr);
        session.setProperty (IDs::fixedLengthRecordingBars, Defaults::fixedLengthRecordingBars, nullptr);
        session.setProperty (IDs::recordAutomationEnabled, Defaults::recordAutomationEnabled, nullptr);
        session.setProperty (IDs::fixedVelocity, Defaults::fixedVelocity, nullptr);
        session.setProperty (IDs::bpm, Defaults::bpm, nullptr);
        session.setProperty (IDs::meter, Defaults::meter, nullptr);
        session.setProperty (IDs::barCount, Defaults::barCount, nullptr);
        session.setProperty (IDs::metronomeOn, Defaults::metronomeOn, nullptr);
        
        /*
        for (int tn = 0; tn < 8; ++tn)
        {
            juce::ValueTree t (IDs::TRACK);
            const juce::String trackName ("Track " + juce::String (tn + 1));
            t.setProperty (IDs::name, trackName, nullptr);
            Helpers::createUuidProperty (t);

            for (int cn = 0; cn < 8; ++cn)
            {
                juce::ValueTree c (IDs::CLIP);
                Helpers::createUuidProperty (c);
                c.setProperty (IDs::name, trackName + ", Clip " + juce::String (cn + 1), nullptr);
                c.setProperty (IDs::length, 0.0, nullptr);
                t.addChild (c, -1, nullptr);
            }

            session.addChild (t, -1, nullptr);
        }*/

        return session;
    }

}
