#include "rr/m7_library.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>
#include <regex>

namespace rr {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trimSeparators(std::string s) {
    auto isSep = [](unsigned char c) { return std::isspace(c) || c == ',' || c == '_' || c == '-' || c == '.' || c == '('; };
    while (!s.empty() && isSep(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == '_' || s[i] == '-')) ++i;
    return s.substr(i);
}

bool isAudioFile(const std::filesystem::path& p) {
    const auto ext = lower(p.extension().string());
    return ext == ".wav" || ext == ".aif" || ext == ".aiff";
}

int preferenceForRate(int rate) {
    switch (rate) {
        case 48000: return 0;
        case 44100: return 1;
        case 96000: return 2;
        case 88200: return 3;
        case 0: return 4;
        default: return 5;
    }
}

} // namespace

ParsedIrName parseIrFileStem(const std::string& stemIn) {
    ParsedIrName out;
    std::string s = stemIn;
    static const std::regex rateRe(R"((^|[\s,_\-\(])(44\.1|44|48|88\.2|88|96|176\.4|176|192)\s?k(hz)?\)?(?=$|[\s,_\-\)]))",
                                   std::regex::icase);
    auto stripRate = [&]() {
        std::smatch m;
        if (std::regex_search(s, m, rateRe)) {
            const std::string v = m[2].str();
            if (v.rfind("44", 0) == 0) out.sampleRateTag = 44100;
            else if (v == "48") out.sampleRateTag = 48000;
            else if (v.rfind("88", 0) == 0) out.sampleRateTag = 88200;
            else if (v == "96") out.sampleRateTag = 96000;
            else if (v.rfind("176", 0) == 0) out.sampleRateTag = 176400;
            else if (v == "192") out.sampleRateTag = 192000;
            s = m.prefix().str() + (m[1].str().empty() ? "" : " ") + m.suffix().str();
        }
    };
    stripRate();
    s = trimSeparators(s);

    struct TagRule { std::regex re; FileChannelTag tag; };
    static const std::vector<TagRule> rules = [] {
        std::vector<TagRule> r;
        const auto flags = std::regex::icase;
        r.push_back({std::regex(R"((^|[\s,_\-\(])(m\s?(to|-to-|2|_to_)\s?s|mono\s?to\s?stereo|mono)\)?$)", flags), FileChannelTag::MonoToStereo});
        r.push_back({std::regex(R"((^|[\s,_\-\(])(s\s?(to|-to-|2|_to_)\s?s|stereo\s?to\s?stereo|stereo|true\s?stereo)\)?$)", flags), FileChannelTag::StereoToStereo});
        r.push_back({std::regex(R"((^|[\s,_\-\(])(l|left|l\s?in|in\s?l)\)?$)", flags), FileChannelTag::Left});
        r.push_back({std::regex(R"((^|[\s,_\-\(])(r|right|r\s?in|in\s?r)\)?$)", flags), FileChannelTag::Right});
        return r;
    }();
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& rule : rules) {
            std::smatch m;
            if (std::regex_search(s, m, rule.re)) {
                // Combine "S to S ... L" style tags: L/R take precedence.
                if (out.tag == FileChannelTag::None || rule.tag == FileChannelTag::Left
                    || rule.tag == FileChannelTag::Right)
                    out.tag = rule.tag;
                s = trimSeparators(m.prefix().str());
                break;
            }
        }
        stripRate();
        s = trimSeparators(s);
    }
    out.base = s.empty() ? stemIn : s;
    return out;
}

std::string IrProgram::displayName() const {
    if (number > 0) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%02d ", number);
        return buf + name;
    }
    return name;
}

