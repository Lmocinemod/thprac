#include "thprac_cfg.h"
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

// NOTE: The "Links" page consists of a list of Filters, each of which has a name, an open/closed
// state, and a list of Leaves. Leaves, in turn, have a name and a target (URL or path). Leaves are
// referred to as "links" in the GUI.
// (Yes, this nomenclature sucks, but it's hard to meaningfully improve on without changing the
// localization strings, which sadly isn't practical right now.)
// The one exception to this rule is that the list of Filters is stored in a file named
// `links.json`, and not `filters.json` or `leaves.json` as one might expect.
// TODO: Standardize around a better set of names. Maybe "Groups" that contain "Links"?

namespace THPrac {
// TODO: Move these to some other file (thprac_launcher_utils?)
namespace Utils {
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
enum class ButtonResult {
    NonePressed,
    LeftPressed,
    RightPressed,
};
// TODO: This function in particular should go in thprac_gui_components
// TODO: Maybe overload this for the single-button use case?
ButtonResult GuiCornerButtons(
    const char* left_button_text,
    const char* optional_right_button_text,
    ImVec2 offset, // TODO: Why was this originally passed by const-reference? Is there some reason?
    bool use_current_y
) {
    auto const& style = ImGui::GetStyle();
    float font_size = ImGui::GetFontSize();
    auto text_size_left = ImGui::CalcTextSize(left_button_text);
    auto text_size_right = optional_right_button_text != nullptr
        ? ImGui::CalcTextSize(optional_right_button_text)
        : ImVec2(0.0f, font_size);
    float text_height = std::max(text_size_left.y, text_size_right.y);

    ImVec2 cursor = ImGui::GetWindowSize();
    cursor.x -= text_size_left.x + (offset.x * font_size);
    if (optional_right_button_text) {
        cursor.x -= style.FramePadding.x * 2 + style.ItemSpacing.x + text_size_right.x;
    }
    auto current_cursor_y = ImGui::GetCursorPosY();
    cursor.y -= text_height + (offset.y * font_size);
    if (use_current_y ||  cursor.y < current_cursor_y) {
        cursor.y = current_cursor_y;
    }
    ImGui::SetCursorPos(cursor);

    auto result = ButtonResult::NonePressed;
    if (ImGui::Button(left_button_text)) {
        result = ButtonResult::LeftPressed;
    }
    if (optional_right_button_text) {
        ImGui::SameLine();
        if (ImGui::Button(optional_right_button_text)) {
            result = ButtonResult::RightPressed;
        }
    }
    return result;
}
// TODO: This should also go in thprac_gui_components
void GuiColumnText(const char* text) {
    auto column_width = ImGui::GetColumnWidth();
    auto text_width = ImGui::CalcTextSize(text).x + ImGui::GetStyle().ItemSpacing.x;
    ImGui::TextUnformatted(text);
    if (!ImGui::IsItemHovered() || text_width <= column_width) {
        return;
    }

    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetIO().DisplaySize.x * 0.9f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}
} // namespace Utils

struct BaseSelectableObject {};

struct Leaf : BaseSelectableObject {
    std::string name;
    std::string target;
};

struct Filter : BaseSelectableObject {
    std::string name;
    std::vector<Leaf> leaves;
    mutable bool is_open = false;
};

enum class UiAction {
    None,
    AddLeaf,
    EditLeaf,
    DeleteLeaf,
    AddFilter,
    DeleteFilter,
    ErrorDuplicate,
    ErrorExecutable,
};

// TODO: Remove the magic number hints
enum class TargetType {
    Normal, // 0
    FilePath, // 1
    FolderPath, // 2
};

// TODO: Remove the magic number hints
enum class EditError {
    Ok, // 0
    MissingName, // 1
    MissingTarget, // 2
    DuplicateName, // 3
    ReservedName, // 4
};

#pragma region GlobalMutableState

constexpr size_t INPUT_CHARS_MAX = 1024;
struct THLinksPageState {
    std::vector<Filter> filters;
    UiAction ui_action = UiAction::None;
    BaseSelectableObject* selected_object = nullptr;
    size_t current_filter = 0;
    size_t current_leaf = 0;
    // TODO: Don't use -1 to mean "invalid index".
    // TODO: Why do these need to be global state, anyway?
    // TODO: Apparently [0] is the index for filters, and [1] for leaves?
    int move_indexes[2] = {-1, -1}; // "moveIdx"
    int filter_move_index = -1; // "filterMoveIdx"
    // TODO: These aren't properly bounds-checked in several cases. Maybe use strings/arrays instead?
    // (Maybe also move all the input_* stuff into a sub-struct, which could then have a Reset method.)
    char input_name[INPUT_CHARS_MAX];
    char input_target[INPUT_CHARS_MAX];
    char input_target_parameters[INPUT_CHARS_MAX];
    // These are used exclusively by ClearEditPopupState(), EditPopupMain(), and HandleUiAction()
    TargetType input_target_type = TargetType::Normal;
    EditError input_error = EditError::Ok;
};
static THLinksPageState state;

#pragma endregion // GlobalMutableState

// TODO: Either remove this function, or replace it with something better.
static void Debug(const char* message, bool is_error = false) {
    if (is_error) {
        fprintf(stderr, "ERROR: %s\n", message);
    } else {
        fprintf(stderr, "INFO: %s\n", message);
    }

    constexpr auto LP_CLASS_NAME = L"thprac launcher window";
    // This should always be non-null, since we can't make it here without a window being spawned.
    // But even if it does somehow end up null, it just means the MessageBox will have no parent.
    HWND hWnd = FindWindowW(LP_CLASS_NAME, nullptr);
    UINT uType = is_error ? MB_ICONERROR : MB_ICONINFORMATION;
    MessageBoxA(hWnd, message, "DEBUG", uType);
}

// TODO: Probably handle errors more gracefully... or something?
[[noreturn]] static void PanicOutOfMemory() {
    Debug("Out of memory (probably)", true);
    exit(EXIT_FAILURE);
}

namespace Json {
constexpr auto JSON_FILENAME = std::wstring_view(L"links.json");
constexpr auto JSON_WRITE_FLAGS = YYJSON_WRITE_PRETTY | YYJSON_WRITE_NEWLINE_AT_END;

constexpr auto KEY_DEFAULT = "Default";
constexpr auto KEY_IS_OPEN = "__is_open__";

constexpr size_t N_DEFAULT_FILTER_LEAVES = 8;
constexpr const char* DEFAULT_FILTER_LEAVES[N_DEFAULT_FILTER_LEAVES][2] = {
    {"Royalflare Archive", "https://maribelhearn.com/royalflare"},
    {"Lunarcast", "http://replay.lunarcast.net/"},
    {"PND's Scoreboard", "https://thscore.pndsng.com/index.php"},
    {"Maribel Hearn's Touhou Portal", "https://maribelhearn.com/"},
    {"Touhou Patch Center", "https://www.thpatch.net/"},
    {"Touhou Replay Showcase", "https://twitch.tv/touhou_replay_showcase"},
    {"甜品站 (isndes)", "https://www.isndes.com/"},
    {"THBWiki", "https://thwiki.cc/"}
};

static const std::wstring& GetLinksJsonFilePath() {
    static std::wstring path;
    if (path.size() > 0) [[likely]] {
        return path;
    }

    path.reserve(_gConfigDirLen + JSON_FILENAME.size());
    path.append(_gConfigDir);
    path.append(JSON_FILENAME);
    return path;
}

static std::pair<yyjson_mut_doc*, yyjson_mut_val*> NewJsonDoc() {
    auto doc = yyjson_mut_doc_new(nullptr);
    if (doc == nullptr) {
        PanicOutOfMemory();
    }
    auto root = yyjson_mut_obj(doc);
    if (root == nullptr) {
        PanicOutOfMemory();
    }
    yyjson_mut_doc_set_root(doc, root);
    return std::pair(doc, root);
}

static void WriteJsonDocToLinksJson(yyjson_mut_doc* doc) {
    FILE* f;
    auto io_error = _wfopen_s(&f, GetLinksJsonFilePath().c_str(), L"wb");
    if (io_error != NULL) {
        // TODO: Probably have some kind of retry behavior?
        auto message = std::format("Failed to open links.json for editing. Error code: {}", io_error);
        Debug(message.c_str(), true);
    }
    defer(fclose(f));

    yyjson_write_err write_err;
    yyjson_mut_write_fp(f, doc, JSON_WRITE_FLAGS, nullptr, &write_err);
    if (write_err.code != YYJSON_WRITE_SUCCESS) {
        // TODO: Probably have some kind of retry behavior?
        auto message = std::format("Failed to save links to links.json: {}", write_err.msg);
        Debug(message.c_str(), true);
    }
}

static void SaveLinksJson() {
    auto [doc, root] = NewJsonDoc();
    defer(yyjson_mut_doc_free(doc));

    for (auto const& filter : state.filters) {
        auto filter_obj = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, filter_obj, KEY_IS_OPEN, filter.is_open);
        for (auto const& leaf : filter.leaves) {
            yyjson_mut_obj_add_str(doc, filter_obj, leaf.name.c_str(), leaf.target.c_str());
        }
        yyjson_mut_obj_add_val(doc, root, filter.name.c_str(), filter_obj);
    }

