#include "thprac_cfg.h"
#include "thprac_gui_components.h"
#include "thprac_gui_locale.h"
#include "thprac_launcher_links.h"
#include "thprac_locale_def.h"
#include "thprac_utils.h"
#include "utils/utils.h"

#include <imgui.h>
#include <shlobj_core.h>
#include <string.h>
#include <yyjson.h>

#include <array>
// TODO: std::format() apparently adds 0.3MB to the binary size, so replace it before upstreaming.
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

#pragma region Constants

constexpr size_t INPUT_CHARS_MAX = 1024;

// Make sure to increment this when making a breaking change to the JSON structure! (And also add a
// function that converts the old format to the new one.)
constexpr int64_t JSON_FILE_VERSION = 1;
constexpr auto JSON_FILENAME = std::wstring_view(L"links.json");
constexpr auto JSON_WRITE_FLAGS = YYJSON_WRITE_PRETTY | YYJSON_WRITE_NEWLINE_AT_END;
constexpr auto JSON_KEY_IS_OPEN = "__is_open__";
constexpr auto JSON_KEY_METADATA = "__metadata__";
// This name isn't reserved, because it's a sub-key of JSON_KEY_METADATA.
constexpr auto JSON_KEY_METADATA_VERSION = "version";

constexpr std::array<const char*, 2> RESERVED_NAMES = {
    JSON_KEY_IS_OPEN,
    JSON_KEY_METADATA,
};

constexpr auto DEFAULT_FILTER_NAME = "Default"; // TODO: Make this localizable?
constexpr size_t N_DEFAULT_FILTER_LEAVES = 11;
constexpr const char* DEFAULT_FILTER_LEAVES[N_DEFAULT_FILTER_LEAVES][2] = {
    // TODO: Make these names localizable?
    {"Royalflare Archive", "https://maribelhearn.com/royalflare/"},
    {"PND's Scoreboard", "https://thscore.pndsng.com/index.php"},
    {"甜品站 (isndes)", "https://www.isndes.com/"},
    {"Lunarcast", "http://replay.lunarcast.net/"},
    {"Silent Selene", "https://www.silentselene.net/"},
    {"Maribel Hearn's Touhou Portal", "https://maribelhearn.com/"},
    {"Touhou Patch Center", "https://www.thpatch.net/"},
    {"Touhou Replay Showcase", "https://twitch.tv/touhou_replay_showcase/"},
    {"Touhou World Cup", "https://touhouworldcup.com/"},
    {"THBWiki", "https://thwiki.cc/"},
    {"Touhou Wiki (EN)", "https://en.touhouwiki.net/"}
};

constexpr auto LABEL_INPUT_NAME = "##__input_name";
constexpr auto LABEL_INPUT_TARGET = "##__input_target";
constexpr auto LABEL_INPUT_TARGET_PARAMETERS = "##__input_target_parameteres";
constexpr auto LABEL_ACTION_TOOLBAR = "##__action_toolbar";
constexpr auto LABEL_LINKS = "##links";
constexpr auto LABEL_COL_FILTERS = "##@__col_filters";
constexpr auto LABEL_DND_LINK_FILTER = "##@__dnd_link_filter";
constexpr auto LABEL_DND_LINK_LEAF = "##@__dnd_link_leaf";

#pragma endregion // Constants

namespace THPrac {
// TODO: Move these to some other file (thprac_launcher_utils?)
namespace Utils {
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
inline HWND GetLauncherHwnd() {
    constexpr auto LP_CLASS_NAME = L"thprac launcher window";
    // This should always be non-null, since we can't make it here without a window being spawned.
    return FindWindowW(LP_CLASS_NAME, nullptr);
}
std::wstring WindowsFilePicker(
    const wchar_t* lpstrTitle = L"Browse for file...",
    const wchar_t* lpstrFilter = nullptr
) {
    wchar_t file_path[MAX_PATH] = {};
    OPENFILENAMEW ofn = {
        .lStructSize = sizeof(OPENFILENAMEW),
        .hwndOwner = GetLauncherHwnd(),
        .lpstrFilter = lpstrFilter,
        .nFilterIndex = 1,
        .lpstrFile = file_path,
        .nMaxFile = sizeof(file_path),
        .lpstrTitle = lpstrTitle,
        .Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NODEREFERENCELINKS | OFN_DONTADDTORECENT
    };
    GetOpenFileNameW(&ofn);
    return std::wstring(file_path);
}
std::optional<std::wstring> WindowsFolderPicker(const wchar_t* title = L"Browse for folder...") {
    // This function used to have a fallback for XP support, but thprac doesn't support XP anymore,
    // so it's been removed.
    if (FAILED(CoInitialize(nullptr))) {
        return std::nullopt;
    }
    defer(CoUninitialize());

    PIDLIST_ABSOLUTE initial_path = nullptr;
    wchar_t folder_path[MAX_PATH] = {};
    GetCurrentDirectoryW(MAX_PATH, folder_path);
    // MSDN says you're not supposed to call this from the main thread, but uh... YOLO?
    auto result_1 = SHParseDisplayName(folder_path, nullptr, &initial_path, NULL, nullptr);
    if (result_1 != S_OK) {
        return std::nullopt;
    }

    IFileDialog* file_dialog = nullptr;
    if (FAILED(CoCreateInstance(
        CLSID_FileOpenDialog,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&file_dialog)
    )) || file_dialog == nullptr) {
        return std::nullopt;
    }
    defer(file_dialog->Release());
    IShellItem* item_1 = nullptr;
    SHCreateItemFromIDList(initial_path, IID_PPV_ARGS(&item_1));
    if (item_1 == nullptr) {
        return std::nullopt;
    }
    defer(item_1->Release());

