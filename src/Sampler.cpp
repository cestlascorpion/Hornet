#include "Sampler.h"

#include <sys/stat.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include <atomic>
#include <random>
#include <set>

#include "Common.h"

using namespace std;
using namespace opentelemetry;

namespace detail {

// GetRandom: range [0, 10000)
unsigned long int GetRandom() {
    static random_device r;
    static default_random_engine e(r());
    return e() % tracing::kMaxRatioValue;
}

// Format std::set<T> to std::string
template <typename T>
string FormatSet(const set<T> &set) {
    stringstream ss;
    for (const auto &s : set) {
        ss << s << " ";
    }
    return ss.str();
}

class SampleConf final {
public:
    SampleConf()
        : _path()
        , _ratio(tracing::kMaxRatioValue)
        , _cmdList()
        , _idx(0)
        , _uidList() {
        for (auto &item : _cmdList) {
            item.store(0, std::memory_order_relaxed);
        }

        const char *path = getenv(tracing::k_DefaultPathEnv);
        if (path == nullptr || strlen(path) == 0) {
            _path = tracing::k_DefaultPath;
        } else {
            _path = path;
        }

        unsigned ratio;
        set<unsigned> list;
        if (loadRatioAndWhiteList(_path, ratio, list)) {
            _ratio.store(ratio, std::memory_order_relaxed);
            _uidList[_idx.load(memory_order_relaxed)].swap(list);
        }
    }

public:
    bool CheckPass(unsigned uid, unsigned cmd, bool rot) {
        if (!rot) {
            return false;
        }

        static auto lastLoadTs = time(nullptr);

        const auto now = time(nullptr);
        if (now > lastLoadTs + 60) {
            static auto lastModifyTs = lastLoadTs;

            struct stat st {};
            if (stat(_path, &st) == 0 && st.st_mtime > lastModifyTs) {
                unsigned ratio;
                set<unsigned> list;
                if (loadRatioAndWhiteList(_path, ratio, list)) {
                    _ratio.store(ratio, std::memory_order_relaxed);
                    _uidList[_idx.load(memory_order_relaxed)].swap(list);
                }

                lastLoadTs = now;
                lastModifyTs = st.st_mtime;
            }
        }

        // some fast decision
        auto ratio = _ratio.load(memory_order_relaxed); // [0, 10000]
        if (ratio == 0) {
            return false;
        }
        if (ratio == tracing::kMaxRatioValue) {
            return true;
        }

        auto &cur = _uidList[_idx.load(std::memory_order_relaxed) % 2u];

        // hit the white-list
        if (uid > 0 && cur.count(uid) != 0) {
            if (cmd > 0 && cmd < tracing::kMaxCmdValue) {
                _cmdList[cmd].store(now, memory_order_relaxed);
            }
            return true;
        }

        // decide by the ratio
        auto r = GetRandom();
        if (r < ratio) {
            if (cmd > 0 && cmd < tracing::kMaxCmdValue) {
                _cmdList[cmd] = now;
            }
            return true;
        }

        // sample one every kMaxInterval(5min) at least
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
    static bool loadRatioAndWhiteList(const char *path, unsigned &r, set<unsigned> &s) {
        if (access(path, F_OK) != 0) {
            return false;
        }

        auto root = YAML::LoadFile(path);
        if (root.IsNull() || !root.IsMap()) {
            return false;
        }

        auto sampler = root["sampler"];
        if (sampler.IsNull() || !sampler.IsMap()) {
            return false;
        }
        auto ratio = sampler["ratio"];
        if (!ratio.IsNull() && ratio.IsScalar()) {
            r = ratio.as<unsigned int>();
            if (r > tracing::kMaxRatioValue) {
                r = tracing::kMaxRatioValue;
            }
        }
        auto whiteList = sampler["white-list"];
        if (!whiteList.IsNull() && whiteList.IsSequence()) {
            auto l = whiteList.as<vector<unsigned int>>();
            s = set<unsigned int>(l.begin(), l.end());
        }

        return true;
    }

private:
    const char *_path;
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
        return true; // which means continue
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