std::vector<IrProgram> groupIrFiles(const std::vector<std::string>& paths) {
    struct FileEntry { std::string path; ParsedIrName parsed; };
    std::map<std::string, std::vector<FileEntry>> groups;
    std::map<std::string, std::pair<std::string, std::string>> groupInfo; // key -> (parent, base)
    for (const auto& p : paths) {
        const std::filesystem::path fp(p);
        if (!isAudioFile(fp)) continue;
        auto parsed = parseIrFileStem(fp.stem().string());
        const std::string parent = fp.parent_path().filename().string();
        const std::string key = lower(fp.parent_path().string() + "/" + parsed.base);
        groups[key].push_back({p, parsed});
        groupInfo[key] = {parent, parsed.base};
    }

    static const std::regex catRe(R"(^\s*(\d+)\s*[-_.]?\s*([A-Za-z][A-Za-z &'/]*?)\s+(\d{1,3})\s*[-_.]?\s*(.+)$)");
    std::vector<IrProgram> programs;
    for (auto& [key, files] : groups) {
        // Choose the preferred sample rate first.
        int bestPref = 99;
        for (const auto& f : files) bestPref = std::min(bestPref, preferenceForRate(f.parsed.sampleRateTag));
        std::vector<FileEntry> chosen;
        for (const auto& f : files)
            if (preferenceForRate(f.parsed.sampleRateTag) == bestPref) chosen.push_back(f);

        IrProgram prog;
        prog.key = key;
        prog.sampleRateTag = chosen.front().parsed.sampleRateTag;
        auto find = [&](FileChannelTag t) -> const FileEntry* {
            for (const auto& f : chosen) if (f.parsed.tag == t) return &f;
            return nullptr;
        };
        const auto* left = find(FileChannelTag::Left);
        const auto* right = find(FileChannelTag::Right);
        if (left && right) {
            prog.format = IrFormat::TrueStereo;
            prog.files = {left->path, right->path};
        } else if (const auto* s2s = find(FileChannelTag::StereoToStereo)) {
            prog.format = IrFormat::Stereo;
            prog.files = {s2s->path};
        } else if (const auto* plain = find(FileChannelTag::None)) {
            prog.format = IrFormat::Stereo;
            prog.files = {plain->path};
        } else if (const auto* m2s = find(FileChannelTag::MonoToStereo)) {
            prog.format = IrFormat::MonoToStereo;
            prog.files = {m2s->path};
        } else {
            prog.format = IrFormat::Stereo;
            prog.files = {chosen.front().path};
        }

        const auto& [parent, base] = groupInfo[key];
        std::smatch m;
        if (std::regex_match(base, m, catRe)) {
            prog.categoryNumber = std::stoi(m[1].str());
            prog.category = trimSeparators(m[2].str());
            prog.number = std::stoi(m[3].str());
            prog.name = trimSeparators(m[4].str());
            // Folder-independent key so presets survive moving the library.
            char num[8];
            std::snprintf(num, sizeof num, "%02d", prog.number);
            prog.key = lower(prog.category + "/" + num + " " + prog.name);
        } else {
            prog.category = parent.empty() ? "Other" : parent;
            prog.name = base;
        }
        programs.push_back(std::move(prog));
    }
    // The same program may exist in several sub-folders (44.1/48 kHz, quad vs
    // L/R files). Keep one per key: true stereo first, then the preferred rate.
    std::stable_sort(programs.begin(), programs.end(), [](const IrProgram& a, const IrProgram& b) {
        if (a.key != b.key) return a.key < b.key;
        const int fa = a.format == IrFormat::TrueStereo ? 0 : 1, fb = b.format == IrFormat::TrueStereo ? 0 : 1;
        if (fa != fb) return fa < fb;
        if (a.files.size() != b.files.size()) return a.files.size() < b.files.size(); // single quad file first
        return preferenceForRate(a.sampleRateTag) < preferenceForRate(b.sampleRateTag);
    });
    programs.erase(std::unique(programs.begin(), programs.end(),
                               [](const IrProgram& a, const IrProgram& b) { return a.key == b.key; }),
                   programs.end());
    std::sort(programs.begin(), programs.end(), [](const IrProgram& a, const IrProgram& b) {
        if (a.categoryNumber != b.categoryNumber) return a.categoryNumber < b.categoryNumber;
        if (a.category != b.category) return a.category < b.category;
        if (a.number != b.number) return a.number < b.number;
        return a.name < b.name;
    });
    return programs;
}

std::vector<IrProgram> scanIrLibrary(const std::string& root) {
    std::vector<std::string> paths;
    std::error_code ec;
    const std::filesystem::path rootPath(root);
    if (!std::filesystem::is_directory(rootPath, ec)) return {};
    for (auto it = std::filesystem::recursive_directory_iterator(
             rootPath, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec) && isAudioFile(it->path())) paths.push_back(it->path().string());
        if (paths.size() > 5000) break;
    }
    return groupIrFiles(paths);
}

} // namespace rr
