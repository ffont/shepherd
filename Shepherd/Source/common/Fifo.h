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

template<typename T, size_t Size = 30>
struct Fifo
{
    size_t getSize() const noexcept { return Size; }
    
    bool push(const T& t)
    {
        auto write = fifo.write(1);
        if( write.blockSize1 > 0 )
        {
            size_t index = static_cast<size_t>(write.startIndex1);
            buffer[index] = t;
            return true;
        }
        
        return false;
    }
    
    bool pull(T& t)
    {
        auto read = fifo.read(1);
        if( read.blockSize1 > 0 )
        {
            t = buffer[static_cast<size_t>(read.startIndex1)];
            return true;
        }
        
        return false;
    }
    
    int getNumAvailableForReading() const
    {
        return fifo.getNumReady();
    }
    
    int getAvailableSpace() const
    {
        return fifo.getFreeSpace();
    }

private:
    juce::AbstractFifo fifo { Size };
    std::array<T, Size> buffer;
};
