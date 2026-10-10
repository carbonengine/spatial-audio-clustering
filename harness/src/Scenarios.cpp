/*
 * Copyright 2026 CCP ehf.
 *
 * This software was developed by CCP Games for spatial audio object clustering
 * in EVE Online and EVE Frontier.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This license does not grant any rights to CCP's trademarks or game content.
 * EVE Online and EVE Frontier are registered trademarks of CCP ehf.
 */

#include "Scenarios.h"
#include "HarnessMath.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace
{
double ToRadians(double in_degrees)
{
    return in_degrees * kPi / 180.0;
}

Vec3 Add(const Vec3& in_a, const Vec3& in_b)
{
    return {in_a.x + in_b.x, in_a.y + in_b.y, in_a.z + in_b.z};
}

Vec3 Scale(const Vec3& in_v, double in_s)
{
    return {in_v.x * in_s, in_v.y * in_s, in_v.z * in_s};
}

double Dot(const Vec3& in_a, const Vec3& in_b)
{
    return in_a.x * in_b.x + in_a.y * in_b.y + in_a.z * in_b.z;
}

Vec3 Cross(const Vec3& in_a, const Vec3& in_b)
{
    return {in_a.y * in_b.z - in_a.z * in_b.y, in_a.z * in_b.x - in_a.x * in_b.z, in_a.x * in_b.y - in_a.y * in_b.x};
}

double Length(const Vec3& in_v)
{
    return std::sqrt(Dot(in_v, in_v));
}

Vec3 Normalize(const Vec3& in_v)
{
    const double length = Length(in_v);
    return length > 0.0 ? Scale(in_v, 1.0 / length) : Vec3{0.0, 0.0, 1.0};
}

double AngleDegrees(const Vec3& in_a, const Vec3& in_b)
{
    const double cosine = Dot(Normalize(in_a), Normalize(in_b));
    return std::acos(std::max(-1.0, std::min(1.0, cosine))) * kDegreesPerRadian;
}

double Uniform(std::mt19937& io_random, double in_low, double in_high)
{
    const double unit = (static_cast<double>(io_random()) + 0.5) / 4294967296.0;
    return in_low + (in_high - in_low) * unit;
}

int UniformInt(std::mt19937& io_random, int in_low, int in_high)
{
    const uint32_t span = static_cast<uint32_t>(in_high - in_low) + 1u;
    return in_low + static_cast<int>(io_random() % span);
}

int Poisson(std::mt19937& io_random, double in_mean)
{
    const double limit = std::exp(-in_mean);
    double product = Uniform(io_random, 0.0, 1.0);
    int count = 0;
    while (product > limit)
    {
        ++count;
        product *= Uniform(io_random, 0.0, 1.0);
    }
    return count;
}

double LogUniform(std::mt19937& io_random, double in_low, double in_high)
{
    return std::exp(Uniform(io_random, std::log(in_low), std::log(in_high)));
}

Vec3 RandomDirection(std::mt19937& io_random)
{
    const double z = Uniform(io_random, -1.0, 1.0);
    const double azimuth = Uniform(io_random, 0.0, 2.0 * kPi);
    const double planar = std::sqrt(std::max(0.0, 1.0 - z * z));
    return {planar * std::cos(azimuth), planar * std::sin(azimuth), z};
}

Vec3 AnyPerpendicular(const Vec3& in_direction)
{
    const Vec3 helper = std::fabs(in_direction.x) < 0.9 ? Vec3{1.0, 0.0, 0.0} : Vec3{0.0, 1.0, 0.0};
    return Normalize(Cross(in_direction, helper));
}

Vec3 RotateAround(const Vec3& in_v, const Vec3& in_axis, double in_radians)
{
    const double cosine = std::cos(in_radians);
    const double sine = std::sin(in_radians);
    const Vec3 cross = Cross(in_axis, in_v);
    const double along = Dot(in_axis, in_v);
    return Add(Add(Scale(in_v, cosine), Scale(cross, sine)), Scale(in_axis, along * (1.0 - cosine)));
}

Vec3 OffsetDirection(const Vec3& in_direction, double in_maxDegrees, std::mt19937& io_random)
{
    const Vec3 direction = Normalize(in_direction);
    const double angle = ToRadians(in_maxDegrees) * std::sqrt(Uniform(io_random, 0.0, 1.0));
    const double azimuth = Uniform(io_random, 0.0, 2.0 * kPi);
    const Vec3 side = AnyPerpendicular(direction);
    const Vec3 tilt = RotateAround(side, direction, azimuth);
    return RotateAround(direction, Normalize(Cross(direction, tilt)), angle);
}

Vec3 PickSeparatedDirection(std::vector<Vec3>& io_used, double in_minDegrees, std::mt19937& io_random)
{
    Vec3 candidate = RandomDirection(io_random);
    for (int attempt = 0; attempt < 10000; ++attempt)
    {
        bool separated = true;
        for (const Vec3& used : io_used)
        {
            if (AngleDegrees(used, candidate) < in_minDegrees)
            {
                separated = false;
                break;
            }
        }
        if (separated)
        {
            break;
        }
        candidate = RandomDirection(io_random);
    }
    io_used.push_back(candidate);
    return candidate;
}

/** @brief Produces unique random input keys from a seed. */
class KeySource
{
public:
    /** @brief Creates a key source for a seed. */
    explicit KeySource(uint32_t in_seed)
        : m_random(in_seed ^ 0x5EEDu)
    {
    }

    /** @brief Returns a key that this source has not returned before. */
    uint64_t Next()
    {
        for (;;)
        {
            const uint64_t key = (static_cast<uint64_t>(m_random()) << 32) | m_random();
            if (m_used.insert(key).second)
            {
                return key;
            }
        }
    }

private:
    std::mt19937 m_random;               ///< Random source for the keys.
    std::unordered_set<uint64_t> m_used; ///< Keys returned so far.
};

int SecondsToFrames(const Scenario& in_scenario, double in_seconds)
{
    return std::max(1, static_cast<int>(std::lround(in_seconds / in_scenario.frameSeconds)));
}

/** @brief Centre of a group of sources, which the members copy their motion from. */
struct Centre
{
    Vec3 direction;                 ///< Unit direction of the centre from the listener.
    double range = 1.0;             ///< Distance of the centre from the listener.
    Motion motion = Motion::Static; ///< Motion model of the members.
    Vec3 axis;                      ///< Unit rotation axis of orbiting members.
    double degreesPerSecond = 0.0;  ///< Rotation speed of orbiting members.
};

SourceSpec& AddMember(Scenario& io_scenario, KeySource& io_keys, std::mt19937& io_random, const Centre& in_centre,
                      double in_spreadDegrees, double in_rangeJitter, uint32_t in_group)
{
    SourceSpec source;
    source.key = io_keys.Next();
    source.group = in_group;
    source.priority = static_cast<uint32_t>(UniformInt(io_random, 10, 90));
    source.loudness = static_cast<float>(Uniform(io_random, 0.02, 0.5));
    const Vec3 direction = OffsetDirection(in_centre.direction, in_spreadDegrees, io_random);
    const double range = in_centre.range * Uniform(io_random, 1.0 - in_rangeJitter, 1.0 + in_rangeJitter);
    source.origin = Scale(direction, range);
    source.motion = in_centre.motion;
    source.axis = in_centre.axis;
    source.degreesPerSecond = in_centre.degreesPerSecond;
    io_scenario.sources.push_back(source);
    return io_scenario.sources.back();
}

void AddOneShots(Scenario& io_scenario, KeySource& io_keys, std::mt19937& io_random, const std::vector<Centre>& in_centres,
                 double in_ratePerSecond, double in_spreadDegrees, double in_passthroughShare)
{
    const double perFrame = in_ratePerSecond * io_scenario.frameSeconds;
    for (int frame = 0; frame < io_scenario.frames; ++frame)
    {
        const int count = Poisson(io_random, perFrame);
        for (int index = 0; index < count; ++index)
        {
            const Centre& centre = in_centres[static_cast<size_t>(UniformInt(io_random, 0, static_cast<int>(in_centres.size()) - 1))];
            SourceSpec& source = AddMember(io_scenario, io_keys, io_random, centre, in_spreadDegrees, 0.3, 0);
            source.startFrame = frame;
            source.endFrame = frame + SecondsToFrames(io_scenario, Uniform(io_random, 0.2, 2.0));
            source.loudness = static_cast<float>(Uniform(io_random, 0.01, 0.8));
            source.priority = static_cast<uint32_t>(UniformInt(io_random, 0, 100));
            source.clusterable = Uniform(io_random, 0.0, 1.0) >= in_passthroughShare;
        }
    }
}

std::vector<Centre> MakeStaticCentres(std::mt19937& io_random, int in_count, double in_minSeparation, double in_minRange,
                                      double in_maxRange)
{
    std::vector<Vec3> used;
    std::vector<Centre> centres;
    for (int index = 0; index < in_count; ++index)
    {
        Centre centre;
        centre.direction = PickSeparatedDirection(used, in_minSeparation, io_random);
        centre.range = LogUniform(io_random, in_minRange, in_maxRange);
        centres.push_back(centre);
    }
    return centres;
}

void BuildStatic(Scenario& io_scenario, std::mt19937& io_random, KeySource& io_keys)
{
    io_scenario.description = "static groups with fixed members";
    io_scenario.frames = 400;
    io_scenario.settleFrame = 60;
    for (const Centre& centre : MakeStaticCentres(io_random, 12, 25.0, 10.0, 1000.0))
    {
        const int members = UniformInt(io_random, 3, 8);
        for (int member = 0; member < members; ++member)
        {
            AddMember(io_scenario, io_keys, io_random, centre, 2.0, 0.1, 0);
        }
    }
}

void BuildCrossing(Scenario& io_scenario, std::mt19937& io_random, KeySource& io_keys)
{
    io_scenario.description = "groups orbiting in opposite directions that cross and separate";
    io_scenario.frames = 1500;
    for (int index = 0; index < 8; ++index)
    {
        Centre centre;
        const double tilt = ToRadians(Uniform(io_random, -4.0, 4.0));
        centre.axis = Normalize(Vec3{0.0, std::cos(tilt), std::sin(tilt)});
        const Vec3 start = AnyPerpendicular(centre.axis);
        centre.direction = RotateAround(start, centre.axis, Uniform(io_random, 0.0, 2.0 * kPi));
        centre.range = LogUniform(io_random, 20.0, 400.0);
        centre.motion = Motion::Orbit;
        const double speed = Uniform(io_random, 20.0, 40.0);
        centre.degreesPerSecond = (index % 2 == 0) ? speed : -speed;
        const int members = UniformInt(io_random, 3, 6);
        for (int member = 0; member < members; ++member)
        {
            AddMember(io_scenario, io_keys, io_random, centre, 1.5, 0.1, 0);
        }
    }
}

void BuildOneShots(Scenario& io_scenario, std::mt19937& io_random, KeySource& io_keys, double in_passthroughShare)
{
    io_scenario.description = "persistent groups plus random one-shots of 0.2 to 2 s around the group centres";
    io_scenario.frames = 2000;
    const std::vector<Centre> centres = MakeStaticCentres(io_random, 10, 20.0, 20.0, 500.0);
    for (const Centre& centre : centres)
    {
        for (int member = 0; member < 3; ++member)
        {
            SourceSpec& source = AddMember(io_scenario, io_keys, io_random, centre, 1.5, 0.1, 0);
            source.clusterable = Uniform(io_random, 0.0, 1.0) >= in_passthroughShare;
        }
    }
    AddOneShots(io_scenario, io_keys, io_random, centres, 100.0, 4.0, in_passthroughShare);
}

void BuildJitter(Scenario& io_scenario, std::mt19937& io_random, KeySource& io_keys)
{
    io_scenario.description = "static groups whose members jitter by up to 0.25 degrees every frame";
    io_scenario.frames = 1000;
    io_scenario.settleFrame = 60;
    for (const Centre& centre : MakeStaticCentres(io_random, 10, 30.0, 10.0, 1000.0))
    {
        const int members = UniformInt(io_random, 3, 6);
        for (int member = 0; member < members; ++member)
        {
            SourceSpec& source = AddMember(io_scenario, io_keys, io_random, centre, 1.5, 0.1, 0);
            source.jitterDegrees = 0.25;
        }
    }
}

void BuildPassBy(Scenario& io_scenario, std::mt19937& io_random, KeySource& io_keys)
{
    io_scenario.description = "sources that fly past a static group, join it and leave it again";
    io_scenario.frames = 800;
    Centre centre;
    centre.direction = {0.0, 0.0, 1.0};
    centre.range = 100.0;
    for (int member = 0; member < 6; ++member)
    {
        SourceSpec& source = AddMember(io_scenario, io_keys, io_random, centre, 1.0, 0.05, 0);
        source.loudness = 0.2f;
    }

    SourceSpec first;
    first.key = io_keys.Next();
    first.loudness = 0.3f;
    first.motion = Motion::Linear;
    first.origin = {-400.0, 3.0, 100.0};
    first.velocity = {100.0, 0.0, 0.0};
    io_scenario.sources.push_back(first);

    SourceSpec second;
    second.key = io_keys.Next();
    second.loudness = 0.1f;
    second.motion = Motion::Linear;
    second.origin = {300.0, -2.0, 60.0};
    second.velocity = {-80.0, 0.0, 0.0};
    io_scenario.sources.push_back(second);
}

void BuildSqueeze(Scenario& io_scenario, std::mt19937& io_random, KeySource& io_keys)
{
    io_scenario.description = "many groups with maxOutputs lowered and raised while running";
    io_scenario.frames = 600;
    for (const Centre& centre : MakeStaticCentres(io_random, 24, 12.0, 10.0, 1000.0))
    {
        for (int member = 0; member < 4; ++member)
        {
            AddMember(io_scenario, io_keys, io_random, centre, 1.5, 0.1, 0);
        }
    }
    for (int index = 0; index < 4; ++index)
    {
        SourceSpec source;
        source.key = io_keys.Next();
        source.clusterable = false;
        source.origin = Scale(RandomDirection(io_random), 50.0);
        io_scenario.sources.push_back(source);
    }
    const uint32_t budgets[] = {12, 20, 8};
    const int frames[] = {100, 300, 450};
    for (int index = 0; index < 3; ++index)
    {
        ParamChange change;
        change.frame = frames[index];
        change.params = io_scenario.params;
        change.params.maxOutputs = budgets[index];
        io_scenario.changes.push_back(change);
    }
}

void BuildNearField(Scenario& io_scenario, std::mt19937& io_random, KeySource& io_keys)
{
    io_scenario.description = "sources around the listener inside and outside the near field, and high priority sources";
    io_scenario.frames = 1200;
    io_scenario.params.nearFieldRadius = 5.f;
    io_scenario.params.excludePriorityAbove = 80;
    for (int index = 0; index < 18; ++index)
    {
        const bool inside = index < 12;
        SourceSpec source;
        source.key = io_keys.Next();
        source.loudness = static_cast<float>(Uniform(io_random, 0.05, 0.5));
        const Vec3 direction = RandomDirection(io_random);
        source.origin = Scale(direction, inside ? Uniform(io_random, 1.0, 4.5) : Uniform(io_random, 6.0, 10.0));
        source.motion = Motion::Orbit;
        source.axis = AnyPerpendicular(direction);
        source.degreesPerSecond = inside ? Uniform(io_random, 30.0, 90.0) : Uniform(io_random, 5.0, 20.0);
        io_scenario.sources.push_back(source);
    }
    for (int index = 0; index < 2; ++index)
    {
        SourceSpec source;
        source.key = io_keys.Next();
        source.loudness = 0.2f;
        source.motion = Motion::Linear;
        source.origin = {-50.0, 1.0 + index, 2.0};
        source.velocity = {20.0 + 5.0 * index, 0.0, 0.0};
        io_scenario.sources.push_back(source);
    }
    Centre centre;
    centre.direction = Normalize(Vec3{0.3, 0.2, 1.0});
    centre.range = 50.0;
    const uint32_t priorities[] = {90, 90, 40, 40, 40};
    for (uint32_t priority : priorities)
    {
        SourceSpec& source = AddMember(io_scenario, io_keys, io_random, centre, 1.5, 0.05, 0);
        source.priority = priority;
    }
}

void BuildGroups(Scenario& io_scenario, std::mt19937& io_random, KeySource& io_keys)
{
    io_scenario.description = "three group IDs that share the same directions";
    io_scenario.frames = 400;
    io_scenario.settleFrame = 60;
    for (const Centre& centre : MakeStaticCentres(io_random, 4, 30.0, 20.0, 400.0))
    {
        for (uint32_t group = 0; group < 3; ++group)
        {
            for (int member = 0; member < 4; ++member)
            {
                AddMember(io_scenario, io_keys, io_random, centre, 1.5, 0.1, group * 7u + 1u);
            }
        }
    }
}

void BuildCloud(Scenario& io_scenario, std::mt19937& io_random, KeySource& io_keys)
{
    io_scenario.description = "dense cloud of slowly drifting sources inside a 60 degree cone";
    io_scenario.frames = 1500;
    for (int index = 0; index < 200; ++index)
    {
        SourceSpec source;
        source.key = io_keys.Next();
        source.loudness = static_cast<float>(Uniform(io_random, 0.01, 0.5));
        source.priority = static_cast<uint32_t>(UniformInt(io_random, 0, 100));
        const Vec3 direction = OffsetDirection(Vec3{0.0, 0.0, 1.0}, 30.0, io_random);
        source.origin = Scale(direction, LogUniform(io_random, 10.0, 1000.0));
        source.motion = Motion::Orbit;
        source.axis = Normalize(Cross(direction, RandomDirection(io_random)));
        source.degreesPerSecond = Uniform(io_random, 1.0, 6.0);
        io_scenario.sources.push_back(source);
    }
}

void BuildStress(Scenario& io_scenario, std::mt19937& io_random, KeySource& io_keys)
{
    io_scenario.description = "about 1024 inputs around 64 slowly orbiting centres";
    io_scenario.frames = 1000;
    std::vector<Centre> centres;
    std::vector<Vec3> used;
    for (int index = 0; index < 64; ++index)
    {
        Centre centre;
        centre.direction = PickSeparatedDirection(used, 8.0, io_random);
        centre.range = LogUniform(io_random, 10.0, 1000.0);
        centre.motion = Motion::Orbit;
        centre.axis = Normalize(Cross(centre.direction, RandomDirection(io_random)));
        centre.degreesPerSecond = Uniform(io_random, 2.0, 10.0);
        centres.push_back(centre);
        for (int member = 0; member < 12; ++member)
        {
            AddMember(io_scenario, io_keys, io_random, centre, 3.0, 0.2, 0);
        }
    }
    AddOneShots(io_scenario, io_keys, io_random, centres, 233.0, 3.0, 0.0);
}

struct ScenarioEntry
{
    const char* name;
    uint32_t seed;
};

const ScenarioEntry kScenarios[] = {
    {"static", 1001},     {"crossing", 1002}, {"oneshots", 1003}, {"jitter", 1004},
    {"passby", 1005},     {"squeeze", 1006},  {"nearfield", 1007}, {"groups", 1008},
    {"passthrough", 1009}, {"shuffle", 1010}, {"cloud", 1011},    {"stress", 1012},
};
}

