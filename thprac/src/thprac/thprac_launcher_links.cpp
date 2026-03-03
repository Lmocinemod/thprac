#include "thprac_gui_locale.h"
#include "thprac_launcher_links.h"
#include "thprac_locale_def.h"
#include "thprac_utils.h"
#include "utils/utils.h"

#include <imgui.h>
#include <string.h>
#include <yyjson.h>

#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace THPrac {
// TODO: Move these to some other file (thprac_launcher_utils?)
namespace Utils {
// TODO: Maybe it'd be better for thprac to keep its launcher config document in memory,
// and then have this function return only a reference to it? That way we're not needlessly
// re-parsing the same JSON file over and over.
static yyjson_mut_doc* LoadLauncherCfg() {
    // TODO: Actually implement this function
    // TODO: This function must never return on failure
    return yyjson_mut_doc_new(nullptr);
}
static void SaveLauncherCfg(yyjson_mut_doc* doc) {
    // TODO: Actually implement this function
    [[maybe_unused]] auto silence_warning = doc;
}
std::optional<int> GetLauncherSetting(const char* name) {
    // TODO: Actually implement this function
    [[maybe_unused]] auto silence_warning = name;
    return std::optional(0);
}
std::string_view GetSuffixFromPath(const char* path_c_str) {
    auto path = std::string_view(path_c_str);
    size_t pos = path.rfind('.');
    if (pos == std::string_view::npos) {
        return std::string_view("");
    }
    return path.substr(pos + 1);
}
std::string GetUnifiedPath(std::string const& path) {
    std::string result;
    char last_char = '\0';
    for (auto const& c : path) {
        if (c == '/' || c == '\\') {
            if (last_char == '\\') {
                continue;
            }
            result.push_back('\\');
            last_char = '\\';
        } else {
            char lower = static_cast<char>(tolower(c));
            result.push_back(lower);
            last_char = lower;
        }
    }
    return result;
}
std::wstring GetDirFromFullPath(std::wstring const& dir) {
    size_t last_slash = dir.rfind(L'\\');
    if (last_slash == std::wstring::npos) {
        // To be safe, check for forward slashes too.
        last_slash = dir.rfind(L'/');
    }
    if (last_slash == std::wstring::npos) {
        return dir;
    }
    return dir.substr(0, last_slash + 1);
}
std::wstring LauncherWndFileSelect(const wchar_t* title = L"Browse for file...", const wchar_t* filter = nullptr) {
    // TODO: Actually implement this function
    [[maybe_unused]] auto silence_warning_1 = title;
    [[maybe_unused]] auto silence_warning_2 = filter;
    return std::wstring();
}
std::wstring LauncherWndFolderSelect(const wchar_t* title = L"Browse for folder...") {
    // TODO: Actually implement this function
    [[maybe_unused]] auto silence_warning = title;
    return std::wstring();
}
// TODO: Probably return an enum instead?
int GuiCornerButton(const char* text, const char* text_2, const ImVec2& offset, bool use_current_y) {
    // TODO: Actually implement this function
    [[maybe_unused]] auto silence_warning_1 = text;
    [[maybe_unused]] auto silence_warning_2 = text_2;
    [[maybe_unused]] auto silence_warning_3 = offset;
    [[maybe_unused]] auto silence_warning_4 = use_current_y;
    return 0;
}
} // namespace Utils

struct BaseLinkSelectable {};

struct LinkLeaf : BaseLinkSelectable {
    std::string name;
    std::string link;
};

struct LinkEntry : BaseLinkSelectable {
    std::string name;
    std::vector<LinkLeaf> leaves;
    bool is_open = false;
};

enum class UiTrigger {
    TRIGGER_ERROR,
    ADD_LINK,
    EDIT_LINK,
    DELETE_LINK,
    ADD_FILTER,
    DELETE_FILTER,
    ERR_REPEATED,
    ERR_EXEC,
};

enum class LinkType {
    Normal,
    FilePath,
    FolderPath,
};

enum class LinkError {
    Ok,
    MissingName,
    MissingLink,
    DuplicateName,
    ReservedName,
};

#pragma region GlobalMutableState