    // TODO: Should we check the return codes for these method calls?
    file_dialog->SetDefaultFolder(item_1);
    file_dialog->SetOptions(
        FOS_NOCHANGEDIR | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST
        | FOS_FILEMUSTEXIST | FOS_DONTADDTORECENT
    );
    file_dialog->SetTitle(title);

    IShellItem* item_2 = nullptr;
    if (FAILED(file_dialog->Show(GetLauncherHwnd())) || FAILED(file_dialog->GetResult(&item_2))) {
        return std::nullopt;
    }
    defer(item_2->Release());
    PIDLIST_ABSOLUTE id_list = nullptr;
    SHGetIDListFromObject(item_2, &id_list);
    if (id_list != nullptr) {
        defer(CoTaskMemFree(id_list));
        if (SHGetPathFromIDListW(id_list, folder_path)) {
            return std::optional(folder_path);
        }
    }
    return std::nullopt;
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

struct Leaf {
    std::string name;
    std::string target;
};

struct Filter {
    std::string name;
    std::vector<Leaf> leaves;
    // This is mutable because some parts of the code don't want to change the name/leaves, but DO
    // want to change the open/closed state.
    mutable bool is_open = false;
};

enum class UiAction {
    None,
    AddLeaf,
    EditLeaf,
    DeleteLeaf,
    AddFilter,
    RenameFilter,
    DeleteFilter,
    OpenAllFilters,
    CloseAllFilters,
    RestoreDefaultFilter,
    ErrorDuplicateName,
    ErrorExecutingTarget,
};

enum class TargetType {
    Url,
    ExecutablePath,
    NonExecutablePath,
};

enum class EditError {
    Ok,
    MissingName,
    MissingTarget,
    DuplicateName,
    ReservedName,
};

// TODO: Either remove this function, or replace it with something better.
static void Debug(const char* message, bool is_error = false) {
    if (is_error) {
        fprintf(stderr, "ERROR: %s\n", message);
    } else {
        fprintf(stderr, "INFO: %s\n", message);
    }

    UINT uType = is_error ? MB_ICONERROR : MB_ICONINFORMATION;
    MessageBoxA(Utils::GetLauncherHwnd(), message, "DEBUG", uType);
}

// TODO: Probably handle errors more gracefully... or something?
[[noreturn]] static void PanicOutOfMemory() {
    Debug("Out of memory (probably)", true);
    exit(EXIT_FAILURE);
}

namespace GlobalMutableState {
struct SelectedFilterInfo {
    size_t i;
    bool is_selected;
};
struct SelectedLeafInfo {
    size_t filter_i;
    size_t leaf_i;
};

class Selection {
private:
    bool m_is_leaf = false;
    std::optional<size_t> m_filter_i = std::nullopt;
    std::optional<size_t> m_leaf_i = std::nullopt;
    union {
        const Filter* filter;
        const Leaf* leaf;
    } m_target_ptr = {.filter = nullptr};

    [[noreturn]] void PanicDoesNotExist(const char* method_name) const {
        auto message = std::format(
            "CONSTRAINT VIOLATION: Attempt to call Selection.{}() while no selection exists",
            method_name
        );
        Debug(message.c_str(), true);
        exit(EXIT_FAILURE);
    }

public:
    void SelectFilter(size_t filter_i, Filter const& filter) {
        m_is_leaf = false;
        m_target_ptr.filter = &filter;
        m_filter_i = std::optional(filter_i);
        m_leaf_i = std::nullopt;
    }
    void SelectLeaf(size_t filter_i, size_t leaf_i, Leaf const& leaf) {
        m_is_leaf = true;
        m_target_ptr.leaf = &leaf;
        m_filter_i = std::optional(filter_i);
        m_leaf_i = std::optional(leaf_i);
    }
    void Deselect() {
        m_is_leaf = false;
        m_target_ptr.filter = nullptr;
        m_filter_i = std::nullopt;
        m_leaf_i = std::nullopt;
    }

    inline bool Exists() const {
        // These branches do the same thing, but both must be present to avoid UB.
        if (m_is_leaf) {
            return m_target_ptr.leaf != nullptr;
        } else {
            return m_target_ptr.filter != nullptr;
        }
    }
    inline bool ExistsAndIsLeaf() const {
        if (!Exists()) {
            return false;
        }
        return m_is_leaf;
    }

    inline bool Is(Filter const& filter) const {
        if (m_is_leaf) {
            return false;
        }
        return m_target_ptr.filter == &filter;
    }
    inline bool Is(Leaf const& leaf) const {
        if (!m_is_leaf) {
            return false;
        }
        return m_target_ptr.leaf == &leaf;
    }