    WriteJsonDocToLinksJson(doc);
}

static void ResetLinksJsonToDefault() {
    auto [doc, root] = NewJsonDoc();
    defer(yyjson_mut_doc_free(doc));

    auto default_ = yyjson_mut_obj(doc);
    if (default_ == nullptr) {
        PanicOutOfMemory();
    }
    yyjson_mut_obj_add_bool(doc, default_, KEY_IS_OPEN, true);
    for (size_t i = 0; i < N_DEFAULT_FILTER_LEAVES; i++) {
        auto [k, v] = DEFAULT_FILTER_LEAVES[i];
        yyjson_mut_obj_add_str(doc, default_, k, v);
    }
    yyjson_mut_obj_add_val(doc, root, KEY_DEFAULT, default_);

    WriteJsonDocToLinksJson(doc);
}

static void LoadDefaultFilterAndLeaves() {
    state = {};
    std::vector<Leaf> leaves;
    leaves.reserve(sizeof(Leaf) * N_DEFAULT_FILTER_LEAVES);
    for (size_t i = 0; i < N_DEFAULT_FILTER_LEAVES; i++) {
        auto [name, target] = DEFAULT_FILTER_LEAVES[i];
        leaves.push_back(Leaf {
            .name = name,
            .target = target,
        });
    }
    state.filters.push_back(Filter {
        .name = KEY_DEFAULT,
        .leaves = std::move(leaves),
        .is_open = true,
    });
}

static void LoadLinksJson() {
    // TODO: Turn this magic number into an enum if possible.
    int filter_state = Utils::GetLauncherSetting("filter_default").value();

    FILE* f;
    auto io_error = _wfopen_s(&f, GetLinksJsonFilePath().c_str(), L"rb");
    if (io_error != NULL) {
        if (io_error == ERROR_FILE_NOT_FOUND) {
            // JSON file doesn't exist, so create it.
            ResetLinksJsonToDefault();
            LoadDefaultFilterAndLeaves();
            return;
        }

        // TODO: Probably have some kind of retry behavior?
        auto message = std::format(
            "FATAL: Failed to open links.json for reading. Error code: {}",
            io_error
        );
        Debug(message.c_str(), true);
        exit(EXIT_FAILURE);
    }
    defer(fclose(f));

    yyjson_read_err read_err;
    yyjson_read_flag flags = 0;
    auto doc = yyjson_read_fp(f, flags, nullptr, &read_err);
    if (read_err.code != YYJSON_READ_SUCCESS) {
        // TODO: Probably warn the user that the file is corrupt or something instead? And then do
        // retry behavior (or give the option to just exit).
        auto message = std::format(
            "FATAL: Failed to read links from links.json: {}, code {}, pos {}",
            read_err.msg, read_err.code, read_err.pos
        );
        Debug(message.c_str(), true);
        exit(EXIT_FAILURE);
    }
    defer(yyjson_doc_free(doc));
    auto root = yyjson_doc_get_root(doc); // Will never return nullptr

    size_t filter_i, filter_max;
    yyjson_val* filter_k;
    yyjson_val* filter_v;
    yyjson_obj_foreach(root, filter_i, filter_max, filter_k, filter_v) {
        if (yyjson_get_type(filter_v) != YYJSON_TYPE_OBJ) {
            // Ignore non-object values.
            // TODO: Should a warning be displayed here?
            continue;
        }
        Filter filter = {};
        filter.name = yyjson_get_str(filter_k);

        size_t leaf_i, leaf_max;
        yyjson_val* leaf_k;
        yyjson_val* leaf_v;
        yyjson_obj_foreach(filter_v, leaf_i, leaf_max, leaf_k, leaf_v) {
            bool k_is_is_open = strcmp(yyjson_get_str(leaf_k), KEY_IS_OPEN) == 0;
            if (k_is_is_open && yyjson_is_bool(leaf_v)) {
                filter.is_open = unsafe_yyjson_get_bool(leaf_v);
            } else if (!k_is_is_open && yyjson_is_str(leaf_v)) {
                filter.leaves.push_back(Leaf {
                    .name = yyjson_get_str(leaf_k),
                    .target = unsafe_yyjson_get_str(leaf_v),
                });
            }
            // Ignore everything else.
            // TODO: Should a warning be displayed here?
        }

        if (filter_state == 1) {
            filter.is_open = true;
        } else if (filter_state == 2) {
            filter.is_open = false;
        }

        state.filters.push_back(filter);
    }
}
} // namespace Json

