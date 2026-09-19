// datasource.h — read a named Ultima IV asset (e.g. "SHAPES.EGA") from either
// a loose directory or an ultima4.zip, mirroring how tu4 (u4file.cpp) locates
// the DOS data. Lets tu4-setup work whether the user unpacked ultima4.zip or
// left it zipped.
#ifndef TU4SETUP_DATASOURCE_H
#define TU4SETUP_DATASOURCE_H
#include <cstdint>
#include <vector>
#include <string>

namespace tu4setup {

class DataSource {
public:
    // Locate the Ultima IV data, searching the same root x subdir combinations
    // tu4 uses (see findDataDir), for either a loose data dir (containing
    // SHAPES.EGA / charset.ega) OR an ultima4.zip. If `explicitPath` is given
    // it is used directly (a dir or a .zip). Returns true on success.
    bool open(const char *explicitPath /* may be NULL for auto-search */);

    // Read a named asset (case-insensitive; e.g. "TREE.EGA"). Returns false if
    // absent. For a zip, the internal folder prefix (e.g. "ultima4/") is
    // handled automatically.
    bool read(const std::string &name, std::vector<uint8_t> &out) const;

    // A human-readable description of where the data was found.
    const std::string &location() const { return loc; }
    bool isZip() const { return zip; }

private:
    bool   zip = false;
    std::string dir;          // loose-dir path (when !zip)
    std::string zipPath;      // .zip path (when zip)
    std::string zipPrefix;    // internal folder prefix inside the zip
    std::string loc;
};

} // namespace tu4setup
#endif
