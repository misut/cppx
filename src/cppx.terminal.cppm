// Pure terminal formatting primitives for CLI-facing tools. Real
// terminal detection and live rendering stay in cppx.terminal.system.

export module cppx.terminal;
import std;

export namespace cppx::terminal {

enum class StyleRole {
    accent,
    dim,
    success,
    warning,
    error,
    bold,
};

enum class StatusKind {
    run,
    ok,
    fail,
    timeout,
    skip,
};

enum class DiagnosticSeverity {
    info,
    warning,
    error,
};

enum class CapabilitySetting {
    auto_detect,
    always,
    never,
};

struct TerminalOptions {
    std::optional<CapabilitySetting> color;
    std::optional<CapabilitySetting> progress;
    std::optional<CapabilitySetting> unicode;
    std::optional<CapabilitySetting> hyperlinks;
    std::string_view color_env;
    std::string_view progress_env;
    std::string_view unicode_env;
    std::string_view hyperlinks_env;
    bool progress_allowed = true;
};

struct ProgressSnapshot {
    int done = 0;
    int total = 0;
    int percent = 0;
    double rate = 0.0;
    std::chrono::milliseconds elapsed{0};
    std::chrono::milliseconds remaining{0};
    std::string_view label;
    std::string detail;
    std::vector<std::string> detail_lines;
};

struct ProgressRenderOptions {
    bool color_enabled = false;
    bool unicode_enabled = false;
    bool show_bar = false;
    std::size_t bar_width = 18;
};

namespace ansi {

inline constexpr std::string_view reset = "\x1b[0m";
inline constexpr std::string_view bold = "\x1b[1m";
inline constexpr std::string_view dim = "\x1b[2m";
inline constexpr std::string_view red = "\x1b[31m";
inline constexpr std::string_view green = "\x1b[32m";
inline constexpr std::string_view yellow = "\x1b[33m";
inline constexpr std::string_view cyan = "\x1b[36m";
inline constexpr std::string_view bright_white = "\x1b[97m";

} // namespace ansi

std::optional<CapabilitySetting> parse_capability_setting(std::string_view value) {
    if (value == "auto")
        return CapabilitySetting::auto_detect;
    if (value == "always")
        return CapabilitySetting::always;
    if (value == "never")
        return CapabilitySetting::never;
    return std::nullopt;
}

std::string_view to_string(CapabilitySetting setting) {
    switch (setting) {
    case CapabilitySetting::auto_detect:
        return "auto";
    case CapabilitySetting::always:
        return "always";
    case CapabilitySetting::never:
        return "never";
    }
    return "auto";
}

std::string_view style_code(StyleRole role) {
    switch (role) {
    case StyleRole::accent:
        return ansi::cyan;
    case StyleRole::dim:
        return ansi::dim;
    case StyleRole::success:
        return ansi::green;
    case StyleRole::warning:
        return ansi::yellow;
    case StyleRole::error:
        return ansi::red;
    case StyleRole::bold:
        return ansi::bold;
    }
    return {};
}

std::string style(std::string_view text, StyleRole role, bool enabled) {
    if (!enabled)
        return std::string{text};
    return std::format("{}{}{}", style_code(role), text, ansi::reset);
}

std::string_view status_label(StatusKind status) {
    switch (status) {
    case StatusKind::run:
        return "RUN";
    case StatusKind::ok:
        return "OK";
    case StatusKind::fail:
        return "FAIL";
    case StatusKind::timeout:
        return "TIMEOUT";
    case StatusKind::skip:
        return "SKIP";
    }
    return "RUN";
}

StyleRole status_role(StatusKind status) {
    switch (status) {
    case StatusKind::run:
        return StyleRole::accent;
    case StatusKind::ok:
        return StyleRole::success;
    case StatusKind::fail:
    case StatusKind::timeout:
        return StyleRole::error;
    case StatusKind::skip:
        return StyleRole::dim;
    }
    return StyleRole::accent;
}

std::string status_cell(StatusKind status, bool color_enabled,
                        std::size_t width = 7) {
    auto label = status_label(status);
    auto padded = std::format("{:<{}}", label, width);
    return style(padded, status_role(status), color_enabled);
}

std::string_view status_symbol(StatusKind status, bool unicode_enabled) {
    if (!unicode_enabled) {
        switch (status) {
        case StatusKind::run:
            return ">";
        case StatusKind::ok:
            return "+";
        case StatusKind::fail:
            return "x";
        case StatusKind::timeout:
            return "!";
        case StatusKind::skip:
            return "-";
        }
        return ">";
    }

    switch (status) {
    case StatusKind::run:
        return "\u2022";
    case StatusKind::ok:
        return "\u2713";
    case StatusKind::fail:
        return "\u00d7";
    case StatusKind::timeout:
        return "\u23f1";
    case StatusKind::skip:
        return "\u2013";
    }
    return "\u2022";
}

std::string status_badge(StatusKind status, bool color_enabled = false,
                         bool unicode_enabled = false) {
    auto badge = std::format("{} {}", status_symbol(status, unicode_enabled),
                             status_label(status));
    return style(badge, status_role(status), color_enabled);
}

std::string key_value(std::string_view key, std::string_view value,
                      std::size_t width = 10) {
    return std::format("  {:<{}} {}", key, width, value);
}

std::string stage(std::string_view name, int index, int total,
                  std::string_view context = {}, bool color_enabled = false) {
    auto prefix = total > 0
        ? std::format("[{}/{}]", index, total)
        : std::string{"[ ]"};
    prefix = style(prefix, StyleRole::accent, color_enabled);

    if (context.empty())
        return std::format("{} {}", prefix, name);
    return std::format("{} [{}] {}", prefix, context, name);
}

std::string section(std::string_view title, bool color_enabled = false) {
    return style(title, StyleRole::bold, color_enabled);
}

std::string tail_excerpt(std::string_view text, std::size_t max_chars = 2000) {
    if (text.empty() || text.size() <= max_chars)
        return std::string{text};

    auto start = text.size() - max_chars;
    if (auto line_start = text.find('\n', start);
        line_start != std::string_view::npos && line_start + 1 < text.size()) {
        start = line_start + 1;
    }
    return std::format("...\n{}", text.substr(start));
}

std::string output_block_header(std::string_view heading,
                                bool color_enabled = false) {
    return style(std::format("---- {} ----", heading), StyleRole::dim,
                 color_enabled);
}

std::string osc8_hyperlink(std::string_view text, std::string_view uri,
                           bool enabled = false) {
    if (!enabled || text.empty() || uri.empty())
        return std::string{text};
    return std::format("\x1b]8;;{}\x1b\\{}\x1b]8;;\x1b\\", uri, text);
}

bool is_ascii_label(std::string_view text) {
    return std::ranges::all_of(text, [](unsigned char ch) {
        return ch >= 0x20 && ch <= 0x7e;
    });
}

void append_shimmer_char(std::string& out, char ch, bool active) {
    if (active) {
        out.append(ansi::bold);
        out.append(ansi::bright_white);
    } else {
        out.append(ansi::dim);
    }
    out.push_back(ch);
    out.append(ansi::reset);
}

std::string shimmer_label(std::string_view label, std::size_t frame_index,
                          bool color_enabled = false) {
    if (!color_enabled || label.empty() || !is_ascii_label(label))
        return std::string{label};

    auto const width = std::min<std::size_t>(2, label.size());
    auto const period = label.size() + width;
    auto const cursor = frame_index % period;

    auto out = std::string{};
    out.reserve(label.size() * 16);
    for (std::size_t index = 0; index < label.size(); ++index) {
        auto const active = cursor < label.size() &&
            index >= cursor && index < cursor + width;
        append_shimmer_char(out, label[index], active);
    }
    return out;
}

std::string format_progress_duration(std::chrono::milliseconds elapsed) {
    auto total_ms = elapsed.count();
    if (total_ms <= 0)
        return {};
    if (total_ms < 1000)
        return std::format("{}ms", total_ms);
    auto seconds = static_cast<double>(total_ms) / 1000.0;
    if (seconds < 10.0)
        return std::format("{:.1f}s", seconds);
    return std::format("{:.0f}s", seconds);
}

void append_progress_timing(std::string& out, ProgressSnapshot const& snapshot) {
    if (auto elapsed = format_progress_duration(snapshot.elapsed); !elapsed.empty())
        out += std::format(" elapsed {}", elapsed);
    if (snapshot.remaining.count() > 0) {
        if (auto remaining = format_progress_duration(snapshot.remaining); !remaining.empty())
            out += std::format(" eta {}", remaining);
    }
    if (snapshot.rate > 0.0)
        out += std::format(" {:.1f}/s", snapshot.rate);
}

void append_progress_detail_lines(std::string& out, ProgressSnapshot const& snapshot,
                                  bool color_enabled) {
    for (auto const& line : snapshot.detail_lines) {
        if (line.empty())
            continue;
        out.push_back('\n');
        out += style(std::format("    {}", line), StyleRole::dim, color_enabled);
    }
}

std::string_view spinner_frame(std::size_t frame_index, bool unicode_enabled) {
    if (unicode_enabled) {
        constexpr auto unicode_frames = std::array<std::string_view, 10>{
            "\u280b", "\u2819", "\u2839", "\u2838", "\u283c",
            "\u2834", "\u2826", "\u2827", "\u2807", "\u280f"};
        return unicode_frames[frame_index % unicode_frames.size()];
    }

    constexpr auto ascii_frames =
        std::array<std::string_view, 4>{"|", "/", "-", "\\"};
    return ascii_frames[frame_index % ascii_frames.size()];
}

std::string progress_bar(int percent, std::size_t width,
                         bool unicode_enabled) {
    if (width == 0)
        return {};
    auto clamped = std::clamp(percent, 0, 100);
    auto filled = static_cast<std::size_t>(
        (static_cast<std::int64_t>(clamped) * static_cast<std::int64_t>(width)) /
        100);
    auto out = std::string{};
    if (unicode_enabled) {
        for (std::size_t i = 0; i < filled; ++i)
            out += "\u2588";
        for (std::size_t i = filled; i < width; ++i)
            out += "\u2591";
        return out;
    }

    out.append(filled, '=');
    out.append(width - filled, '-');
    return out;
}

std::string progress_measure(ProgressSnapshot const& snapshot,
                             ProgressRenderOptions const& options) {
    if (snapshot.total <= 0)
        return {};
    if (options.show_bar && options.bar_width > 0) {
        return std::format("[{}] {}% [{}/{}]",
                           progress_bar(snapshot.percent, options.bar_width,
                                        options.unicode_enabled),
                           snapshot.percent, snapshot.done, snapshot.total);
    }
    return std::format("[{}/{} {}%]", snapshot.done, snapshot.total,
                       snapshot.percent);
}

std::string format_progress_frame(ProgressSnapshot const& snapshot,
                                  std::size_t frame_index,
                                  ProgressRenderOptions options) {
    auto spin = spinner_frame(frame_index, options.unicode_enabled);
    auto run = status_cell(StatusKind::run, options.color_enabled);
    if (snapshot.total <= 0) {
        auto label = snapshot.label.empty() ? std::string_view{"working"}
                                            : snapshot.label;
        auto out = std::format("  {} [{}] {}...", run, spin,
                               shimmer_label(label, frame_index,
                                             options.color_enabled));
        append_progress_timing(out, snapshot);
        if (!snapshot.detail.empty())
            out += std::format("\n{}", style(snapshot.detail, StyleRole::dim,
                                             options.color_enabled));
        append_progress_detail_lines(out, snapshot, options.color_enabled);
        return out;
    }

    auto measure = progress_measure(snapshot, options);
    auto out = snapshot.label.empty()
        ? std::format("  {} [{}] {}", run, spin, measure)
        : std::format("  {} [{}] {} {}", run, spin, measure,
                      shimmer_label(snapshot.label, frame_index,
                                    options.color_enabled));

    append_progress_timing(out, snapshot);
    if (!snapshot.detail.empty())
        out += std::format("\n{}", style(snapshot.detail, StyleRole::dim,
                                         options.color_enabled));
    append_progress_detail_lines(out, snapshot, options.color_enabled);
    return out;
}

std::string format_progress_frame(ProgressSnapshot const& snapshot,
                                  std::size_t frame_index,
                                  bool color_enabled = false) {
    return format_progress_frame(snapshot, frame_index, ProgressRenderOptions{
        .color_enabled = color_enabled,
    });
}

enum class KeyCode {
    unknown,
    character,
    enter,
    escape,
    backspace,
    tab,
    arrow_up,
    arrow_down,
    arrow_left,
    arrow_right,
    home,
    end,
    page_up,
    page_down,
    delete_key,
    ctrl_c,
    ctrl_d,
};

struct KeyEvent {
    KeyCode code = KeyCode::unknown;
    std::string text;
    bool alt = false;
};

std::vector<KeyEvent> parse_key_events(std::string_view bytes) {
    auto events = std::vector<KeyEvent>{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        auto ch = static_cast<unsigned char>(bytes[i]);
        if (ch == '\r' || ch == '\n') {
            events.push_back({.code = KeyCode::enter});
        } else if (ch == '\t') {
            events.push_back({.code = KeyCode::tab});
        } else if (ch == 0x7f || ch == '\b') {
            events.push_back({.code = KeyCode::backspace});
        } else if (ch == 0x03) {
            events.push_back({.code = KeyCode::ctrl_c});
        } else if (ch == 0x04) {
            events.push_back({.code = KeyCode::ctrl_d});
        } else if (ch == 0x1b) {
            if (i + 2 < bytes.size() && bytes[i + 1] == '[') {
                auto third = bytes[i + 2];
                switch (third) {
                case 'A':
                    events.push_back({.code = KeyCode::arrow_up});
                    i += 2;
                    continue;
                case 'B':
                    events.push_back({.code = KeyCode::arrow_down});
                    i += 2;
                    continue;
                case 'C':
                    events.push_back({.code = KeyCode::arrow_right});
                    i += 2;
                    continue;
                case 'D':
                    events.push_back({.code = KeyCode::arrow_left});
                    i += 2;
                    continue;
                case 'H':
                    events.push_back({.code = KeyCode::home});
                    i += 2;
                    continue;
                case 'F':
                    events.push_back({.code = KeyCode::end});
                    i += 2;
                    continue;
                case '3':
                case '5':
                case '6':
                    if (i + 3 < bytes.size() && bytes[i + 3] == '~') {
                        events.push_back({
                            .code = third == '3' ? KeyCode::delete_key
                                  : third == '5' ? KeyCode::page_up
                                                 : KeyCode::page_down,
                        });
                        i += 3;
                        continue;
                    }
                    break;
                default:
                    break;
                }
            }
            events.push_back({.code = KeyCode::escape});
        } else if (ch >= 0x20) {
            events.push_back({
                .code = KeyCode::character,
                .text = std::string{static_cast<char>(ch)},
            });
        } else {
            events.push_back({.code = KeyCode::unknown});
        }
    }
    return events;
}

class PromptComposer {
public:
    bool apply(KeyEvent const& event) {
        switch (event.code) {
        case KeyCode::character:
            buffer_.insert(cursor_, event.text);
            cursor_ += event.text.size();
            return true;
        case KeyCode::backspace:
            if (cursor_ == 0)
                return false;
            buffer_.erase(cursor_ - 1, 1);
            --cursor_;
            return true;
        case KeyCode::delete_key:
            if (cursor_ >= buffer_.size())
                return false;
            buffer_.erase(cursor_, 1);
            return true;
        case KeyCode::arrow_left:
            if (cursor_ > 0)
                --cursor_;
            return true;
        case KeyCode::arrow_right:
            if (cursor_ < buffer_.size())
                ++cursor_;
            return true;
        case KeyCode::home:
            cursor_ = 0;
            return true;
        case KeyCode::end:
            cursor_ = buffer_.size();
            return true;
        default:
            return false;
        }
    }

