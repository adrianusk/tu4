// datasource.cpp — loose-dir or zip Ultima IV data reader for tu4-setup.

#include "datasource.h"
#include "../unzip.h"
#include <cstdio>
#include <cstdlib>
#include <cctype>

namespace tu4setup {

// ---- path search (mirrors u4file.cpp roots x subdirs) ----------------------
static std::vector<std::string> roots() {
    std::vector<std::string> r;
    r.push_back(".");
#ifdef _WIN32
    r.push_back("C:"); r.push_back("C:/DOS"); r.push_back("C:/GAMES");
#else
#ifdef __linux__
    const char *home = std::getenv("HOME");
    if (home && home[0]) r.push_back(std::string(home) + "/.local/share/tu4");
#endif
    r.push_back("/usr/share/tu4");
    r.push_back("/usr/local/share/tu4");
#endif
    return r;
}

static bool fileExists(const std::string &p) {
    FILE *f = std::fopen(p.c_str(), "rb");
    if (f) { std::fclose(f); return true; }
    return false;
}

static std::string lower(std::string s) {
    for (char &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Candidate zip filenames (subset of u4file.cpp's list, case variants covered
// by trying a few common spellings).
static const char *kZipNames[] = {
    "ultima4.zip", "Ultima4.zip", "ULTIMA4.zip", "u4.zip", "U4.zip", nullptr
};

// Detect the internal folder prefix of a zip by probing for charset.ega.
static bool zipPrefixFor(const std::string &zipPath, std::string &prefixOut) {
    unzFile z = unzOpen(zipPath.c_str());
    if (!z) return false;
    const char *prefixes[] = { "", "ultima4/", "Ultima4/", "ULTIMA4/", "u4/", "U4/", nullptr };
    bool found = false;
    for (int i = 0; prefixes[i]; ++i) {
        std::string probe = std::string(prefixes[i]) + "charset.ega";
        if (unzLocateFile(z, probe.c_str(), 2) == UNZ_OK) {  // 2 = case-insensitive
            prefixOut = prefixes[i]; found = true; break;
        }
    }
    unzClose(z);
    return found;
}

bool DataSource::open(const char *explicitPath) {
    // 1. Explicit path: a .zip or a directory.
    if (explicitPath && explicitPath[0]) {
        std::string p = explicitPath;
        if (lower(p).size() >= 4 && lower(p).substr(lower(p).size()-4) == ".zip") {
            if (fileExists(p) && zipPrefixFor(p, zipPrefix)) {
                zip = true; zipPath = p; loc = p + " (zip)"; return true;
            }
            return false;
        }
        // directory
        if (fileExists(p + "/SHAPES.EGA") || fileExists(p + "/shapes.ega")) {
            zip = false; dir = p; loc = p; return true;
        }
        return false;
    }

    // 2. Auto-search: for each root x subdir, prefer a loose dir, else a zip.
    const char *subdirs[] = { ".", "u4", "ultima4", nullptr };
    for (const std::string &root : roots()) {
        for (int s = 0; subdirs[s]; ++s) {
            std::string d = root + "/" + subdirs[s];
            if (fileExists(d + "/SHAPES.EGA") || fileExists(d + "/shapes.ega")) {
                zip = false; dir = d; loc = d; return true;
            }
        }
        // zip candidates directly under the root
        for (int zi = 0; kZipNames[zi]; ++zi) {
            std::string zp = root + "/" + kZipNames[zi];
            if (fileExists(zp) && zipPrefixFor(zp, zipPrefix)) {
                zip = true; zipPath = zp; loc = zp + " (zip)"; return true;
            }
        }
    }
    return false;
}

bool DataSource::read(const std::string &name, std::vector<uint8_t> &out) const {
    if (!zip) {
        // loose dir: try exact then lowercase filename
        for (const std::string &fn : { name, lower(name) }) {
            std::string p = dir + "/" + fn;
            FILE *f = std::fopen(p.c_str(), "rb");
            if (!f) continue;
            std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
            if (n < 0) { std::fclose(f); return false; }
            out.resize((size_t)n);
            size_t got = std::fread(out.data(), 1, (size_t)n, f);
            std::fclose(f);
            return got == (size_t)n;
        }
        return false;
    }

    // zip: locate prefix+name (case-insensitive), read the member.
    unzFile z = unzOpen(zipPath.c_str());
    if (!z) return false;
    std::string member = zipPrefix + name;
    bool ok = false;
    if (unzLocateFile(z, member.c_str(), 2) == UNZ_OK &&
        unzOpenCurrentFile(z) == UNZ_OK) {
        unz_file_info info;
        if (unzGetCurrentFileInfo(z, &info, nullptr, 0, nullptr, 0, nullptr, 0) == UNZ_OK) {
            out.resize(info.uncompressed_size);
            int rd = unzReadCurrentFile(z, out.data(), (unsigned)out.size());
            ok = (rd == (int)out.size());
        }
        unzCloseCurrentFile(z);
    }
    unzClose(z);
    return ok;
}

} // namespace tu4setup
