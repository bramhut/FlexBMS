#include "Peripherals/CurrentSensingPolicy.h"
#include <cassert>

int main()
{
    // Every supported source, including none, with one through 32 slaves.
    for (uint8_t count = 1; count <= 32; ++count) {
        for (uint8_t source = 0; source <= count; ++source) {
            unsigned sources = 0, powered = 0;
            for (uint8_t slave = 1; slave <= count; ++slave) {
                const bool selected = CurrentSensingPolicy::isPackCurrentSource(source, slave);
                sources += selected;
                powered += CurrentSensingPolicy::measurementCircuitEnabled(source != 0, selected, true);
                assert(CurrentSensingPolicy::measurementCircuitEnabled(source != 0, selected, false) == selected);
            }
            assert(sources == (source == 0 ? 0U : 1U));
            assert(powered == (source == 0 ? 0U : count));
        }
    }
    assert(!CurrentSensingPolicy::isPackCurrentSource(0, 0));
}
