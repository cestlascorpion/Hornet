#include "Sampler.h"
#include "Common.h"

#include <atomic>
#include <random>
#include <set>

using namespace std;
using namespace opentelemetry;

namespace detail {

// [0, 10000)
unsigned long int GetRandom() {
    static random_device r;
    static default_random_engine e(r());
    return e() % tracing::kMaxRatioValue;
}

class SampleConf final {
public:
    SampleConf()
        : _ratio(tracing::kMaxRatioValue) // which means 100%
        , _cmdList()
        , _idx(0)
        , _uidList() {
        for (auto &item : _cmdList) {
            item.store(0, std::memory_order_relaxed);
        }

        const char *path = getenv(tracing::kTraceConfEnv);
        if (path == nullptr || strlen(path) == 0) {
            path = tracing::kTraceConfPath;
        }
        // 重载配置文件
        // LoadIfModified();
    }

public:
    bool CheckPass(unsigned uid, unsigned cmd, bool rot) {
        // 非 trace 根结点
        if (!rot) {
            return false;
        }
        // 重载配置文件
        // LoadIfModified();
        // 当前时间戳 sec
        const auto now = time(nullptr);
        // 命中白名单
        auto &cur = _uidList[_idx.load(std::memory_order_relaxed) % 2u];
        if (uid > 0 && cur.count(uid) != 0) {
            // cmd 标记
            if (cmd > 0 && cmd < tracing::kMaxCmdValue) {
                _cmdList[cmd].store(now, memory_order_relaxed);
            }
            return true;
        }
        // 概率采样
        auto ratio = _ratio.load(memory_order_relaxed); // [0, 10000]
        if (GetRandom() < ratio) {
            // cmd 标记
            if (cmd > 0 && cmd < tracing::kMaxCmdValue) {
                _cmdList[cmd] = now;
            }
            return true;
        }
        // 检查上次该 cmd 命中中间戳 保证 5min 内至少由一次采样
        if (cmd > 0 && cmd < tracing::kMaxCmdValue) {
            auto last = _cmdList[cmd].load(memory_order_relaxed);
            if (now > last + tracing::kMaxInterval) {
                _cmdList[cmd].compare_exchange_weak(last, now, memory_order_relaxed);
                return true;
            }
        }
        return false;
    }

private:
    atomic<unsigned> _ratio;
    array<atomic<long>, tracing::kMaxCmdValue> _cmdList;
    atomic<unsigned> _idx;
    array<set<unsigned>, 2u> _uidList;
};

SampleConf *GetControlConfig() {
    static SampleConf instance;
    return &instance;
}

} // namespace detail

namespace tracing {

CustomSampler::CustomSampler() noexcept
    : _desc("CustomSampler{conf-based sampler}") {}

SampleResult CustomSampler::ShouldSample(const trace::SpanContext &context, trace::TraceId trace,
                                         nostd::string_view name, trace::SpanKind kind,
                                         const common::KeyValueIterable &attr,
                                         const trace::SpanContextKeyValueIterable &link) noexcept {
    // get cmd/uid/root flag from attr
    auto uid = 0u;
    auto cmd = 0u;
    auto rot = false;
    attr.ForEachKeyValue([&](nostd::string_view key, common::AttributeValue value) noexcept -> bool {
        if (key == kTraceTagUid && nostd::holds_alternative<unsigned>(value)) {
            uid = nostd::get<unsigned>(value);
        }
        if (key == kTraceTagCmd && nostd::holds_alternative<unsigned>(value)) {
            cmd = nostd::get<unsigned>(value);
        }
        if (key == kTraceTagRot && nostd::holds_alternative<bool>(value)) {
            rot = nostd::get<bool>(value);
        }
        return true; // which means continue;
    });
    // conf-base sampler, so
    if (detail::GetControlConfig()->CheckPass(uid, cmd, rot)) {
        return {sdk::trace::Decision::RECORD_AND_SAMPLE, nullptr, nostd::shared_ptr<trace::TraceState>(nullptr)};
    }
    return {sdk::trace::Decision::DROP, nullptr, nostd::shared_ptr<trace::TraceState>(nullptr)};
}

nostd::string_view CustomSampler::GetDescription() const noexcept {
    return _desc;
}

} // namespace tracing