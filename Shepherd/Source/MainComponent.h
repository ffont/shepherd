
#pragma once

#include <JuceHeader.h>
#include "Sequencer.h"
#include "DevelopmentUIComponent.h"


//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/
class MainComponent: public juce::AudioAppComponent,
                     private juce::ActionListener
                       
{
public:
    MainComponent() : devUiComponent(&sequencer)
    {
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
        
        #if !RPI_BUILD
        addAndMakeVisible(devUiComponent);
        setSize (devUiComponent.getWidth(), devUiComponent.getHeight());
        #else
        setSize(10, 10); // I think this needs to be called anyway...
        #endif
        
        // Add action listener so sequencer can send messages to MainComponent
        sequencer.addActionListener(this);
    }

    ~MainComponent() override
    {
        shutdownAudio();
    }
    
    void actionListenerCallback (const juce::String &message) override
    {
        juce::String actionName = message.substring(0, message.indexOf(":"));
        juce::String actionData = message.substring(message.indexOf(":") + 1);
    
        if (actionName == ACTION_UPDATE_DEVUI_RELOAD_BROWSER) {
            devUiComponent.reloadBrowser();
        }
    }

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override
    {
        std::cout << "Prepare to play called with samples per block " << samplesPerBlockExpected << " and sample rate " << sampleRate << std::endl;
        sequencer.prepareSequencer(samplesPerBlockExpected, sampleRate);
    }
    
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        sequencer.getNextMIDISlice(bufferToFill);
    }
    
    void releaseResources() override
    {
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
    }
    
    void resized() override
    {
        #if !RPI_BUILD
        devUiComponent.setBounds(getLocalBounds());
        #else
        setSize(10, 10); // I think this needs to be called anyway...
        #endif
    }

private:
    Sequencer sequencer;
    
    #if !RPI_BUILD
    // Only for desktop app UI
    DevelopmentUIComponent devUiComponent;
    #endif
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
