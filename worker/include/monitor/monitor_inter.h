#pragma once

namespace monitor {

/// Abstract interface for any monitor / collector.
struct MonitorInterface {
    virtual ~MonitorInterface() = default;
    virtual bool Init() = 0;
    virtual void Collect() = 0;
    virtual void Reset() {}
};

} // namespace monitor