static bool TargetIsExecutable(const char* target, TargetType type) {
    // TODO: Would it be better to check against everything in PATHEXT?
    return type == TargetType::FilePath && Utils::GetSuffixFromPath(target) == "exe";
}

static std::string WrapTarget(const char* target, const char* parameters, TargetType type) {
    if (!TargetIsExecutable(target, type)) {
        return std::string(target);
    }
    return std::format("\"{}\" {}", target, parameters);
}

struct TargetInfo {
    TargetType type;
    std::string file;
    std::string parameters;
};
static TargetInfo GetTargetInfo(std::string const& target) {
    TargetInfo result = {};
    size_t first_quote = target.find('"');
    size_t last_quote = target.rfind('"');
    if (
        first_quote != std::string::npos
        && last_quote != std::string::npos
        && first_quote != last_quote
    ) {
        result.file = target.substr(first_quote + 1, last_quote - first_quote - 1);
        result.parameters = target.substr(last_quote + 1);
        while (result.parameters.size() > 0 && isblank(result.parameters[0])) {
            result.parameters.erase(0, 1);
        }
        result.type = TargetType::FilePath;
    } else {
        result.file = target;
        result.parameters = "";
        auto unified = Utils::GetUnifiedPath(target);
        if (isalpha(unified[0]) && unified[1] == ':' && unified[2] == '\\') {
            result.type = TargetType::FolderPath;
        } else {
            result.type = TargetType::Normal;
        }
    }
    return result;
}