    inline SelectedFilterInfo GetFilterInfo() const {
        if (!Exists()) {
            PanicDoesNotExist("GetFilterInfo");
        }

        return {
            .i = m_filter_i.value(),
            .is_selected = !m_is_leaf,
        };
    }
    inline SelectedLeafInfo GetLeafInfo() const {
        if (!Exists()) {
            PanicDoesNotExist("GetLeafInfo");
        } else if (!m_is_leaf) {
            Debug(
                "CONSTRAINT VIOLATION: Attempt to call Selection.GetLeafInfo() while filter is selected",
                true
            );
            exit(EXIT_FAILURE);
        }

        return {
            .filter_i = m_filter_i.value(),
            .leaf_i = m_leaf_i.value(),
        };
    }
};

struct LinksPageState {
    bool page_active = false;
    std::vector<Filter> filters;
    // TODO: Manually managing this from outside is probably a bad idea.
    std::optional<size_t> default_filter_i = std::nullopt;
    UiAction ui_action = UiAction::None;
    Selection selection;
    // TODO: Don't use -1 to mean "invalid index".
    // TODO: Why do these need to be global state, anyway?
    // TODO: Apparently [0] is the index for filters, and [1] for leaves?
    int move_indexes[2] = {-1, -1}; // "moveIdx"
    int filter_move_index = -1; // "filterMoveIdx"
    // TODO: Maybe move all the input_* stuff into a sub-struct, with a Reset() method?
    // TODO: Verify that these raw buffer shenanigans are correct.
    char input_name[INPUT_CHARS_MAX];
    char input_target[INPUT_CHARS_MAX];
    char input_target_parameters[INPUT_CHARS_MAX];
    // These are used exclusively by ClearEditPopupState(), EditLeafPopupMain(),
    // EditFilterPopupMain(), and HandleUiAction()
    TargetType input_target_type = TargetType::Url;
    EditError input_error = EditError::Ok;
};
} // namespace GlobalMutableState
static GlobalMutableState::LinksPageState state;

static void LoadDefaultFilterAndLeaves() {
    if (!state.default_filter_i.has_value()) {
        state.default_filter_i = std::optional(state.filters.size());
        state.filters.push_back(Filter {
            .name = DEFAULT_FILTER_NAME,
            // .leaves = (default)
            .is_open = true,
        });
    }

    auto& filter = state.filters[*state.default_filter_i]; // Guaranteed to not be std::nullopt
    filter.leaves.clear();
    filter.leaves.reserve(sizeof(Leaf) * N_DEFAULT_FILTER_LEAVES);
    for (size_t i = 0; i < N_DEFAULT_FILTER_LEAVES; i++) {
        auto [name, target] = DEFAULT_FILTER_LEAVES[i];
        filter.leaves.push_back(Leaf {
            .name = name,
            .target = target,
        });
    }

    state.selection.SelectFilter(*state.default_filter_i, state.filters[*state.default_filter_i]);
}

namespace Json {
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

static void SaveLinksJson() {
    auto doc = yyjson_mut_doc_new(nullptr);
    if (doc == nullptr) {
        PanicOutOfMemory();
    }
    auto root = yyjson_mut_obj(doc);
    if (root == nullptr) {
        PanicOutOfMemory();
    }
    yyjson_mut_doc_set_root(doc, root);
    defer(yyjson_mut_doc_free(doc));

    // Metadata
    auto metadata_obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, metadata_obj, JSON_KEY_METADATA_VERSION, JSON_FILE_VERSION);
    yyjson_mut_obj_add_val(doc, root, JSON_KEY_METADATA, metadata_obj);

    // Filters/leaves
    for (auto const& filter : state.filters) {
        auto filter_obj = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, filter_obj, JSON_KEY_IS_OPEN, filter.is_open);
        for (auto const& leaf : filter.leaves) {
            yyjson_mut_obj_add_str(doc, filter_obj, leaf.name.c_str(), leaf.target.c_str());
        }
        yyjson_mut_obj_add_val(doc, root, filter.name.c_str(), filter_obj);
    }

    FILE* f;
    // We could theoretically use "w" mode here, since yyjson will happily ignore the CR characters
    // the next time it reads the file. But there's no real benefit to doing this.
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

// TODO: Extract the "read the JSON object" part of this function into a different function, so that
// the user's old links can be migrated to the new format.
static void LoadLinksJson() {
    FILE* f;
    // We can't use "r" mode, because yyjson_read_fp() expects EOL to be exactly 1 byte in size.
    // (See yyjson.c:6327, where it compares the number of characters read to the size of the file
    // in bytes, and errors out because they're not the same.)
    auto io_error = _wfopen_s(&f, GetLinksJsonFilePath().c_str(), L"rb");
    if (io_error != NULL) {
        if (io_error == ERROR_FILE_NOT_FOUND) {
            // JSON file doesn't exist, so create it.
            LoadDefaultFilterAndLeaves();
            SaveLinksJson();
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
        if (strcmp(yyjson_get_str(filter_k), JSON_KEY_METADATA) == 0) {
            // TODO: Parse metadata and version number.
            //  * If version number is missing, assume version 0.
            //  * If version number is lower than JSON_FILE_VERSION, auto-upgrade the file. (Make
            //    sure the file gets saved immediately afterwards in this case!)
            //  * If version number is HIGHER than JSON_FILE_VERSION, error out, so that we don't
            //    overwrite any newer configs. (Realistically this should only ever happen to thprac
            //    devs, but an annoyance is an annoyance.)
            continue;
        }
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
            bool k_is_is_open = strcmp(yyjson_get_str(leaf_k), JSON_KEY_IS_OPEN) == 0;
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

        state.filters.push_back(filter);
    }
}
} // namespace Json

static bool NameIsReserved(const char* name) {
    for (auto reserved : RESERVED_NAMES) {
        if (strncmp(name, reserved, INPUT_CHARS_MAX) == 0) {
            return true;
        }
    }
    return false;
}

inline bool LastEditErrorWasNameRelated() {
    return state.input_error == EditError::MissingName
        || state.input_error == EditError::DuplicateName
        || state.input_error == EditError::ReservedName;
}

static void LocateDefaultFilter() {
    // TODO: Rather than calling this function every time state.filters is mutated, it might be
    // worthwhile to put a wrapper class around state.filters, and have it do its own bookkeeping.
    state.default_filter_i = std::nullopt;
    for (size_t i = 0; i < state.filters.size(); i++) {
        const auto& filter = state.filters[i];
        if (filter.name == DEFAULT_FILTER_NAME) {
            state.default_filter_i = std::optional(i);
            break;
        }
    }
}

static void OpenOrCloseAllFilters(bool should_be_open) {
    for (auto const& filter : state.filters) {
        filter.is_open = should_be_open;
    }
    Json::SaveLinksJson();

    if (!should_be_open && state.selection.ExistsAndIsLeaf()) {
        // Move selection to containing filter
        size_t filter_i = state.selection.GetFilterInfo().i;
        state.selection.SelectFilter(filter_i, state.filters[filter_i]);
    }
}

static bool TargetIsExecutable(const char* target, TargetType type) {
    // TODO: Would it be better to check against everything in PATHEXT?
    return type == TargetType::ExecutablePath && Utils::GetSuffixFromPath(target) == "exe";
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
        result.type = TargetType::ExecutablePath;
    } else {
        result.file = target;
        result.parameters = "";
        auto unified = Utils::GetUnifiedPath(target);
        if (isalpha(unified[0]) && unified[1] == ':' && unified[2] == '\\') {
            result.type = TargetType::NonExecutablePath;
        } else {
            result.type = TargetType::Url;
        }
    }
    return result;
}

/** Returns `true` on success, `false` otherwise. */
static bool ExecuteTarget(std::string const& target) {
    auto target_info = GetTargetInfo(target);
    auto target_file_w = utf8_to_utf16(target_info.file.c_str());
    auto target_parameters_w = utf8_to_utf16(target_info.parameters.c_str());
    HINSTANCE result = nullptr;
    std::wstring target_directory_w;

    switch (target_info.type) {
    case TargetType::ExecutablePath:
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
    case TargetType::Url:
    case TargetType::NonExecutablePath:
        result = ShellExecuteW(nullptr, nullptr, target_file_w.c_str(), nullptr, nullptr, SW_SHOW);
        break;
    }

    constexpr DWORD SUCCESS_THRESHOLD = 32;
    return ((DWORD)result > SUCCESS_THRESHOLD);
}

namespace Gui {
inline Utils::ButtonResult CornerOkCancelButtons() {
    return Utils::GuiCornerButtons(S(THPRAC_OK), S(THPRAC_CANCEL), ImVec2(1.0f, 0.0f), true);
}

inline bool CornerOkButton() {
    auto result = Utils::GuiCornerButtons(S(THPRAC_OK), nullptr, ImVec2(1.0f, 0.0f), true);
    return result == Utils::ButtonResult::LeftPressed;
}

static bool ButtonRestoreDefaultLinksCentered() {
    auto label = S(THPRAC_LINKS_RESTORE_DEFAULT);
    float padding = ImGui::GetStyle().FramePadding.x;
    float button_width = ImGui::CalcTextSize(label).x + (padding * 2.0f);
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - button_width) * 0.5f);
    return ImGui::Button(label);
}