const std::vector<std::string>& GetScenarioNames()
{
    static const std::vector<std::string> s_names = [] {
        std::vector<std::string> names;
        for (const ScenarioEntry& entry : kScenarios)
        {
            names.push_back(entry.name);
        }
        return names;
    }();
    return s_names;
}

uint32_t GetDefaultSeed(const std::string& in_name)
{
    for (const ScenarioEntry& entry : kScenarios)
    {
        if (in_name == entry.name)
        {
            return entry.seed;
        }
    }
    return 1;
}

bool BuildScenario(const std::string& in_name, uint32_t in_seed, Scenario& out_scenario)
{
    out_scenario = Scenario();
    out_scenario.name = in_name;
    out_scenario.seed = in_seed;
    std::mt19937 random(in_seed);
    KeySource keys(in_seed);

    if (in_name == "static")
    {
        BuildStatic(out_scenario, random, keys);
    }
    else if (in_name == "crossing")
    {
        BuildCrossing(out_scenario, random, keys);
    }
    else if (in_name == "oneshots")
    {
        BuildOneShots(out_scenario, random, keys, 0.0);
    }
    else if (in_name == "jitter")
    {
        BuildJitter(out_scenario, random, keys);
    }
    else if (in_name == "passby")
    {
        BuildPassBy(out_scenario, random, keys);
    }
    else if (in_name == "squeeze")
    {
        BuildSqueeze(out_scenario, random, keys);
    }
    else if (in_name == "nearfield")
    {
        BuildNearField(out_scenario, random, keys);
    }
    else if (in_name == "groups")
    {
        BuildGroups(out_scenario, random, keys);
    }
    else if (in_name == "passthrough")
    {
        out_scenario.params.maxOutputs = 32;
        BuildOneShots(out_scenario, random, keys, 0.1);
        out_scenario.description = "one-shots where a tenth of the inputs pass through, with maxOutputs 32";
    }
    else if (in_name == "shuffle")
    {
        BuildOneShots(out_scenario, random, keys, 0.0);
        out_scenario.shuffle = true;
        out_scenario.description = "one-shots with the input order shuffled every frame";
    }
    else if (in_name == "cloud")
    {
        BuildCloud(out_scenario, random, keys);
    }
    else if (in_name == "stress")
    {
        BuildStress(out_scenario, random, keys);
    }
    else
    {
        return false;
    }
    return true;
}

