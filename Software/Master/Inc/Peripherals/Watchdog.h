#pragma once

namespace Watchdog
{
    // The independent watchdog is refreshed only while both safety workers
    // continue to make progress. A stalled worker therefore resets the MCU.
    void setup();
    void reportPccProgress();
    void reportBccProgress();
    void loop();
}