static void ClearEditPopupState() {
    state.input_target[0] = state.input_name[0] = state.input_target_parameters[0] = '\0';
    state.input_target_type = TargetType::Url;
    state.input_error = EditError::Ok;
}

static void EditPopupShowErrorIfApplicable(EditError error) {
    th_glossary_t error_message = A0000ERROR_C;
    switch (error) {
    case EditError::Ok:
        break;
    case EditError::MissingName:
        error_message = THPRAC_LINKS_EDIT_ERR_NAME;
        break;
    case EditError::MissingTarget:
        error_message = THPRAC_LINKS_EDIT_ERR_LINK;
        break;
    case EditError::DuplicateName:
        error_message = THPRAC_LINKS_EDIT_ERR_DUPLICATE;
        break;
    case EditError::ReservedName:
        error_message = THPRAC_LINKS_EDIT_ERR_RESERVED;
        break;
    }

    auto red = ImVec4(255.0f, 0.0f, 0.0f, 255.0f);
    ImGui::TextColored(red, "%s", S(error_message));
}

enum class EditResult {
    InProgress,
    Complete,
    Cancelled,
};
static EditResult EditLeafPopupMain() {
    ImGui::TextUnformatted(S(THPRAC_LINKS_EDIT_NAME));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText(LABEL_INPUT_NAME, state.input_name, INPUT_CHARS_MAX)) {
        // User changed the name
        if (LastEditErrorWasNameRelated()) {
            state.input_error = EditError::Ok;
        }
    }

    ImGui::TextUnformatted(S(THPRAC_LINKS_EDIT_LINK));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (state.input_target_type == TargetType::Url) {
        if (ImGui::InputText(LABEL_INPUT_TARGET, state.input_target, INPUT_CHARS_MAX)) {
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
        ImGui::InputText(
            LABEL_INPUT_TARGET_PARAMETERS,
            state.input_target_parameters,
            INPUT_CHARS_MAX
        );
    }
    EditPopupShowErrorIfApplicable(state.input_error);

    if (ImGui::Button(S(THPRAC_LINKS_BUTTON_SELECT_FILE))) {
        auto file_str = Utils::WindowsFilePicker();
        if (file_str.length() > 0) {
            state.input_error = EditError::Ok;
            // TODO: Is this a safe assumption to make?
            state.input_target_type = TargetType::ExecutablePath;
            state.input_target_parameters[0] = '\0';
            sprintf_s(
                state.input_target,
                INPUT_CHARS_MAX,
                "%s",
                utf16_to_utf8(file_str.c_str()).c_str()
            );
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(S(THPRAC_LINKS_BUTTON_SELECT_FOLDER))) {
        auto maybe_folder = Utils::WindowsFolderPicker();
        if (maybe_folder.has_value()) {
            state.input_error = EditError::Ok;
            state.input_target_type = TargetType::NonExecutablePath;
            sprintf_s(
                state.input_target,
                INPUT_CHARS_MAX,
                "%s",
                utf16_to_utf8((*maybe_folder).c_str()).c_str()
            );
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(S(THPRAC_LINKS_BUTTON_MANUAL_INPUT))) {
        state.input_error = EditError::Ok;
        state.input_target[0] = '\0';
        state.input_target_type = TargetType::Url;
    }
    ImGui::SameLine();

    auto button_clicked = CornerOkCancelButtons();
    if (button_clicked == Utils::ButtonResult::NonePressed) {
        return EditResult::InProgress;
    } else if (button_clicked == Utils::ButtonResult::RightPressed) {
        // User clicked "Cancel"
        return EditResult::Cancelled;
    }
    // User clicked "OK"
    if (strnlen_s(state.input_name, INPUT_CHARS_MAX) == 0) {
        state.input_error = EditError::MissingName;
    } else if (strnlen_s(state.input_target, INPUT_CHARS_MAX) == 0) {
        state.input_error = EditError::MissingTarget;
    } else if (NameIsReserved(state.input_name)) {
        state.input_error = EditError::ReservedName;
    } else if (state.selection.Exists()) {
        // TODO: It shouldn't be possible for a selection to NOT exist, because this function should
        // only be called if something is already selected.
        auto const& current_filter = state.filters[state.selection.GetFilterInfo().i];
        for (auto const& leaf : current_filter.leaves) {
            if (state.selection.Is(leaf)) {
                // Don't error for a "duplicate" of the leaf we're editing.
                continue;
            }
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
}
static EditResult EditFilterPopupMain() {
    ImGui::TextUnformatted(S(THPRAC_LINKS_EDIT_NAME));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText(LABEL_INPUT_NAME, state.input_name, INPUT_CHARS_MAX)) {
        // User changed the name
        if (LastEditErrorWasNameRelated()) {
            state.input_error = EditError::Ok;
        }
    }
    EditPopupShowErrorIfApplicable(state.input_error);

    auto button_clicked = CornerOkCancelButtons();
    if (button_clicked == Utils::ButtonResult::NonePressed) {
        return EditResult::InProgress;
    } else if (button_clicked == Utils::ButtonResult::RightPressed) {
        // User clicked "Cancel"
        return EditResult::Cancelled;
    }
    // User clicked "OK"
    if (strnlen_s(state.input_name, INPUT_CHARS_MAX) == 0) {
        state.input_error = EditError::MissingName;
    } else if (NameIsReserved(state.input_name)) {
        state.input_error = EditError::ReservedName;
    } else {
        for (auto const& filter : state.filters) {
            if (state.selection.Is(filter)) {
                // Don't error for a "duplicate" of the filter we're editing.
                continue;
            }
            if (filter.name == state.input_name) {
                state.input_error = EditError::DuplicateName;
                break;
            }
        }
    }
    if (state.input_error != EditError::Ok) {
        return EditResult::InProgress;
    }
    return EditResult::Complete;
}

// TODO: Maybe this should take a UiAction parameter instead of reading global state? The reset to
// UiAction::None should be handled by the caller.
static void HandleUiAction() {
    th_glossary_t popup_str_id = A0000ERROR_C;
    switch (state.ui_action) {
    case UiAction::None:
        break;
    case UiAction::AddLeaf:
        ClearEditPopupState();
        // TODO: Not THPRAC_LINKS_ADD_MODAL?
        popup_str_id = THPRAC_LINKS_ADD;
        break;
    case UiAction::EditLeaf:
        ClearEditPopupState();
        {
            auto [filter_i, leaf_i] = state.selection.GetLeafInfo();
            auto const& leaf = state.filters[filter_i].leaves[leaf_i];
            auto info = GetTargetInfo(leaf.target);
            state.input_target_type = info.type;
            strncpy_s(state.input_name, leaf.name.c_str(), INPUT_CHARS_MAX);
            strncpy_s(state.input_target, info.file.c_str(), INPUT_CHARS_MAX);
            strncpy_s(state.input_target_parameters, info.parameters.c_str(), INPUT_CHARS_MAX);
        }
        // TODO: Not THPRAC_LINKS_EDIT_MODAL?
        popup_str_id = THPRAC_LINKS_EDIT;
        break;
    case UiAction::DeleteLeaf:
        popup_str_id = THPRAC_LINKS_DELETE_MODAL;
        break;
    case UiAction::AddFilter:
        ClearEditPopupState();
        popup_str_id = THPRAC_LINKS_FILTER_ADD_MODAL;
        break;
    case UiAction::RenameFilter:
        ClearEditPopupState();
        {
            auto const& filter = state.filters[state.selection.GetFilterInfo().i];
            strncpy_s(state.input_name, filter.name.c_str(), INPUT_CHARS_MAX);
        }
        popup_str_id = THPRAC_LINKS_FILTER_RENAME_MODAL;
        break;
    case UiAction::DeleteFilter:
        popup_str_id = THPRAC_LINKS_FILTER_DEL_MODAL;
        break;
    case UiAction::OpenAllFilters:
        OpenOrCloseAllFilters(true);
        break;
    case UiAction::CloseAllFilters:
        OpenOrCloseAllFilters(false);
        break;
    case UiAction::RestoreDefaultFilter:
        LoadDefaultFilterAndLeaves();
        Json::SaveLinksJson();
        break;
    case UiAction::ErrorDuplicateName:
        popup_str_id = THPRAC_LINKS_ERR_MOVE_MODAL;
        break;
    case UiAction::ErrorExecutingTarget:
        popup_str_id = THPRAC_LINKS_ERR_EXEC_MODAL;
        break;
    }

    if (popup_str_id != A0000ERROR_C) {
        ImGui::OpenPopup(S(popup_str_id));
    }
    state.ui_action = UiAction::None;

    // TODO: The bodies of these if statements should probably be their own functions.
    auto modal_size_rel = ImVec2(ImGui::GetIO().DisplaySize.x * 0.9f, 0.0f);
    if (Modal(S(THPRAC_LINKS_ADD), modal_size_rel)) {
        auto result = EditLeafPopupMain();
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
                if (state.selection.ExistsAndIsLeaf()) {
                    insert_i = state.selection.GetLeafInfo().leaf_i;
                }
                size_t filter_i = state.selection.GetFilterInfo().i;
                auto& current_filter = state.filters[filter_i];
                current_filter.leaves.insert(
                    current_filter.leaves.begin() + (int)insert_i,
                    std::move(new_leaf)
                );
                // Make sure the user can see the new leaf
                current_filter.is_open = true;
                // Select newly-added leaf
                state.selection.SelectLeaf(filter_i, insert_i, current_filter.leaves[insert_i]);
                Json::SaveLinksJson();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (Modal(S(THPRAC_LINKS_EDIT), modal_size_rel)) {
        auto result = EditLeafPopupMain();
        if (result != EditResult::InProgress) {
            if (result == EditResult::Complete) {
                auto final_target = WrapTarget(
                    state.input_target,
                    state.input_target_parameters,
                    state.input_target_type
                );
                auto [filter_i, leaf_i] = state.selection.GetLeafInfo();
                auto& leaf_to_edit = state.filters[filter_i].leaves[leaf_i];
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
            auto [filter_i, leaf_i] = state.selection.GetLeafInfo();
            auto& current_filter = state.filters[filter_i];
            current_filter.leaves.erase(current_filter.leaves.begin() + (int)leaf_i);
            // Select containing filter
            state.selection.SelectFilter(filter_i, current_filter);
            Json::SaveLinksJson();
        }
        ImGui::EndPopup();
    }

    modal_size_rel = ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, 0.0f);
    if (Modal(S(THPRAC_LINKS_FILTER_ADD_MODAL), modal_size_rel)) {
        auto result = EditFilterPopupMain();
        if (result != EditResult::InProgress) {
            if (result == EditResult::Complete) {
                Filter new_filter = {
                    .name = state.input_name,
                    // .leaves = (default)
                    .is_open = true,
                };
                size_t insert_i = state.filters.size();
                if (state.selection.Exists()) {
                    insert_i = state.selection.GetFilterInfo().i;
                }
                state.filters.insert(state.filters.begin() + (int)insert_i, std::move(new_filter));
                // Select newly-added filter
                state.selection.SelectFilter(insert_i, state.filters[insert_i]);
                Json::SaveLinksJson();
                LocateDefaultFilter();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (Modal(S(THPRAC_LINKS_FILTER_RENAME_MODAL), modal_size_rel)) {
        auto result = EditFilterPopupMain();
        if (result != EditResult::InProgress) {
            if (result == EditResult::Complete) {
                auto& filter_to_edit = state.filters[state.selection.GetFilterInfo().i];
                if (filter_to_edit.name == DEFAULT_FILTER_NAME) {
                    state.default_filter_i = std::nullopt;
                }
                filter_to_edit.name = state.input_name;
                Json::SaveLinksJson();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (Modal(S(THPRAC_LINKS_FILTER_DEL_MODAL) /* (Default size) */)) {
        ImGui::TextUnformatted(S(THPRAC_LINKS_FILTER_DELETE_WARNING));
        if (YesNoChoice(S(THPRAC_YES), S(THPRAC_NO), 6.0f)) {
            size_t filter_i = state.selection.GetFilterInfo().i;
            if (state.filters[filter_i].name == DEFAULT_FILTER_NAME) {
                state.default_filter_i = std::nullopt;
            }
            state.filters.erase(state.filters.begin() + (int)state.selection.GetFilterInfo().i);
            // Unclear what should be selected at this point
            state.selection.Deselect();
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

enum class ContextMenuSource {
    Background,
    Filter,
    Leaf,
};
static bool ShowContextMenuIfApplicable(ContextMenuSource source) {
	bool active = false;
	if (source == ContextMenuSource::Background) {
		active = ImGui::BeginPopupContextWindow();
	} else /* ContextMenuSource::Filter || ContextMenuSource::Leaf */ {
		active = ImGui::BeginPopupContextItem();
	}
    if (!active) {
        return false;
    }
    defer(ImGui::EndPopup());

    #define OPTION(glossary_id, action) \
		if (ImGui::Selectable(S(glossary_id))) { \
			state.ui_action = action; \
		}

    switch (source) {
    case ContextMenuSource::Background:
        OPTION(THPRAC_LINKS_FILTER_ADD, UiAction::AddFilter);
        if (!state.default_filter_i.has_value()) {
            OPTION(THPRAC_LINKS_RESTORE_DEFAULT, UiAction::RestoreDefaultFilter);
        }
        break;
    case ContextMenuSource::Filter:
        OPTION(THPRAC_LINKS_FILTER_RENAME, UiAction::RenameFilter);
        OPTION(THPRAC_LINKS_FILTER_DEL, UiAction::DeleteFilter);
        OPTION(THPRAC_LINKS_ADD, UiAction::AddLeaf);
        ImGui::Separator();
        OPTION(THPRAC_LINKS_FILTER_ADD, UiAction::AddFilter);
        break;
    case ContextMenuSource::Leaf:
        OPTION(THPRAC_LINKS_EDIT, UiAction::EditLeaf);
        OPTION(THPRAC_LINKS_DELETE, UiAction::DeleteLeaf);
        ImGui::Separator();
        OPTION(THPRAC_LINKS_ADD, UiAction::AddLeaf);
        ImGui::Separator();
        OPTION(THPRAC_LINKS_FILTER_ADD, UiAction::AddFilter);
        break;
    }

    #undef OPTION
    return true;
}

static void ActionToolbarMain() {
    // My hope is that this code is so awful I'm never allowed to write ImGui code again.
    auto padding = ImGui::GetStyle().FramePadding;
    float font_size = ImGui::GetFontSize();

    // Evil hack to ignore window padding and draw the toolbar across the entire window...
    // in theory. In practice, a little bit of padding still remains, and I have no idea why.
    // Inspired by: https://github.com/ocornut/imgui/issues/3226#issuecomment-863335922
    float parent_padding_x = ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(0);
    float width = ImGui::GetWindowWidth();
    // Multiply padding by 4 instead of 2, because buttons are also padded internally
    float height = ImGui::GetFontSize() + (padding.y * 4.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_MenuBarBg));
    ImGui::BeginChild(
        LABEL_ACTION_TOOLBAR,
        ImVec2(width, height),
        false,
        ImGuiWindowFlags_NoScrollbar
    );
    ImGui::PopStyleColor();
    ImGui::SetCursorPos(ImVec2(parent_padding_x, padding.y));

    // Make buttons appear as only text until hovered
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_MenuBarBg));
    // The buttons will be exactly next to each other, so increase their internal padding
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(font_size, padding.y));

    // Obviously a bit overkill for only 3 buttons, but I plan on adding more later.
    #define OPTION(glossary_id, action) \
        if (ImGui::Button(S(glossary_id))) { \
            state.ui_action = action; \
        } \
        ImGui::SameLine(0.0f, 0.0f);

    OPTION(THPRAC_LINKS_BUTTON_EXPAND_ALL, UiAction::OpenAllFilters);
    OPTION(THPRAC_LINKS_BUTTON_COLLAPSE_ALL, UiAction::CloseAllFilters);
    if (!state.selection.Exists() && !state.default_filter_i.has_value()) {
        OPTION(THPRAC_LINKS_RESTORE_DEFAULT, UiAction::RestoreDefaultFilter);
    }

    #undef OPTION
    ImGui::PopStyleVar();

    if constexpr (false) { // TODO: Actually implement this.
    // If there's still room on the toolbar to do so, show this button, aligned right.
    float help_button_width = ImGui::CalcTextSize("Help").x + (padding.x * 2.0f);
    float space_required = help_button_width + parent_padding_x;
    if (ImGui::GetContentRegionAvail().x >= space_required) {
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionWidth() - space_required);
        if (ImGui::Button("Help")) {
            // ...
            state.selection.Deselect();
        }
    }
    }

    ImGui::PopStyleColor();
    ImGui::EndChild();
}
} // namespace Gui

void LauncherLinksUiUpdate() {
    // TODO: Rename this function to match the other pages' convention.
    static bool page_initialized = false;
    if (!page_initialized) [[unlikely]] {
        Json::LoadLinksJson();
        LocateDefaultFilter();
        page_initialized = true;
    }
    state.page_active = true;

    // TODO: Don't use -1 to mean "invalid index".
    // TODO: Apparently [0] is the index for filters, and [1] for leaves?
    int move_destination_indexes[2] = {-1, -1}; // "destIdx"
    int filter_destination_index = -1; // "filterDestIdx"

    if (state.filters.size() > 0) {
        Gui::ActionToolbarMain();
    }
    ImGui::BeginChild(LABEL_LINKS);
    defer(ImGui::EndChild());

    defer(Gui::HandleUiAction());
    if (Gui::ShowContextMenuIfApplicable(Gui::ContextMenuSource::Background)) {
        state.selection.Deselect();
    }
    if (state.filters.size() == 0) {
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() * 0.5f - ImGui::GetFontSize());
        Gui::TextCentered(S(THPRAC_GAMES_MISSING), ImGui::GetWindowWidth());
        if (Gui::ButtonRestoreDefaultLinksCentered()) {
            state.ui_action = UiAction::RestoreDefaultFilter;
        }
        return;
    }
    ImGui::Columns(2, LABEL_COL_FILTERS, true, true);

    for (size_t filter_i = 0; filter_i < state.filters.size(); filter_i++) {
        auto const& filter = state.filters[filter_i];

        // Override ImGui's usual on-click behavior for TreeNodes (we're doing it ourselves).
        // Modifying filter.is_open will take effect on the next UI update, which is slightly janky,
        // but good enough for our purposes.
        ImGui::SetNextItemOpen(filter.is_open, ImGuiCond_Always);
        int filter_flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
        if (state.selection.Is(filter)) {
            filter_flags |= ImGuiTreeNodeFlags_Selected;
        }

        auto current_filter_is_open = ImGui::TreeNodeEx(filter.name.c_str(), filter_flags);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            if (filter.is_open != current_filter_is_open) {
                // User clicked the arrow
                state.selection.SelectFilter(filter_i, filter);
                filter.is_open = !filter.is_open;
                Json::SaveLinksJson();
            } else {
                // User clicked the filter, but not the arrow
                if (!state.selection.Is(filter)) {
                    state.selection.SelectFilter(filter_i, filter);
                } else {
                    filter.is_open = !filter.is_open;
                    Json::SaveLinksJson();
                }
            }
        }
        if (ImGui::BeginDragDropSource()) {
            state.filter_move_index = (int)filter_i;
            ImGui::SetDragDropPayload(
                LABEL_DND_LINK_FILTER,
                &state.move_indexes, // ???
                sizeof(state.move_indexes)
            );
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            auto payload = ImGui::AcceptDragDropPayload(LABEL_DND_LINK_FILTER);
            if (payload != nullptr) {
                filter_destination_index = (int)filter_i;
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginDragDropTarget()) {
            auto payload = ImGui::AcceptDragDropPayload(LABEL_DND_LINK_LEAF);
            if (payload != nullptr) {
                move_destination_indexes[0] = (int)filter_i;
                move_destination_indexes[1] = 0;
            }
            ImGui::EndDragDropTarget();
        }

        if (Gui::ShowContextMenuIfApplicable(Gui::ContextMenuSource::Filter)) {
            state.selection.SelectFilter(filter_i, filter);
        }
        ImGui::NextColumn();
        ImGui::NextColumn();

        // TODO: Maybe extract this into its own function?
        if (current_filter_is_open) {
            for (size_t leaf_i = 0; leaf_i < filter.leaves.size(); leaf_i++) {
                auto const& leaf = filter.leaves[leaf_i];

                int leaf_flags = ImGuiTreeNodeFlags_Leaf
                    | ImGuiTreeNodeFlags_NoTreePushOnOpen
                    | ImGuiTreeNodeFlags_SpanAvailWidth;
                if (state.selection.Is(leaf)) {
                    leaf_flags |= ImGuiTreeNodeFlags_Selected;
                }
                ImGui::TreeNodeEx((void*)(intptr_t)leaf_i, leaf_flags, "%s", leaf.name.c_str());

                if (ImGui::IsItemHovered()) {
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        state.selection.SelectLeaf(filter_i, leaf_i, leaf);
                        if (!ExecuteTarget(leaf.target)) {
                            state.ui_action = UiAction::ErrorExecutingTarget;
                        }
                    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        state.selection.SelectLeaf(filter_i, leaf_i, leaf);
                    }
                }

                if (ImGui::BeginDragDropSource()) {
                    state.move_indexes[0] = (int)filter_i;
                    state.move_indexes[1] = (int)leaf_i;
                    ImGui::SetDragDropPayload(
                        LABEL_DND_LINK_LEAF,
                        &state.move_indexes, // ???
                        sizeof(state.move_indexes)
                    );
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget()) {
                    auto payload = ImGui::AcceptDragDropPayload(LABEL_DND_LINK_LEAF);
                    if (payload != nullptr) {
                        move_destination_indexes[0] = (int)filter_i;
                        move_destination_indexes[1] = (int)leaf_i;
                    }
                    ImGui::EndDragDropTarget();
                }

                if (Gui::ShowContextMenuIfApplicable(Gui::ContextMenuSource::Leaf)) {
                    state.selection.SelectLeaf(filter_i, leaf_i, leaf);
                }

                ImGui::NextColumn();
                Utils::GuiColumnText(leaf.target.c_str());
                ImGui::NextColumn();
            }
            ImGui::TreePop();
        }
    }

    ImGui::Columns(1);

    // TODO: It looks like these conditionals are where the actual swapping happens. Maybe extract
    // these into functions? (Also, this first one can probably be a rotate instead.)
    if (filter_destination_index >= 0) {
        // TODO: I'm pretty sure this unconditionally makes a copy. Why?
        auto filter_temp = state.filters[(size_t)state.filter_move_index];
        if (filter_destination_index > state.filter_move_index) {
            state.filters.insert(
                state.filters.begin() + filter_destination_index + 1,
                std::move(filter_temp)
            );
            state.filters.erase(state.filters.begin() + state.filter_move_index);
        } else if (filter_destination_index < state.filter_move_index) {
            state.filters.insert(
                state.filters.begin() + filter_destination_index,
                std::move(filter_temp)
            );
            state.filters.erase(state.filters.begin() + state.filter_move_index + 1);
        }

        // Select the now-moved filter (TODO: Except that it might not have been moved...?)
        state.selection.SelectFilter(
            (size_t)filter_destination_index,
            state.filters[(size_t)filter_destination_index]
        );
        Json::SaveLinksJson();
        LocateDefaultFilter();
    }

    if (move_destination_indexes[0] >= 0 && move_destination_indexes[1] >= 0) {
        // TODO: I'm pretty sure this unconditionally makes a copy. Why?
        // TODO: This is gross.
        auto leaf_temp = state.filters[
            (size_t)state.move_indexes[0]
        ].leaves[
            (size_t)state.move_indexes[1]
        ];
        auto& source_filter = state.filters[(size_t)state.move_indexes[0]];
        auto& source_leaves = source_filter.leaves;
        auto& destination_filter = state.filters[(size_t)move_destination_indexes[0]];
        auto& destination_leaves = destination_filter.leaves;

        if (state.move_indexes[0] != move_destination_indexes[0]) {
            for (auto const& leaf : destination_leaves) {
                if (leaf.name == leaf_temp.name) {
                    state.ui_action = UiAction::ErrorDuplicateName;
                    break;
                }
            }
        }

        if (state.ui_action != UiAction::ErrorDuplicateName) {
            if (state.move_indexes[0] == move_destination_indexes[0]) {
                // Source and destination filters are the same
                // TODO: This can probably be a rotate?
                if (move_destination_indexes[1] > state.move_indexes[1]) {
                    destination_leaves.insert(
                        destination_leaves.begin() + move_destination_indexes[1] + 1,
                        std::move(leaf_temp)
                    );
                    source_leaves.erase(source_leaves.begin() + state.move_indexes[1]);
                } else {
                    destination_leaves.insert(
                        destination_leaves.begin() + move_destination_indexes[1],
                        std::move(leaf_temp)
                    );
                    source_leaves.erase(source_leaves.begin() + state.move_indexes[1] + 1);
                }
            } else {
                // Source and destination filters are different
                destination_leaves.insert(
                    destination_leaves.begin() + move_destination_indexes[1],
                    std::move(leaf_temp)
                );
                source_leaves.erase(source_leaves.begin() + state.move_indexes[1]);
            }

            // Make sure the user can see where the leaf was moved to
            destination_filter.is_open = true;
            // Select the now-moved leaf (TODO: Except that it might not have been moved...?)
            state.selection.SelectLeaf(
                (size_t)move_destination_indexes[0],
                (size_t)move_destination_indexes[1],
                destination_leaves[(size_t)move_destination_indexes[1]]
            );
        }
    }

    if (
        ImGui::IsWindowFocused() && ImGui::IsWindowHovered()
        && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()
        && !ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()
    ) {
        // User clicked the background - clear selection
        state.selection.Deselect();
    }
}

void LauncherLinksInformPageClosing() {
    if (!state.page_active) [[likely]] {
        return;
    }
    state.selection.Deselect();
    state.page_active = false;
}
} // namespace THPrac