void BuildBenchScenario(uint32_t in_inputs, uint32_t in_seed, int in_frames, Scenario& out_scenario)
{
    out_scenario = Scenario();
    out_scenario.name = "bench";
    out_scenario.description = "fixed input count around slowly orbiting centres with respawning one-shots";
    out_scenario.seed = in_seed;
    out_scenario.frames = in_frames;
    std::mt19937 random(in_seed);
    KeySource keys(in_seed);

    const int centreCount = std::max(8, std::min(128, static_cast<int>(in_inputs / 4u)));
    std::vector<Centre> centres;
    std::vector<Vec3> used;
    for (int index = 0; index < centreCount; ++index)
    {
        Centre centre;
        centre.direction = PickSeparatedDirection(used, 8.0, random);
        centre.range = LogUniform(random, 10.0, 1000.0);
        centre.motion = Motion::Orbit;
        centre.axis = Normalize(Cross(centre.direction, RandomDirection(random)));
        centre.degreesPerSecond = Uniform(random, 2.0, 10.0);
        centres.push_back(centre);
    }

    const uint32_t persistent = in_inputs * 3u / 4u;
    for (uint32_t index = 0; index < persistent; ++index)
    {
        AddMember(out_scenario, keys, random, centres[index % centres.size()], 3.0, 0.2, 0);
    }
    for (uint32_t index = persistent; index < in_inputs; ++index)
    {
        int frame = 0;
        while (frame < in_frames)
        {
            const Centre& centre = centres[static_cast<size_t>(UniformInt(random, 0, centreCount - 1))];
            SourceSpec& source = AddMember(out_scenario, keys, random, centre, 3.0, 0.3, 0);
            source.startFrame = frame;
            source.endFrame = frame + SecondsToFrames(out_scenario, Uniform(random, 0.2, 2.0));
            source.loudness = static_cast<float>(Uniform(random, 0.01, 0.8));
            frame = source.endFrame;
        }
    }
}