constexpr size_t INPUT_CHARS_MAX = 1024;
struct THLinksState {
    bool page_initialized = false;
    std::vector<LinkEntry> links;
    UiTrigger ui_trigger = UiTrigger::TRIGGER_ERROR;
    BaseLinkSelectable* current_selection = nullptr;
    size_t current_link = 0;
    size_t current_leaf = 0;
    // TODO: These aren't properly bounds-checked in several cases. Maybe use strings instead?
    char input_name[INPUT_CHARS_MAX];
    char input_link[INPUT_CHARS_MAX];
    char input_link_parameter[INPUT_CHARS_MAX];
    LinkType input_type = LinkType::Normal;
    LinkError input_error = LinkError::Ok;
};
static THLinksState state;

#pragma endregion // GlobalMutableState

// TODO: Probably handle errors more gracefully... or something?
[[noreturn]] void PanicOutOfMemory() {
    fputs("Out of memory (probably)", stderr);
    exit(EXIT_FAILURE);
}

static void ReplaceLinksInLauncherCfg(yyjson_mut_val* links_val) {
    auto cfg_doc = Utils::LoadLauncherCfg();
    defer(yyjson_mut_doc_free(cfg_doc));
    auto cfg = yyjson_mut_doc_get_root(cfg_doc);

    auto cfg_links = yyjson_mut_obj_get(cfg, "links");
    if (cfg_links) {
        yyjson_mut_obj_remove(cfg, cfg_links);
    }
    yyjson_mut_obj_add_val(cfg_doc, cfg, "links", links_val);
    Utils::SaveLauncherCfg(cfg_doc);
}

static void WriteLinksToLauncherCfg() {
    auto links_doc = yyjson_mut_doc_new(nullptr);
    if (links_doc == nullptr) {
        PanicOutOfMemory();
    }
    defer(yyjson_mut_doc_free(links_doc));
    auto links_obj = yyjson_mut_obj(links_doc);
    yyjson_mut_doc_set_root(links_doc, links_obj);

    for (auto const& current_link : state.links) {
        auto current_link_obj = yyjson_mut_obj(links_doc);
        yyjson_mut_obj_add_bool(links_doc, current_link_obj, "__is_open__", current_link.is_open);
        for (auto const& leaf : current_link.leaves) {
            yyjson_mut_obj_add_str(
                links_doc,
                current_link_obj,
                leaf.name.c_str(),
                leaf.link.c_str()
            );
        }

        yyjson_mut_obj_add_val(links_doc, links_obj, current_link.name.c_str(), current_link_obj);
    }

    ReplaceLinksInLauncherCfg(links_obj);
}

constexpr auto DEFAULT_LINKS_JSON_STR = std::string_view(R"123({
    "Default":{
        "__is_open__" : true,
        "Royalflare Archive":"https://maribelhearn.com/royalflare",
        "Lunarcast":"http://replay.lunarcast.net/",
        "PND's Scoreboard":"https://thscore.pndsng.com/index.php",
        "Maribel Hearn's Touhou Portal":"https://maribelhearn.com/",
        "Touhou Patch Center":"https://www.thpatch.net/",
        "Touhou Replay Showcase":"https://twitch.tv/touhou_replay_showcase",
        "甜品站 (isndes)":"https://www.isndes.com/",
        "THBWiki":"https://thwiki.cc/"
    }
})123");
static void WriteDefaultLinksToLauncherCfg() {
    auto links_doc_immutable = yyjson_read(
        DEFAULT_LINKS_JSON_STR.data(),
        DEFAULT_LINKS_JSON_STR.length(),
        YYJSON_READ_NOFLAG
    );
    if (links_doc_immutable == nullptr) {
        PanicOutOfMemory();
    }
    defer(yyjson_doc_free(links_doc_immutable));
    auto links_doc = yyjson_doc_mut_copy(links_doc_immutable, nullptr);
    if (links_doc == nullptr) {
        PanicOutOfMemory();
    }
    defer(yyjson_mut_doc_free(links_doc));
    auto links = yyjson_mut_doc_get_root(links_doc);

    ReplaceLinksInLauncherCfg(links);
}

