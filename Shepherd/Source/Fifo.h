/*
  ==============================================================================

    Fifo.h
    Created: 8 Jun 2022 7:09:10pm
    Author:  Frederic Font Corbera

  ==============================================================================
*/

// Code inspired from https://github.com/matkatmusic/SimpleMultiBandComp/blob/master/Source/DSP/Fifo.h (GPLv3 license)

#pragma once

#include <JuceHeader.h>

#include <array>
template<typename T, int Capacity>
struct Fifo
{
    /*
    // Commenting these methods as these are only used if FIFOs are used to share audio buffers...
    void prepare(int numChannels, int numSamples)
    {
        static_assert( std::is_same_v<T, juce::AudioBuffer<float>>,
                      "prepare(numChannels, numSamples) should only be used when the Fifo is holding juce::AudioBuffer<float>");
        for( auto& buffer : buffers)
        {
            buffer.setSize(numChannels,
                           numSamples,
                           false,   //clear everything?
                           true,    //including the extra space?
                           true);   //avoid reallocating if you can?
            buffer.clear();
        }
    }
    
    void prepare(size_t numElements)
    {
        static_assert( std::is_same_v<T, std::vector<float>>,
                      "prepare(numElements) should only be used when the Fifo is holding std::vector<float>");
        for( auto& buffer : buffers )
        {
            buffer.clear();
            buffer.resize(numElements, 0);
        }
    }
     */
    
    bool push(const T& t)
    {
        auto write = fifo.write(1);
        if( write.blockSize1 > 0 )
        {
            buffers[static_cast<size_t>(write.startIndex1)] = t;
            return true;
        }
        
        return false;
    }
    
    bool pull(T& t)
    {
        auto read = fifo.read(1);
        if( read.blockSize1 > 0 )
        {
            t = buffers[static_cast<size_t>(read.startIndex1)];
            return true;
        }
        
        return false;
    }
    
    int getNumAvailableForReading() const
    {
        return fifo.getNumReady();
    }
private:
    //static constexpr int Capacity = 50;
    std::array<T, Capacity> buffers;
    juce::AbstractFifo fifo {Capacity};
};