/** Returns `true` on success, `false` otherwise. */
static bool ExecuteTarget(std::string const& target) {
    auto target_info = GetTargetInfo(target);
    auto target_file_w = utf8_to_utf16(target_info.file.c_str());
    auto target_parameters_w = utf8_to_utf16(target_info.parameters.c_str());
    HINSTANCE result;
    std::wstring target_directory_w;

    switch (target_info.type) {
    case TargetType::FilePath:
        target_directory_w = Utils::GetDirFromFullPath(target_file_w);
        result = ShellExecuteW(
            nullptr,
            L"open",
            target_file_w.c_str(),
            target_parameters_w.c_str(),
            target_directory_w.c_str(),
            SW_SHOW
        );
        break;
    case TargetType::Normal:
    case TargetType::FolderPath:
        result = ShellExecuteW(nullptr, nullptr, target_file_w.c_str(), nullptr, nullptr, SW_SHOW);
        break;
    default:
        // Should be unreachable
        return false;
    }

    return ((DWORD)result > 32);
}

namespace Gui {
inline Utils::ButtonResult CornerOkCancelButtons() {
    return Utils::GuiCornerButtons(S(THPRAC_OK), S(THPRAC_CANCEL), ImVec2(1.0f, 0.0f), true);
}

inline bool CornerOkButton() {
    auto result = Utils::GuiCornerButtons(S(THPRAC_OK), nullptr, ImVec2(1.0f, 0.0f), true);
    return result == Utils::ButtonResult::LeftPressed;
}

static void ClearEditPopupState() {
    state.input_target[0] = state.input_name[0] = state.input_target_parameters[0] = '\0';
    state.input_target_type = TargetType::Normal;
    state.input_error = EditError::Ok;
}

static void EditPopupShowErrorIfApplicable(EditError error) {
    th_glossary_t error_message = A0000ERROR_C;
    switch (error) {
    case EditError::MissingName:
        error_message = THPRAC_LINKS_EDIT_ERR_NAME;
        break;
    case EditError::MissingTarget:
        error_message = THPRAC_LINKS_EDIT_ERR_LINK;
        break;
    case EditError::DuplicateName:
        error_message = THPRAC_LINKS_EDIT_ERR_REPEATED;
        break;
    case EditError::ReservedName:
        error_message = THPRAC_LINKS_EDIT_ERR_RSV;
        break;
    default:
        return;
    }

    auto red = ImVec4(255.0f, 0.0f, 0.0f, 255.0f);
    ImGui::TextColored(red, "%s", S(error_message));
}

enum class EditResult {
    InProgress,
    Complete,
    Cancelled,
};
static EditResult EditPopupMain() {
    ImGui::TextUnformatted(S(THPRAC_LINKS_EDIT_NAME));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##__input_name", state.input_name, INPUT_CHARS_MAX)) {
        if (
            state.input_error == EditError::MissingName
            || state.input_error == EditError::DuplicateName
        ) {
            state.input_error = EditError::Ok;
        }
    }