static void LoadLinksFromLauncherCfg() {
    // TODO: Turn this magic number into an enum if possible.
    int filter_state = Utils::GetLauncherSetting("filter_default").value();

    auto cfg_doc = Utils::LoadLauncherCfg();
    defer(yyjson_mut_doc_free(cfg_doc));
    auto cfg = yyjson_mut_doc_get_root(cfg_doc);

    auto links_obj = yyjson_mut_obj_get(cfg, "links");
    if (links_obj == nullptr) {
        // cfg["links"] either doesn't exist or is not an object.
        WriteDefaultLinksToLauncherCfg();
        return;
    }

    size_t idx1, max1;
    yyjson_mut_val* link_k;
    yyjson_mut_val* link_v;
    yyjson_mut_obj_foreach(links_obj, idx1, max1, link_k, link_v) {
        if (yyjson_mut_get_type(link_v) != YYJSON_TYPE_OBJ) {
            continue;
        }
        LinkEntry link = {};
        link.name = yyjson_mut_get_str(link_k);

        size_t idx2, max2;
        yyjson_mut_val* leaf_k;
        yyjson_mut_val* leaf_v;
        yyjson_mut_obj_foreach(link_v, idx2, max2, leaf_k, leaf_v) {
            if (strcmp(yyjson_mut_get_str(leaf_k), "__is_open__") == 0) {
                link.is_open = yyjson_mut_get_bool(leaf_v);
                continue;
            }

            LinkLeaf leaf = {};
            leaf.name = yyjson_mut_get_str(leaf_k);
            // TODO: This will silently produce an empty string for non-string value types.
            // Is that what we want?
            leaf.link = yyjson_mut_get_str(leaf_v);
        }

        if (filter_state == 1) {
            link.is_open = true;
        } else if (filter_state == 2){
            link.is_open = false;
        }

        state.links.push_back(link);
    }
}

static bool LinkIsExecutable(const char* link, LinkType type) {
    // TODO: Would it be better to check against everything in PATHEXT?
    return type == LinkType::FilePath && Utils::GetSuffixFromPath(link) == "exe";
}

static std::string WrapLink(const char* link, const char* parameters, LinkType type) {
    if (!LinkIsExecutable(link, type)) {
        return std::string(link);
    }
    return std::format("\"{}\" {}", link, parameters);
}

struct LinkResolution {
    LinkType type;
    std::string link;
    std::string parameters;
};
static LinkResolution ResolveLink(std::string const& link) {
    LinkResolution result = {};
    size_t first_quote = link.find('"');
    size_t last_quote = link.rfind('"');
    if (
        first_quote != std::string::npos
        && last_quote != std::string::npos
        && first_quote != last_quote
    ) {
        result.link = link.substr(first_quote + 1, last_quote - first_quote - 1);
        result.parameters = link.substr(last_quote + 1);
        while (result.parameters.size() > 0 && isblank(result.parameters[0])) {
            result.parameters.erase(0, 1);
        }
        result.type = LinkType::FilePath;
    } else {
        result.link = link;
        result.parameters = "";
        auto unified = Utils::GetUnifiedPath(link);
        if (isalpha(unified[0]) && unified[1] == ':' && unified[2] == '\\') {
            result.type = LinkType::FolderPath;
        } else {
            result.type = LinkType::Normal;
        }
    }
    return result;
}

/** Returns `true` on success, `false` otherwise. */
static bool ExecuteLink(std::string const& link) {
    auto link_info = ResolveLink(link);
    auto link_exec_w = utf8_to_utf16(link_info.link.c_str());
    auto link_parameters_w = utf8_to_utf16(link_info.parameters.c_str());
    HINSTANCE result;
    std::wstring link_directory_w;

    switch (link_info.type) {
    case LinkType::FilePath:
        link_directory_w = Utils::GetDirFromFullPath(link_exec_w);
        result = ShellExecuteW(
            nullptr,
            L"open",
            link_exec_w.c_str(),
            link_parameters_w.c_str(),
            link_directory_w.c_str(),
            SW_SHOW
        );
        break;
    case LinkType::Normal:
    case LinkType::FolderPath:
        result = ShellExecuteW(nullptr, nullptr, link_exec_w.c_str(), nullptr, nullptr, SW_SHOW);
        break;
    default:
        // Should be unreachable
        return false;
    }

    return ((DWORD)result > 32);
}