    void set(std::string value) {
        buffer_ = std::move(value);
        cursor_ = buffer_.size();
    }

    void clear() {
        buffer_.clear();
        cursor_ = 0;
    }

    std::string_view text() const { return buffer_; }
    std::size_t cursor() const { return cursor_; }

private:
    std::string buffer_;
    std::size_t cursor_ = 0;
};

class CommandHistory {
public:
    void push(std::string command) {
        if (command.empty())
            return;
        if (!entries_.empty() && entries_.back() == command) {
            reset();
            return;
        }
        entries_.push_back(std::move(command));
        reset();
    }

    std::optional<std::string_view> previous() {
        if (entries_.empty())
            return std::nullopt;
        if (!cursor_)
            cursor_ = entries_.size();
        if (*cursor_ == 0)
            return entries_.front();
        --*cursor_;
        return entries_[*cursor_];
    }

    std::optional<std::string_view> next() {
        if (!cursor_)
            return std::nullopt;
        if (*cursor_ + 1 >= entries_.size()) {
            reset();
            return std::nullopt;
        }
        ++*cursor_;
        return entries_[*cursor_];
    }

    void reset() {
        cursor_.reset();
    }

    std::span<std::string const> entries() const {
        return entries_;
    }

private:
    std::vector<std::string> entries_;
    std::optional<std::size_t> cursor_;
};

enum class InputKind {
    empty,
    prompt,
    slash_command,
    shell_command,
};

struct ClassifiedInput {
    InputKind kind = InputKind::empty;
    std::string body;
};

ClassifiedInput classify_input(std::string_view line) {
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front())))
        line.remove_prefix(1);
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
        line.remove_suffix(1);

    if (line.empty())
        return {};
    if (line.front() == '/') {
        line.remove_prefix(1);
        return {
            .kind = InputKind::slash_command,
            .body = std::string{line},
        };
    }
    if (line.front() == '!') {
        line.remove_prefix(1);
        return {
            .kind = InputKind::shell_command,
            .body = std::string{line},
        };
    }
    return {
        .kind = InputKind::prompt,
        .body = std::string{line},
    };
}

