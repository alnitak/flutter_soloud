// Standalone native regression tests for the playback-device list exports in
// src/bindings.cpp.
//
// listPlaybackDevices() writes one entry per output device into three arrays
// the caller allocates, without being told their size. The web bindings
// allocate 50 entries, so a 51st device was written past them.
// listPlaybackDevicesBounded() is told the capacity and stops there, and
// listPlaybackDevices() now stops at the 50 entries its callers allocate.
// Both drop devices whose names look like garbage, which must not include
// short real names such as "DAC" or "HDMI".
//
// A test hook (soloudTestSetPlaybackDevices(), SOLOUD_LIFECYCLE_TEST_HOOKS)
// replaces the backend's devices with a list the test chooses. Every array is
// allocated with exactly the number of entries the export may write, so under
// AddressSanitizer a write past one is reported where it happens.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_device_list_test.sh [--asan|--tsan]

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

extern "C"
{
    void listPlaybackDevices(char **devicesName, int **deviceId,
                             int **isDefault, int *n_devices);
    void listPlaybackDevicesBounded(char **devicesName, int **deviceId,
                                    int **isDefault, int *n_devices,
                                    int capacity);
    void freeListPlaybackDevices(char **devicesName, int **deviceId,
                                 int **isDefault, int n_devices);

    // Test-only hook (SOLOUD_LIFECYCLE_TEST_HOOKS).
    void soloudTestSetPlaybackDevices(const char *const *names, int count);
}

namespace
{

int gAssertions = 0;
int gFailures = 0;

void check(bool condition, const std::string &what)
{
    gAssertions++;
    if (condition)
    {
        std::printf("  ok: %s\n", what.c_str());
        return;
    }
    std::printf("  FAILED: %s\n", what.c_str());
    gFailures++;
}

struct Device
{
    std::string name;
    int id;
    int isDefault;
};

struct Listing
{
    int count = -1;
    std::vector<Device> devices;
};

/// Makes the backend report [names], the first one the default.
void fakeDevices(const std::vector<std::string> &names)
{
    std::vector<const char *> pointers;
    for (const std::string &name : names)
        pointers.push_back(name.c_str());
    soloudTestSetPlaybackDevices(pointers.data(), (int)pointers.size());
}

std::vector<std::string> numberedDevices(int count)
{
    std::vector<std::string> names;
    for (int i = 0; i < count; i++)
        names.push_back("Test Output " + std::to_string(i));
    return names;
}

/// Lists into arrays of [entries] entries each: through the bounded export
/// with that capacity, or through the legacy one when [legacy] is true.
Listing listInto(int entries, bool legacy)
{
    const size_t n = entries > 0 ? (size_t)entries : 0;
    auto **names = static_cast<char **>(std::calloc(n, sizeof(char *)));
    auto **ids = static_cast<int **>(std::calloc(n, sizeof(int *)));
    auto **defaults = static_cast<int **>(std::calloc(n, sizeof(int *)));

    Listing listing;
    if (legacy)
        listPlaybackDevices(names, ids, defaults, &listing.count);
    else
        listPlaybackDevicesBounded(names, ids, defaults, &listing.count,
                                   entries);

    // Never read or free past the arrays, even when the export wrote past
    // them: the check on the count reports that.
    const int written = listing.count < (int)n ? listing.count : (int)n;
    for (int i = 0; i < written; i++)
        listing.devices.push_back({names[i], *ids[i], *defaults[i]});
    freeListPlaybackDevices(names, ids, defaults, written);
    std::free(names);
    std::free(ids);
    std::free(defaults);
    return listing;
}

/// Whether [listing] holds exactly the first [count] of numberedDevices().
bool isNumberedPrefix(const Listing &listing, int count)
{
    if (listing.count != count || (int)listing.devices.size() != count)
        return false;
    for (int i = 0; i < count; i++)
    {
        const Device &d = listing.devices[i];
        if (d.name != "Test Output " + std::to_string(i) || d.id != i ||
            d.isDefault != (i == 0 ? 1 : 0))
            return false;
    }
    return true;
}

void testBoundedExportStopsAtTheCapacity()
{
    std::printf("bounded export, 60 devices, capacity 50\n");
    fakeDevices(numberedDevices(60));
    const Listing listing = listInto(50, false);
    check(listing.count == 50,
          "writes 50 entries (got " + std::to_string(listing.count) + ")");
    check(isNumberedPrefix(listing, 50),
          "the first 50 devices, with their names, ids and default flags");
}

void testBoundedExportListsEveryDeviceThatFits()
{
    std::printf("bounded export, 60 devices, capacity 60 and 64\n");
    fakeDevices(numberedDevices(60));
    check(isNumberedPrefix(listInto(60, false), 60),
          "capacity 60 lists all 60 devices");
    check(isNumberedPrefix(listInto(64, false), 60),
          "capacity 64 lists all 60 devices");
}

void testBoundedExportWithNoCapacity()
{
    std::printf("bounded export, 3 devices, capacity 0 and -1\n");
    fakeDevices(numberedDevices(3));
    check(listInto(0, false).count == 0, "capacity 0 writes nothing");
    check(listInto(-1, false).count == 0, "capacity -1 writes nothing");
}

void testLegacyExportStopsAtFifty()
{
    std::printf("legacy export, 60 devices, 50-entry arrays\n");
    fakeDevices(numberedDevices(60));
    const Listing listing = listInto(50, true);
    check(listing.count == 50,
          "writes 50 entries, what its web caller allocates (got " +
              std::to_string(listing.count) + ")");
    check(isNumberedPrefix(listing, 50), "the first 50 devices");
}

void testLegacyExportListsAFewDevices()
{
    std::printf("legacy export, 3 devices, 50-entry arrays\n");
    fakeDevices(numberedDevices(3));
    check(isNumberedPrefix(listInto(50, true), 3), "lists all 3 devices");
}

/// Names of five bytes or fewer are real devices too. Only a name with a
/// control character in its first five bytes, or none at all, is dropped.
void testShortNamesAreListed()
{
    std::printf("short device names\n");
    fakeDevices({"DAC", "HDMI", "USB-A", "\x01" "device", ""});
    for (bool legacy : {false, true})
    {
        const Listing listing = listInto(legacy ? 50 : 8, legacy);
        const std::string via = legacy ? " (legacy export)" : "";
        check(listing.count == 3,
              "lists 3 of the 5 devices" + via + " (got " +
                  std::to_string(listing.count) + ")");
        const char *const expected[] = {"DAC", "HDMI", "USB-A"};
        bool same = (int)listing.devices.size() == 3;
        for (int i = 0; same && i < 3; i++)
            same = listing.devices[i].name == expected[i] &&
                   listing.devices[i].id == i &&
                   listing.devices[i].isDefault == (i == 0 ? 1 : 0);
        check(same, "DAC, HDMI and USB-A with ids 0, 1, 2, DAC the default" +
                        via);
    }
}

} // namespace

int main()
{
    testBoundedExportStopsAtTheCapacity();
    testBoundedExportListsEveryDeviceThatFits();
    testBoundedExportWithNoCapacity();
    testLegacyExportStopsAtFifty();
    testLegacyExportListsAFewDevices();
    testShortNamesAreListed();
    soloudTestSetPlaybackDevices(nullptr, -1);

    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}