namespace Ui {
static void EditPopupClear() {
    state.input_link[0] = state.input_name[0] = state.input_link_parameter[0] = '\0';
    state.input_type = LinkType::Normal;
    state.input_error = LinkError::Ok;
}

static void EditPopupShowErrorIfApplicable(LinkError error) {
    th_glossary_t error_message = A0000ERROR_C;
    switch (error) {
    case LinkError::MissingName:
        error_message = THPRAC_LINKS_EDIT_ERR_NAME;
        break;
    case LinkError::MissingLink:
        error_message = THPRAC_LINKS_EDIT_ERR_LINK;
    case LinkError::DuplicateName:
        error_message = THPRAC_LINKS_EDIT_ERR_REPEATED;
    case LinkError::ReservedName:
        error_message = THPRAC_LINKS_EDIT_ERR_RSV;
    default:
        return;
    }

    auto red = ImVec4(255.0f, 0.0f, 0.0f, 255.0f);
    ImGui::TextColored(red, "%s", S(error_message));
}

// TODO: Probably return a bool or enum instead?
static int EditPopupMain() {
    ImGui::TextUnformatted(S(THPRAC_LINKS_EDIT_NAME));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##__link_name_input", state.input_name, INPUT_CHARS_MAX)) {
        if (
            state.input_error == LinkError::MissingName
            || state.input_error == LinkError::DuplicateName
        ) {
            state.input_error = LinkError::Ok;
        }
    }

    ImGui::TextUnformatted(S(THPRAC_LINKS_EDIT_LINK));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (state.input_type == LinkType::Normal) {
        if (ImGui::InputText("##__link_input", state.input_link, INPUT_CHARS_MAX)) {
            if (state.input_error == LinkError::MissingLink) {
                state.input_error = LinkError::Ok;
            }
        }
    } else {
        ImGui::TextUnformatted(state.input_link);
    }
    if (LinkIsExecutable(state.input_link, state.input_type)) {
        ImGui::TextUnformatted(S(THPRAC_LINKS_EDIT_PARAM));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##__link_parameter_input", state.input_link_parameter, INPUT_CHARS_MAX);
    }

    EditPopupShowErrorIfApplicable(state.input_error);

    if (ImGui::Button(S(THPRAC_LINKS_EDIT_FILE))) {
        auto file_str = Utils::LauncherWndFileSelect();
        if (file_str.length() > 0) {
            state.input_error = LinkError::Ok;
            state.input_type = LinkType::FilePath;
            state.input_link_parameter[0] = '\0';
            sprintf_s(state.input_link, "%s", utf16_to_utf8(file_str.c_str()).c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(S(THPRAC_LINKS_EDIT_FOLDER))) {
        auto folder_str = Utils::LauncherWndFolderSelect();
        if (folder_str.length() > 0) {
            state.input_error = LinkError::Ok;
            state.input_type = LinkType::FolderPath;
            sprintf_s(state.input_link, "%s", utf16_to_utf8(folder_str.c_str()).c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(S(THPRAC_LINKS_EDIT_INPUT))) {
        state.input_error = LinkError::Ok;
        state.input_link[0] = '\0';
        state.input_type = LinkType::Normal;
    }
    ImGui::SameLine();

    // Another magic number...
    auto result = Utils::GuiCornerButton(S(THPRAC_OK), S(THPRAC_CANCEL), ImVec2(1.0f, 0.0f), true);
    if (result == 1) {
        if (strnlen_s(state.input_name, INPUT_CHARS_MAX) == 0) {
            state.input_error = LinkError::MissingName;
        } else if (strnlen_s(state.input_link, INPUT_CHARS_MAX)) {
            state.input_error = LinkError::MissingLink;
        } else if (strcmp(state.input_name, "__is_open__") == 0) {
            state.input_error = LinkError::ReservedName;
        } else {
            for (auto const& leaf : state.links[state.current_link].leaves) {
                if (leaf.name == state.input_name) {
                    state.input_error = LinkError::DuplicateName;
                    break;
                }
            }
        }
        if (state.input_error != LinkError::Ok) {
            result = 0;
        }
    }
    return result;
}

static void ContextMenuUpdate() {
    // TODO: Actually implement this function
}

static bool PopupContextMenuMain(int type) {
    // TODO: Actually implement this function
    [[maybe_unused]] auto silence_warning = type;
    return false;
}
} // namespace Ui

void LauncherLinksUiUpdate() {
    // TODO: Actually implement this function
    if (!state.page_initialized) [[unlikely]] {
        LoadLinksFromLauncherCfg();
        state.page_initialized = true;
    }

    ImGui::BeginChild("##links");
    ImGui::TextUnformatted("Links");
    ImGui::EndChild();
}
} // namespace THPrac
