#define NOMINMAX
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <regex>
#include <limits>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <windows.h>

namespace fs = std::filesystem;

static bool g_mapCsvMode = false;

// Classification rule for mapping folder + extension to a type
struct Rule {
    std::string folder;
    std::regex extensionPattern;
    std::string type;
    bool forceFullPath;
};

static const std::vector<Rule> rules = {
    { "xsurface",        std::regex(R"(\.xsb$)",                std::regex::icase), "xmodelsurfs",    false },
    { "xmodel",          std::regex(R"(\.xmb$)",                std::regex::icase), "xmodel",         false },
    { "xanime",          std::regex(R"(\.xab$)",                std::regex::icase), "xanim",          false },
    { "weapons",         std::regex(R"(\.json$)",               std::regex::icase), "weapon",         false },
    { "vision",          std::regex(R"(\.vision$)",             std::regex::icase), "rawfile",        false },
    { "vehicles",        std::regex(R"(\.json$)",               std::regex::icase), "vehicle",        false },
    { "tracer",          std::regex(R"(^[^.]+$)",               std::regex::icase), "tracer",         false },
    { "techsets",        std::regex(R"(\.(cbi|cbt)$)",          std::regex::icase), "material",       false },
    { "techsets/ps",     std::regex(R"(\.(hlsl_h2|cso)$)",      std::regex::icase), "pixelshader",    false },
    { "techsets/vs",     std::regex(R"(\.(hlsl_h2|cso)$)",      std::regex::icase), "vertexshader",   false },
    { "sounds",          std::regex(R"(\.json$)",               std::regex::icase), "sound",          false },
    { "sndcurve",        std::regex(R"(\.json$)",               std::regex::icase), "sndcurve",       false },
    { "sndcontext",      std::regex(R"(^[^.]+$)",               std::regex::icase), "sndcontext",     false },
    { "rumble",          std::regex(R"(^[^.]+$)",               std::regex::icase), "rawfile",        true  },
    { "reverbsendcurve", std::regex(R"(\.json$)",               std::regex::icase), "sndcurve",       false },
    { "physpreset",      std::regex(R"(\.pp$)",                 std::regex::icase), "physpreset",     false },
    { "physcollmap",     std::regex(R"(\.pc$)",                 std::regex::icase), "phys_collmap",   false },
    { "materials",       std::regex(R"(\.json$)",               std::regex::icase), "material",       false },
    { "lpfcurve",        std::regex(R"(\.json$)",               std::regex::icase), "lpfcurve",       false },
    { "loaded_sound",    std::regex(R"(\.(flac|wav|mp3)$)",     std::regex::icase), "loaded_sound",   false },
    { "images",          std::regex(R"(\.(h1Image|tga|dds)$)",   std::regex::icase), "image",         false },
    { "effects",         std::regex(R"(\.fxe$)",                std::regex::icase), "fx",             false },
    { "aim_assist",      std::regex(R"(\.graph$)",              std::regex::icase), "rawfile",        false },
    { "animtrees",       std::regex(R"(\.atr$)",                std::regex::icase), "rawfile",        false },
    { "attachments",     std::regex(R"(\.json$)",               std::regex::icase), "attachment",     false },
    { "info",            std::regex(R"(^[^.]+$)",               std::regex::icase), "rawfile",        false },
    { "maps",            std::regex(R"(\.(gsc|gscbin)$)",       std::regex::icase), "scriptfile",     false },
    { "mp",              std::regex(R"(\.(script|cfg|txt|recipe)$)", std::regex::icase), "rawfile",   false },
    { "netconststrings", std::regex(R"(\.json$)",               std::regex::icase), "netconststrings",false },
    { "skeletonscript",  std::regex(R"(^[^.]+$)",               std::regex::icase), "skeletonscript", false },
    { "transient",       std::regex(R"(\.asslist$)",            std::regex::icase), "rawfile",        false },
    { "ui",              std::regex(R"(\.lua$)",                std::regex::icase), "luafile",        false },
    { "ui_mp",           std::regex(R"(\.txt$)",                std::regex::icase), "menufile",       false },
    { "localizedstrings",std::regex(R"(^[^.]+$)",               std::regex::icase), "localize",       false },
};

static bool should_skip_file(const fs::path& file)
{
    return file.extension() == ".d3dbsp";
}

