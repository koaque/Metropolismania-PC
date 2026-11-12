// The player's settings file: display, sound and window options as plain `key = value` lines, read once at start-up (before
// main, so before anything reads them) and turned into the environment variables the runtime, the renderer and the port
// already read. A variable set in the environment wins over the file, so test runs and one-off changes still work.
//
// Where: the file named by MMPC_SETTINGS (MMPC_SETTINGS=none: no file; tools/pipeline.py sets that for its test runs and
// points `pipeline.py play` at settings.ini in the repository), else settings.ini beside the executable. A missing file
// means the defaults, the native renderer among them; test runs (MMPC_SETTINGS=none) keep the emulated one unless they
// set MMPC_RENDERER. `[section]` lines and comments (# or ;) are skipped; keys are in docs/SETTINGS.md.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// a named namespace: the stage step may build this file in one unity batch with others
namespace mmpc_settings
{
    std::string trim(const std::string &s)
    {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
            ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
            --b;
        return s.substr(a, b - a);
    }

    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    // on / off and their usual spellings; -1 for anything else
    int flag(const std::string &value)
    {
        const std::string v = lower(value);
        if (v == "on" || v == "yes" || v == "true" || v == "1")
            return 1;
        if (v == "off" || v == "no" || v == "false" || v == "0")
            return 0;
        return -1;
    }

    void setVariable(const std::string &name, const std::string &value)
    {
#ifdef _WIN32
        _putenv_s(name.c_str(), value.c_str());
#else
        setenv(name.c_str(), value.c_str(), 1);
#endif
    }

    std::filesystem::path executableFolder()
    {
#ifdef _WIN32
        char *path = nullptr;
        if (_get_pgmptr(&path) == 0 && path != nullptr && path[0] != '\0')
            return std::filesystem::path(path).parent_path();
#else
        std::error_code error;
        const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", error);
        if (!error)
            return self.parent_path();
#endif
        return std::filesystem::current_path();
    }

    struct Applied
    {
        std::vector<std::string> set, kept, unknown;
    };

    // One `key = value`: the variable(s) it stands for, unless the environment has them already.
    void apply(const std::string &key, const std::string &value, Applied &out)
    {
        const auto put = [&](const char *name, const std::string &v) {
            if (std::getenv(name) != nullptr)
                out.kept.push_back(name);
            else
            {
                setVariable(name, v);
                out.set.push_back(std::string(name) + "=" + v);
            }
        };
        const auto onOff = [&](const char *name, const char *on, const char *off) {
            const int f = flag(value);
            if (f < 0)
                out.unknown.push_back(key + " = " + value);
            else if (f == 1 ? on != nullptr : off != nullptr)
                put(name, f == 1 ? on : off);
        };
        const std::string k = lower(key), v = lower(value);
        if (k == "renderer")
        {
            if (v == "native" || v == "split" || v == "side" || v == "emulated")
                put("MMPC_RENDERER", v);
            else
                out.unknown.push_back(key + " = " + value);
        }
        else if (k == "render_scale")
            put("MMPC_RENDER_SCALE", v);
        else if (k == "msaa")
            put("MMPC_RENDER_MSAA", v);
        else if (k == "anisotropic")
            put("MMPC_RENDER_ANISO", v == "off" ? std::string("1") : v);
        else if (k == "filter_2d")
        {
            if (v == "sharp" || v == "smooth" || v == "ps2")
                put("MMPC_RENDER_FILTER_2D", v);
            else
                out.unknown.push_back(key + " = " + value);
        }
        else if (k == "widescreen")
        {
            const int f = flag(value);
            if (f == 1)
                put("MMPC_WIDESCREEN", "1");
            else if (f < 0)
                put("MMPC_WIDESCREEN", value);
        }
        else if (k == "fullscreen")
            onOff("PS2X_FULLSCREEN", "1", nullptr);
        else if (k == "window_size")
            put("PS2X_WINDOW_SIZE", v);
        else if (k == "window_position")
            put("PS2X_WINDOW_POS", v);
        else if (k == "gpu")
        {
            if (v != "auto")
                put("PS2X_GPU", v);
        }
        else if (k == "vsync")
            onOff("PS2X_VSYNC", "1", nullptr);
        else if (k == "game_rate")
        {
            if (v != "ps2")
                put("PS2X_VSYNC_HZ", v);
        }
        else if (k == "sound")
            onOff("PS2X_SPU2", nullptr, "0");
        else if (k == "skip_movies")
            onOff("MMPC_SKIP_MOVIES", "1", nullptr);
        else if (k.rfind("ps2x_", 0) == 0 || k.rfind("mmpc_", 0) == 0) // any switch by its variable's name
        {
            std::string name = key;
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            put(name.c_str(), value);
        }
        else
            out.unknown.push_back(key + " = " + value);
    }

    bool loadSettings()
    {
        std::filesystem::path path;
        if (const char *named = std::getenv("MMPC_SETTINGS"))
        {
            if (lower(named) == "none" || named[0] == '\0')
                return false;
            path = named;
        }
        else
            path = executableFolder() / "settings.ini";
        std::ifstream file(path);
        Applied applied;
        std::string line;
        while (file && std::getline(file, line))
        {
            line = trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[')
                continue;
            const size_t eq = line.find('=');
            if (eq == std::string::npos)
            {
                applied.unknown.push_back(line);
                continue;
            }
            std::string value = trim(line.substr(eq + 1));
            if (const size_t comment = value.find_first_of("#;"); comment != std::string::npos)
                value = trim(value.substr(0, comment));
            const std::string key = trim(line.substr(0, eq));
            if (!value.empty())
                apply(key, value, applied);
        }
        // The native renderer unless the file or the environment says otherwise (renderer = emulated).
        if (std::getenv("MMPC_RENDERER") == nullptr)
        {
            setVariable("MMPC_RENDERER", "native");
            applied.set.push_back("MMPC_RENDERER=native (the default)");
        }
        std::cerr << "[settings] " << path.string() << (file.is_open() ? ":" : " (none, the defaults):");
        for (const std::string &s : applied.set)
            std::cerr << " " << s;
        if (!applied.kept.empty())
        {
            std::cerr << "; kept from the environment:";
            for (const std::string &s : applied.kept)
                std::cerr << " " << s;
        }
        if (!applied.unknown.empty())
        {
            std::cerr << "; not understood:";
            for (const std::string &s : applied.unknown)
                std::cerr << " [" << s << "]";
        }
        std::cerr << std::endl;
        return true;
    }

    const bool s_loaded = loadSettings();
}
