/*
  ==============================================================================

    ReleasePool.h
    Created: 8 Jun 2022 7:09:24pm
    Author:  Frederic Font Corbera

  ==============================================================================
*/

// Code adapted from https://github.com/matkatmusic/AudioFilePlayer/blob/91fdf80192135096b044c9d4070f8b3055ee1672/Source/PluginProcessor.h
#pragma once

#include <JuceHeader.h>

template<typename T, size_t Size = 30>
struct ReleasePoolFifo
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

template<typename ReferenceCountedType>
struct ReleasePool : juce::Timer
{
    ReleasePool()
    {
        deletionPool.reserve(5000);
        
        startTimer(1 * 1000);
    }
    
    ~ReleasePool() override
    {
        stopTimer();
    }
    
    using Ptr = typename ReferenceCountedType::Ptr;
    
    void add(Ptr ptr)
    {
        if( ptr == nullptr )
            return;
        
        if( juce::MessageManager::getInstance()->isThisTheMessageThread() )
        {
            addIfNotAlreadyThere(ptr);
        }
        else
        {
            if( fifo.push(ptr) )
            {
                successfullyAdded.set(true);
            }
            else
            {
                jassertfalse;
            }
        }
    }
    
    void timerCallback() override
    {
        if( successfullyAdded.compareAndSetBool(false, true))
        {
            Ptr ptr;
            while( fifo.pull(ptr) )
            {
                addIfNotAlreadyThere(ptr);
                ptr = nullptr;
            }
        }
        
        deletionPool.erase(std::remove_if(deletionPool.begin(),
                                          deletionPool.end(),
                                          [](const auto& ptr)
                                          {
                                              return ptr->getReferenceCount() <= 1;
                                          }),
                           deletionPool.end());
    }
private:
    ReleasePoolFifo<Ptr, 512> fifo;
    std::vector<Ptr> deletionPool;
    juce::Atomic<bool> successfullyAdded { false };
    
    void addIfNotAlreadyThere(Ptr ptr)
    {
        auto found = std::find_if(deletionPool.begin(),
                                  deletionPool.end(),
                                  [ptr](const auto& elem)
                                  {
                                      return elem.get() == ptr.get();
                                  });
        
        if( found == deletionPool.end() )
            deletionPool.push_back(ptr);
    }
};
