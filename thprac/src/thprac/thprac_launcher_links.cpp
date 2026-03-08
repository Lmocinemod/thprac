#include "thprac_gui_components.h"
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
// TODO: This function in particular should go in thprac_gui_components
// TODO: Probably return an enum instead?
int GuiCornerButton(const char* text, const char* text_2, const ImVec2& offset, bool use_current_y) {
    // TODO: Actually implement this function
    // TODO: At least text_2 needs to be safely nullable
    [[maybe_unused]] auto silence_warning_1 = text;
    [[maybe_unused]] auto silence_warning_2 = text_2;
    [[maybe_unused]] auto silence_warning_3 = offset;
    [[maybe_unused]] auto silence_warning_4 = use_current_y;
    return 0;
}
// TODO: This function's name is terrible. It should also go in thprac_gui_components
inline void GuiSetPosYRel(float rel) {
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() * rel);
}
// TODO: This should also go in thprac_gui_components
void GuiColumnText(const char* text) {
    // TODO: Actually implement this function
    [[maybe_unused]] auto silence_warning = text;
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
    mutable bool is_open = false;
};

enum class UiTrigger {
    Invalid,
    AddLink,
    EditLink,
    DeleteLink,
    AddFilter,
    DeleteFilter,
    ErrorDuplicate,
    ErrorExecutable,
};

// TODO: Remove the magic number hints
enum class LinkType {
    Normal, // 0
    FilePath, // 1
    FolderPath, // 2
};

// TODO: Remove the magic number hints
enum class LinkError {
    Ok, // 0
    MissingName, // 1
    MissingLink, // 2
    DuplicateName, // 3
    ReservedName, // 4
};

#pragma region GlobalMutableState