struct StatusLine {
    std::string label;
    std::string value;
    StatusKind status = StatusKind::run;
};

struct DiagnosticMessage {
    DiagnosticSeverity severity = DiagnosticSeverity::info;
    std::string message;
    std::string context;
    std::vector<std::string> hints;
};

std::string format_status_frame(std::span<StatusLine const> lines,
                                bool color_enabled = false) {
    auto out = std::string{};
    for (auto const& line : lines) {
        if (!out.empty())
            out.push_back('\n');
        out += std::format("  {} {}",
                           status_cell(line.status, color_enabled),
                           key_value(line.label, line.value, 10).substr(2));
    }
    return out;
}

std::string_view diagnostic_label(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::info:
        return "info";
    case DiagnosticSeverity::warning:
        return "warning";
    case DiagnosticSeverity::error:
        return "error";
    }
    return "info";
}

StyleRole diagnostic_role(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::info:
        return StyleRole::accent;
    case DiagnosticSeverity::warning:
        return StyleRole::warning;
    case DiagnosticSeverity::error:
        return StyleRole::error;
    }
    return StyleRole::accent;
}

std::string diagnostic_line(DiagnosticSeverity severity,
                            std::string_view message,
                            bool color_enabled = false) {
    auto label = std::format("{}:", diagnostic_label(severity));
    return std::format("{} {}",
                       style(label, diagnostic_role(severity), color_enabled),
                       message);
}