ScenarioPlayer::ScenarioPlayer(const Scenario& in_scenario, uint32_t in_orderSeed, bool in_shuffle)
    : m_scenario(in_scenario)
    , m_jitterRandom(in_scenario.seed ^ 0x717E7u)
    , m_orderRandom(in_orderSeed)
    , m_shuffle(in_shuffle)
    , m_params(in_scenario.params)
{
}

const std::vector<ClusterInput>& ScenarioPlayer::NextFrame()
{
    ++m_frame;
    while (m_nextChange < m_scenario.changes.size() && m_scenario.changes[m_nextChange].frame <= m_frame)
    {
        m_params = m_scenario.changes[m_nextChange].params;
        ++m_nextChange;
    }

    const double seconds = m_frame * m_scenario.frameSeconds;
    m_inputs.clear();
    for (const SourceSpec& source : m_scenario.sources)
    {
        if (m_frame < source.startFrame || m_frame >= source.endFrame)
        {
            continue;
        }
        Vec3 position = source.origin;
        if (source.motion == Motion::Orbit)
        {
            position = RotateAround(source.origin, source.axis, ToRadians(source.degreesPerSecond * seconds));
        }
        else if (source.motion == Motion::Linear)
        {
            position = Add(source.origin, Scale(source.velocity, seconds));
        }
        if (source.jitterDegrees > 0.0)
        {
            position = Scale(OffsetDirection(position, source.jitterDegrees, m_jitterRandom), Length(position));
        }

        ClusterInput input;
        input.key = source.key;
        input.position = {static_cast<float>(position.x), static_cast<float>(position.y), static_cast<float>(position.z)};
        input.loudness = source.loudness;
        input.priority = source.priority;
        input.group = source.group;
        input.clusterable = source.clusterable;
        m_inputs.push_back(input);
    }
    if (m_shuffle)
    {
        for (size_t index = m_inputs.size(); index > 1; --index)
        {
            const size_t other = m_orderRandom() % index;
            std::swap(m_inputs[index - 1], m_inputs[other]);
        }
    }
    return m_inputs;
}

const ClusterParams& ScenarioPlayer::GetParams() const
{
    return m_params;
}

int ScenarioPlayer::GetFrame() const
{
    return m_frame;
}