constexpr size_t INPUT_CHARS_MAX = 1024;
struct THLinksState {
    bool page_initialized = false;
    std::vector<LinkEntry> links;
    UiTrigger ui_trigger = UiTrigger::Invalid;
    BaseLinkSelectable* selected_link = nullptr;
    size_t current_link = 0;
    size_t current_leaf = 0;
    // TODO: Don't use -1 to mean "invalid index".
    // TODO: Why do these need to be global state, anyway?
    // TODO: Apparently [0] is the index for links, and [1] for leaves?
    int move_indexes[2] = {-1, -1}; // "moveIdx"
    int filter_move_index = -1; // "filterMoveIdx"
    // TODO: These aren't properly bounds-checked in several cases. Maybe use strings instead?
    char input_link_name[INPUT_CHARS_MAX];
    char input_link[INPUT_CHARS_MAX];
    char input_link_parameters[INPUT_CHARS_MAX];
    LinkType input_link_type = LinkType::Normal;
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
            bool k_is_is_open = strcmp(yyjson_mut_get_str(leaf_k), "__is_open__") == 0;
            if (!k_is_is_open && yyjson_mut_is_str(leaf_v)) {
                LinkLeaf leaf = {
                    .name = yyjson_mut_get_str(leaf_k),
                    .link = yyjson_mut_get_str(leaf_v),
                };
                link.leaves.push_back(leaf);
            } else if (k_is_is_open && yyjson_mut_is_bool(leaf_v)) {
                link.is_open = yyjson_mut_get_bool(leaf_v);
            }
            // Ignore everything else.
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

struct LinkInfo {
    LinkType type;
    std::string link;
    std::string parameters;
};
static LinkInfo GetLinkInfo(std::string const& link) {
    LinkInfo result = {};
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
    auto link_info = GetLinkInfo(link);
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

namespace Gui {
static void ClearEditPopupState() {
    state.input_link[0] = state.input_link_name[0] = state.input_link_parameters[0] = '\0';
    state.input_link_type = LinkType::Normal;
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
        break;
    case LinkError::DuplicateName:
        error_message = THPRAC_LINKS_EDIT_ERR_REPEATED;
        break;
    case LinkError::ReservedName:
        error_message = THPRAC_LINKS_EDIT_ERR_RSV;
        break;
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
    if (ImGui::InputText("##__input_link_name", state.input_link_name, INPUT_CHARS_MAX)) {
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
    if (state.input_link_type == LinkType::Normal) {
        if (ImGui::InputText("##__input_link", state.input_link, INPUT_CHARS_MAX)) {
            if (state.input_error == LinkError::MissingLink) {
                state.input_error = LinkError::Ok;
            }
        }
    } else {
        ImGui::TextUnformatted(state.input_link);
    }
    if (LinkIsExecutable(state.input_link, state.input_link_type)) {
        ImGui::TextUnformatted(S(THPRAC_LINKS_EDIT_PARAM));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##__input_link_parameters", state.input_link_parameters, INPUT_CHARS_MAX);
    }

    EditPopupShowErrorIfApplicable(state.input_error);

    if (ImGui::Button(S(THPRAC_LINKS_EDIT_FILE))) {
        auto file_str = Utils::LauncherWndFileSelect();
        if (file_str.length() > 0) {
            state.input_error = LinkError::Ok;
            state.input_link_type = LinkType::FilePath;
            state.input_link_parameters[0] = '\0';
            sprintf_s(state.input_link, "%s", utf16_to_utf8(file_str.c_str()).c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(S(THPRAC_LINKS_EDIT_FOLDER))) {
        auto folder_str = Utils::LauncherWndFolderSelect();
        if (folder_str.length() > 0) {
            state.input_error = LinkError::Ok;
            state.input_link_type = LinkType::FolderPath;
            sprintf_s(state.input_link, "%s", utf16_to_utf8(folder_str.c_str()).c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(S(THPRAC_LINKS_EDIT_INPUT))) {
        state.input_error = LinkError::Ok;
        state.input_link[0] = '\0';
        state.input_link_type = LinkType::Normal;
    }
    ImGui::SameLine();

    // Another magic number...
    auto result = Utils::GuiCornerButton(S(THPRAC_OK), S(THPRAC_CANCEL), ImVec2(1.0f, 0.0f), true);
    if (result == 1) {
        if (strnlen_s(state.input_link_name, INPUT_CHARS_MAX) == 0) {
            state.input_error = LinkError::MissingName;
        } else if (strnlen_s(state.input_link, INPUT_CHARS_MAX)) {
            state.input_error = LinkError::MissingLink;
        } else if (strcmp(state.input_link_name, "__is_open__") == 0) {
            state.input_error = LinkError::ReservedName;
        } else {
            for (auto const& leaf : state.links[state.current_link].leaves) {
                if (leaf.name == state.input_link_name) {
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
    th_glossary_t popup_str_id = A0000ERROR_C;
    switch (state.ui_trigger) {
    case UiTrigger::AddLink:
        ClearEditPopupState();
        popup_str_id = THPRAC_LINKS_ADD;
        break;
    case UiTrigger::EditLink:
        ClearEditPopupState();
        {
            auto& leaf = state.links[state.current_link].leaves[state.current_leaf];
            auto info = GetLinkInfo(leaf.link);
            state.input_link_type = info.type;
            strcpy_s(state.input_link_name, leaf.name.c_str());
            strcpy_s(state.input_link, info.link.c_str());
            strcpy_s(state.input_link_parameters, info.parameters.c_str());
        }
        popup_str_id = THPRAC_LINKS_EDIT;
        break;
    case UiTrigger::DeleteLink:
        popup_str_id = THPRAC_LINKS_DELETE_MODAL;
        break;
    case UiTrigger::AddFilter:
        ClearEditPopupState();
        popup_str_id = THPRAC_LINKS_FILTER_ADD_MODAL;
        break;
    case UiTrigger::DeleteFilter:
        popup_str_id = THPRAC_LINKS_FILTER_DEL_MODAL;
        break;
    case UiTrigger::ErrorDuplicate:
        popup_str_id = THPRAC_LINKS_ERR_MOVE_MODAL;
        break;
    case UiTrigger::ErrorExecutable:
        popup_str_id = THPRAC_LINKS_ERR_EXEC_MODAL;
        break;
    default:
        break;
    }

    if (popup_str_id != A0000ERROR_C) {
        ImGui::OpenPopup(S(popup_str_id));
    }
    state.ui_trigger = UiTrigger::Invalid;

    // TODO: The bodies of these if statements should probably be their own functions.
    auto modal_size_rel = ImVec2(ImGui::GetIO().DisplaySize.x * 0.9f, 0.0f);
    if (Modal(S(THPRAC_LINKS_ADD), modal_size_rel)) {
        auto result = EditPopupMain();
        if (result != 0) {
            if (result == 1) {
                auto final_link = WrapLink(
                    state.input_link,
                    state.input_link_parameters,
                    state.input_link_type
                );
                LinkLeaf new_leaf = {
                    .name = state.input_link_name,
                    .link = final_link,
                };

                size_t insert_i = 0;
                if (state.selected_link != nullptr) {
                    insert_i = state.current_leaf;
                }
                auto& current_link = state.links[state.current_link];
                current_link.leaves.insert(current_link.leaves.begin() + (int)insert_i, new_leaf);
                state.current_leaf = insert_i;
                state.selected_link = nullptr;
                WriteLinksToLauncherCfg();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (Modal(S(THPRAC_LINKS_EDIT), modal_size_rel)) {
        auto result = EditPopupMain();
        if (result != 0) {
            if (result == 1) {
                auto final_link = WrapLink(
                    state.input_link,
                    state.input_link_parameters,
                    state.input_link_type
                );
                auto& leaf_to_edit = state.links[state.current_leaf].leaves[state.current_leaf];
                leaf_to_edit.name = state.input_link_name;
                leaf_to_edit.link = final_link;
                WriteLinksToLauncherCfg();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (Modal(S(THPRAC_LINKS_DELETE_MODAL) /* (Default size) */)) {
        ImGui::TextUnformatted(S(THPRAC_LINKS_DELETE_WARNING));
        if (YesNoChoice(S(THPRAC_YES), S(THPRAC_NO), 6.0f)) {
            auto& current_link = state.links[state.current_link];
            current_link.leaves.erase(current_link.leaves.begin() + (int)state.current_leaf);
            state.selected_link = nullptr;
            WriteLinksToLauncherCfg();
        }
        ImGui::EndPopup();
    }

    modal_size_rel = ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, 0.0f);
    if (Modal(S(THPRAC_LINKS_FILTER_ADD_MODAL), modal_size_rel)) {
        ImGui::TextUnformatted(S(THPRAC_LINKS_EDIT_NAME));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##__input_link_name", state.input_link_name, INPUT_CHARS_MAX)) {
            if (
                state.input_error == LinkError::MissingName
                || state.input_error == LinkError::DuplicateName
            ) {
                state.input_error = LinkError::Ok;
            }
        }
        EditPopupShowErrorIfApplicable(state.input_error);

        auto result = Utils::GuiCornerButton(
            S(THPRAC_OK),
            S(THPRAC_CANCEL),
            ImVec2(1.0f, 0.0f),
            true
        );
        if (result == 1) {
            if (strnlen_s(state.input_link_name, INPUT_CHARS_MAX) == 0) {
                state.input_error = LinkError::MissingName;
            } else for (auto const& link : state.links) {
                if (link.name == state.input_link_name) {
                    state.input_error = LinkError::DuplicateName;
                    break;
                }
            }
            if (state.input_error == LinkError::Ok) {
                LinkEntry new_link = {
                    .name = state.input_link_name,
                    // .links = (default)
                    .is_open = true,
                };
                state.links.insert(state.links.begin() + (int)state.current_link, new_link);
                WriteLinksToLauncherCfg();
                ImGui::CloseCurrentPopup();
            }
        } else if (result == 2) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (Modal(S(THPRAC_LINKS_FILTER_DEL_MODAL) /* (Default size) */)) {
        ImGui::TextUnformatted(S(THPRAC_LINKS_FILTER_DELETE_WARNING));
        if (YesNoChoice(S(THPRAC_YES), S(THPRAC_NO), 6.0f)) {
            state.links.erase(state.links.begin() + (int)state.current_link);
            state.selected_link = nullptr;
            WriteLinksToLauncherCfg();
        }
        ImGui::EndPopup();
    }

    if (Modal(S(THPRAC_LINKS_ERR_MOVE_MODAL) /* (Default size) */)) {
        ImGui::TextUnformatted(S(THPRAC_LINKS_ERR_MOVE));
        if (Utils::GuiCornerButton(S(THPRAC_OK), nullptr, ImVec2(1.0f, 0.0f), true)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (Modal(S(THPRAC_LINKS_ERR_EXEC_MODAL) /* (Default size) */)) {
        ImGui::TextUnformatted(S(THPRAC_LINKS_ERR_EXEC));
        if (Utils::GuiCornerButton(S(THPRAC_OK), nullptr, ImVec2(1.0f, 0.0f), true)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// TODO: Is "main" really the right name for what this does?
// TODO: Have this take an enum instead of a magic number
static bool PopupContextMenuMain(int type) {
    if (type == 1 || type == 2) {
        if (!ImGui::BeginPopupContextItem()) {
            return false;
        }

        if (type == 2) {
            if (ImGui::Selectable(S(THPRAC_LINKS_EDIT))) {
                state.ui_trigger = UiTrigger::EditLink;
            }
            if (ImGui::Selectable(S(THPRAC_LINKS_DELETE))) {
                state.ui_trigger = UiTrigger::DeleteLink;
            }
            ImGui::Separator();
        } else /* type == 1 */ {
            if (ImGui::Selectable(S(THPRAC_LINKS_FILTER_DEL))) {
                state.ui_trigger = UiTrigger::DeleteFilter;
            }
        }

        if (ImGui::Selectable(S(THPRAC_LINKS_ADD))) {
            state.ui_trigger = UiTrigger::AddLink;
        }
        ImGui::Separator();
        if (ImGui::Selectable(S(THPRAC_LINKS_FILTER_ADD))) {
            state.ui_trigger = UiTrigger::AddFilter;
        }
        ImGui::EndPopup();
        return true;
    } else /* type == 0 */ {
        if (!ImGui::BeginPopupContextWindow()) {
            return false;
        }
        if (ImGui::Selectable(S(THPRAC_LINKS_FILTER_ADD))) {
            state.ui_trigger = UiTrigger::AddFilter;
        }
        if (state.links.size() == 0) {
            if (ImGui::Selectable(S(THPRAC_LINKS_RESET))) {
                WriteDefaultLinksToLauncherCfg();
                LoadLinksFromLauncherCfg();
            }
        }
        ImGui::EndPopup();
        return true;
    }
}
} // namespace Gui

// TODO: Rename this function to match the other pages' convention.
void LauncherLinksUiUpdate() {
    if (!state.page_initialized) [[unlikely]] {
        LoadLinksFromLauncherCfg();
        state.page_initialized = true;
    }

    ImGui::BeginChild("##links");
    defer(ImGui::EndChild());

    // TODO: Don't use -1 to mean "invalid index".
    // TODO: Apparently [0] is the index for links, and [1] for leaves?
    int move_destination_indexes[2] = {-1, -1}; // "destIdx"
    int filter_destination_index = -1; // "filterDestIdx"

    // TODO: What the heck does "type 0" mean?
    if (Gui::PopupContextMenuMain(0)) {
        state.selected_link = nullptr;
        state.current_link = state.links.size(); // TODO: Invalid, but not obviously so. Why?
    }
    if (state.links.size() == 0) {
        Utils::GuiSetPosYRel(0.5f);
        Gui::TextCentered(S(THPRAC_GAMES_MISSING), ImGui::GetWindowWidth());
        return;
    }
    ImGui::Columns(2, "##@__col_links", true, true);

    for (size_t links_i = 0; links_i < state.links.size(); links_i++) {
        auto const& link = state.links[links_i];

        ImGui::SetNextItemOpen(link.is_open, ImGuiCond_FirstUseEver);
        auto link_flags = (state.selected_link == &link)
            ? ImGuiTreeNodeFlags_Selected
            : ImGuiTreeNodeFlags_None;
        auto link_is_open = ImGui::TreeNodeEx(link.name.c_str(), link_flags);
        if (ImGui::BeginDragDropSource()) {
            state.filter_move_index = (int)links_i;
            ImGui::SetDragDropPayload(
                "##@__dnd_link_filter",
                &state.move_indexes, // ???
                sizeof(state.move_indexes)
            );
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            auto payload = ImGui::AcceptDragDropPayload("##__dnd_link_filter");
            if (payload != nullptr) {
                filter_destination_index = (int)links_i;
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginDragDropTarget()) {
            auto payload = ImGui::AcceptDragDropPayload("##@__dnd_link_leaf");
            if (payload != nullptr) {
                move_destination_indexes[0] = (int)links_i;
                move_destination_indexes[1] = 0;
            }
            ImGui::EndDragDropTarget();
        }

        // TODO: What the heck does "type 1" mean?
        if (Gui::PopupContextMenuMain(1)) {
            // TODO: This pointer cast is UGLY. There has GOT to be a better way to do this.
            state.selected_link = (BaseLinkSelectable*)&link;
            state.current_link = links_i;
        } else if (state.selected_link == &link) {
            state.selected_link = nullptr;
        }
        ImGui::NextColumn();
        ImGui::NextColumn();

        // TODO: Maybe extract this into its own function?
        if (link_is_open) {
            for (size_t leaves_i = 0; leaves_i < link.leaves.size(); leaves_i++) {
                auto const& leaf = link.leaves[leaves_i];

                auto leaf_flags = ImGuiTreeNodeFlags_Leaf
                    | ImGuiTreeNodeFlags_NoTreePushOnOpen
                    | ImGuiTreeNodeFlags_SpanAvailWidth;
                if (state.selected_link == &leaf) {
                    leaf_flags |= ImGuiTreeNodeFlags_Selected;
                }
                ImGui::TreeNodeEx((void*)(intptr_t)leaves_i, leaf_flags, "%s", leaf.name.c_str());

                if (ImGui::IsItemHovered()) {
                    auto SelectCurrentLeaf = [&]() {
                        // TODO: Another ugly pointer cast...
                        state.selected_link = (BaseLinkSelectable*)&leaf;
                        state.current_link = links_i;
                        state.current_leaf = leaves_i;
                    };
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        SelectCurrentLeaf();
                        if (!ExecuteLink(leaf.link)) {
                            state.ui_trigger = UiTrigger::ErrorExecutable;
                        }
                    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        SelectCurrentLeaf();
                    }
                }

                if (ImGui::BeginDragDropSource()) {
                    state.move_indexes[0] = (int)links_i;
                    state.move_indexes[1] = (int)leaves_i;
                    ImGui::SetDragDropPayload(
                        "##@__dnd_link_leaf",
                        &state.move_indexes, // ???
                        sizeof(state.move_indexes)
                    );
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget()) {
                    auto payload = ImGui::AcceptDragDropPayload("##@__dnd_link_leaf");
                    if (payload != nullptr) {
                        move_destination_indexes[0] = (int)links_i;
                        move_destination_indexes[1] = (int)leaves_i;
                    }
                    ImGui::EndDragDropTarget();
                }

                // TODO: What the heck does "type 2" mean?
                if (Gui::PopupContextMenuMain(2)) {
                    // TODO: Another ugly pointer cast...
                    state.selected_link = (BaseLinkSelectable*)&leaf;
                    state.current_link = links_i;
                    state.current_leaf = leaves_i;
                }

                ImGui::NextColumn();
                Utils::GuiColumnText(leaf.link.c_str());
                ImGui::NextColumn();
            }
            ImGui::TreePop();
        }

        if (link.is_open != link_is_open) {
            link.is_open = link_is_open; // This is why .is_open is mutable
            WriteLinksToLauncherCfg();
            state.selected_link = nullptr;
        }
    }

    ImGui::Columns(1);

    // TODO: It looks like these conditionals are where the actual swapping happens. Maybe extract
    // these into functions? (Also, this first one can probably be a rotate instead.)
    if (filter_destination_index >= 0) {
        auto link_temp = state.links[(size_t)state.filter_move_index];
        if (filter_destination_index > state.filter_move_index) {
            state.links.insert(state.links.begin() + filter_destination_index + 1, link_temp);
            state.links.erase(state.links.begin() + state.filter_move_index);
        } else if (filter_destination_index < state.filter_move_index) {
            state.links.insert(state.links.begin() + filter_destination_index, link_temp);
            state.links.erase(state.links.begin() + state.filter_move_index + 1);
        }

        state.current_link = (size_t)filter_destination_index;
        state.selected_link = nullptr;
        WriteLinksToLauncherCfg();
    }

    if (move_destination_indexes[0] >= 0 && move_destination_indexes[1] >= 0) {
        // TODO: This is gross.
        auto leaf_temp = state.links[
            (size_t)state.move_indexes[0]
        ].leaves[
            (size_t)state.move_indexes[1]
        ];
        auto& source_leaves = state.links[(size_t)state.move_indexes[0]].leaves;
        auto& destination_leaves = state.links[(size_t)move_destination_indexes[0]].leaves;

        if (state.move_indexes[0] != move_destination_indexes[0]) {
            for (auto& leaf : destination_leaves) {
                if (leaf.name == leaf_temp.name) {
                    state.ui_trigger = UiTrigger::ErrorDuplicate;
                    break;
                }
            }
        }

        if (state.ui_trigger != UiTrigger::ErrorDuplicate) {
            if (state.move_indexes[0] == move_destination_indexes[0]) {
                // Source and destination links are the same
                if (move_destination_indexes[1] > state.move_indexes[1]) {
                    destination_leaves.insert(destination_leaves.begin() + move_destination_indexes[1] + 1, leaf_temp);
                    source_leaves.erase(source_leaves.begin() + state.move_indexes[1]);
                } else {
                    destination_leaves.insert(destination_leaves.begin() + move_destination_indexes[1], leaf_temp);
                    source_leaves.erase(source_leaves.begin() + state.move_indexes[1] + 1);
                }
            } else {
                // Source and destination links are different
                destination_leaves.insert(destination_leaves.begin() + move_destination_indexes[1], leaf_temp);
                source_leaves.erase(source_leaves.begin() + state.move_indexes[1]);
            }
            state.current_link = (size_t)move_destination_indexes[0];
            state.current_leaf = (size_t)move_destination_indexes[1];
            state.selected_link = &destination_leaves[(size_t)move_destination_indexes[1]];
        }
    }

    Gui::ContextMenuUpdate();
}
} // namespace THPrac