std::string hint_line(std::string_view message, bool color_enabled = false) {
    return std::format("{} {}",
                       style("hint:", StyleRole::dim, color_enabled),
                       message);
}

std::string format_diagnostic(DiagnosticMessage const& diagnostic,
                              bool color_enabled = false) {
    auto out = diagnostic.context.empty()
        ? diagnostic_line(diagnostic.severity, diagnostic.message, color_enabled)
        : diagnostic_line(
              diagnostic.severity,
              std::format("[{}] {}", diagnostic.context, diagnostic.message),
              color_enabled);

    for (auto const& hint : diagnostic.hints) {
        if (hint.empty())
            continue;
        out.push_back('\n');
        out += "  ";
        out += hint_line(hint, color_enabled);
    }
    return out;
}

std::string github_actions_data_escape(std::string_view value) {
    auto out = std::string{};
    for (auto ch : value) {
        switch (ch) {
        case '%':
            out += "%25";
            break;
        case '\r':
            out += "%0D";
            break;
        case '\n':
            out += "%0A";
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    return out;
}

std::string github_actions_property_escape(std::string_view value) {
    auto out = github_actions_data_escape(value);
    auto escaped = std::string{};
    escaped.reserve(out.size());
    for (auto ch : out) {
        if (ch == ':')
            escaped += "%3A";
        else if (ch == ',')
            escaped += "%2C";
        else
            escaped.push_back(ch);
    }
    return escaped;
}

std::string github_actions_group_start(std::string_view title) {
    return std::format("::group::{}", github_actions_data_escape(title));
}

std::string github_actions_group_end() {
    return "::endgroup::";
}

struct GithubActionsAnnotation {
    DiagnosticSeverity severity = DiagnosticSeverity::info;
    std::string message;
    std::string title;
    std::string file;
    int line = 0;
    int column = 0;
    int end_line = 0;
    int end_column = 0;
};

std::string_view github_actions_command(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::info:
        return "notice";
    case DiagnosticSeverity::warning:
        return "warning";
    case DiagnosticSeverity::error:
        return "error";
    }
    return "notice";
}

void append_github_property(std::vector<std::string>& properties,
                            std::string_view key,
                            std::string_view value) {
    if (value.empty())
        return;
    properties.push_back(std::format("{}={}", key,
                                     github_actions_property_escape(value)));
}

void append_github_property(std::vector<std::string>& properties,
                            std::string_view key,
                            int value) {
    if (value <= 0)
        return;
    properties.push_back(std::format("{}={}", key, value));
}

std::string github_actions_annotation(GithubActionsAnnotation const& annotation) {
    auto properties = std::vector<std::string>{};
    append_github_property(properties, "file", annotation.file);
    append_github_property(properties, "line", annotation.line);
    append_github_property(properties, "col", annotation.column);
    append_github_property(properties, "endLine", annotation.end_line);
    append_github_property(properties, "endColumn", annotation.end_column);
    append_github_property(properties, "title", annotation.title);

    auto property_text = std::string{};
    for (auto const& property : properties) {
        if (!property_text.empty())
            property_text.push_back(',');
        property_text += property;
    }
    if (!property_text.empty())
        property_text = " " + property_text;

    return std::format("::{}{}::{}",
                       github_actions_command(annotation.severity),
                       property_text,
                       github_actions_data_escape(annotation.message));
}

} // namespace cppx::terminal