    ImGui::TextUnformatted(S(THPRAC_LINKS_EDIT_LINK));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (state.input_target_type == TargetType::Normal) {
        if (ImGui::InputText("##__input_target", state.input_target, INPUT_CHARS_MAX)) {
            if (state.input_error == EditError::MissingTarget) {
                state.input_error = EditError::Ok;
            }
        }
    } else {
        ImGui::TextUnformatted(state.input_target);
    }
    if (TargetIsExecutable(state.input_target, state.input_target_type)) {
        ImGui::TextUnformatted(S(THPRAC_LINKS_EDIT_PARAM));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##__input_target_parameters", state.input_target_parameters, INPUT_CHARS_MAX);
    }

    EditPopupShowErrorIfApplicable(state.input_error);

    if (ImGui::Button(S(THPRAC_LINKS_EDIT_FILE))) {
        auto file_str = Utils::LauncherWndFileSelect();
        if (file_str.length() > 0) {
            state.input_error = EditError::Ok;
            state.input_target_type = TargetType::FilePath;
            state.input_target_parameters[0] = '\0';
            sprintf_s(state.input_target, "%s", utf16_to_utf8(file_str.c_str()).c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(S(THPRAC_LINKS_EDIT_FOLDER))) {
        auto folder_str = Utils::LauncherWndFolderSelect();
        if (folder_str.length() > 0) {
            state.input_error = EditError::Ok;
            state.input_target_type = TargetType::FolderPath;
            sprintf_s(state.input_target, "%s", utf16_to_utf8(folder_str.c_str()).c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(S(THPRAC_LINKS_EDIT_INPUT))) {
        state.input_error = EditError::Ok;
        state.input_target[0] = '\0';
        state.input_target_type = TargetType::Normal;
    }
    ImGui::SameLine();

    auto button_clicked = CornerOkCancelButtons();
    if (button_clicked == Utils::ButtonResult::LeftPressed) {
        if (strnlen_s(state.input_name, INPUT_CHARS_MAX) == 0) {
            state.input_error = EditError::MissingName;
        } else if (strnlen_s(state.input_target, INPUT_CHARS_MAX) == 0) {
            state.input_error = EditError::MissingTarget;
        } else if (strcmp(state.input_name, "__is_open__") == 0) {
            state.input_error = EditError::ReservedName;
        } else {
            for (auto const& leaf : state.filters[state.current_filter].leaves) {
                if (leaf.name == state.input_name) {
                    state.input_error = EditError::DuplicateName;
                    break;
                }
            }
        }
        if (state.input_error != EditError::Ok) {
            return EditResult::InProgress;
        }
        return EditResult::Complete;
    } else if (button_clicked == Utils::ButtonResult::RightPressed) {
        return EditResult::Cancelled;
    }
    return EditResult::InProgress;
}

// TODO: Maybe this should take a UiAction parameter instead of reading global state? The reset to
// UiAction::None should be handled by the caller.
static void HandleUiAction() {
    th_glossary_t popup_str_id = A0000ERROR_C;
    switch (state.ui_action) {
    case UiAction::AddLeaf:
        ClearEditPopupState();
        popup_str_id = THPRAC_LINKS_ADD;
        break;
    case UiAction::EditLeaf:
        ClearEditPopupState();
        {
            auto& leaf = state.filters[state.current_filter].leaves[state.current_leaf];
            auto info = GetTargetInfo(leaf.target);
            state.input_target_type = info.type;
            strcpy_s(state.input_name, leaf.name.c_str());
            strcpy_s(state.input_target, info.file.c_str());
            strcpy_s(state.input_target_parameters, info.parameters.c_str());
        }
        popup_str_id = THPRAC_LINKS_EDIT;
        break;
    case UiAction::DeleteLeaf:
        popup_str_id = THPRAC_LINKS_DELETE_MODAL;
        break;
    case UiAction::AddFilter:
        ClearEditPopupState();
        popup_str_id = THPRAC_LINKS_FILTER_ADD_MODAL;
        break;
    case UiAction::DeleteFilter:
        popup_str_id = THPRAC_LINKS_FILTER_DEL_MODAL;
        break;
    case UiAction::ErrorDuplicate:
        popup_str_id = THPRAC_LINKS_ERR_MOVE_MODAL;
        break;
    case UiAction::ErrorExecutable:
        popup_str_id = THPRAC_LINKS_ERR_EXEC_MODAL;
        break;
    default:
        break;
    }

    if (popup_str_id != A0000ERROR_C) {
        ImGui::OpenPopup(S(popup_str_id));
    }
    state.ui_action = UiAction::None;

    // TODO: The bodies of these if statements should probably be their own functions.
    auto modal_size_rel = ImVec2(ImGui::GetIO().DisplaySize.x * 0.9f, 0.0f);
    if (Modal(S(THPRAC_LINKS_ADD), modal_size_rel)) {
        auto result = EditPopupMain();
        if (result != EditResult::InProgress) {
            if (result == EditResult::Complete) {
                auto final_target = WrapTarget(
                    state.input_target,
                    state.input_target_parameters,
                    state.input_target_type
                );
                Leaf new_leaf = {
                    .name = state.input_name,
                    .target = final_target,
                };

                size_t insert_i = 0;
                if (state.selected_object != nullptr) {
                    insert_i = state.current_leaf;
                }
                auto& current_filter = state.filters[state.current_filter];
                current_filter.leaves.insert(current_filter.leaves.begin() + (int)insert_i, new_leaf);
                state.current_leaf = insert_i;
                state.selected_object = nullptr;
                Json::SaveLinksJson();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (Modal(S(THPRAC_LINKS_EDIT), modal_size_rel)) {
        auto result = EditPopupMain();
        if (result != EditResult::InProgress) {
            if (result == EditResult::Complete) {
                auto final_target = WrapTarget(
                    state.input_target,
                    state.input_target_parameters,
                    state.input_target_type
                );
                auto& leaf_to_edit = state.filters[state.current_filter].leaves[state.current_leaf];
                leaf_to_edit.name = state.input_name;
                leaf_to_edit.target = final_target;
                Json::SaveLinksJson();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (Modal(S(THPRAC_LINKS_DELETE_MODAL) /* (Default size) */)) {
        ImGui::TextUnformatted(S(THPRAC_LINKS_DELETE_WARNING));
        if (YesNoChoice(S(THPRAC_YES), S(THPRAC_NO), 6.0f)) {
            auto& current_filter = state.filters[state.current_filter];
            current_filter.leaves.erase(current_filter.leaves.begin() + (int)state.current_leaf);
            state.selected_object = nullptr;
            Json::SaveLinksJson();
        }
        ImGui::EndPopup();
    }

    modal_size_rel = ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, 0.0f);
    if (Modal(S(THPRAC_LINKS_FILTER_ADD_MODAL), modal_size_rel)) {
        ImGui::TextUnformatted(S(THPRAC_LINKS_EDIT_NAME));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##__input_name", state.input_name, INPUT_CHARS_MAX)) {
            if (
                state.input_error == EditError::MissingName
                || state.input_error == EditError::DuplicateName
            ) {
                state.input_error = EditError::Ok;
            }
        }
        EditPopupShowErrorIfApplicable(state.input_error);

        auto result = CornerOkCancelButtons();
        if (result == Utils::ButtonResult::LeftPressed) {
            if (strnlen_s(state.input_name, INPUT_CHARS_MAX) == 0) {
                state.input_error = EditError::MissingName;
            } else for (auto const& filter : state.filters) {
                if (filter.name == state.input_name) {
                    state.input_error = EditError::DuplicateName;
                    break;
                }
            }
            if (state.input_error == EditError::Ok) {
                Filter new_filter = {
                    .name = state.input_name,
                    // .links = (default)
                    .is_open = true,
                };
                state.filters.insert(state.filters.begin() + (int)state.current_filter, new_filter);
                Json::SaveLinksJson();
                ImGui::CloseCurrentPopup();
            }
        } else if (result == Utils::ButtonResult::RightPressed) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (Modal(S(THPRAC_LINKS_FILTER_DEL_MODAL) /* (Default size) */)) {
        ImGui::TextUnformatted(S(THPRAC_LINKS_FILTER_DELETE_WARNING));
        if (YesNoChoice(S(THPRAC_YES), S(THPRAC_NO), 6.0f)) {
            state.filters.erase(state.filters.begin() + (int)state.current_filter);
            state.selected_object = nullptr;
            Json::SaveLinksJson();
        }
        ImGui::EndPopup();
    }

    if (Modal(S(THPRAC_LINKS_ERR_MOVE_MODAL) /* (Default size) */)) {
        ImGui::TextUnformatted(S(THPRAC_LINKS_ERR_MOVE));
        if (CornerOkButton()) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (Modal(S(THPRAC_LINKS_ERR_EXEC_MODAL) /* (Default size) */)) {
        ImGui::TextUnformatted(S(THPRAC_LINKS_ERR_EXEC));
        if (CornerOkButton()) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// TODO: Figure out what each of these types are.
enum class ContextMenuType {
    Unknown0,
    Unknown1,
    Unknown2,
};
// TODO: Maybe this should return UiAction, instead of mutating global state?
static bool ShowContextMenuIfApplicable(ContextMenuType type) {
    if (type == ContextMenuType::Unknown1 || type == ContextMenuType::Unknown2) {
        if (!ImGui::BeginPopupContextItem()) {
            return false;
        }

        if (type == ContextMenuType::Unknown2) {
            if (ImGui::Selectable(S(THPRAC_LINKS_EDIT))) {
                state.ui_action = UiAction::EditLeaf;
            }
            if (ImGui::Selectable(S(THPRAC_LINKS_DELETE))) {
                state.ui_action = UiAction::DeleteLeaf;
            }
            ImGui::Separator();
        } else /* type == 1 */ {
            if (ImGui::Selectable(S(THPRAC_LINKS_FILTER_DEL))) {
                state.ui_action = UiAction::DeleteFilter;
            }
        }

        if (ImGui::Selectable(S(THPRAC_LINKS_ADD))) {
            state.ui_action = UiAction::AddLeaf;
        }
        ImGui::Separator();
        if (ImGui::Selectable(S(THPRAC_LINKS_FILTER_ADD))) {
            state.ui_action = UiAction::AddFilter;
        }
        ImGui::EndPopup();
        return true;
    } else /* type == ContextMenuType::Unknown0 */ {
        if (!ImGui::BeginPopupContextWindow()) {
            return false;
        }
        if (ImGui::Selectable(S(THPRAC_LINKS_FILTER_ADD))) {
            state.ui_action = UiAction::AddFilter;
        }
        if (state.filters.size() == 0) {
            if (ImGui::Selectable(S(THPRAC_LINKS_RESET))) {
                Json::ResetLinksJsonToDefault();
                Json::LoadDefaultFilterAndLeaves();
            }
        }
        ImGui::EndPopup();
        return true;
    }
}
} // namespace Gui

// TODO: Rename this function to match the other pages' convention.
void LauncherLinksUiUpdate() {
    static bool page_initialized = false;
    if (!page_initialized) [[unlikely]] {
        Json::LoadLinksJson();
        page_initialized = true;
    }

    ImGui::BeginChild("##links");
    defer(ImGui::EndChild());

    // TODO: Don't use -1 to mean "invalid index".
    // TODO: Apparently [0] is the index for filters, and [1] for leaves?
    int move_destination_indexes[2] = {-1, -1}; // "destIdx"
    int filter_destination_index = -1; // "filterDestIdx"

    if (Gui::ShowContextMenuIfApplicable(Gui::ContextMenuType::Unknown0)) {
        state.selected_object = nullptr;
        state.current_filter = state.filters.size(); // TODO: Invalid, but not obviously so. Why?
    }
    if (state.filters.size() == 0) {
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() * 0.5f);
        Gui::TextCentered(S(THPRAC_GAMES_MISSING), ImGui::GetWindowWidth());
        return;
    }
    ImGui::Columns(2, "##@__col_filters", true, true);

    for (size_t filter_i = 0; filter_i < state.filters.size(); filter_i++) {
        auto const& filter = state.filters[filter_i];

        ImGui::SetNextItemOpen(filter.is_open, ImGuiCond_FirstUseEver);
        auto filter_flags = (state.selected_object == &filter)
            ? ImGuiTreeNodeFlags_Selected
            : ImGuiTreeNodeFlags_None;
        auto filter_is_open = ImGui::TreeNodeEx(filter.name.c_str(), filter_flags);
        if (ImGui::BeginDragDropSource()) {
            state.filter_move_index = (int)filter_i;
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
                filter_destination_index = (int)filter_i;
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginDragDropTarget()) {
            auto payload = ImGui::AcceptDragDropPayload("##@__dnd_link_leaf");
            if (payload != nullptr) {
                move_destination_indexes[0] = (int)filter_i;
                move_destination_indexes[1] = 0;
            }
            ImGui::EndDragDropTarget();
        }

        if (Gui::ShowContextMenuIfApplicable(Gui::ContextMenuType::Unknown1)) {
            // TODO: This pointer cast is UGLY. There has GOT to be a better way to do this.
            state.selected_object = (BaseSelectableObject*)&filter;
            state.current_filter = filter_i;
        } else if (state.selected_object == &filter) {
            state.selected_object = nullptr;
        }
        ImGui::NextColumn();
        ImGui::NextColumn();

        // TODO: Maybe extract this into its own function?
        if (filter_is_open) {
            for (size_t leaf_i = 0; leaf_i < filter.leaves.size(); leaf_i++) {
                auto const& leaf = filter.leaves[leaf_i];

                auto leaf_flags = ImGuiTreeNodeFlags_Leaf
                    | ImGuiTreeNodeFlags_NoTreePushOnOpen
                    | ImGuiTreeNodeFlags_SpanAvailWidth;
                if (state.selected_object == &leaf) {
                    leaf_flags |= ImGuiTreeNodeFlags_Selected;
                }
                ImGui::TreeNodeEx((void*)(intptr_t)leaf_i, leaf_flags, "%s", leaf.name.c_str());

                if (ImGui::IsItemHovered()) {
                    auto SelectCurrentLeaf = [&]() {
                        // TODO: Another ugly pointer cast...
                        state.selected_object = (BaseSelectableObject*)&leaf;
                        state.current_filter = filter_i;
                        state.current_leaf = leaf_i;
                    };
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        SelectCurrentLeaf();
                        if (!ExecuteTarget(leaf.target)) {
                            state.ui_action = UiAction::ErrorExecutable;
                        }
                    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        SelectCurrentLeaf();
                    }
                }

                if (ImGui::BeginDragDropSource()) {
                    state.move_indexes[0] = (int)filter_i;
                    state.move_indexes[1] = (int)leaf_i;
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
                        move_destination_indexes[0] = (int)filter_i;
                        move_destination_indexes[1] = (int)leaf_i;
                    }
                    ImGui::EndDragDropTarget();
                }

                if (Gui::ShowContextMenuIfApplicable(Gui::ContextMenuType::Unknown2)) {
                    // TODO: Another ugly pointer cast...
                    state.selected_object = (BaseSelectableObject*)&leaf;
                    state.current_filter = filter_i;
                    state.current_leaf = leaf_i;
                }

                ImGui::NextColumn();
                Utils::GuiColumnText(leaf.target.c_str());
                ImGui::NextColumn();
            }
            ImGui::TreePop();
        }

        if (filter.is_open != filter_is_open) {
            filter.is_open = filter_is_open; // This is why .is_open is mutable
            Json::SaveLinksJson();
            state.selected_object = nullptr;
        }
    }

    ImGui::Columns(1);

    // TODO: It looks like these conditionals are where the actual swapping happens. Maybe extract
    // these into functions? (Also, this first one can probably be a rotate instead.)
    if (filter_destination_index >= 0) {
        auto filter_temp = state.filters[(size_t)state.filter_move_index];
        if (filter_destination_index > state.filter_move_index) {
            state.filters.insert(state.filters.begin() + filter_destination_index + 1, filter_temp);
            state.filters.erase(state.filters.begin() + state.filter_move_index);
        } else if (filter_destination_index < state.filter_move_index) {
            state.filters.insert(state.filters.begin() + filter_destination_index, filter_temp);
            state.filters.erase(state.filters.begin() + state.filter_move_index + 1);
        }

        state.current_filter = (size_t)filter_destination_index;
        state.selected_object = nullptr;
        Json::SaveLinksJson();
    }

    if (move_destination_indexes[0] >= 0 && move_destination_indexes[1] >= 0) {
        // TODO: This is gross.
        auto leaf_temp = state.filters[
            (size_t)state.move_indexes[0]
        ].leaves[
            (size_t)state.move_indexes[1]
        ];
        auto& source_leaves = state.filters[(size_t)state.move_indexes[0]].leaves;
        auto& destination_leaves = state.filters[(size_t)move_destination_indexes[0]].leaves;

        if (state.move_indexes[0] != move_destination_indexes[0]) {
            for (auto& leaf : destination_leaves) {
                if (leaf.name == leaf_temp.name) {
                    state.ui_action = UiAction::ErrorDuplicate;
                    break;
                }
            }
        }

        if (state.ui_action != UiAction::ErrorDuplicate) {
            if (state.move_indexes[0] == move_destination_indexes[0]) {
                // Source and destination filters are the same
                if (move_destination_indexes[1] > state.move_indexes[1]) {
                    destination_leaves.insert(destination_leaves.begin() + move_destination_indexes[1] + 1, leaf_temp);
                    source_leaves.erase(source_leaves.begin() + state.move_indexes[1]);
                } else {
                    destination_leaves.insert(destination_leaves.begin() + move_destination_indexes[1], leaf_temp);
                    source_leaves.erase(source_leaves.begin() + state.move_indexes[1] + 1);
                }
            } else {
                // Source and destination filters are different
                destination_leaves.insert(destination_leaves.begin() + move_destination_indexes[1], leaf_temp);
                source_leaves.erase(source_leaves.begin() + state.move_indexes[1]);
            }
            state.current_filter = (size_t)move_destination_indexes[0];
            state.current_leaf = (size_t)move_destination_indexes[1];
            state.selected_object = &destination_leaves[(size_t)move_destination_indexes[1]];
        }
    }

    Gui::HandleUiAction();
}
} // namespace THPrac
