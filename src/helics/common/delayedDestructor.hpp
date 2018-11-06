/*
Copyright © 2017-2018,
Battelle Memorial Institute; Lawrence Livermore National Security, LLC; Alliance for Sustainable Energy, LLC
All rights reserved. See LICENSE file and DISCLAIMER for more details.
*/
#pragma once

#include "TripWire.hpp"
#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

/** helper class to destroy objects at a late time when it is convenient and there are no more possibilities of
 * threading issues*/
template <class X>
class DelayedDestructor
{
  private:
    std::mutex destructionLock;
    std::vector<std::shared_ptr<X>> ElementsToBeDestroyed;
    std::function<void(std::shared_ptr<X> &ptr)> callBeforeDeleteFunction;
    tripwire::TripWireDetector tripDetect;

  public:
    DelayedDestructor () = default;
    explicit DelayedDestructor (std::function<void(std::shared_ptr<X> &ptr)> callFirst)
        : callBeforeDeleteFunction (std::move (callFirst))
    {
    }
    ~DelayedDestructor ()
    {
        int ii = 0;
        while (!ElementsToBeDestroyed.empty ())
        {
            ++ii;
            destroyObjects ();
            if (!ElementsToBeDestroyed.empty ())
            {
                // short circuit if the tripline was triggered
                if (tripDetect.isTripped ())
                {
                    return;
                }
                if (ii > 4)
                {
                    destroyObjects ();
                    break;
                }
                if (ii % 2 == 0)
                {
                    std::this_thread::sleep_for (std::chrono::milliseconds (100));
                }
                else
                {
                    std::this_thread::yield ();
                }
            }
        }
    }
    DelayedDestructor (DelayedDestructor &&) noexcept = delete;
    DelayedDestructor &operator= (DelayedDestructor &&) noexcept = delete;

    /** destroy objects that are now longer used*/
    size_t destroyObjects ()
    {
        std::unique_lock<std::mutex> lock (destructionLock);
        if (!ElementsToBeDestroyed.empty ())
        {
            std::vector<std::shared_ptr<X>> ecall;
            for (auto &element : ElementsToBeDestroyed)
            {
                if (element.use_count () == 1)
                {
                    ecall.push_back (element);
                }
            }
            // so apparently remove_if can actually call the destructor for shared_ptrs so the call function needs
            // to be before this call
            auto loc = std::remove_if (ElementsToBeDestroyed.begin (), ElementsToBeDestroyed.end (),
                                       [](const auto &element) { return (element.use_count () <= 1); });
            ElementsToBeDestroyed.erase (loc, ElementsToBeDestroyed.end ());
            auto deleteFunc = callBeforeDeleteFunction;
            lock.unlock ();
			//this needs to be done after the lock, so a destructor can never called while under the lock
            if (deleteFunc)
            {
                for (auto &element : ecall)
                {
                    deleteFunc (element);
                }
            }
            ecall.clear ();  //make sure the destructors get called before returning.
            lock.lock ();  //reengage the lock so the size is correct
        }
        return ElementsToBeDestroyed.size ();
    }

    size_t destroyObjects (std::chrono::milliseconds delay)
    {
        using namespace std::literals::chrono_literals;
        std::unique_lock<std::mutex> lock (destructionLock);
        auto delayTime = (delay < 100ms) ? delay : 50ms;
        int delayCount = (delay < 100ms) ? 1 : (delay / 50).count ();

        int cnt = 0;
        while ((!ElementsToBeDestroyed.empty ()) && (cnt < delayCount))
        {
            if (cnt > 0) //don't sleep on the first loop
            {
                lock.unlock ();
                std::this_thread::sleep_for (delayTime);
                ++cnt;
                lock.lock ();
            }
            else
            {
                ++cnt;
            }

            if (!ElementsToBeDestroyed.empty ())
            {
                lock.unlock ();
                destroyObjects ();
                lock.lock ();
            }
        }
        return ElementsToBeDestroyed.size ();
    }

    void addObjectsToBeDestroyed (std::shared_ptr<X> &&obj)
    {
        std::lock_guard<std::mutex> lock (destructionLock);
        ElementsToBeDestroyed.push_back (std::move (obj));
    }
    void addObjectsToBeDestroyed (std::shared_ptr<X> &obj)
    {
        std::lock_guard<std::mutex> lock (destructionLock);
        ElementsToBeDestroyed.push_back (obj);
    }
};
