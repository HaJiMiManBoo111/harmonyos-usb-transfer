/*
 * DeskflowPoC -- HarmonyOS Deskflow protocol client
 * Client implementation: handshake + message loop + injection
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow_client.h"
#include "proto_util.h"
#include "protocol_types.h"

#include <cstdarg>
#include <cstring>
#include <chrono>
#include <sstream>
#include <utility>
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "hilog/log.h"
#include "multimodalinput/oh_input_manager.h"

namespace dfpoc {

namespace {

constexpr unsigned int DF_LOG_DOMAIN = 0xD002;
constexpr const char* DF_LOG_TAG = "DeskflowPoC";

// 诊断日志开关：编译期宏，1 输出每条消息的 INFO 级 recv 日志（高频，调协议时用），
// 0 仅保留 ERROR / 有限 INFO，避免正常使用时刷屏。
#define DF_VERBOSE_MESSAGE_LOG 0

#define DF_LOGI(...) OH_LOG_Print(LOG_APP, LOG_INFO, DF_LOG_DOMAIN, DF_LOG_TAG, __VA_ARGS__)
#define DF_LOGD(...)                                                     \
    do {                                                                 \
        if (DF_VERBOSE_MESSAGE_LOG) {                                    \
            OH_LOG_Print(LOG_APP, LOG_DEBUG, DF_LOG_DOMAIN, DF_LOG_TAG, __VA_ARGS__); \
        }                                                                \
    } while (0)
#define DF_LOGE(...) OH_LOG_Print(LOG_APP, LOG_ERROR, DF_LOG_DOMAIN, DF_LOG_TAG, __VA_ARGS__)
#define DF_LOGW(...) OH_LOG_Print(LOG_APP, LOG_WARN, DF_LOG_DOMAIN, DF_LOG_TAG, __VA_ARGS__)

// 注入授权异步回调（MMI IPC 线程）
void OnInjectAuthCallback(Input_InjectionStatus status)
{
    const char* text = "Unknown";
    switch (status) {
        case UNAUTHORIZED: text = "UNAUTHORIZED(0)"; break;
        case AUTHORIZING:  text = "AUTHORIZING(1)"; break;
        case AUTHORIZED:   text = "AUTHORIZED(2)"; break;
        default: break;
    }
    DF_LOGI("injection auth callback: %{public}s", text);
}

// Map X11 keysym (deskflow KeyID) to HarmonyOS KeyCode.
// deskflow KeyID: U+E000..U+EFFF for control keys (= X11 keysym - 0x1000),
// printable ASCII are their Unicode codepoints. Values from deskflow KeyTypes.h.
// HarmonyOS KeyCode values from oh_key_code.h. Returns -1 if no equivalent.
int32_t keysymToKeycode(uint32_t keysym)
{
    // deskflow 对 0xEF00~0xEFFF 区间的键以 16 位有符号值传输，到达时可能被符号扩展
    // 成 0xFFFFEFxx。必须掩码回 16 位才能命中下面的常量（例：Tab 0xEF09->0xFFFFEF09）。
    keysym &= 0xFFFF;

    // 可打印 ASCII：字母（大小写）/ 数字 / 空格与符号 -> 对应物理键
    // （Shift 等作为独立修饰键注入，字母可能带大写 keysym，统一映射到同一物理键）
    if ((keysym >= 'a' && keysym <= 'z') || (keysym >= 'A' && keysym <= 'Z')) {
        return KEYCODE_A + static_cast<int32_t>((keysym & 0x1F) - 1); // KEYCODE_A=2017
    }
    if (keysym >= '0' && keysym <= '9') {
        return KEYCODE_0 + static_cast<int32_t>(keysym - '0'); // KEYCODE_0=2000
    }
    switch (keysym) {
        case ' ':       return KEYCODE_SPACE;            // 2050
        case ',':       return KEYCODE_COMMA;            // 2043
        case '.':       return KEYCODE_PERIOD;           // 2044
        case '-':       return KEYCODE_MINUS;            // 2057
        case '=':       return KEYCODE_EQUALS;           // 2058
        case '[':       return KEYCODE_LEFT_BRACKET;     // 2059
        case ']':       return KEYCODE_RIGHT_BRACKET;    // 2060
        case '\\':      return KEYCODE_BACKSLASH;        // 2061
        case ';':       return KEYCODE_SEMICOLON;        // 2062
        case '\'':      return KEYCODE_APOSTROPHE;       // 2063
        case '/':       return KEYCODE_SLASH;            // 2064
        case '`':       return KEYCODE_GRAVE;            // 2056
        case '@':       return KEYCODE_AT;               // 2065
        case '+':       return KEYCODE_PLUS;             // 2066
        case 0x002a:    return KEYCODE_STAR;             // '*' (=2010)
        case 0x0023:    return KEYCODE_POUND;            // '#' (=2011)
    }

    // 光标/编辑键
    switch (keysym) {
        case kKeyBackSpace: return KEYCODE_DEL;          // 2055
        case kKeyTab:       return KEYCODE_TAB;          // 2049
        case kKeyReturn:    return KEYCODE_ENTER;        // 2054
        case kKeyEscape:    return KEYCODE_ESCAPE;       // 2070
        case kKeyHome:      return KEYCODE_MOVE_HOME;    // 2081
        case kKeyEnd:       return KEYCODE_MOVE_END;     // 2082
        case kKeyLeft:      return KEYCODE_DPAD_LEFT;    // 2014
        case kKeyUp:        return KEYCODE_DPAD_UP;      // 2012
        case kKeyRight:     return KEYCODE_DPAD_RIGHT;   // 2015
        case kKeyDown:      return KEYCODE_DPAD_DOWN;    // 2013
        case kKeyPageUp:    return KEYCODE_PAGE_UP;      // 2068
        case kKeyPageDown:  return KEYCODE_PAGE_DOWN;    // 2069
        case kKeyInsert:    return KEYCODE_INSERT;       // 2083
        case kKeyDelete:    return KEYCODE_FORWARD_DEL;  // 2071
        case kKeyClear:     return KEYCODE_MOVE_HOME;    // 用 HOME 近似清屏
        case kKeyLinefeed:  return KEYCODE_LINEFEED;     // 2609
        case kKeyLeftTab:   return KEYCODE_TAB;          // 反向 Tab
        case kKeySelect:    return KEYCODE_FORWARD;      // 2084
        case kKeyExecute:   return KEYCODE_MENU;         // 2067 近似
        case kKeyMenu:      return KEYCODE_MENU;         // 2067
        case kKeyFind:      return KEYCODE_FIND;         // 2623
        case kKeyCancel:    return KEYCODE_CANCEL;       // 2648
        case kKeyHelp:      return KEYCODE_HELP;         // 2625
        case kKeyUndo:      return KEYCODE_UNDO;         // 2619
        case kKeyRedo:      return KEYCODE_REDO;         // 2641
    }

    // 锁定/修饰键
    switch (keysym) {
        case kKeyCapsLock:    return KEYCODE_CAPS_LOCK;      // 2074
        case kKeyNumLock:     return KEYCODE_NUM_LOCK;       // 2102
        case kKeyScrollLock:  return KEYCODE_SCROLL_LOCK;    // 2075
        case kKeyShiftL:      return KEYCODE_SHIFT_LEFT;     // 2047
        case kKeyShiftR:      return KEYCODE_SHIFT_RIGHT;    // 2048
        case kKeyControlL:    return KEYCODE_CTRL_LEFT;      // 2072
        case kKeyControlR:    return KEYCODE_CTRL_RIGHT;     // 2073
        case kKeyAltL:        return KEYCODE_ALT_LEFT;       // 2045
        case kKeyAltR:        return KEYCODE_ALT_RIGHT;      // 2046
        case kKeyAltGr:       return KEYCODE_ALT_RIGHT;      // 用右 Alt 近似
        case kKeyMetaL:       return KEYCODE_META_LEFT;      // 2076
        case kKeyMetaR:       return KEYCODE_META_RIGHT;     // 2077
        case kKeySuperL:      return KEYCODE_META_LEFT;      // Super=Meta
        case kKeySuperR:      return KEYCODE_META_RIGHT;
    }

    // 系统键
    switch (keysym) {
        case kKeyPause:        return KEYCODE_BREAK;         // 2080
        case kKeySysReq:       return KEYCODE_SYSRQ;         // 2079
        case kKeyPrint:        return KEYCODE_PRINT;         // 2645
        case kKeyBreak:        return KEYCODE_BREAK;         // 2080
        case kKeySleep:        return KEYCODE_SLEEP;         // 2600
        case kKeyEject:        return KEYCODE_MEDIA_EJECT;   // 2088
        case kKeyZenkaku:      return KEYCODE_ZENKAKU_HANKAKU; // 2601
        case kKeyKana:         return KEYCODE_KATAKANA;      // 2604
        case kKeyHiraganaKatakana: return KEYCODE_KATAKANA_HIRAGANA; // 2607
        case kKeyHenkan:       return KEYCODE_HENKAN;        // 2606
        case kKeyHangul:       return KEYCODE_HANGUEL;       // 2613
        case kKeyHanja:        return KEYCODE_HANJA;         // 2614
    }

    // 功能键 F1..F35（HarmonyOS 只到 F24=2827）
    if (keysym >= kKeyF1 && keysym <= kKeyF24) {
        return KEYCODE_F1 + static_cast<int32_t>(keysym - kKeyF1); // 2090+
    }

    // 媒体/亮度/应用键（extended 0xE0xx，从 KeyTypes.h）
    switch (keysym) {
        case kKeyAudioMute:     return KEYCODE_VOLUME_MUTE;     // 22
        case kKeyAudioDown:     return KEYCODE_VOLUME_DOWN;       // 17
        case kKeyAudioUp:       return KEYCODE_VOLUME_UP;         // 16
        case kKeyAudioNext:     return KEYCODE_MEDIA_NEXT;        // 12
        case kKeyAudioPrev:     return KEYCODE_MEDIA_PREVIOUS;    // 13
        case kKeyAudioStop:     return KEYCODE_MEDIA_STOP;        // 11
        case kKeyAudioPlay:     return KEYCODE_MEDIA_PLAY_PAUSE;  // 10
        case kKeyAppMail:       return KEYCODE_ENVELOPE;          // 2053
        case kKeyAppMedia:      return KEYCODE_MEDIA_PLAY_PAUSE;  // 10 近似
        case kKeyBrightnessDown: return KEYCODE_BRIGHTNESS_DOWN;  // 41
        case kKeyBrightnessUp:  return KEYCODE_BRIGHTNESS_UP;     // 40
        case kKeyWWWBack:       return KEYCODE_FORWARD;           // 2084 近似
        case kKeyWWWForward:    return KEYCODE_FORWARDMAIL;       // 2654 近似
        case kKeyWWWHome:       return KEYCODE_HOME;              // 1 近似
        case kKeyWWWRefresh:    return KEYCODE_REFRESH;           // 2635
    }

    // 小键盘数字 0-9 与符号（Deskflow KeyID 0xEFB0..=KP_0，映射到 NUMPAD）
    if (keysym >= kKeyKP_0 && keysym <= kKeyKP_9) {
        return KEYCODE_NUMPAD_0 + static_cast<int32_t>(keysym - kKeyKP_0); // 2103+
    }
    switch (keysym) {
        case kKeyKP_Enter:     return KEYCODE_NUMPAD_ENTER;   // 2119
        case kKeyKP_Multiply:  return KEYCODE_NUMPAD_MULTIPLY;// 2114
        case kKeyKP_Add:       return KEYCODE_NUMPAD_ADD;     // 2116
        case kKeyKP_Separator: return KEYCODE_NUMPAD_COMMA;   // 2118
        case kKeyKP_Subtract:  return KEYCODE_NUMPAD_SUBTRACT;// 2115
        case kKeyKP_Decimal:   return KEYCODE_NUMPAD_DOT;     // 2117
        case kKeyKP_Divide:    return KEYCODE_NUMPAD_DIVIDE;  // 2113
        case kKeyKP_Equal:     return KEYCODE_NUMPAD_EQUALS;  // 2120
        case kKeyKP_Home:      return KEYCODE_MOVE_HOME;      // 2081
        case kKeyKP_End:       return KEYCODE_MOVE_END;       // 2082
        case kKeyKP_PageUp:    return KEYCODE_PAGE_UP;        // 2068
        case kKeyKP_PageDown:  return KEYCODE_PAGE_DOWN;      // 2069
        case kKeyKP_Insert:    return KEYCODE_INSERT;         // 2083
        case kKeyKP_Delete:    return KEYCODE_FORWARD_DEL;    // 2071
    }

    return -1;
}

int32_t buttonToHarmony(int32_t buttonId)
{
    switch (buttonId) {
        case kButtonLeft:   return MOUSE_BUTTON_LEFT;
        case kButtonMiddle: return MOUSE_BUTTON_MIDDLE;
        case kButtonRight:  return MOUSE_BUTTON_RIGHT;
        default: return -1;
    }
}

// Flux: OH_Input_InjectMouseEventGlobal rejects coordinate 0 (screen edge)
// with 401 and drops the whole event. Keep the stored pointer and every
// injected coordinate inside [1, dim-1]. Pure function, no state.
int32_t fluxClampPointer(int32_t v, int32_t dim)
{
    if (v < 1) return 1;
    if (v > dim - 1) return dim - 1;
    return v;
}

// ---- 文件接收（DFTR / DDRG）辅助 ----

// 落盘根目录：鸿蒙应用沙箱的 filesDir。C++ 侧拿不到 AbilityContext，
// 由 ArkTS 调 setFileSaveDir(context.filesDir + "/Deskflow") 覆盖；
// 未设置时退回这个标准路径（与 ETS 侧 filesDir 一致，见注释说明）。
constexpr const char* kDefaultFileSaveDir = "/data/storage/el2/base/haps/entry/files/Deskflow";

// 单个文件上限：沿用协议上限，防止对端声明一个超大文件把沙箱写满。
constexpr uint64_t kFileTransferMaxSize = static_cast<uint64_t>(kProtocolMaxMessageLength);

// DFTR 内层布局（全部小端）：
//   [0..3]  transferId
//   [4]     chunkType
//   [5..]   payload
constexpr size_t kDftrHeaderSize = 5;
constexpr uint8_t kDftrDataStart = 1;
constexpr uint8_t kDftrDataChunk = 2;
constexpr uint8_t kDftrDataEnd = 3;

uint32_t readLE32(const std::string& s, size_t off)
{
    const auto* p = reinterpret_cast<const unsigned char*>(s.data() + off);
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t readLE64(const std::string& s, size_t off)
{
    uint64_t v = 0;
    for (size_t i = 0; i < 8; ++i) {
        v |= static_cast<uint64_t>(reinterpret_cast<const unsigned char*>(s.data() + off)[i]) << (8 * i);
    }
    return v;
}

// 逐级创建目录（等价 mkdir -p）。已存在视为成功。
bool makeDirs(const std::string& path)
{
    if (path.empty()) {
        return false;
    }
    std::string cur;
    cur.reserve(path.size());
    size_t i = 0;
    // 保留开头的 '/'，避免把绝对路径拆成相对路径
    if (path[0] == '/') {
        cur.push_back('/');
        i = 1;
    }
    while (i < path.size()) {
        size_t next = path.find('/', i);
        if (next == std::string::npos) {
            next = path.size();
        }
        if (next > i) {
            if (!cur.empty() && cur.back() != '/') {
                cur.push_back('/');
            }
            cur.append(path, i, next - i);
            if (::mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) {
                DF_LOGE("mkdir '%{public}s' failed errno=%{public}d", cur.c_str(), errno);
                return false;
            }
        }
        i = next + 1;
    }
    return true;
}

// 取 basename：同时处理 Windows '\\' 与 POSIX '/' 分隔符。
std::string baseName(const std::string& rawPath)
{
    size_t cut = rawPath.find_last_of("\\/");
    if (cut == std::string::npos) {
        return rawPath;
    }
    return rawPath.substr(cut + 1);
}

// 清洗成可安全落盘的文件名（安全关键）：
//  - 取 basename，丢弃对端传来的全部目录层级 → 彻底阻断 ../ 路径穿越
//  - 去掉 Windows 非法字符 <>:"|?* 与控制字符（替换为 '_'）
//  - 去掉首尾空白与结尾的 '.'（Windows 上非法）
//  - 纯 "." / ".." / 空 → 视为无文件名
std::string sanitizeFileName(const std::string& rawPath)
{
    std::string name = baseName(rawPath);
    // 反斜杠与斜杠已由 baseName 去掉；这里再兜底一次，防止 "../.." 这类不含分隔符的残留
    if (name == "." || name == "..") {
        return "";
    }
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        const auto uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7f) {
            continue;   // 控制字符直接丢弃
        }
        switch (c) {
            case '<': case '>': case ':': case '"':
            case '|': case '?': case '*':
            case '/': case '\\':
                out.push_back('_');
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    // 去掉首尾空白
    size_t begin = out.find_first_not_of(" \t");
    size_t end = out.find_last_not_of(" \t");
    if (begin == std::string::npos) {
        return "";
    }
    out = out.substr(begin, end - begin + 1);
    // 去掉结尾的 '.'（Windows 不允许）
    while (!out.empty() && out.back() == '.') {
        out.pop_back();
    }
    return out;
}

// 在 dir 下生成不冲突的落盘路径：重名时追加 " (1)"、" (2)"…，绝不覆盖已有文件。
std::string uniqueTargetPath(const std::string& dir, const std::string& fileName)
{
    std::string stem = fileName;
    std::string ext;
    size_t dot = fileName.find_last_of('.');
    // 只有非开头位置的 '.' 才当作扩展名分隔（.gitignore 这类保留整体）
    if (dot != std::string::npos && dot != 0) {
        stem = fileName.substr(0, dot);
        ext = fileName.substr(dot);
    }
    std::string candidate = dir + "/" + fileName;
    if (::access(candidate.c_str(), F_OK) != 0) {
        return candidate;
    }
    for (int32_t i = 1; i < 10000; ++i) {
        candidate = dir + "/" + stem + " (" + std::to_string(i) + ")" + ext;
        if (::access(candidate.c_str(), F_OK) != 0) {
            return candidate;
        }
    }
    // 极端情况：退化为加时间戳，仍然避免覆盖
    candidate = dir + "/" + stem + "_" +
                std::to_string(static_cast<long long>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count())) + ext;
    return candidate;
}

// 写满整个缓冲（write 可能短写）。
bool writeAllToFd(int32_t fd, const char* data, size_t len)
{
    size_t written = 0;
    while (written < len) {
        ssize_t n = ::write(fd, data + written, len - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false;
        }
        written += static_cast<size_t>(n);
    }
    return true;
}

} // namespace

DeskflowClient::DeskflowClient() = default;

DeskflowClient::~DeskflowClient()
{
    stop();
}

bool DeskflowClient::start(const std::string& host, uint16_t port, const std::string& screenName)
{
    if (m_running.load()) {
        return false;
    }
    m_host = host;
    m_port = port;
    m_screenName = screenName;
    m_stopRequested = false;
    m_running = true;
    m_thread = std::thread([this] { run(); });
    m_thread.detach();
    return true;
}

void DeskflowClient::stop()
{
    m_stopRequested = true;
    m_stream.close(); // unblock recv
}

void DeskflowClient::setStatus(const std::string& s)
{
    if (m_statusCb) {
        m_statusCb(s);
    }
}

// 把纯文本封装为 IClipboard marshalled 容器（供 DCLP 上传）：
// 4B 格式数(=1) + 4B 格式(Text=0) + 4B 长度 + 文本，全部大端。
std::string DeskflowClient::marshalText(const std::string& utf8Text)
{
    std::string out;
    auto be32 = [&out](uint32_t v) {
        out.push_back(static_cast<char>((v >> 24) & 0xff));
        out.push_back(static_cast<char>((v >> 16) & 0xff));
        out.push_back(static_cast<char>((v >> 8) & 0xff));
        out.push_back(static_cast<char>(v & 0xff));
    };
    be32(1);            // numFormats
    be32(0);            // Format::Text
    be32(static_cast<uint32_t>(utf8Text.size()));
    out += utf8Text;
    return out;
}

// 从 IClipboard marshalled 容器中提取 format 0 (Text) 的负载；无则返回空串。
std::string DeskflowClient::unmarshalText(const std::string& container)
{
    if (container.size() < 4) {
        return "";
    }
    auto rd = [&container](size_t off) -> uint32_t {
        if (off + 4 > container.size()) {
            return 0;
        }
        const auto* p = reinterpret_cast<const unsigned char*>(container.data() + off);
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
    };
    size_t off = 0;
    uint32_t numFormats = rd(off);
    off += 4;
    for (uint32_t i = 0; i < numFormats && off + 8 <= container.size(); ++i) {
        uint32_t format = rd(off);
        off += 4;
        uint32_t size = rd(off);
        off += 4;
        if (off + size > container.size()) {
            break;
        }
        if (format == 0) {   // Format::Text
            return container.substr(off, size);
        }
        off += size;
    }
    return "";
}

// 以分块方式发送 DCLP 给对端（barrier/deskflow: mark=1 Start(ASCII 大小) -> 2 Data -> 3 End）。
// seq 使用当前 CINN 活跃会话序号（与官方客户端一致）。
bool DeskflowClient::sendClipboard(const std::string& text)
{
    if (!m_handshakeDone || text.empty()) {
        return false;
    }
    // 先封装为 marshalled 容器（格式 0 文本），再分块上传
    std::string payload = marshalText(text);   // 4B numFormats + 4B format + 4B size + text
    constexpr uint8_t kDataStart = 1;
    constexpr uint8_t kDataChunk = 2;
    constexpr uint8_t kDataEnd = 3;
    uint8_t id = 0;            // kClipboardClipboard（主剪贴板）
    uint32_t seq = m_seqNum;   // 用活跃 CINN 的会话序号，勿手动递增
    {
        std::lock_guard<std::mutex> lock(m_clipboardMutex);
        m_lastClipboardText = text;
    }
    const size_t kChunk = 512 * 1024;  // 与服务端 StreamChunker 一致（512KB）
    bool ok = true;
    // 1) Start：payload = 期望总大小（ASCI十进制）
    {
        std::vector<uint8_t> start;
        std::string sizeStr = std::to_string(payload.size());
        ProtoUtil::writef(start, kMsgDClipboard, id, seq, kDataStart, &sizeStr);
        ok = ok && m_stream.writeFrame(start.data(), start.size());
    }
    // 2) DataChunk（按 512KB 切块）
    for (size_t off = 0; off < payload.size() && ok; off += kChunk) {
        std::string chunk = payload.substr(off, std::min(kChunk, payload.size() - off));
        std::vector<uint8_t> dc;
        ProtoUtil::writef(dc, kMsgDClipboard, id, seq, kDataChunk, &chunk);
        ok = ok && m_stream.writeFrame(dc.data(), dc.size());
    }
    // 3) End
    {
        std::vector<uint8_t> end;
        std::string empty;
        ProtoUtil::writef(end, kMsgDClipboard, id, seq, kDataEnd, &empty);
        ok = ok && m_stream.writeFrame(end.data(), end.size());
    }
    DF_LOGI("DCLP push id=%{public}u seq=%{public}u text=%{public}zu container=%{public}zu ok=%{public}d",
        id, seq, text.size(), payload.size(), ok ? 1 : 0);
    return ok;
}

// 向对端抓取剪贴板所有权（CCLP id, seq）。仅在上传方向：本机剪贴板变化后、
// 先把所有权声明给自己、再把 DCLP 内容广播出去。官方客户端用活跃 CINN 的 seq。
bool DeskflowClient::grabClipboard()
{
    if (!m_handshakeDone) {
        return false;
    }
    std::vector<uint8_t> out;
    uint8_t id = 0;
    uint32_t seq = m_seqNum;   // 活跃 CINN 的会话序号
    ProtoUtil::writef(out, kMsgCClipboard, id, seq);
    bool ok = m_stream.writeFrame(out.data(), out.size());
    DF_LOGI("CCLP grab id=%{public}u seq=%{public}u ok=%{public}d", id, seq, ok ? 1 : 0);
    return ok;
}

// 把本机剪贴板文本推送/广播给对端（ArkTS 调用；可能来自 worker 或 JS 线程）
// 官方客户端流程：先 CCLP 声明所有权，再分块 DCLP 上传内容。
void DeskflowClient::pushClipboard(const std::string& text)
{
    if (!m_clipboardSync.load() || !m_handshakeDone || text.empty()) {
        return;
    }
    grabClipboard();
    sendClipboard(text);
}

// 保留（不再被 CINN 调用）：进入屏幕时由服务端主动下推 DCLP，客户端无需 CCLP 抓取。
// 若仍想主动刷新一次可用。此方法对接收方向无副作用，但注意 CCLP 会让服务端
// 将本端 m_dirty 清 false 从而抑制下推，故默认不调用。
void DeskflowClient::requestClipboard()
{
    // no-op：见 sendClipboard 与 CINN 处理说明（CCLP 不再用于接收方向）
    if (m_clipboardSync.load()) {
        DF_LOGI("requestClipboard: no-op (server auto-pushes DCLP on CINN)");
    }
}

void DeskflowClient::setFileTransferCallback(FileTransferCallback cb)
{
    std::lock_guard<std::mutex> lock(m_fileMutex);
    m_fileCb = std::move(cb);
}

void DeskflowClient::setFileSaveDir(const std::string& dir)
{
    std::lock_guard<std::mutex> lock(m_fileMutex);
    m_fileDir = dir;
    DF_LOGI("file save dir set to '%{public}s'", dir.c_str());
}

std::string DeskflowClient::fileSaveDir()
{
    std::string dir;
    {
        std::lock_guard<std::mutex> lock(m_fileMutex);
        dir = m_fileDir;
    }
    if (dir.empty()) {
        dir = kDefaultFileSaveDir;
    }
    return dir;
}

// 取落盘目录并确保其存在；失败返回空串。
std::string DeskflowClient::resolveFileSaveDir()
{
    std::string dir = fileSaveDir();
    if (dir.empty() || !makeDirs(dir)) {
        DF_LOGE("file save dir unavailable: '%{public}s'", dir.c_str());
        return "";
    }
    return dir;
}

// 上报完成事件：先拷出回调，出锁再调用，避免回调里回打本类导致死锁。
void DeskflowClient::notifyFileReceived(const std::string& path, uint64_t size)
{
    FileTransferCallback cb = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_fileMutex);
        cb = m_fileCb;
    }
    if (cb) {
        cb(path, size);
    }
}

// 关闭并落定一个传输：success=true 保留文件并上报，false 删除半截文件。
// 无论成败都会 close(fd) 并从 m_fileTransfers 移除，保证 fd 不泄露。
void DeskflowClient::finishFileTransfer(uint32_t transferId, bool success)
{
    auto it = m_fileTransfers.find(transferId);
    if (it == m_fileTransfers.end()) {
        return;
    }
    FileTransferContext& ctx = it->second;
    const std::string path = ctx.targetPath;
    const uint64_t received = ctx.receivedSize;
    const uint64_t expected = ctx.expectedSize;
    if (ctx.fd >= 0) {
        ::close(ctx.fd);
        ctx.fd = -1;
    }
    m_fileTransfers.erase(it);

    if (!success) {
        if (!path.empty()) {
            ::unlink(path.c_str());
        }
        DF_LOGW("DFTR abort transferId=%{public}u '%{public}s' received=%{public}llu/%{public}llu, partial removed",
            transferId, path.c_str(), static_cast<unsigned long long>(received),
            static_cast<unsigned long long>(expected));
        return;
    }
    DF_LOGI("DFTR done transferId=%{public}u '%{public}s' size=%{public}llu",
        transferId, path.c_str(), static_cast<unsigned long long>(received));
    notifyFileReceived(path, received);
}

// DDRG：fileCount + NUL 分隔的绝对路径串。DDRG 不带 transferId，且先于 DFTR 到达，
// 因此这里只做「清洗文件名并入队」，真正的配对发生在 DFTR DataStart。
bool DeskflowClient::handleDragInfo(int16_t fileCount, const std::string& info)
{
    if (fileCount <= 0) {
        DF_LOGW("DDRG ignored: fileCount=%{public}d", static_cast<int32_t>(fileCount));
        return true;
    }
    // 队列上限，避免对端反复发 DDRG 造成无界增长
    constexpr size_t kPendingFileLimit = 256;
    size_t start = 0;
    int32_t parsed = 0;
    while (start <= info.size() && parsed < fileCount) {
        size_t end = info.find('\0', start);
        if (end == std::string::npos) {
            end = info.size();
        }
        std::string raw = info.substr(start, end - start);
        if (!raw.empty()) {
            std::string name = sanitizeFileName(raw);
            if (name.size() > 240) {
                name.resize(240);   // 防超长路径（鸿蒙 NAME_MAX 一般为 255）
            }
            if (!name.empty()) {
                if (m_pendingFileNames.size() >= kPendingFileLimit) {
                    m_pendingFileNames.pop_front();
                    DF_LOGW("DDRG pending filename queue full, dropped oldest");
                }
                m_pendingFileNames.push_back(name);
                DF_LOGI("DDRG file[%{public}d] '%{public}s' -> '%{public}s'",
                    parsed, raw.c_str(), name.c_str());
            } else {
                DF_LOGW("DDRG file[%{public}d] '%{public}s' unsanitizable, skipped",
                    parsed, raw.c_str());
            }
        }
        ++parsed;
        if (end >= info.size()) {
            break;
        }
        start = end + 1;
    }
    if (parsed < fileCount) {
        DF_LOGW("DDRG declared %{public}d files but only parsed %{public}d",
            static_cast<int32_t>(fileCount), parsed);
    }
    return true;
}

// DFTR：data = transferId(4, LE) + chunkType(1) + payload。
// DataStart/DataChunk/DataEnd 三态由 transferId 分桶，多个文件可并发互不干扰。
bool DeskflowClient::handleFileTransfer(const std::string& data)
{
    if (data.size() < kDftrHeaderSize) {
        DF_LOGE("DFTR payload too short: %{public}zu", data.size());
        return true;   // 单条坏消息不值得断链
    }
    const uint32_t transferId = readLE32(data, 0);
    const uint8_t chunkType = static_cast<uint8_t>(data[4]);
    const std::string payload = data.substr(kDftrHeaderSize);

    if (chunkType == kDftrDataStart) {
        if (payload.size() < 8) {
            DF_LOGE("DFTR start transferId=%{public}u short size field (%{public}zu)",
                transferId, payload.size());
            return true;
        }
        const uint64_t declared = readLE64(payload, 0);
        // 同一 transferId 重复 Start：丢弃旧上下文，避免 fd 泄露
        if (m_fileTransfers.find(transferId) != m_fileTransfers.end()) {
            DF_LOGW("DFTR start transferId=%{public}u duplicates in-flight transfer, aborting old",
                transferId);
            finishFileTransfer(transferId, false);
        }
        if (declared > kFileTransferMaxSize) {
            DF_LOGE("DFTR start transferId=%{public}u size %{public}llu exceeds limit %{public}llu, rejected",
                transferId, static_cast<unsigned long long>(declared),
                static_cast<unsigned long long>(kFileTransferMaxSize));
            // 占位一个不活跃的上下文：后续 DataChunk 会被丢弃，DataEnd 清理掉它
            FileTransferContext ctx;
            ctx.expectedSize = declared;
            ctx.oversize = true;
            ctx.active = false;
            m_fileTransfers[transferId] = ctx;
            if (!m_pendingFileNames.empty()) {
                m_pendingFileNames.pop_front();   // 名字一并消费，避免错配下一个文件
            }
            return true;
        }
        // 配对文件名：DDRG 已入队则弹出一个，否则退化为 unnamed_<id>.bin
        std::string name;
        if (!m_pendingFileNames.empty()) {
            name = m_pendingFileNames.front();
            m_pendingFileNames.pop_front();
        }
        if (name.empty()) {
            name = "unnamed_" + std::to_string(transferId) + ".bin";
        }
        const std::string dir = resolveFileSaveDir();
        if (dir.empty()) {
            DF_LOGE("DFTR start transferId=%{public}u no writable dir, dropping", transferId);
            m_fileTransfers[transferId] = FileTransferContext();   // 占位丢弃
            return true;
        }
        const std::string target = uniqueTargetPath(dir, name);
        int32_t fd = ::open(target.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd < 0) {
            DF_LOGE("DFTR start open '%{public}s' failed errno=%{public}d", target.c_str(), errno);
            m_fileTransfers[transferId] = FileTransferContext();   // 占位丢弃
            return true;
        }
        FileTransferContext ctx;
        ctx.fd = fd;
        ctx.targetPath = target;
        ctx.fileName = name;
        ctx.expectedSize = declared;
        ctx.receivedSize = 0;
        ctx.active = true;
        m_fileTransfers[transferId] = ctx;
        DF_LOGI("DFTR start transferId=%{public}u '%{public}s' expected=%{public}llu",
            transferId, target.c_str(), static_cast<unsigned long long>(declared));
        return true;
    }

    if (chunkType == kDftrDataChunk) {
        auto it = m_fileTransfers.find(transferId);
        if (it == m_fileTransfers.end()) {
            DF_LOGD("DFTR chunk for unknown transferId=%{public}u (%{public}zu bytes), dropped",
                transferId, payload.size());
            return true;
        }
        FileTransferContext& ctx = it->second;
        if (!ctx.active || ctx.oversize) {
            return true;   // 已判定丢弃的传输，静默吞掉后续块
        }
        if (ctx.receivedSize + payload.size() > ctx.expectedSize) {
            DF_LOGE("DFTR oversize transferId=%{public}u: %{public}llu+%{public}zu > %{public}llu",
                transferId, static_cast<unsigned long long>(ctx.receivedSize), payload.size(),
                static_cast<unsigned long long>(ctx.expectedSize));
            finishFileTransfer(transferId, false);
            return true;
        }
        if (!payload.empty() && !writeAllToFd(ctx.fd, payload.data(), payload.size())) {
            DF_LOGE("DFTR write failed transferId=%{public}u errno=%{public}d", transferId, errno);
            finishFileTransfer(transferId, false);
            return true;
        }
        ctx.receivedSize += payload.size();
        if (payload.size() > 1024 * 1024) {
            DF_LOGW("DFTR chunk transferId=%{public}u larger than agreed 512KB (%{public}zu)",
                transferId, payload.size());
        }
        return true;
    }

    if (chunkType == kDftrDataEnd) {
        auto it = m_fileTransfers.find(transferId);
        if (it == m_fileTransfers.end()) {
            DF_LOGD("DFTR end for unknown transferId=%{public}u", transferId);
            return true;
        }
        const bool inFlight = it->second.active && !it->second.oversize;
        const uint64_t received = it->second.receivedSize;
        const uint64_t expected = it->second.expectedSize;
        if (inFlight && received != expected) {
            DF_LOGE("DFTR size mismatch transferId=%{public}u received=%{public}llu expected=%{public}llu",
                transferId, static_cast<unsigned long long>(received),
                static_cast<unsigned long long>(expected));
            finishFileTransfer(transferId, false);
            return true;
        }
        finishFileTransfer(transferId, inFlight);
        return true;
    }

    DF_LOGW("DFTR unknown chunkType=%{public}u transferId=%{public}u", chunkType, transferId);
    return true;
}

// 连接断开时清理所有在途传输（否则 fd 会跨重连泄露），并清空待配对文件名。
void DeskflowClient::abortAllFileTransfers()
{
    while (!m_fileTransfers.empty()) {
        const uint32_t id = m_fileTransfers.begin()->first;
        finishFileTransfer(id, false);
    }
    m_pendingFileNames.clear();
}

void DeskflowClient::run()
{
    // 外层循环：负责重连。run() 由 start() 起的 worker 线程执行一次，
    // 内部在断开后按需重连，直到 stop() 被调用或收到不可重连指示。
    while (!m_stopRequested.load()) {
        m_shouldReconnect = true;  // 每次新连接开始时重置

        // 执行一次"连接→握手→消息循环"。返回值为"断开后是否应继续重连"。
        bool shouldContinue = runOnce();

        if (!m_autoReconnect.load() || !shouldContinue || m_stopRequested.load()) {
            break;
        }

        // 退避重连
        setStatus("reconnecting in " + std::to_string(m_reconnectIntervalMs.load()) + "ms");
        DF_LOGI("reconnect in %{public}d ms", m_reconnectIntervalMs.load());
        int32_t waitMs = m_reconnectIntervalMs.load();
        for (int32_t waited = 0; waited < waitMs && !m_stopRequested.load(); waited += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    m_running = false;
    if (!m_stopRequested.load()) {
        // 循环因不再重连而退出（如收到 CBYE/EICV）
        setStatus("disconnected");
    }
}

// 单次连接生命周期：连接 → 握手 → 消息循环。运行期断开返回 true（可重连），否则 false。
bool DeskflowClient::runOnce()
{
    setStatus("connecting to " + m_host + ":" + std::to_string(m_port));
    DF_LOGI("connecting to %{public}s:%{public}d", m_host.c_str(), m_port);
    if (!m_stream.connect(m_host, m_port, 5000)) {
        DF_LOGE("connect failed");
        setStatus("error: connect failed");
        return true;  // 可重连（例如服务端暂不可达）
    }
    DF_LOGI("tcp connected");
    setStatus("connected, handshake...");
    if (!handshake()) {
        DF_LOGE("handshake failed");
        setStatus("error: handshake failed");
        m_stream.close();
        // 版本不兼容时 handshake() 会把 m_shouldReconnect 置 false
        return m_shouldReconnect.load();
    }
    DF_LOGI("handshake ok");
    setStatus("handshake ok, waiting for enter (move mouse to this screen)");

    // 自动申请注入授权（否则 CINN 后的注入事件不会生效）
    // 注意：不能每次重连都重新申请。实测重连时再调一次会返回
    // INPUT_INJECTION_AUTHORIZING(3900005)，系统停在「授权中」且不再回调，
    // 之后所有注入都失败 ret=201（Check Permi:ohos.permission.CONTROL_DEVICE fail）。
    // 反例：进程被 force-stop 后重启，OH_Input_QueryAuthorizedStatus 仍返回
    // AUTHORIZING(1)（系统侧卡住的残留态），此时必须 CancelInjection 复位再重新申请，
    // 否则永远等不到授权回调。
    Input_InjectionStatus authStatus = UNAUTHORIZED;
    Input_Result queryRet = OH_Input_QueryAuthorizedStatus(&authStatus);
    if (queryRet == INPUT_SUCCESS && authStatus == AUTHORIZED) {
        DF_LOGI("injection already authorized, skip request");
    } else if (queryRet == INPUT_SUCCESS && authStatus == AUTHORIZING) {
        DF_LOGW("injection stuck in AUTHORIZING, cancel and re-request");
        OH_Input_CancelInjection();
        Input_InjectionStatus st2 = UNAUTHORIZED;
        OH_Input_QueryAuthorizedStatus(&st2);
        Input_Result rr = OH_Input_RequestInjection(OnInjectAuthCallback);
        DF_LOGI("re-request after cancel: prevStatus=%{public}d now=%{public}d ret=%{public}d",
            static_cast<int>(authStatus), static_cast<int>(st2), rr);
    } else {
        Input_Result authRet = OH_Input_RequestInjection(OnInjectAuthCallback);
        DF_LOGI("auto request injection auth, ret=%{public}d (prevStatus=%{public}d/%{public}d)",
            authRet, static_cast<int>(authStatus), queryRet);
        if (authRet != INPUT_SUCCESS && authRet != INPUT_INJECTION_AUTHORIZED) {
            DF_LOGE("auto injection auth request failed: %{public}d", authRet);
        }
    }

    // message loop -- every message is a length-prefixed frame
    bool keepGoing = true;
    while (keepGoing && !m_stopRequested.load()) {
        std::vector<uint8_t> frame;
        if (!m_stream.readFrame(frame)) {
            if (!m_stopRequested.load()) {
                DF_LOGE("connection lost while reading frame");
                setStatus("error: connection lost");
            } else {
                setStatus("disconnected");
            }
            break;
        }
        // parse message key + payload from frame buffer
        m_stream.pushToBuffer(frame.data(), frame.size());
        uint8_t head[4] = {0, 0, 0, 0};
        if (!m_stream.readExact(head, 4)) {
            DF_LOGE("frame too short");
            break;
        }
        std::string key(reinterpret_cast<char*>(head), 4);
        DF_LOGD("recv: %{public}s (frame %{public}zu, bufRemain %{public}zu)",
            key.c_str(), frame.size(), m_stream.bufferRemaining());
        keepGoing = handleMessage(key);
        // 运行期每条消息处理完后回 CNOP（deskflow client 的 BSD 延迟 ACK 绕开 hack）
        if (keepGoing && m_handshakeDone) {
            if (!m_stream.writeFrame(reinterpret_cast<const uint8_t*>("CNOP"), 4)) {
                DF_LOGE("write CNOP failed");
                keepGoing = false;
            }
        }
    }

    DF_LOGI("message loop exited (stopRequested=%{public}d)", m_stopRequested.load() ? 1 : 0);

    // 连接结束：在途文件传输一律作废（关闭 fd + 删除半截文件），
    // 避免 fd 跨重连泄露，也避免重连后错配到旧的 transferId。
    abortAllFileTransfers();
    m_clipChunk.active = false;
    m_clipChunk.buffer.clear();

    m_stream.close();
    // 断开后是否重连：未被 stop，且未被置为不重连（CBYE/EICV）
    return !m_stopRequested.load() && m_shouldReconnect.load();
}

bool DeskflowClient::handshake()
{
    // Server -> Client: framed Hello = "%7s%2i%2i"
    std::vector<uint8_t> frame;
    if (!m_stream.readFrame(frame)) {
        DF_LOGE("read hello frame failed");
        return false;
    }
    m_stream.pushToBuffer(frame.data(), frame.size());

    std::string protocolName;
    int16_t major = 0;
    int16_t minor = 0;
    if (!ProtoUtil::readf(m_stream, kMsgHello, &protocolName, &major, &minor)) {
        DF_LOGE("read hello failed");
        return false;
    }
    DF_LOGI("hello received: name='%{public}s' len=%{public}zu ver=%{public}d.%{public}d",
        protocolName.c_str(), protocolName.size(), major, minor);
    if (protocolName.size() != kHelloNameLength || major != kProtocolMajorVersion) {
        std::ostringstream oss;
        oss << "protocol mismatch: name='" << protocolName << "' ver=" << major << "." << minor;
        DF_LOGE("protocol mismatch: name='%{public}s' ver=%{public}d.%{public}d",
            protocolName.c_str(), major, minor);
        // 协议名/主版本对不上：重连也无效，放弃
        m_shouldReconnect = false;
        setStatus(oss.str());
        return false;
    }
    int16_t helloBackMinor = (minor < kProtocolMinorVersion) ? minor : kProtocolMinorVersion;

    // Client -> Server: framed HelloBack = name(7) + "%2i%2i%s" (name = screenName)
    std::vector<uint8_t> out;
    ProtoUtil::writef(out, kMsgHelloBack, &protocolName, kProtocolMajorVersion, helloBackMinor,
        &m_screenName);
    if (!m_stream.writeFrame(out.data(), out.size())) {
        DF_LOGE("write hello back failed");
        return false;
    }
    DF_LOGI("hello back sent: %{public}zu bytes, name='%{public}s'", out.size(), m_screenName.c_str());
    return true;
}

bool DeskflowClient::handleMessage(const std::string& key)
{
    bool handled = true;
    if (key == kMsgQInfo) {
        // QINF -> respond DINF: x, y, w, h, 0, mx, my
        std::vector<uint8_t> out;
        ProtoUtil::writef(out, kMsgDInfo, 0, 0, m_screenW, m_screenH, 0, m_pointerX.load(),
            m_pointerY.load());
        bool ok = m_stream.writeFrame(out.data(), out.size());
        DF_LOGI("QINF -> DINF sent=%{public}d (%{public}dx%{public}d)", ok ? 1 : 0, m_screenW, m_screenH);
        return ok;
    }
    if (key == kMsgCKeepAlive) {
        bool ok = m_stream.writeFrame(reinterpret_cast<const uint8_t*>("CALV"), 4);
        DF_LOGD("CALV heartbeat replied=%{public}d", ok ? 1 : 0);
        return ok;
    }
    if (key == "CINN") {
        int16_t x = 0, y = 0;
        uint32_t seq = 0;
        int16_t mask = 0;
        if (!ProtoUtil::readf(m_stream, kMsgCEnter + 4, &x, &y, &seq, &mask)) {
            DF_LOGE("CINN: readf failed (bufRemain=%{public}zu)", m_stream.bufferRemaining());
            return false;
        }
        DF_LOGI("CINN: x=%{public}d y=%{public}d seq=%{public}u mask=0x%{public}x", x, y, seq, mask);
        m_active = true;
        m_seqNum = seq;
        // 规格：x=-1 且 y=-1 表示仅切换修饰键、不移动指针
        if (x >= 0 && y >= 0) {
            const int32_t cx = fluxClampPointer(x, m_screenW);
            const int32_t cy = fluxClampPointer(y, m_screenH);
            m_pointerX = cx;
            m_pointerY = cy;
            injectMouseMove(cx, cy);
        }
        syncModifiers(static_cast<uint32_t>(mask));
        // 服务端在收到 CINN 后会自动推送 DCLP 剪贴板（分块：size→data→end）。
        // 注意：此处绝不能发 CCLP 去"抓取"——据 deskflow Server::handleClipboardGrabbed，
        // 收到 CCLP 会把本客户端剪贴板标记为 m_dirty=false 并清空服务端缓存，
        // 从而抑制服务端后续的 DCLP 推送。故进入本机屏幕时静默等待 DCLP 即可。
        setStatus("active: control acquired, pointer at " + std::to_string(x) + "," + std::to_string(y));
        return true;
    }
    if (key == "COUT") {
        m_active = false;
        // 离开屏幕：清空按钮按下状态（防止粘滞按键/按钮）
        for (auto& [buttonId, down] : m_mouseButtonsDown) {
            if (down) {
                injectMouseButton(buttonId, false);
            }
        }
        m_mouseButtonsDown.clear();
        setStatus("inactive: moved back to primary screen");
        return true;
    }
    if (key == kMsgCClose) {
        // 服务端主动说再见（CBYE）。服务端正常关闭/重启时也会发这条，
        // 因此不能据此永久放弃重连，否则 Windows 端重启 core 后必须手动重开 App。
        // 保留 m_shouldReconnect（true）交给 run() 的退避循环重连；
        // 真正不该重连的情况只有协议不匹配（EICV）与版本不兼容，那里单独置 false。
        setStatus("closed by server, will retry");
        return false;
    }
    if (key == "DMMV") {
        int16_t x = 0, y = 0;
        if (!ProtoUtil::readf(m_stream, "%2i%2i", &x, &y)) {
            return false;
        }
        const int32_t cx = fluxClampPointer(x, m_screenW);
        const int32_t cy = fluxClampPointer(y, m_screenH);
        m_pointerX = cx;
        m_pointerY = cy;
        injectMouseMove(cx, cy);
        return true;
    }
    if (key == "DMRM") {
        int16_t dx = 0, dy = 0;
        if (!ProtoUtil::readf(m_stream, "%2i%2i", &dx, &dy)) {
            return false;
        }
        int32_t x = fluxClampPointer(m_pointerX.load() + dx, m_screenW);
        int32_t y = fluxClampPointer(m_pointerY.load() + dy, m_screenH);
        m_pointerX = x;
        m_pointerY = y;
        injectMouseMove(x, y);
        return true;
    }
    if (key == "DMDN") {
        uint8_t button = 0;
        if (!ProtoUtil::readf(m_stream, "%1i", &button)) {
            return false;
        }
        DF_LOGI("DMDN button=%{public}u at (%{public}d,%{public}d)", button, m_pointerX.load(), m_pointerY.load());
        injectMouseButton(button, true);
        return true;
    }
    if (key == "DMUP") {
        uint8_t button = 0;
        if (!ProtoUtil::readf(m_stream, "%1i", &button)) {
            return false;
        }
        DF_LOGI("DMUP button=%{public}u at (%{public}d,%{public}d)", button, m_pointerX.load(), m_pointerY.load());
        injectMouseButton(button, false);
        return true;
    }
    if (key == "DMWM") {
        int16_t xDelta = 0, yDelta = 0;
        if (!ProtoUtil::readf(m_stream, "%2i%2i", &xDelta, &yDelta)) {
            return false;
        }
        injectMouseWheel(xDelta, yDelta);
        return true;
    }
    if (key == "DKDN") {
        int16_t id = 0, mask = 0, button = 0;
        if (!ProtoUtil::readf(m_stream, kMsgDKeyDown + 4, &id, &mask, &button)) {
            return false;
        }
        syncModifiers(static_cast<uint32_t>(mask));
        uint32_t k = static_cast<uint16_t>(id);
        if (!m_keyStates[k]) {
            injectKeyEvent(k, true);
            m_keyStates[k] = true;
        }
        return true;
    }
    if (key == "DKUP") {
        int16_t id = 0, mask = 0, button = 0;
        if (!ProtoUtil::readf(m_stream, kMsgDKeyUp + 4, &id, &mask, &button)) {
            return false;
        }
        syncModifiers(static_cast<uint32_t>(mask));
        uint32_t k = static_cast<uint16_t>(id);
        if (m_keyStates[k]) {
            injectKeyEvent(k, false);
            m_keyStates[k] = false;
        }
        return true;
    }
    if (key == "DKRP") {
        int16_t id = 0, mask = 0, count = 0, button = 0;
        std::string lang;
        if (!ProtoUtil::readf(m_stream, kMsgDKeyRepeat + 4, &id, &mask, &count, &button, &lang)) {
            return false;
        }
        syncModifiers(static_cast<uint32_t>(mask));
        uint32_t k = static_cast<uint16_t>(id);
        if (!m_keyStates[k]) {
            injectKeyEvent(k, true);
            m_keyStates[k] = true;
        }
        return true;
    }
    if (key == "DKDL") {
        int16_t id = 0, mask = 0, button = 0;
        std::string lang;
        if (!ProtoUtil::readf(m_stream, kMsgDKeyDownLang + 4, &id, &mask, &button, &lang)) {
            return false;
        }
        syncModifiers(static_cast<uint32_t>(mask));
        uint32_t k = static_cast<uint16_t>(id);
        if (!m_keyStates[k]) {
            injectKeyEvent(k, true);
            m_keyStates[k] = true;
        }
        return true;
    }

    // ---- ignored messages (consume payload to keep stream in sync) ----
    if (key == kMsgCNoop) {
        return true;
    }
    if (key == kMsgCResetOptions) {
        return true;
    }
    if (key == kMsgCInfoAck) {
        DF_LOGI("CIAK received (info ack)");
        return true;
    }
    if (key == "CSEC") {
        uint8_t on = 0;
        if (!ProtoUtil::readf(m_stream, kMsgCScreenSaver + 4, &on)) {
            return false;
        }
        return true;
    }
    if (key == "CCLP") {
        uint8_t id = 0;
        uint32_t seq = 0;
        if (!ProtoUtil::readf(m_stream, kMsgCClipboard + 4, &id, &seq)) {
            return false;
        }
        return true;
    }
    if (key == "DSOP") {
        std::vector<uint32_t> options;
        if (!ProtoUtil::readf(m_stream, kMsgDSetOptions + 4, &options)) {
            return false;
        }
        // DSOP 是握手完成的标志：此后进入运行期（每条消息后回 CNOP）
        m_handshakeDone = true;
        DF_LOGI("DSOP received, handshake complete, entering runtime");
        return true;
    }
    if (key == "DCLP") {
        uint8_t id = 0;
        uint32_t seq = 0;
        uint8_t mark = 0;
        std::string data;
        if (!ProtoUtil::readf(m_stream, kMsgDClipboard + 4, &id, &seq, &mark, &data)) {
            return false;
        }
        // DCLP 是按块传输：mark=1 DataStart(payload=ASCII 期望大小) →
        // mark=2 DataChunk(payload=内容分块) → mark=3 DataEnd(完成)。见 deskflow ClipboardChunk。
        constexpr uint8_t kDataStart = 1;
        constexpr uint8_t kDataChunk = 2;
        constexpr uint8_t kDataEnd = 3;
        if (mark == kDataStart) {
            m_clipChunk.active = true;
            m_clipChunk.buffer.clear();
            m_clipChunk.clipSeq = seq;
            try {
                m_clipChunk.expectedSize = static_cast<size_t>(std::stoull(data));
            } catch (...) {
                m_clipChunk.expectedSize = 0;
            }
            // 大小即内容长度，直接校验上限，避免越界
            if (m_clipChunk.expectedSize > kProtocolMaxStringLength) {
                DF_LOGE("DCLP start size too large: %{public}zu", m_clipChunk.expectedSize);
                m_clipChunk.active = false;
                m_clipChunk.buffer.clear();
            }
            return true;
        }
        if (mark == kDataChunk) {
            if (!m_clipChunk.active) {
                DF_LOGI("DCLP data chunk before start");
                return true;
            }
            m_clipChunk.buffer.append(data);
            return true;
        }
        if (mark == kDataEnd) {
            if (!m_clipChunk.active) {
                return true;
            }
            m_clipChunk.active = false;
            std::string assembled = std::move(m_clipChunk.buffer);
            m_clipChunk.buffer.clear();
            // 反序列化容器，提取纯文本（format 0 / Text）
            std::string text = unmarshalText(assembled);
            if (m_clipboardSync.load() && !text.empty()) {
                ClipboardCallback cb = nullptr;
                {
                    std::lock_guard<std::mutex> lock(m_clipboardMutex);
                    cb = m_clipboardCb;
                    m_lastClipboardText = text;   // 记录，用于忽略后续本机同源回环
                }
                if (cb) {
                    cb(text);
                }
                DF_LOGI("DCLP: assembled %{public}zu bytes, text %{public}zu -> JS", assembled.size(), text.size());
            } else {
                DF_LOGD("DCLP: assembled %{public}zu bytes, text %{public}zu, ignored (sync=%{public}d)",
                    assembled.size(), text.size(), m_clipboardSync.load() ? 1 : 0);
            }
            return true;
        }
        DF_LOGD("DCLP: unknown mark %{public}u", mark);
        return true;
    }
    if (key == "DFTR") {
        uint8_t mark = 0;
        std::string data;
        if (!ProtoUtil::readf(m_stream, kMsgDFileTransfer + 4, &mark, &data)) {
            return false;
        }
        // 线上布局为 "DFTR" + mark(1) + 4B大端长度 + data[]，
        // 而 transferId/chunkType/payload 都封装在 data 内层（小端），见 handleFileTransfer。
        return handleFileTransfer(data);
    }
    if (key == "DDRG") {
        int16_t fileCount = 0;
        std::string info;
        if (!ProtoUtil::readf(m_stream, kMsgDDragInfo + 4, &fileCount, &info)) {
            return false;
        }
        return handleDragInfo(fileCount, info);
    }
    if (key == "LSYN") {
        std::string data;
        if (!ProtoUtil::readf(m_stream, kMsgDLanguageSynchronisation + 4, &data)) {
            return false;
        }
        DF_LOGI("LSYN ignored (lang data %{public}zu bytes)", data.size());
        return true;
    }
    if (key == "SECN") {
        std::string data;
        if (!ProtoUtil::readf(m_stream, kMsgDSecureInputNotification + 4, &data)) {
            return false;
        }
        return true;
    }

    // ---- errors ----
    if (key == "EICV") {
        int16_t a = 0, b = 0;
        if (!ProtoUtil::readf(m_stream, kMsgEIncompatible + 4, &a, &b)) {
            return false;
        }
        // 版本不兼容：重连也无效，放弃
        m_shouldReconnect = false;
        setStatus("error: incompatible version, server wants " + std::to_string(a) + "." + std::to_string(b));
        return false;
    }
    if (key == kMsgEBusy) {
        setStatus("error: server busy");
        return false;
    }
    if (key == kMsgEUnknown) {
        setStatus("error: unknown client");
        return false;
    }
    if (key == kMsgEBad) {
        setStatus("error: protocol violation");
        return false;
    }

    setStatus("error: unknown message '" + key + "'");
    return false;
}

// ---------- modifier key synchronization ----------

// mask bit -> keysym of the modifier key to inject
uint32_t DeskflowClient::maskModifierKeysym(uint32_t maskBit) const
{
    switch (maskBit) {
        case kKeyModifierShift:   return kKeyShiftL;
        case kKeyModifierControl: return kKeyControlL;
        case kKeyModifierAlt:     return kKeyAltL;
        case kKeyModifierMeta:    return kKeyMetaL;
        case kKeyModifierSuper:   return kKeySuperL;
        case kKeyModifierAltGr:   return kKeyAltR;
        default: return kKeyNone;
    }
}

// 使本地修饰键按下状态与协议 mask 一致：
// mask 置位但未按下的 -> 注入按下；mask 未置位但按下的 -> 注入抬起
void DeskflowClient::syncModifiers(uint32_t mask)
{
    static const uint32_t kModBits[] = {
        kKeyModifierShift, kKeyModifierControl, kKeyModifierAlt,
        kKeyModifierMeta, kKeyModifierSuper, kKeyModifierAltGr
    };
    for (uint32_t bit : kModBits) {
        uint32_t keysym = maskModifierKeysym(bit);
        if (keysym == kKeyNone) {
            continue;
        }
        bool wantDown = (mask & bit) != 0;
        bool isDown = m_keyStates.count(keysym) > 0 && m_keyStates[keysym];
        if (wantDown && !isDown) {
            injectKeyEvent(keysym, true);
            m_keyStates[keysym] = true;
        } else if (!wantDown && isDown) {
            injectKeyEvent(keysym, false);
            m_keyStates[keysym] = false;
        }
    }
}

// ---------- injection helpers ----------

bool DeskflowClient::recoverInjectionPermission()
{
    // 注入失败 201（INPUT_PERMISSION_DENIED）时调用：
    // CancelInjection 会撤销授权并把系统状态从 AUTHORIZING/AUTHORIZED 复位成 UNAUTHORIZED，
    // 之后再 Requery+Request 才能重新弹出授权框（否则一直卡在「授权中」且注入全 201）。
    // 限流：两次自愈间隔至少 10s，总共最多 20 次。间隔太短会被系统判成
    // 重复请求（3900006），越救越糟。
    const int64_t now = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    const int64_t last = m_permRecoverLastMs.load();
    if (last != 0 && now - last < 10000) {
        return false;
    }
    if (m_permRecoverCount.load() >= 20) {
        return false;
    }
    m_permRecoverLastMs.store(now);
    m_permRecoverCount.fetch_add(1);
    DF_LOGW("injection denied(201), recovering permission (attempt %{public}d)",
        m_permRecoverCount.load());
    // 先查当前状态再决定要不要 Cancel。CancelInjection 会把已有授权彻底撤销，
    // 撤了之后再 Request 会被系统判成「重复请求」返回 3900006（OPERATION_FREQUENT），
    // 而 3900006 期间注入一直是 201 —— 原实现正是这样把一次瞬时失败放大成永久失效。
    Input_InjectionStatus st = UNAUTHORIZED;
    Input_Result qr = OH_Input_QueryAuthorizedStatus(&st);
    DF_LOGI("recover: queryRet=%{public}d status=%{public}d", qr, static_cast<int>(st));
    if (qr == INPUT_SUCCESS && st == AUTHORIZED) {
        // 系统说已授权却仍 201：说明授权挂在别的会话/进程上，
        // 此时撤销重申请没有意义（反而会掉到 3900006），直接等下一轮。
        DF_LOGW("recover: status=AUTHORIZED but inject denied, skip cancel/request");
        return false;
    }
    if (qr == INPUT_SUCCESS && st == AUTHORIZING) {
        // AUTHORIZING 有两种：① 授权框真的在屏上等用户点（此时 Cancel 会关掉框，必须等）；
        // ② 系统侧残留的假 AUTHORIZING（框根本没弹出来，等多久都不会有回调）。
        // 用时间区分：记下第一次看到 AUTHORIZING 的时刻，超过 8s 仍无回调就判定为 ②，
        // 撤销复位后重新申请（这是唯一能把假 AUTHORIZING 解开的手段）。
        if (m_authPendingSinceMs.load() == 0) {
            m_authPendingSinceMs.store(now);
            DF_LOGW("recover: status=AUTHORIZING, start waiting (no cancel yet)");
            return false;
        }
        const int64_t waited = now - m_authPendingSinceMs.load();
        if (waited < 8000) {
            DF_LOGW("recover: status=AUTHORIZING, waited %{public}lldms, keep waiting",
                static_cast<long long>(waited));
            return false;
        }
        DF_LOGW("recover: AUTHORIZING stuck %{public}lldms with no callback, cancel and re-request",
            static_cast<long long>(waited));
        m_authPendingSinceMs.store(0);
        // 落到下面的 Cancel + Request
    }
    OH_Input_CancelInjection();
    Input_InjectionStatus st2 = UNAUTHORIZED;
    OH_Input_QueryAuthorizedStatus(&st2);
    Input_Result rr = OH_Input_RequestInjection(OnInjectAuthCallback);
    DF_LOGI("re-request injection auth after cancel: prev=%{public}d now=%{public}d ret=%{public}d",
        static_cast<int>(st), static_cast<int>(st2), rr);
    if (rr == INPUT_INJECTION_OPERATION_FREQUENT) {
        // 3900006：系统认为我们在刷请求。退避到下一轮，别再纠缠。
        DF_LOGW("recover: request too frequent (3900006), backing off");
        return false;
    }
    return rr == INPUT_SUCCESS || rr == INPUT_INJECTION_AUTHORIZED;
}

bool DeskflowClient::injectMouseMove(int32_t x, int32_t y)
{
    // OH_Input_InjectMouseEventGlobal 把坐标 0（边沿）视为无效：x=0 或 y=0 时返回 401，
    // 导致拖到屏幕边缘（顶部 y=0 / 左缘 x=0）时 MOVE/BUTTON_UP 注入失败、窗口拖不/释放不掉。
    // 钳到 [1, W-1]/[1, H-1] 避开 0，仍在屏幕内，不影响边缘吸附/最大化语义。
    if (x < 1) x = 1; else if (x > m_screenW - 1) x = m_screenW - 1;
    if (y < 1) y = 1; else if (y > m_screenH - 1) y = m_screenH - 1;

    struct Input_MouseEvent* ev = OH_Input_CreateMouseEvent();
    if (ev == nullptr) {
        return false;
    }
    OH_Input_SetMouseEventAction(ev, MOUSE_ACTION_MOVE);
    OH_Input_SetMouseEventDisplayX(ev, x);
    OH_Input_SetMouseEventDisplayY(ev, y);
    OH_Input_SetMouseEventGlobalX(ev, x);
    OH_Input_SetMouseEventGlobalY(ev, y);
    // 拖动：MOVE 事件必须携带当前按下的按钮（真实事件流如此，devecocli 抓包确认）
    for (auto& [buttonId, down] : m_mouseButtonsDown) {
        if (down) {
            int32_t harmony = buttonToHarmony(buttonId);
            if (harmony >= 0) {
                OH_Input_SetMouseEventButton(ev, harmony);
                break;
            }
        }
    }
    // actionTime 传 -1：让系统使用 GetSysClockTime()（与真实事件同源的时间基准）。
    // 之前传 Unix 毫秒时间戳（~1.75e12），与系统时钟（~1e10）量级错乱，
    // 导致手势识别（按住时长/downTime 比较）失效 —— 拖动/长按永不触发
    OH_Input_SetMouseEventActionTime(ev, -1);
    int32_t ret = OH_Input_InjectMouseEventGlobal(ev);
    OH_Input_DestroyMouseEvent(&ev);
    if (ret != INPUT_SUCCESS) {
        DF_LOGE("injectMouseMove(%{public}d,%{public}d) failed ret=%{public}d", x, y, ret);
        if (ret == INPUT_PERMISSION_DENIED) {
            recoverInjectionPermission();
        }
    } else {
        // 注入正常：清空自愈计数，下次真出问题还能再救
        m_permRecoverCount.store(0);
        m_permRecoverLastMs.store(0);
        m_authPendingSinceMs.store(0);
    }
    return ret == INPUT_SUCCESS;
}

bool DeskflowClient::injectMouseButton(int32_t buttonId, bool down)
{
    int32_t harmony = buttonToHarmony(buttonId);
    if (harmony < 0) {
        // wheel buttons 4/5 handled via axis injection
        if (buttonId == kButtonWheelUp || buttonId == kButtonWheelDown) {
            return injectMouseWheel(0, buttonId == kButtonWheelUp ? 1 : -1);
        }
        return false;
    }
    m_mouseButtonsDown[buttonId] = down;
    int32_t x = m_pointerX.load();
    int32_t y = m_pointerY.load();
    // 同 injectMouseMove：edge 坐标为 0 时 OH_Input_InjectMouseEventGlobal 返回 401，
    // 拖到顶部(y=0)松不开正是此因。钳到 [1, W-1]/[1, H-1] 保证 BUTTON_UP 能注入成功。
    if (x < 1) x = 1; else if (x > m_screenW - 1) x = m_screenW - 1;
    if (y < 1) y = 1; else if (y > m_screenH - 1) y = m_screenH - 1;
    struct Input_MouseEvent* ev = OH_Input_CreateMouseEvent();
    if (ev == nullptr) {
        return false;
    }
    OH_Input_SetMouseEventAction(ev, down ? MOUSE_ACTION_BUTTON_DOWN : MOUSE_ACTION_BUTTON_UP);
    OH_Input_SetMouseEventButton(ev, harmony);
    OH_Input_SetMouseEventDisplayX(ev, x);
    OH_Input_SetMouseEventDisplayY(ev, y);
    OH_Input_SetMouseEventGlobalX(ev, x);
    OH_Input_SetMouseEventGlobalY(ev, y);
    // actionTime 传 -1：使用系统时钟（GetSysClockTime），与真实事件时间基准一致
    OH_Input_SetMouseEventActionTime(ev, -1);
    int32_t ret = OH_Input_InjectMouseEventGlobal(ev);
    OH_Input_DestroyMouseEvent(&ev);
    if (ret != INPUT_SUCCESS) {
        DF_LOGE("injectMouseButton(%{public}d,%{public}d) at (%{public}d,%{public}d) failed ret=%{public}d",
            buttonId, down ? 1 : 0, x, y, ret);
        if (ret == INPUT_PERMISSION_DENIED) {
            recoverInjectionPermission();
        }
    }
    return ret == INPUT_SUCCESS;
}

// 触摸事件注入已移除：devecocli 抓包证明成功拖动是纯 MOUSE 事件流
// （BUTTON_DOWN -> MOVE(button=LEFT) -> BUTTON_UP，actionTime 由系统时钟生成）

bool DeskflowClient::injectMouseWheel(int32_t xDelta, int32_t yDelta)
{
    if (yDelta == 0 && xDelta == 0) {
        return true;
    }
    // 反转滚轮方向：垂直滚轮取反（作用于 yDelta）
    if (m_invertScroll.load() && yDelta != 0) {
        yDelta = -yDelta;
    }
    int32_t x = fluxClampPointer(m_pointerX.load(), m_screenW);
    int32_t y = fluxClampPointer(m_pointerY.load(), m_screenH);
    int32_t axisType = (yDelta != 0) ? MOUSE_AXIS_SCROLL_VERTICAL : MOUSE_AXIS_SCROLL_HORIZONTAL;
    float axisValue = (yDelta != 0) ? static_cast<float>(yDelta) : static_cast<float>(xDelta);
    // 滚轮需要完整 AXIS_BEGIN -> AXIS_UPDATE -> AXIS_END 序列（官方 Action 枚举 4/5/6），
    // 只发 UPDATE 系统不识别
    const int32_t actions[3] = { MOUSE_ACTION_AXIS_BEGIN, MOUSE_ACTION_AXIS_UPDATE, MOUSE_ACTION_AXIS_END };
    for (int32_t action : actions) {
        struct Input_MouseEvent* ev = OH_Input_CreateMouseEvent();
        if (ev == nullptr) {
            return false;
        }
        OH_Input_SetMouseEventAction(ev, action);
        OH_Input_SetMouseEventAxisType(ev, axisType);
        OH_Input_SetMouseEventAxisValue(ev, axisValue);
        OH_Input_SetMouseEventDisplayX(ev, x);
        OH_Input_SetMouseEventDisplayY(ev, y);
        OH_Input_SetMouseEventGlobalX(ev, x);
        OH_Input_SetMouseEventGlobalY(ev, y);
        OH_Input_SetMouseEventActionTime(ev, -1);
        int32_t ret = OH_Input_InjectMouseEventGlobal(ev);
        OH_Input_DestroyMouseEvent(&ev);
        if (ret != INPUT_SUCCESS) {
            DF_LOGE("injectMouseWheel(action=%{public}d) failed ret=%{public}d", action, ret);
            if (ret == INPUT_PERMISSION_DENIED) {
                recoverInjectionPermission();
            }
            return false;
        }
    }
    return true;
}

bool DeskflowClient::injectKeyEvent(uint32_t keysym, bool down)
{
    int32_t keyCode = keysymToKeycode(keysym);
    if (keyCode < 0) {
        DF_LOGI("injectKeyEvent: unmapped keysym=0x%{public}x", keysym);
        return false;
    }
    struct Input_KeyEvent* ke = OH_Input_CreateKeyEvent();
    if (ke == nullptr) {
        return false;
    }
    OH_Input_SetKeyEventAction(ke, down ? KEY_ACTION_DOWN : KEY_ACTION_UP);
    OH_Input_SetKeyEventKeyCode(ke, keyCode);
    OH_Input_SetKeyEventActionTime(ke, -1);
    int32_t ret = OH_Input_InjectKeyEvent(ke);
    OH_Input_DestroyKeyEvent(&ke);
    DF_LOGI("injectKeyEvent keysym=0x%{public}x keyCode=%{public}d %{public}s ret=%{public}d",
        keysym, keyCode, down ? "DOWN" : "UP", ret);
    if (ret == INPUT_PERMISSION_DENIED) {
        recoverInjectionPermission();
    }
    return ret == INPUT_SUCCESS;
}

} // namespace dfpoc