static bool is_valid_map_file(const fs::path& file)
{
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

    if (ext == ".gsc" || ext == ".fxe" || ext == ".xmb" || ext == ".xsb") {
        return true;
    }
    if (ext == ".json" && file.parent_path().filename() == "sounds") {
        return true;
    }
    return false;
}

std::string classify_and_format(const fs::path& baseDir, const fs::path& filePath)
{
    fs::path relPath = fs::relative(filePath, baseDir);
    std::string rel = relPath.generic_string();
    size_t depth = std::count(rel.begin(), rel.end(), '/');

    // derive stem and ext
    std::string stem = filePath.stem().generic_string();
    std::string ext = filePath.extension().generic_string();
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    //in map CSV mode keep full rel path+ext for .gsc under maps folder
    if (g_mapCsvMode && depth >= 1 && rel.rfind("maps/", 0) == 0 && ext == "gsc") {
        return "rawfile," + rel;
    }

    // root files
    if (depth == 0) {
        std::string type;
        if (ext == "csv")                type = "stringtable";
        else if (ext == "gsc" || ext == "lua") type = "rawfile";
        else if (ext == "gscbin")        type = "scriptfile";
        else                              type = "rawfile";
        return type + "," + stem;
    }

    // Multi segment prefix match
    for (const auto& rule : rules) {
        std::string prefix = rule.folder + "/";
        if (rel.rfind(prefix, 0) == 0 &&
            std::regex_search(filePath.filename().string(), rule.extensionPattern))
        {
            std::string outPath;
            if (rule.forceFullPath || depth >= 2)
                outPath = relPath.parent_path().generic_string() + "/" + stem;
            else
                outPath = stem;

            // Strip prefix if safe
            if (outPath.rfind(prefix, 0) == 0 && outPath.size() > prefix.size())
                outPath = outPath.substr(prefix.size());

            return rule.type + "," + outPath;
        }
    }

    // Fallback
    size_t pos = rel.find_last_of('.');
    std::string noExt = (pos != std::string::npos) ? rel.substr(0, pos) : rel;
    return "rawfile," + noExt;
}

int main()
{
    // Set up console
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    const WORD defaultColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    const WORD headerColor = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    const WORD errorColor = FOREGROUND_RED | FOREGROUND_INTENSITY;
    const WORD infoColor = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    const WORD promptColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;

    system("cls");
    SetConsoleTextAttribute(hConsole, headerColor);
    std::cout << "========================================\n";
    std::cout << "   CSV Generator by LalisaTM for the    \n";
    std::cout << "        HorizonMW Dev Team              \n";
    std::cout << "========================================\n\n";
    SetConsoleTextAttribute(hConsole, defaultColor);

    bool firstRun = true;
    while (true) {
        if (!firstRun) {
            // only clear on subsequent loops
            system("cls");
        }
        firstRun = false;

        // Verify working directory
        fs::path cwd = fs::current_path();
        if (cwd.filename().string() != "zonetool") {
            SetConsoleTextAttribute(hConsole, errorColor);
            std::cerr << "Error: executable must be in 'zonetool'.\n";
            SetConsoleTextAttribute(hConsole, promptColor);
            std::cout << "Press Enter to exit...";
            SetConsoleTextAttribute(hConsole, defaultColor);
            std::cin.get();
            return 1;
        }

        int csvType = 0;
        while (true) {
            SetConsoleTextAttribute(hConsole, infoColor);
            std::cout << "Select CSV Type:\n";
            SetConsoleTextAttribute(hConsole, defaultColor);
            std::cout << "  [1] Map CSV\n";
            std::cout << "  [2] Normal CSV\n";
            SetConsoleTextAttribute(hConsole, promptColor);
            std::cout << "Enter number: ";
            SetConsoleTextAttribute(hConsole, defaultColor);
            if ((std::cin >> csvType) && (csvType == 1 || csvType == 2))
                break;
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        // set global flag for map CSV
        g_mapCsvMode = (csvType == 1);

        std::vector<fs::path> dirs;
        for (auto& entry : fs::directory_iterator(cwd)) {
            if (fs::is_directory(entry)) {
                std::string name = entry.path().filename().string();
                if (csvType == 1 && name.rfind("mp_", 0) == 0) {
                    dirs.push_back(entry.path());
                }
                else if (csvType == 2) {
                    dirs.push_back(entry.path());
                }
            }
        }

        if (dirs.empty()) {
            SetConsoleTextAttribute(hConsole, errorColor);
            std::cerr << "No matching subdirectories in 'zonetool'.\n";
            SetConsoleTextAttribute(hConsole, promptColor);
            std::cout << "Press Enter to exit...";
            SetConsoleTextAttribute(hConsole, defaultColor);
            std::cin.get();
            return 1;
        }

        // Prompt for folder
        SetConsoleTextAttribute(hConsole, infoColor);
        std::cout << "Select a folder:\n";
        SetConsoleTextAttribute(hConsole, defaultColor);
        for (size_t i = 0; i < dirs.size(); ++i)
            std::cout << "  [" << (i + 1) << "] " << dirs[i].filename().string() << "\n";

        size_t choice = 0;
        while (true) {
            SetConsoleTextAttribute(hConsole, promptColor);
            std::cout << "Enter number: ";
            SetConsoleTextAttribute(hConsole, defaultColor);
            if ((std::cin >> choice) && choice >= 1 && choice <= dirs.size())
                break;
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        // Gather files
        fs::path selected = dirs[choice - 1];
        bool skipTechsets = false;
        if (fs::exists(selected / "techsets")) {
            SetConsoleTextAttribute(hConsole, promptColor);
            std::cout << "Skip 'techsets' folder? (Y/N): ";
            SetConsoleTextAttribute(hConsole, defaultColor);
            char skip;
            std::cin >> skip;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            if (skip == 'Y' || skip == 'y') {
                skipTechsets = true;
            }
        }

        std::vector<fs::path> files;
        for (auto& p : fs::recursive_directory_iterator(selected)) {
            if (fs::is_regular_file(p)) {
                if (skipTechsets) {
                    fs::path relative = fs::relative(p.path(), selected);
                    for (auto& part : relative) {
                        if (part == "techsets") {
                            goto skip_this_file;
                        }
                    }
                }
                files.push_back(p.path());
            skip_this_file:;
            }
        }

        files.erase(
            std::remove_if(files.begin(), files.end(), should_skip_file),
            files.end()
        );

        // Open CSV
        if (csvType == 1) {
            files.erase(
                std::remove_if(files.begin(), files.end(), [](const fs::path& file) {
                    return !is_valid_map_file(file);
                    }),
                files.end()
            );
        }

        std::string csvName = selected.filename().string() + ".csv";
        std::ofstream out(csvName);
        if (!out.is_open()) {
            SetConsoleTextAttribute(hConsole, errorColor);
            std::cerr << "Failed to open " << csvName << " for writing.\n";
            SetConsoleTextAttribute(hConsole, promptColor);
            std::cout << "Press Enter to exit...";
            SetConsoleTextAttribute(hConsole, defaultColor);
            std::cin.get();
            return 1;
        }

        // Generate CSV
        std::unordered_map<std::string, size_t> counts;
        SetConsoleTextAttribute(hConsole, infoColor);
        std::cout << "Generating '" << csvName << "' (" << files.size() << " entries)...\n";
        SetConsoleTextAttribute(hConsole, defaultColor);

        for (size_t i = 0; i < files.size(); ++i) {
            std::string line = classify_and_format(selected, files[i]);
            auto commaPos = line.find(',');
            std::string typ = (commaPos != std::string::npos) ? line.substr(0, commaPos) : line;
            ++counts[typ];

            std::cout << "[" << (i + 1) << "/" << files.size() << "] " << line << "\r" << std::flush;
            out << line << "\n";
        }
        out.close();

        // Done
        system("cls");
        SetConsoleTextAttribute(hConsole, infoColor);
        std::cout << "[OK] CSV generated: " << csvName << "\n\n";
        SetConsoleTextAttribute(hConsole, defaultColor);

        // Summary
        SetConsoleTextAttribute(hConsole, infoColor);
        std::cout << "Summary of types:\n";
        SetConsoleTextAttribute(hConsole, defaultColor);
        for (auto& p : counts)
            std::cout << "  " << p.first << ": " << p.second << "\n";

        // Repeat?
        SetConsoleTextAttribute(hConsole, promptColor);
        std::cout << "\nGenerate another CSV? (Y/N): ";
        SetConsoleTextAttribute(hConsole, defaultColor);
        char again;
        std::cin >> again;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        if (again == 'Y' || again == 'y')
            continue;
        break;
    }

    return 0;
}
